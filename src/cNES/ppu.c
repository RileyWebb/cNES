#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <malloc.h>
#include <time.h>
#include <math.h> // For fabsf in scalar color emphasis, or general float math
#define PPU_USE_SIMD_COLOR_EMPHASIS

#ifdef __AVX2__ //AVX2

#elif defined ( __AVX__ ) //AVX

#elif (defined(_M_AMD64) || defined(_M_X64)) //SSE2 x64

#define PPU_USE_SIMD_COLOR_EMPHASIS

#elif _M_IX86_FP == 2 //SSE2 x32

#define PPU_USE_SIMD_COLOR_EMPHASIS

#elif _M_IX86_FP == 1 //SSE x32

#else
//nothing
#endif

#ifdef PPU_USE_SIMD_COLOR_EMPHASIS
#include <emmintrin.h> // SSE2 intrinsics
#endif

#include "debug.h"    // Assuming debug logging is desired
#include "cNES/nes.h" // Assuming NES structure is needed
#include "cNES/bus.h" // Assuming BUS access is needed

#include "cNES/ppu.h" // Header for PPU struct, MirrorMode, PPUSTATUS/PPUCTRL/PPUMASK bits

// --- NES master palette (64 colors, ABGR format: 0xAABBGGRR) ---


// --- Helper Functions ---

#define PPU_OPEN_BUS_DECAY_MS 750ULL

static inline uint64_t ppu_now_ms(void) {
    struct timespec now;
    if (timespec_get(&now, TIME_UTC) == 0) {
        return 0;
    }

    return (uint64_t)now.tv_sec * 1000ULL + (uint64_t)(now.tv_nsec / 1000000ULL);
}

static inline void ppu_decay_open_bus(PPU *ppu) {
    if (!ppu) {
        return;
    }

    if (ppu->open_bus != 0 && ppu->open_bus_last_update_ms != 0) {
        uint64_t now = ppu_now_ms();
        if ((now - ppu->open_bus_last_update_ms) >= PPU_OPEN_BUS_DECAY_MS) {
            ppu->open_bus = 0;
        }
    }
}

static inline void ppu_drive_open_bus(PPU *ppu, uint8_t value) {
    ppu->open_bus = value;
    ppu->open_bus_last_update_ms = ppu_now_ms();
}

static inline void ppu_update_nmi_line(PPU *ppu) {
    bool nmi_condition = ppu->nmi_output && (ppu->status & PPUSTATUS_VBLANK);

    if (!nmi_condition) {
        ppu->previous_nmi_output = false;
        ppu->nmi_interrupt_line = false;
        return;
    }

    if (!ppu->previous_nmi_output) {
        ppu->nmi_interrupt_line = true;
    }

    ppu->previous_nmi_output = true;
}

static inline uint16_t mirror_vram_addr(PPU *ppu, uint16_t addr) {
    addr &= 0x0FFF;
    switch (ppu->mirror_mode) {
        case MIRROR_HORIZONTAL:         return ((addr >> 1) & 0x0400) | (addr & 0x03FF);
        case MIRROR_VERTICAL:           return addr & 0x07FF;
        case MIRROR_SINGLE_SCREEN_LOW:  return addr & 0x03FF;
        case MIRROR_SINGLE_SCREEN_HIGH: return (addr & 0x03FF) | 0x0400;
        case MIRROR_FOUR_SCREEN:        return addr;
        default:                        return ((addr >> 1) & 0x0400) | (addr & 0x03FF);
    }
}

static inline uint8_t ppu_read_vram(PPU *ppu, uint16_t addr) {
    addr &= 0x3FFF; 

    if (addr < 0x2000) { // CHR ROM/RAM ($0000 - $1FFF)
        return BUS_PPU_ReadCHR(ppu->nes->bus, addr);
    } else if (addr < 0x3F00) { // Nametable RAM ($2000 - $3EFF)
        return ppu->vram[mirror_vram_addr(ppu, addr & 0x2FFF)];
    } else { // Palette RAM ($3F00 - $3FFF)
        uint16_t pal_addr = addr & 0x1F;
        if ((pal_addr & 0x03) == 0) { // Mirror $3F10, $3F14, $3F18, $3F1C to $3F00, $3F04, $3F08, $3F0C
            pal_addr &= ~0x10;
        }
        return ppu->palette[pal_addr];
    }
    // DEBUG_ERROR("PPU: Unhandled PPU VRAM read at address 0x%04X", addr); // Should be unreachable
    // return 0; 
}

static inline void ppu_write_vram(PPU *ppu, uint16_t addr, uint8_t value) {
    addr &= 0x3FFF; 

    if (addr < 0x2000) { // CHR RAM ($0000 - $1FFF)
        BUS_PPU_WriteCHR(ppu->nes->bus, addr, value);
    } else if (addr < 0x3F00) { // Nametable RAM ($2000 - $3EFF)
        ppu->vram[mirror_vram_addr(ppu, addr & 0x2FFF)] = value;
    } else if (addr < 0x4000) { // Palette RAM ($3F00 - $3FFF)
        uint16_t pal_addr = addr & 0x1F;
        if ((pal_addr & 0x03) == 0) {
            pal_addr &= ~0x10;
        }
        ppu->palette[pal_addr] = value;
    } else {
        DEBUG_ERROR("PPU: Unhandled PPU VRAM write at address 0x%04X value 0x%02X", addr, value);
    }
}


// --- VRAM Address Update Helpers (Scrolling) ---
static inline void increment_coarse_x(PPU *ppu) {
    if ((ppu->vram_addr & 0x001F) == 31) { 
        ppu->vram_addr &= ~0x001F;         
        ppu->vram_addr ^= 0x0400;          
    } else {
        ppu->vram_addr += 1;               
    }
}

static inline void increment_fine_y(PPU *ppu) {
    if ((ppu->vram_addr & 0x7000) != 0x7000) { 
        ppu->vram_addr += 0x1000;              
    } else {
        ppu->vram_addr &= ~0x7000;             
        uint16_t y = (ppu->vram_addr & 0x03E0) >> 5; 
        if (y == 29) {                         
            y = 0;                             
            ppu->vram_addr ^= 0x0800;          
        } else if (y == 31) {                  
            y = 0;                             
        } else {
            y += 1;                            
        }
        ppu->vram_addr = (ppu->vram_addr & ~0x03E0) | (y << 5); 
    }
}

static inline void copy_horizontal_bits(PPU *ppu) {
    ppu->vram_addr = (ppu->vram_addr & ~0x041F) | (ppu->temp_addr & 0x041F);
}

static inline void copy_vertical_bits(PPU *ppu) {
    ppu->vram_addr = (ppu->vram_addr & ~0x7BE0) | (ppu->temp_addr & 0x7BE0);
}


// --- Background Rendering Pipeline Helpers ---
static void load_background_tile_data(PPU *ppu) {
    uint16_t nt_addr = 0x2000 | (ppu->vram_addr & 0x0FFF); 
    ppu->bg_nt_latch = ppu_read_vram(ppu, nt_addr);

    uint16_t at_addr = 0x23C0 | (ppu->vram_addr & 0x0C00) |   
                       ((ppu->vram_addr >> 4) & 0x38) |      
                       ((ppu->vram_addr >> 2) & 0x07);      
    uint8_t at_byte = ppu_read_vram(ppu, at_addr);
    
    uint8_t shift = (uint8_t)(((ppu->vram_addr >> 3) & 0x04) | ((ppu->vram_addr << 1) & 0x02));
    uint8_t palette_bits = (at_byte >> shift) & 0x03; 

    // Expand to 8 bits for shifter using bitwise trick ( -(condition) yields 0xFF..FF if true, 0 if false)
    ppu->bg_at_latch_low = -((palette_bits & 0x01) != 0); 
    ppu->bg_at_latch_high = -((palette_bits & 0x02) != 0);

    uint16_t fine_y = (ppu->vram_addr >> 12) & 7;             
    uint16_t pt_base = (ppu->ctrl & PPUCTRL_BG_TABLE_ADDR) ? 0x1000 : 0; 
    uint16_t tile_offset = ppu->bg_nt_latch * 16;             
    
    uint16_t pt_addr_low = pt_base + tile_offset + fine_y;
    ppu->bg_pt_low_latch = ppu_read_vram(ppu, pt_addr_low);
    ppu->bg_pt_high_latch = ppu_read_vram(ppu, pt_addr_low + 8); 
}

static void feed_background_shifters(PPU *ppu) {
    ppu->bg_pattern_shift_low = (ppu->bg_pattern_shift_low & 0xFF00) | ppu->bg_pt_low_latch;
    ppu->bg_pattern_shift_high = (ppu->bg_pattern_shift_high & 0xFF00) | ppu->bg_pt_high_latch;
    ppu->bg_attrib_shift_low = (ppu->bg_attrib_shift_low & 0xFF00) | ppu->bg_at_latch_low;
    ppu->bg_attrib_shift_high = (ppu->bg_attrib_shift_high & 0xFF00) | ppu->bg_at_latch_high;
}


// --- Sprite Evaluation and Rendering Helpers ---
static void evaluate_sprites(PPU *ppu) {
    ppu->sprite_count_current_scanline = 0;
    // SPRITE_OVERFLOW flag is only cleared at pre-render line, NOT during sprite evaluation
    // This allows the flag to persist if 9+ sprites are found on any scanline
    ppu->sprite_zero_found_for_next_scanline = false;

    memset(ppu->secondary_oam, 0xFF, PPU_SECONDARY_OAM_SIZE); // PPU_SECONDARY_OAM_SIZE = 8 sprites * 4 bytes/sprite = 32

    uint8_t secondary_oam_idx = 0;
    uint8_t sprite_height = (ppu->ctrl & PPUCTRL_SPRITE_SIZE) ? 16 : 8;

    for (int oam_idx = 0; oam_idx < 64; ++oam_idx) {
        uint8_t sprite_y = ppu->oam[oam_idx * 4 + 0];
        int next_scanline = (ppu->scanline == ppu->nes->settings.timing.scanline_prerender) ? 0 : (ppu->scanline + 1);
        int row_on_scanline = (next_scanline - sprite_y) & 0xFF;

        if (row_on_scanline >= 0 && row_on_scanline < sprite_height) {
            if (secondary_oam_idx < 8) {
                memcpy(&ppu->secondary_oam[secondary_oam_idx * 4], &ppu->oam[oam_idx * 4], 4);
                ppu->secondary_oam_original_indices[secondary_oam_idx] = oam_idx; // Store original OAM index
                
                if (oam_idx == 0) {
                    ppu->sprite_zero_found_for_next_scanline = true; // Sprite 0 is among the candidates
                }
                secondary_oam_idx++;
            } else {
                ppu->status |= PPUSTATUS_SPRITE_OVERFLOW;
                // TODO: Implement accurate sprite overflow bug if needed
                break; 
            }
        }
    }
    ppu->sprite_count_current_scanline = secondary_oam_idx;
}

static void fetch_sprite_patterns(PPU *ppu) {
    uint8_t sprite_height = (ppu->ctrl & PPUCTRL_SPRITE_SIZE) ? 16 : 8;

    for (int i = 0; i < ppu->sprite_count_current_scanline; ++i) {
        uint8_t sprite_y_oam = ppu->secondary_oam[i * 4 + 0];
        uint8_t tile_id      = ppu->secondary_oam[i * 4 + 1];
        uint8_t attributes   = ppu->secondary_oam[i * 4 + 2];
        uint8_t sprite_x     = ppu->secondary_oam[i * 4 + 3];

        ppu->sprite_shifters[i].x_pos      = sprite_x;
        ppu->sprite_shifters[i].attributes = attributes;
        ppu->sprite_shifters[i].original_oam_index = ppu->secondary_oam_original_indices[i]; // Carry over original index

        int next_scanline = (ppu->scanline == ppu->nes->settings.timing.scanline_prerender) ? 0 : (ppu->scanline + 1);
        int row_in_sprite = (next_scanline - sprite_y_oam) & 0xFF;

        if (attributes & 0x80) { // Vertical flip
            row_in_sprite = (sprite_height - 1) - row_in_sprite;
        }

        uint16_t pattern_addr_base;
        if (sprite_height == 16) { 
            pattern_addr_base = ((tile_id & 0x01) ? 0x1000 : 0x0000) + ((tile_id & 0xFE) * 16);
            if (row_in_sprite >= 8) { 
                pattern_addr_base += 16; 
                row_in_sprite -= 8;    
            }
        } else { // 8x8 sprites
            pattern_addr_base = ((ppu->ctrl & PPUCTRL_SPRITE_TABLE_ADDR) ? 0x1000 : 0x0000) + (tile_id * 16);
        }
        
        uint16_t pattern_addr = pattern_addr_base + row_in_sprite;
        ppu->sprite_shifters[i].pattern_low  = ppu_read_vram(ppu, pattern_addr);
        ppu->sprite_shifters[i].pattern_high = ppu_read_vram(ppu, pattern_addr + 8);
    }
}

// --- Color Emphasis Helpers ---
#ifndef PPU_USE_SIMD_COLOR_EMPHASIS
// Scalar version using floating point for clarity on the emphasis model
static inline uint32_t apply_color_emphasis(uint32_t color_val, uint8_t ppu_mask) {
    if (!(ppu_mask & (PPUMASK_EMPHASIZE_RED | PPUMASK_EMPHASIZE_GREEN | PPUMASK_EMPHASIZE_BLUE))) {
        return color_val;
    }

    // color_val is 0xAABBGGRR (ABGR format)
    float a = (float)((color_val >> 24) & 0xFF);
    float b = (float)((color_val >> 16) & 0xFF);
    float g = (float)((color_val >> 8) & 0xFF);
    uint32_t r = color_val & 0xFF;

    // Common attenuation factor for non-emphasized channels (e.g., ~0.74 to ~0.8)
    const float attenuate_factor = 0.75f; // Using 3/4 for simplicity

    float r_mult = 1.0f, g_mult = 1.0f, b_mult = 1.0f;

    if (ppu_mask & PPUMASK_EMPHASIZE_RED) {   // Emphasize Red: Attenuate Green and Blue
        g_mult *= attenuate_factor;
        b_mult *= attenuate_factor;
    }
    if (ppu_mask & PPUMASK_EMPHASIZE_GREEN) { // Emphasize Green: Attenuate Red and Blue
        r_mult *= attenuate_factor;
        b_mult *= attenuate_factor;
    }
    if (ppu_mask & PPUMASK_EMPHASIZE_BLUE) {  // Emphasize Blue: Attenuate Red and Green
        r_mult *= attenuate_factor;
        g_mult *= attenuate_factor;
    }

    float r_f = r * r_mult;
    g *= g_mult;
    b *= b_mult;
    
    // Clamp results to 0-255
    uint32_t R_final = (r_f < 0.0f) ? 0 : ((r_f > 255.0f) ? 255 : (uint32_t)r_f);
    uint32_t G_final = (g < 0.0f) ? 0 : ((g > 255.0f) ? 255 : (uint32_t)g);
    uint32_t B_final = (b < 0.0f) ? 0 : ((b > 255.0f) ? 255 : (uint32_t)b);
    uint32_t A_final = (uint32_t)a;
    
    return (A_final << 24) | (B_final << 16) | (G_final << 8) | R_final;
}
#else // PPU_USE_SIMD_COLOR_EMPHASIS
// SIMD (SSE2) version using fixed-point arithmetic
static inline uint32_t apply_color_emphasis(uint32_t color_val, uint8_t ppu_mask) {
    if (!(ppu_mask & (PPUMASK_EMPHASIZE_RED | PPUMASK_EMPHASIZE_GREEN | PPUMASK_EMPHASIZE_BLUE))) {
        return color_val;
    }

    // color_val is 0xAABBGGRR. SSE2 deals with it as little-endian, so bytes are R,G,B,A in memory.
    __m128i color_vec = _mm_cvtsi32_si128(color_val); // Loads color_val into the low 32 bits of an XMM register.
                                                     // In memory order for the XMM reg: [RR, GG, BB, AA, 0, ...]

    // Unpack 8-bit components to 16-bit components for multiplication room.
    // Input bytes: val[0]=RR, val[1]=GG, val[2]=BB, val[3]=AA
    // Output words: {RR,0, GG,0, BB,0, AA,0 ...}
    color_vec = _mm_unpacklo_epi8(color_vec, _mm_setzero_si128()); // Now holds [R, G, B, A] as 16-bit words (red, green, blue, alpha)

    // Attenuation factor: 192 represents 192/256 = 0.75
    // Normal factor: 256 represents 256/256 = 1.0
    const uint16_t factor_attenuate = 192;
    const uint16_t factor_normal = 256;

    uint16_t r_f = factor_normal, g_f = factor_normal, b_f = factor_normal;
    // Alpha component (at index 3 in color_vec words) is not affected, its factor is always 256.

    if (ppu_mask & PPUMASK_EMPHASIZE_RED) {
        g_f = (uint16_t)(((uint32_t)g_f * factor_attenuate) / factor_normal);
        b_f = (uint16_t)(((uint32_t)b_f * factor_attenuate) / factor_normal);
    }
    if (ppu_mask & PPUMASK_EMPHASIZE_GREEN) {
        r_f = (uint16_t)(((uint32_t)r_f * factor_attenuate) / factor_normal);
        b_f = (uint16_t)(((uint32_t)b_f * factor_attenuate) / factor_normal);
    }
    if (ppu_mask & PPUMASK_EMPHASIZE_BLUE) {
        r_f = (uint16_t)(((uint32_t)r_f * factor_attenuate) / factor_normal);
        g_f = (uint16_t)(((uint32_t)g_f * factor_attenuate) / factor_normal);
    }

    // Factors in XMM register, matching component order R, G, B, A (lowest to highest words)
    __m128i factors = _mm_set_epi16(
        0, 0, 0, 0,      // High 4 words unused
        factor_normal, b_f, g_f, r_f // Factors for A, B, G, R (order from high word to low word)
    );

    
    // Multiply components by factors: (component * factor)
    color_vec = _mm_mullo_epi16(color_vec, factors);
    // Divide by 256 (effectively scaling back after fixed-point multiplication)
    color_vec = _mm_srli_epi16(color_vec, 8);

    // Pack 16-bit words back to 8-bit bytes with saturation.
    // Result: {R,G,B,A, R,G,B,A, ...} in bytes.
    color_vec = _mm_packus_epi16(color_vec, color_vec); 

    return _mm_cvtsi128_si32(color_vec); // Extract the low 32 bits (AABBGGRR)
}
#endif // PPU_USE_SIMD_COLOR_EMPHASIS


// --- PPU API Implementation ---
PPU *PPU_Create(NES *nes) {
    PPU *ppu = calloc(1, sizeof(PPU));
    if (!ppu) {
        DEBUG_ERROR("PPU_Create: Failed to allocate memory for PPU.");
        return NULL;
    }
    ppu->nes = nes;

    // Allocate framebuffer with alignment for potential SIMD operations
    size_t framebuffer_size = PPU_FRAMEBUFFER_WIDTH * PPU_FRAMEBUFFER_HEIGHT * sizeof(uint32_t);

#if defined(__MINGW32__) || defined(__MINGW64__)
    ppu->framebuffer = _aligned_malloc(framebuffer_size, 16);
#elif __STDC_VERSION__ >= 201112L
    #include <stdlib.h> // Ensure aligned_alloc is declared
    ppu->framebuffer = aligned_alloc(16, framebuffer_size);
#else
    // Fallback: use regular calloc, alignment not guaranteed but often works for 16-bytes on modern systems.
    // Or use posix_memalign if available.
    ppu->framebuffer = calloc(PPU_FRAMEBUFFER_WIDTH * PPU_FRAMEBUFFER_HEIGHT, sizeof(uint32_t));
    DEBUG_WARN("PPU_Create: Using calloc for framebuffer, 16-byte alignment not guaranteed without C11 or MSVC specifics.");
#endif
    if (!ppu->framebuffer) {
        DEBUG_ERROR("PPU_Create: Failed to allocate memory for framebuffer.");
        free(ppu);
        return NULL;
    }
    memset(ppu->framebuffer, 0, framebuffer_size);


    PPU_Reset(ppu);
    return ppu;
}

void PPU_Destroy(PPU *ppu) {
    if (ppu) {
        if (ppu->framebuffer) {
#if defined(_MSC_VER) && !(__STDC_VERSION__ >= 201112L) // Use _aligned_free if _aligned_malloc was used
            _aligned_free(ppu->framebuffer);
#else // Standard free for aligned_alloc or calloc
            free(ppu->framebuffer);
#endif
        }
        free(ppu);
    }
}

void PPU_Reset(PPU *ppu) {
    memset(ppu->vram, 0, sizeof(ppu->vram));
    memset(ppu->palette, 0, sizeof(ppu->palette));
    memset(ppu->oam, 0, sizeof(ppu->oam)); 
    memset(ppu->secondary_oam, 0xFF, sizeof(ppu->secondary_oam));
    memset(ppu->secondary_oam_original_indices, 0, sizeof(ppu->secondary_oam_original_indices));

    ppu->ctrl = 0;
    ppu->mask = 0;
    ppu->status = PPUSTATUS_VBLANK;
    ppu->oam_addr = 0;
    
    ppu->addr_latch = 0; 
    ppu->fine_x = 0;
    ppu->data_buffer = 0; 
    ppu->open_bus = 0;
    ppu->open_bus_last_update_ms = 0;

    ppu->vram_addr = 0;   
    ppu->temp_addr = 0;   

    ppu->scanline = ppu->nes->settings.timing.scanline_prerender; 
    ppu->cycle = 0;
    ppu->frame_odd = false; 
    ppu->frame_count = 0;

    ppu->nmi_occured = false;
    ppu->nmi_output = false; 
    ppu->nmi_interrupt_line = false;
    ppu->previous_nmi_output = false;
    ppu->suppress_vblank_start = false;

    ppu->bg_nt_latch = 0;
    ppu->bg_at_latch_low = 0;
    ppu->bg_at_latch_high = 0;
    ppu->bg_pt_low_latch = 0;
    ppu->bg_pt_high_latch = 0;
    ppu->bg_pattern_shift_low = 0;
    ppu->bg_pattern_shift_high = 0;
    ppu->bg_attrib_shift_low = 0;
    ppu->bg_attrib_shift_high = 0;

    ppu->sprite_count_current_scanline = 0;
    ppu->sprite_zero_found_for_next_scanline = false;
    memset(ppu->sprite_shifters, 0, sizeof(ppu->sprite_shifters));
    
    // Framebuffer is cleared in PPU_Create, not reset usually, unless explicitly needed.
    // memset(ppu->framebuffer, 0, PPU_FRAMEBUFFER_WIDTH * PPU_FRAMEBUFFER_HEIGHT * sizeof(uint32_t));

    ppu->mirror_mode = MIRROR_HORIZONTAL; 
}


uint8_t PPU_ReadRegister(PPU *ppu, uint16_t addr) {
    ppu_decay_open_bus(ppu);
    uint8_t data = ppu->open_bus;
    switch (addr & 0x0007) {
        case 0x0002: // PPUSTATUS ($2002)
            data = (ppu->status & 0xE0) | (ppu->open_bus & 0x1F);
            ppu->status &= ~PPUSTATUS_VBLANK; 
            ppu->nmi_occured = false;
            bool first_vblank_read_cycle = (ppu->cycle == 0) || (ppu->cycle == 1 && ppu->frame_odd && (ppu->mask & PPUMASK_SHOW_BG));
            ppu->suppress_vblank_start = (ppu->scanline == ppu->nes->settings.timing.scanline_vblank && first_vblank_read_cycle);
            ppu_update_nmi_line(ppu);
            ppu->addr_latch = 0;              
            ppu_drive_open_bus(ppu, data);
            break;

        case 0x0004: // OAMDATA ($2004)
            // TODO: More accurate OAMDATA read behavior during rendering
            data = ppu->oam[ppu->oam_addr];
            ppu_drive_open_bus(ppu, data);
            break;

        case 0x0007: // PPUDATA ($2007)
            if (ppu->vram_addr <= 0x3EFF) { 
                data = ppu->data_buffer; 
                ppu->data_buffer = ppu_read_vram(ppu, ppu->vram_addr); 
            } else { 
                uint8_t palette_data = ppu_read_vram(ppu, ppu->vram_addr);
                uint8_t palette_mask = (ppu->mask & PPUMASK_GRAYSCALE) ? 0x30 : 0x3F;
                data = (ppu->open_bus & 0xC0) | (palette_data & palette_mask);
                ppu->data_buffer = ppu_read_vram(ppu, ppu->vram_addr & 0x2FFF); // Palette reads buffer with underlying VRAM
            }
            ppu->vram_addr += (ppu->ctrl & PPUCTRL_VRAM_INCREMENT) ? 32 : 1;
            ppu->vram_addr &= 0x3FFF; 
            ppu_drive_open_bus(ppu, data);
            break;

        default: // Write-only registers or unmapped reads
            data = ppu->open_bus;
            //DEBUG_WARN("PPU_ReadRegister: Reading from PPU register 0x%04X (effective 0x%04X)", addr, addr & 0x0007);
            break;
    }
    return data;
}

void PPU_WriteRegister(PPU *ppu, uint16_t addr, uint8_t value) {
    ppu_drive_open_bus(ppu, value);
    
    switch (addr & 0x0007) {
        case 0x0000: // PPUCTRL ($2000)
            ppu->ctrl = value;
            ppu->nmi_output = (value & PPUCTRL_NMI_ENABLE) != 0;
            ppu->temp_addr = (ppu->temp_addr & 0xF3FF) | ((uint16_t)(value & 0x03) << 10);
            ppu_update_nmi_line(ppu);
            break;

        case 0x0001: // PPUMASK ($2001)
            ppu->mask = value;
            break;

        case 0x0002: // PPUSTATUS ($2002) - Read-only
            break;

        case 0x0003: // OAMADDR ($2003)
            ppu->oam_addr = value;
            break;

        case 0x0004: // OAMDATA ($2004)
            // TODO: More accurate OAMDATA write behavior during rendering
            if (!((ppu->scanline >= 0 && ppu->scanline <= (ppu->nes->settings.timing.scanlines_visible - 1)) && (ppu->cycle >= 1 && ppu->cycle <= 256) && (ppu->mask & (PPUMASK_SHOW_BG | PPUMASK_SHOW_SPRITES)))) {
                 ppu->oam[ppu->oam_addr] = value;
            }
            ppu->oam_addr++;
            break;

        case 0x0005: // PPUSCROLL ($2005)
            if (ppu->addr_latch == 0) { 
                ppu->temp_addr = (ppu->temp_addr & 0xFFE0) | (value >> 3); 
                ppu->fine_x = value & 0x07;                               
                ppu->addr_latch = 1;
            } else { 
                ppu->temp_addr = (ppu->temp_addr & 0x8C1F) | ((uint16_t)(value & 0xF8) << 2) | ((uint16_t)(value & 0x07) << 12); 
                ppu->addr_latch = 0;
            }
            break;

        case 0x0006: // PPUADDR ($2006)
            if (ppu->addr_latch == 0) { 
                ppu->temp_addr = (ppu->temp_addr & 0x00FF) | ((uint16_t)(value & 0x3F) << 8);
                ppu->addr_latch = 1;
            } else { 
                ppu->temp_addr = (ppu->temp_addr & 0xFF00) | value;
                ppu->vram_addr = ppu->temp_addr; 
                ppu->vram_addr &= 0x3FFF; 
                ppu->addr_latch = 0;
            }
            break;

        case 0x0007: // PPUDATA ($2007)
            ppu_write_vram(ppu, ppu->vram_addr, value);
            ppu->vram_addr += (ppu->ctrl & PPUCTRL_VRAM_INCREMENT) ? 32 : 1;
            ppu->vram_addr &= 0x3FFF; 
            break;
    }
}

inline void PPU_DoOAMDMA(PPU *ppu, const uint8_t *dma_page_data) {
    memcpy(ppu->oam, dma_page_data, 256);
    // OAMADDR behavior during/after DMA is complex and varies. Some emus set to 0.
    // For simplicity, we don't modify ppu->oam_addr here.
}

void PPU_DriveOpenBus(PPU *ppu, uint8_t value) {
    ppu_drive_open_bus(ppu, value);
}

uint8_t PPU_GetOpenBusWithDecay(PPU *ppu) {
    ppu_decay_open_bus(ppu);
    return ppu->open_bus;
}

void PPU_TriggerNMI(PPU *ppu) { // Usually called by PPU_Step logic
    ppu_update_nmi_line(ppu);
}

void PPU_SetMirroring(PPU *ppu, MirrorMode mode) {
    ppu->mirror_mode = mode;
}

void PPU_Step(PPU *ppu) {
    bool rendering_enabled = (ppu->mask & PPUMASK_SHOW_BG) || (ppu->mask & PPUMASK_SHOW_SPRITES);

    if (ppu->scanline == ppu->nes->settings.timing.scanline_prerender) { // Pre-render line
        bool first_prerender_cycle = (ppu->cycle == 0) || (ppu->cycle == 1 && ppu->frame_odd && rendering_enabled && (ppu->mask & PPUMASK_SHOW_BG));
        if (first_prerender_cycle) {
            ppu->status &= ~(PPUSTATUS_VBLANK | PPUSTATUS_SPRITE_0_HIT | PPUSTATUS_SPRITE_OVERFLOW);
            ppu->nmi_occured = false;
            ppu_update_nmi_line(ppu);
        }
        if (rendering_enabled && ppu->cycle >= 280 && ppu->cycle <= 304) {
            copy_vertical_bits(ppu);
        }
    }

    bool is_render_scanline = (ppu->scanline <= (ppu->nes->settings.timing.scanlines_visible - 1)) || ppu->scanline == ppu->nes->settings.timing.scanline_prerender;



    // --- Pixel Rendering (Cycles 1-256 of visible scanlines 0-(ppu->nes->settings.timing.scanlines_visible - 1)) ---
    if (ppu->scanline <= (ppu->nes->settings.timing.scanlines_visible - 1) && ppu->cycle >= 1 && ppu->cycle <= 256) {
        int x = ppu->cycle - 1; 
        int y = ppu->scanline;  

        uint8_t bg_pixel_pattern_val = 0;
        uint8_t bg_palette_idx = 0;
        
        bool bg_visible_at_pixel = (ppu->mask & PPUMASK_SHOW_BG) && (x >= 8 || (ppu->mask & PPUMASK_CLIP_BG));
        if (bg_visible_at_pixel) {
            uint16_t bit_selector = 0x8000 >> ppu->fine_x;
            uint8_t pt_bit0 = (ppu->bg_pattern_shift_low & bit_selector) ? 1 : 0;
            uint8_t pt_bit1 = (ppu->bg_pattern_shift_high & bit_selector) ? 1 : 0;
            bg_pixel_pattern_val = (pt_bit1 << 1) | pt_bit0;

            uint8_t attrib_bit0 = (ppu->bg_attrib_shift_low & bit_selector) ? 1 : 0;
            uint8_t attrib_bit1 = (ppu->bg_attrib_shift_high & bit_selector) ? 1 : 0;
            bg_palette_idx = (attrib_bit1 << 1) | attrib_bit0;
        }

        uint8_t final_bg_color_idx = (bg_pixel_pattern_val == 0) ? 
                                     ppu->palette[0] : // Universal BG color from $3F00
                                     ppu->palette[0x00 + (bg_palette_idx << 2) + bg_pixel_pattern_val];
        final_bg_color_idx &= 0x3F;


        uint8_t spr_pixel_pattern_val = 0;
        uint8_t spr_final_color_idx = 0; // Universal BG if sprite is transparent
        bool spr_is_opaque = false;
        bool spr_is_foreground = true; 
        
        // Check sprite 0 opacity INDEPENDENTLY of which sprite actually renders
        // This is needed for proper sprite 0 hit detection
        bool sprite_0_opaque_at_pixel = false;
        if (ppu->mask & PPUMASK_SHOW_SPRITES) {
            for (int i = 0; i < ppu->sprite_count_current_scanline; ++i) {
                SpriteShifter* s = &ppu->sprite_shifters[i];
                if (s->original_oam_index == 0 && x >= s->x_pos && x < (s->x_pos + 8)) {
                    int col_in_sprite = x - s->x_pos;
                    if (s->attributes & 0x40) { col_in_sprite = 7 - col_in_sprite; }

                    uint8_t spr_pt_bit0 = (s->pattern_low >> (7 - col_in_sprite)) & 1;
                    uint8_t spr_pt_bit1 = (s->pattern_high >> (7 - col_in_sprite)) & 1;
                    uint8_t spr_val = (spr_pt_bit1 << 1) | spr_pt_bit0;

                    if (spr_val != 0) {
                        sprite_0_opaque_at_pixel = true;
                    }
                    break; // Only one sprite 0, so we can break after checking it
                }
            }
        }

        bool sprites_visible_at_pixel = (ppu->mask & PPUMASK_SHOW_SPRITES) && (x >= 8 || (ppu->mask & PPUMASK_CLIP_SPRITES));
        if (sprites_visible_at_pixel) {
            for (int i = 0; i < ppu->sprite_count_current_scanline; ++i) {
                SpriteShifter* s = &ppu->sprite_shifters[i];
                if (x >= s->x_pos && x < (s->x_pos + 8)) { 
                    int col_in_sprite = x - s->x_pos;
                    if (s->attributes & 0x40) { col_in_sprite = 7 - col_in_sprite; }

                    uint8_t spr_pt_bit0 = (s->pattern_low >> (7 - col_in_sprite)) & 1;
                    uint8_t spr_pt_bit1 = (s->pattern_high >> (7 - col_in_sprite)) & 1;
                    uint8_t current_spr_val = (spr_pt_bit1 << 1) | spr_pt_bit0;

                    if (current_spr_val != 0) { 
                        spr_pixel_pattern_val = current_spr_val;
                        uint8_t spr_palette_idx_bits = s->attributes & 0x03;
                        spr_final_color_idx = ppu->palette[0x10 + (spr_palette_idx_bits << 2) + spr_pixel_pattern_val];
                        spr_final_color_idx &= 0x3F;
                        
                        spr_is_opaque = true;
                        spr_is_foreground = !(s->attributes & 0x20); 

                        break; // Highest priority sprite found for this pixel
                    }
                }
            }
        }
        
        // Sprite 0 Hit Detection
        if (sprite_0_opaque_at_pixel && bg_pixel_pattern_val != 0 &&
            bg_visible_at_pixel && sprites_visible_at_pixel && // Both BG & Sprites must be enabled and not clipped for this pixel
            x < 255 && // Hit does not occur on pixel 255
            !(ppu->status & PPUSTATUS_SPRITE_0_HIT)) {
             ppu->status |= PPUSTATUS_SPRITE_0_HIT;
        }


        uint8_t combined_color_idx;
        if (spr_is_opaque) {
            if (bg_pixel_pattern_val == 0) { // BG transparent
                combined_color_idx = spr_final_color_idx;
            } else { // Both opaque
                if (spr_is_foreground) {
                    combined_color_idx = spr_final_color_idx;
                } else { // Sprite behind BG
                    combined_color_idx = final_bg_color_idx;
                }
            }
        } else { // Sprite transparent
            combined_color_idx = final_bg_color_idx;
        }
        
        uint32_t final_pixel_color = ppu->nes->settings.video.palette[combined_color_idx];
        final_pixel_color = apply_color_emphasis(final_pixel_color, ppu->mask);
        
        ppu->framebuffer[y * PPU_FRAMEBUFFER_WIDTH + x] = final_pixel_color;
    }

    if (is_render_scanline && rendering_enabled) {
        // Background Shifter Updates (Cycles 1-256 & 321-336)
        if ((ppu->cycle >= 1 && ppu->cycle <= 256) || (ppu->cycle >= 321 && ppu->cycle <= 336)) {
            ppu->bg_pattern_shift_low <<= 1;
            ppu->bg_pattern_shift_high <<= 1;
            ppu->bg_attrib_shift_low <<= 1;
            ppu->bg_attrib_shift_high <<= 1;
        }

        // Background Fetching Logic (Cycles 1-256 & 321-336, actions on specific cycle % 8)
        bool is_fetch_cycle_range = (ppu->cycle >= 1 && ppu->cycle <= 256) || (ppu->cycle >= 321 && ppu->cycle <= 336);
        if (is_fetch_cycle_range) {
            switch (ppu->cycle % 8) {
                case 1: load_background_tile_data(ppu); break;
                case 0: // cycle 8, 16, ..., 256 or 328, 336
                    feed_background_shifters(ppu);
                    increment_coarse_x(ppu);
                    break;
            }
        }
        
        if (ppu->cycle == 256) { // End of visible pixel rendering for the line
            increment_fine_y(ppu);
        }

        if (ppu->cycle == 257) {
            copy_horizontal_bits(ppu);
            if (ppu->scanline <= (ppu->nes->settings.timing.scanlines_visible - 1)) { // Evaluate sprites for the *next* scanline, but data is for current render pass
                 evaluate_sprites(ppu); // Evaluates sprites that will be visible on `ppu->scanline`
            } else if (ppu->scanline == ppu->nes->settings.timing.scanline_prerender) { // Pre-render line clears sprites for scanline 0
                 ppu->sprite_count_current_scanline = 0;
            }
        }
        
        // Sprite Pattern Fetching (Simplified: after BG prefetch for next line starts)
        if (ppu->cycle == 321 && ppu->scanline <= (ppu->nes->settings.timing.scanlines_visible - 1)) {
            fetch_sprite_patterns(ppu); // Fetches patterns for sprites on `ppu->scanline`
        }
    }

    bool first_vblank_cycle = (ppu->cycle == 0) || (ppu->cycle == 1 && ppu->frame_odd && rendering_enabled && (ppu->mask & PPUMASK_SHOW_BG));
    if (ppu->scanline == ppu->nes->settings.timing.scanline_vblank && first_vblank_cycle) {
        if (!ppu->suppress_vblank_start) {
            ppu->status |= PPUSTATUS_VBLANK; 
            ppu->nmi_occured = true;
            PPU_TriggerNMI(ppu); // Check if NMI should be asserted
        }
        ppu->suppress_vblank_start = false;
        
        //if (ppu->nes && ppu->nes->frame_ready_callback) { // If a callback is set up
           //ppu->nes->frame_ready_callback(ppu->nes);
        //}
    }

    ppu->cycle++;
    if (ppu->cycle > 340) { 
        ppu->cycle = 0;
        ppu->scanline++;

        if (ppu->scanline == ppu->nes->settings.timing.scanline_prerender && ppu->frame_odd && rendering_enabled && (ppu->mask & PPUMASK_SHOW_BG)) { // Odd frame, BG enabled
            ppu->cycle = 1; // Skip cycle 0 (dummy NT fetch)
        }
        
        if (ppu->scanline > ppu->nes->settings.timing.scanline_prerender) { 
            ppu->scanline = 0;     
            ppu->frame_odd = !ppu->frame_odd; 
            ppu->frame_count++;
        }
    }
}


// --- CHR ROM/RAM access ---
inline uint8_t PPU_CHR_Read(PPU *ppu, uint16_t addr) {
    return BUS_PPU_ReadCHR(ppu->nes->bus, addr);
}

inline void PPU_CHR_Write(PPU *ppu, uint16_t addr, uint8_t value) {
    BUS_PPU_WriteCHR(ppu->nes->bus, addr, value);
}

// --- Expose NES palette ---
const uint32_t* PPU_GetPalette(PPU *ppu) {
    if (ppu && ppu->nes) {
        return ppu->nes->settings.video.palette;
    }
    return NULL;
}

// --- Expose pattern table data ---
void PPU_GetPatternTableData(PPU* ppu, int table_idx, uint8_t* buffer_128x128_indexed_pixels) {
    uint16_t base_addr = (table_idx == 0) ? 0x0000 : 0x1000;

    for (int tile_y = 0; tile_y < 16; ++tile_y) {
        for (int tile_x = 0; tile_x < 16; ++tile_x) {
            uint16_t tile_offset_in_pt = (tile_y * 16 + tile_x) * 16;
            for (int row = 0; row < 8; ++row) {
                // Note: BUS_PPU_ReadCHR might involve mapper logic.
                uint8_t pt_low = BUS_PPU_ReadCHR(ppu->nes->bus, base_addr + tile_offset_in_pt + row);
                uint8_t pt_high = BUS_PPU_ReadCHR(ppu->nes->bus, base_addr + tile_offset_in_pt + row + 8);
                for (int col = 0; col < 8; ++col) {
                    uint8_t bit0 = (pt_low >> (7 - col)) & 1;
                    uint8_t bit1 = (pt_high >> (7 - col)) & 1;
                    uint8_t pixel_palette_idx = (bit1 << 1) | bit0; // 0-3

                    int buffer_x = tile_x * 8 + col;
                    int buffer_y = tile_y * 8 + row;
                    buffer_128x128_indexed_pixels[buffer_y * 128 + buffer_x] = pixel_palette_idx;
                }
            }
        }
    }
}

// --- Expose nametable ---
const uint8_t* PPU_GetNametable(PPU* ppu, int index) {
    if (index < 0 || index > 3) {
        return NULL; // Invalid index
    }
    uint16_t logical_base_addr = 0x2000 + index * 0x0400;
    uint16_t physical_addr = mirror_vram_addr(ppu, logical_base_addr); // This returns offset in the 2KB VRAM

    if (physical_addr < PPU_VRAM_SIZE) { // Ensure it's within bounds of the ppu->vram array
        return &ppu->vram[physical_addr];
    }
    return NULL; // Should not happen with correct mirroring to 2KB VRAM
}

// --- Expose palette RAM ---
const uint8_t* PPU_GetPaletteRAM(PPU* ppu) {
    return ppu->palette;
}

// --- Expose OAM ---
const uint8_t* PPU_GetOAM(PPU* ppu) {
    return ppu->oam;
}

// --- Expose scanline/cycle ---
void PPU_GetScanlineCycle(PPU* ppu, int* scanline, int* cycle) {
    if (scanline) *scanline = ppu->scanline;
    if (cycle) *cycle = ppu->cycle;
}