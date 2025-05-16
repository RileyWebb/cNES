#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "debug.h"    // Assuming debug logging is desired
#include "cNES/nes.h" // Assuming NES structure is needed
#include "cNES/bus.h" // Assuming BUS access is needed

#include "cNES/ppu.h"

// --- NES master palette (64 colors, RGBA format) ---
// This palette is standard and remains the same.
static const uint32_t nes_palette[64] = {
    0x666666FF, 0x002A88FF, 0x1412A7FF, 0x3B00A4FF, 0x5C007EFF, 0x6E0040FF, 0x6C0600FF, 0x561D00FF,
    0x333500FF, 0x0B4800FF, 0x005200FF, 0x004F08FF, 0x00404DFF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xADADADFF, 0x155FD9FF, 0x4240FFFF, 0x7527FEFF, 0xA01ACCFF, 0xB71E7BFF, 0xB53120FF, 0x994E00FF,
    0x6B6D00FF, 0x388700FF, 0x0E9300FF, 0x008F32FF, 0x007C8DFF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xFFFEFFFF, 0x64B0FFFF, 0x9290FFFF, 0xC676FFFF, 0xF36AFFFF, 0xFE6ECCFF, 0xFE8170FF, 0xEA9E22FF,
    0xBCBE00FF, 0x88D800FF, 0x5CE430FF, 0x45E082FF, 0x48CDDEFF, 0x4F4F4FFF, 0x000000FF, 0x000000FF,
    0xFFFEFFFF, 0xC0DFFFFF, 0xD3D2FFFF, 0xE8C8FFFF, 0xFBC2FFFF, 0xFEC4EAFF, 0xFECCC5FF, 0xF7D8A5FF,
    0xE4E594FF, 0xCFEE96FF, 0xBDF4ABFF, 0xB3F3CCFF, 0xB5EBF2FF, 0xB8B8B8FF, 0x000000FF, 0x000000FF
};

// --- Helper Functions ---

// Mirrors a PPU nametable address based on the current mirroring mode.
// Handles horizontal, vertical, single screen, and four-screen (mapper-dependent).
static uint16_t mirror_vram_addr(PPU *ppu, uint16_t addr) {
    addr &= 0x0FFF; // Mask to the 4 nametable region (0x000 - 0xFFF relative to $2000)
                    // This results in an offset within the 4KB nametable space.

    uint16_t table_index = addr / 0x0400;     // Nametable index (0, 1, 2, or 3)
    uint16_t offset_in_table = addr % 0x0400; // Offset within that 1KB nametable (0x000 - 0x3FF)

    switch (ppu->mirror_mode) {
        case MIRROR_HORIZONTAL:
            // Nametables 0 ($2000) and 1 ($2400) map to VRAM bank 0.
            // Nametables 2 ($2800) and 3 ($2C00) map to VRAM bank 1.
            if (table_index == 0 || table_index == 1) { // NT0 or NT1
                return offset_in_table;                 // Maps to VRAM $0000 - $03FF
            } else {                                    // NT2 or NT3
                return offset_in_table + 0x0400;        // Maps to VRAM $0400 - $07FF
            }
        case MIRROR_VERTICAL:
            // Nametables 0 ($2000) and 2 ($2800) map to VRAM bank 0.
            // Nametables 1 ($2400) and 3 ($2C00) map to VRAM bank 1.
            if (table_index == 0 || table_index == 2) { // NT0 or NT2
                return offset_in_table;                 // Maps to VRAM $0000 - $03FF
            } else {                                    // NT1 or NT3
                return offset_in_table + 0x0400;        // Maps to VRAM $0400 - $07FF
            }
        case MIRROR_SINGLE_SCREEN_LOW: // Or MIRROR_SINGLE_SCREEN_A
            // All nametables map to the first 1KB of PPU VRAM (bank 0).
            return offset_in_table; // Maps to VRAM $0000 - $03FF
        case MIRROR_SINGLE_SCREEN_HIGH: // Or MIRROR_SINGLE_SCREEN_B
            // All nametables map to the second 1KB of PPU VRAM (bank 1).
            return offset_in_table + 0x0400; // Maps to VRAM $0400 - $07FF
        case MIRROR_FOUR_SCREEN:
            // Each nametable maps to a distinct 1KB area.
            // This mode requires external VRAM provided by the cartridge/mapper.
            // The PPU itself doesn't handle this mapping; it relies on the bus/mapper.
            // We return the original relative address within the 4KB nametable space.
            return addr; // The mapper will translate this 0x0000-0x0FFF range.
        default:
            // Fallback or error, though should ideally not happen with valid modes.
            // Default to horizontal or a known safe mode if unsure.
            DEBUG_WARN("PPU: Unknown mirroring mode %d, defaulting to horizontal.", ppu->mirror_mode);
            if (table_index == 0 || table_index == 1) {
                return offset_in_table;
            } else {
                return offset_in_table + 0x0400;
            }
    }
}

// Reads a byte from PPU memory (VRAM, CHR ROM/RAM, Palette RAM).
// Handles mirroring and palette specifics.
static uint8_t ppu_read_vram(PPU *ppu, uint16_t addr) {
    addr &= 0x3FFF; // Mask to valid PPU address space

    if (addr < 0x2000) {
        // CHR ROM/RAM ($0000 - $1FFF) - handled by the mapper via the bus
        return BUS_PPU_ReadCHR(ppu->nes->bus, addr);
    } else if (addr < 0x3F00) {
        // Nametable RAM ($2000 - $3EFF), includes mirrors up to $3EFF
        // The range $3000-$3EFF is a mirror of $2000-$2EFF.
        // mirror_vram_addr handles mapping $2000-$2FFF (logical) to physical VRAM.
        return ppu->vram[mirror_vram_addr(ppu, addr & 0x2FFF)];
    } else if (addr < 0x4000) {
        // Palette RAM ($3F00 - $3FFF)
        uint16_t pal_addr = addr & 0x1F; // Mask to 32 bytes (0x00 - 0x1F)
        // Hardware mirroring: $3F10, $3F14, $3F18, $3F1C mirror $3F00, $3F04, $3F08, $3F0C respectively.
        // These are the universal background color slots for the sprite palettes.
        if ((pal_addr & 0x03) == 0) { // If address is $xx00, $xx04, $xx08, $xx0C
            pal_addr &= ~0x10;        // Clear bit 4 to map $3F1x to $3F0x
                                      // e.g., $10 -> $00, $14 -> $04
        }
        return ppu->palette[pal_addr];
    }
    // Should not be reached if addr is correctly masked.
    DEBUG_ERROR("PPU: Unhandled PPU VRAM read at address 0x%04X", addr);
    return 0;
}

// Writes a byte to PPU memory (VRAM, CHR RAM, Palette RAM).
// Handles mirroring and palette specifics.
static void ppu_write_vram(PPU *ppu, uint16_t addr, uint8_t value) {
    addr &= 0x3FFF; // Mask to valid PPU address space

    if (addr < 0x2000) {
        // CHR RAM ($0000 - $1FFF) - handled by the mapper via the bus (if CHR RAM is present)
        BUS_PPU_WriteCHR(ppu->nes->bus, addr, value);
    } else if (addr < 0x3F00) {
        // Nametable RAM ($2000 - $3EFF)
        // The range $3000-$3EFF is a mirror of $2000-$2EFF.
        ppu->vram[mirror_vram_addr(ppu, addr & 0x2FFF)] = value;
    } else if (addr < 0x4000) {
        // Palette RAM ($3F00 - $3FFF)
        uint16_t pal_addr = addr & 0x1F; // Mask to 32 bytes
        // Hardware mirroring for writes (same as reads)
        if ((pal_addr & 0x03) == 0) {
            pal_addr &= ~0x10;
        }
        ppu->palette[pal_addr] = value;
    } else {
        DEBUG_ERROR("PPU: Unhandled PPU VRAM write at address 0x%04X", addr);
    }
}

// --- VRAM Address Update Helpers (Scrolling) ---
// Based on NESDev Wiki: PPU scrolling
// vram_addr (v) and temp_addr (t) bitfields:
// yyy NN YYYYY XXXXX
// ||| || ||||| +++++-- coarse X scroll (0-31)
// ||| || +++++-------- coarse Y scroll (0-31)
// ||| ++------------- nametable select (0-3) (bits 11-10)
// +++---------------- fine Y scroll (0-7) (bits 14-12)

// Increment coarse X in vram_addr (v)
static void increment_coarse_x(PPU *ppu) {
    if ((ppu->vram_addr & 0x001F) == 31) { // If coarse X is 31 (max)
        ppu->vram_addr &= ~0x001F;         // Coarse X = 0
        ppu->vram_addr ^= 0x0400;          // Switch horizontal nametable (toggle bit 10)
    } else {
        ppu->vram_addr += 1;               // Increment coarse X
    }
}

// Increment fine Y in vram_addr (v)
static void increment_fine_y(PPU *ppu) {
    if ((ppu->vram_addr & 0x7000) != 0x7000) { // If fine Y < 7
        ppu->vram_addr += 0x1000;              // Increment fine Y
    } else {
        ppu->vram_addr &= ~0x7000;             // Fine Y = 0
        uint16_t y = (ppu->vram_addr & 0x03E0) >> 5; // Get coarse Y (bits 9-5)
        if (y == 29) {                         // Coarse Y is 29 (bottom of nametable row)
            y = 0;                             // Coarse Y = 0
            ppu->vram_addr ^= 0x0800;          // Switch vertical nametable (toggle bit 11)
        } else if (y == 31) {                  // Coarse Y is 31 (attribute table wrap)
            y = 0;                             // Coarse Y = 0 (doesn't switch nametable)
        } else {
            y += 1;                            // Increment coarse Y
        }
        ppu->vram_addr = (ppu->vram_addr & ~0x03E0) | (y << 5); // Put coarse Y back
    }
}

// Copy horizontal scroll bits from temp_addr (t) to vram_addr (v)
// Bits: Coarse X (5 bits) and Nametable select X (bit 10)
static void copy_horizontal_bits(PPU *ppu) {
    // Mask: 0000 0100 0001 1111 (binary) = 0x041F
    ppu->vram_addr = (ppu->vram_addr & ~0x041F) | (ppu->temp_addr & 0x041F);
}

// Copy vertical scroll bits from temp_addr (t) to vram_addr (v)
// Bits: Fine Y (3 bits, 14-12), Nametable select Y (bit 11), Coarse Y (5 bits, 9-5)
static void copy_vertical_bits(PPU *ppu) {
    // Mask: 0111 1011 1110 0000 (binary) = 0x7BE0
    ppu->vram_addr = (ppu->vram_addr & ~0x7BE0) | (ppu->temp_addr & 0x7BE0);
}

// --- Background Rendering Pipeline Helpers ---
// Note: This is a simplified model. A cycle-accurate PPU has a more complex pipeline
// with individual fetches for NT, AT, PT low, PT high bytes, each taking 2 cycles,
// and internal latches. Here, we fetch all data for a tile at once.

// Loads the next background tile's data into the PPU's internal latches.
static void load_background_tile_data(PPU *ppu) {
    // Nametable byte: Tile index
    uint16_t nt_addr = 0x2000 | (ppu->vram_addr & 0x0FFF); // Address for tile index
    ppu->bg_nt_latch = ppu_read_vram(ppu, nt_addr);

    // Attribute Table byte: Palette for a 4x4 tile area (32x32 pixels)
    uint16_t at_addr = 0x23C0 | (ppu->vram_addr & 0x0C00) |   // Nametable select (bits 11-10 of v)
                       ((ppu->vram_addr >> 4) & 0x38) |      // Coarse Y / 4 (bits 8-6 of v, effectively)
                       ((ppu->vram_addr >> 2) & 0x07);      // Coarse X / 4 (bits 3-1 of v, effectively)
    uint8_t at_byte = ppu_read_vram(ppu, at_addr);

    // Determine which 2 bits of the attribute byte to use for the current 2x2 tile group
    int shift = ((ppu->vram_addr >> 4) & 0x04) | // Use bit 6 of v (part of Coarse Y / 2)
                ((ppu->vram_addr >> 1) & 0x02);  // Use bit 2 of v (part of Coarse X / 2)
                // Corrected shift: ((coarse_y / 2) % 2) * 4 + ((coarse_x / 2) % 2) * 2
                // Or simpler: ( (vram_addr >> 4) & 4 ) | ( (vram_addr >> 2) & 2 )
                // Example: Coarse Y bit 2 -> (vram_addr & 0x0040) >> 4 -> bit 2 of AT byte y select
                //          Coarse X bit 2 -> (vram_addr & 0x0004) >> 1 -> bit 0 of AT byte x select
    uint8_t palette_bits = (at_byte >> shift) & 0x03; // Get 2 palette bits

    ppu->bg_at_latch_low = (palette_bits & 0x01) ? 0xFF : 0x00; // Expand to 8 bits for shifter
    ppu->bg_at_latch_high = (palette_bits & 0x02) ? 0xFF : 0x00;// Expand to 8 bits for shifter

    // Pattern Table bytes: Tile graphics
    uint16_t fine_y = (ppu->vram_addr >> 12) & 7;             // Fine Y (bits 14-12 of v)
    uint16_t pt_base = (ppu->ctrl & PPUCTRL_BG_TABLE_ADDR) ? 0x1000 : 0; // BG Pattern Table Address ($2000 bit 4)
    uint16_t tile_offset = ppu->bg_nt_latch * 16;             // Each tile is 16 bytes
    
    uint16_t pt_addr_low = pt_base + tile_offset + fine_y;
    ppu->bg_pt_low_latch = ppu_read_vram(ppu, pt_addr_low);
    ppu->bg_pt_high_latch = ppu_read_vram(ppu, pt_addr_low + 8); // High byte is 8 bytes after low
}

// Pushes the latched background tile data into the shift registers.
static void feed_background_shifters(PPU *ppu) {
    // Load pattern data into the lower 8 bits of the 16-bit shift registers
    ppu->bg_pattern_shift_low = (ppu->bg_pattern_shift_low & 0xFF00) | ppu->bg_pt_low_latch;
    ppu->bg_pattern_shift_high = (ppu->bg_pattern_shift_high & 0xFF00) | ppu->bg_pt_high_latch;

    // Load attribute data into the lower 8 bits of the 16-bit attribute shifters
    // These already contain expanded 8-bit values from the latches.
    ppu->bg_attrib_shift_low = (ppu->bg_attrib_shift_low & 0xFF00) | ppu->bg_at_latch_low;
    ppu->bg_attrib_shift_high = (ppu->bg_attrib_shift_high & 0xFF00) | ppu->bg_at_latch_high;
}


// --- Sprite Evaluation and Rendering Helpers ---

// Performs sprite evaluation for the *next* scanline.
// Happens during cycles 65-256 of visible and pre-render scanlines (simplified here to occur at cycle 257).
static void evaluate_sprites(PPU *ppu) {
    ppu->sprite_count_current_scanline = 0; // Sprites found for the *next* scanline
    ppu->sprite_zero_on_current_scanline = false; // Will sprite 0 be on the *next* scanline?
    ppu->status &= ~PPUSTATUS_SPRITE_OVERFLOW; // Clear overflow flag before evaluation
                                               // More accurately, this flag is sticky and cleared on pre-render line.
                                               // For simplicity, we clear it here.

    // Clear secondary OAM (fill with 0xFF)
    // In hardware, this happens during cycles 1-64 of each scanline.
    memset(ppu->secondary_oam, 0xFF, PPU_SECONDARY_OAM_SIZE);

    uint8_t secondary_oam_idx = 0;
    uint8_t sprite_height = (ppu->ctrl & PPUCTRL_SPRITE_SIZE) ? 16 : 8; // ($2000 bit 5)

    // Iterate through primary OAM (64 sprites)
    for (int oam_idx = 0; oam_idx < 64; ++oam_idx) {
        uint8_t sprite_y = ppu->oam[oam_idx * 4 + 0]; // Y position of sprite's top
        
        // Calculate vertical distance from current scanline to sprite's top.
        // Evaluation is for the *next* scanline to be rendered.
        // If current scanline is S, we are evaluating for scanline S+1.
        // However, PPU sprite Y coordinates are "off by one" for rendering;
        // a sprite with Y=0 is on the first visible scanline.
        // So, if `ppu->scanline` is the scanline currently being processed by PPU_Step,
        // we check if this sprite is visible on `ppu->scanline`.
        int row_on_scanline = ppu->scanline - sprite_y;

        // Check if the sprite is within the vertical range of the current scanline
        if (row_on_scanline >= 0 && row_on_scanline < sprite_height) {
            if (secondary_oam_idx < 8) {
                // Copy sprite data (Y, Tile, Attributes, X) to secondary OAM
                memcpy(&ppu->secondary_oam[secondary_oam_idx * 4], &ppu->oam[oam_idx * 4], 4);
                
                if (oam_idx == 0) { // Check if this is Sprite 0
                    ppu->sprite_zero_on_current_scanline = true;
                }
                secondary_oam_idx++;
            } else {
                // 8 sprites found, set sprite overflow flag.
                // NES PPU has a buggy overflow detection mechanism that continues scanning
                // OAM incorrectly. This is a simplified version.
                ppu->status |= PPUSTATUS_SPRITE_OVERFLOW;
                // TODO: Implement accurate sprite overflow bug if needed for specific games.
                break; // Stop searching after 8 sprites for simplicity (real PPU continues with bugs)
            }
        }
    }
    ppu->sprite_count_current_scanline = secondary_oam_idx;
}

// Fetches pattern data for the sprites found during evaluation.
// This prepares data for rendering on the *current* `ppu->scanline`.
// In hardware, this is interleaved during cycles 257-320 for the 8 found sprites.
// Simplified here to happen around cycle 321.
static void fetch_sprite_patterns(PPU *ppu) {
    uint8_t sprite_height = (ppu->ctrl & PPUCTRL_SPRITE_SIZE) ? 16 : 8; // ($2000 bit 5)

    for (int i = 0; i < ppu->sprite_count_current_scanline; ++i) {
        uint8_t sprite_y_oam = ppu->secondary_oam[i * 4 + 0]; // Y from OAM
        uint8_t tile_id      = ppu->secondary_oam[i * 4 + 1];
        uint8_t attributes   = ppu->secondary_oam[i * 4 + 2];
        uint8_t sprite_x     = ppu->secondary_oam[i * 4 + 3];

        // Store raw data for rendering pass
        ppu->sprite_shifters[i].x_pos      = sprite_x;
        ppu->sprite_shifters[i].attributes = attributes;
        //ppu->sprite_shifters[i].y_pos      = sprite_y_oam; // Not strictly needed for shifter if row_in_sprite is calc'd

        // Calculate which row of the sprite tile to fetch
        // `ppu->scanline` is the current scanline being rendered.
        // `sprite_y_oam` is the top Y coordinate of the sprite.
        int row_in_sprite = ppu->scanline - sprite_y_oam;

        if (attributes & 0x80) { // Vertical flip (Attribute bit 7)
            row_in_sprite = (sprite_height - 1) - row_in_sprite;
        }

        uint16_t pattern_addr_base;
        if (sprite_height == 16) { // 8x16 sprites
            // Bit 0 of tile_id selects pattern table (0 = $0000, 1 = $1000)
            // Bits 1-7 of tile_id select the tile index (even number for the pair)
            pattern_addr_base = ((tile_id & 0x01) ? 0x1000 : 0x0000) + ((tile_id & 0xFE) * 16);
            if (row_in_sprite >= 8) { // If on the bottom half of the 8x16 sprite
                pattern_addr_base += 16; // Move to the second tile in the pair
                row_in_sprite -= 8;    // Adjust row_in_sprite for the 8x8 tile
            }
        } else { // 8x8 sprites
            // PPUCTRL bit 3 selects pattern table for 8x8 sprites
            pattern_addr_base = ((ppu->ctrl & PPUCTRL_SPRITE_TABLE_ADDR) ? 0x1000 : 0x0000) + (tile_id * 16);
        }
        
        uint16_t pattern_addr = pattern_addr_base + row_in_sprite;

        ppu->sprite_shifters[i].pattern_low  = ppu_read_vram(ppu, pattern_addr);
        ppu->sprite_shifters[i].pattern_high = ppu_read_vram(ppu, pattern_addr + 8);
    }
}


// --- PPU API Implementation ---

PPU *PPU_Create(NES *nes) {
    PPU *ppu = calloc(1, sizeof(PPU));
    if (!ppu) {
        DEBUG_ERROR("PPU_Create: Failed to allocate memory for PPU.");
        return NULL;
    }
    ppu->nes = nes;
    PPU_Reset(ppu);
    return ppu;
}

void PPU_Destroy(PPU *ppu) {
    if (ppu) {
        free(ppu);
    }
}

void PPU_Reset(PPU *ppu) {
    memset(ppu->vram, 0, sizeof(ppu->vram));
    memset(ppu->palette, 0, sizeof(ppu->palette));
    memset(ppu->oam, 0, sizeof(ppu->oam)); // OAM is typically $00 or $FF on reset, $00 is safer.
    memset(ppu->secondary_oam, 0xFF, sizeof(ppu->secondary_oam));

    ppu->ctrl = 0;
    ppu->mask = 0;
    // PPUSTATUS on power-up/reset: VBlank flag usually set, others can be indeterminate.
    // Common practice is to set VBlank, some emulators set Sprite Overflow too.
    // NESDev Wiki: "Upon power/reset, PPUSTATUS reads back as $80..."
    // Forcing other bits (like overflow) to 0 or 1 might be needed for specific test ROMs.
    // Let's use $80 (VBlank set, others clear) for a cleaner start.
    ppu->status = PPUSTATUS_VBLANK;
    ppu->oam_addr = 0;
    
    ppu->addr_latch = 0; // For $2005/$2006
    ppu->fine_x = 0;
    ppu->data_buffer = 0; // PPUDATA read buffer

    ppu->vram_addr = 0;   // Current VRAM address (v)
    ppu->temp_addr = 0;   // Temporary VRAM address (t)

    // Start PPU in VBlank, just before scanline 0.
    // Scanline 261 is the pre-render scanline.
    ppu->scanline = 261; // Start at pre-render line or late VBlank (e.g. 241)
    ppu->cycle = 0;
    ppu->frame_odd = false; // First frame is even

    ppu->nmi_occured = false;
    ppu->nmi_output = false; // From PPUCTRL bit 7
    ppu->nmi_interrupt_line = false; // Actual NMI line state to CPU

    // Background rendering pipeline state
    ppu->bg_nt_latch = 0;
    ppu->bg_at_latch_low = 0;
    ppu->bg_at_latch_high = 0;
    ppu->bg_pt_low_latch = 0;
    ppu->bg_pt_high_latch = 0;
    ppu->bg_pattern_shift_low = 0;
    ppu->bg_pattern_shift_high = 0;
    ppu->bg_attrib_shift_low = 0;
    ppu->bg_attrib_shift_high = 0;

    // Sprite state
    ppu->sprite_count_current_scanline = 0;
    ppu->sprite_zero_on_current_scanline = false;
    // sprite_zero_being_rendered is a per-pixel flag, not global state here.
    memset(ppu->sprite_shifters, 0, sizeof(ppu->sprite_shifters));

    memset(ppu->framebuffer, 0, sizeof(ppu->framebuffer));

    // Default mirroring mode, usually set by cartridge.
    ppu->mirror_mode = MIRROR_HORIZONTAL; // A common default
}

// Reads from PPU registers ($2000-$2007).
uint8_t PPU_ReadRegister(PPU *ppu, uint16_t addr) {
    uint8_t data = 0;
    // PPU registers are mirrored every 8 bytes from $2008-$3FFF.
    switch (addr & 0x0007) {
        case 0x0002: // PPUSTATUS ($2002)
            // Return upper 3 bits of status register, and lower 5 bits of open bus data (often last PPU write)
            // For simplicity, we can use ppu->data_buffer for the lower 5 bits, or just 0.
            data = (ppu->status & 0xE0) | (ppu->data_buffer & 0x1F); // Or just status & 0xE0 for simplicity if open bus isn't detailed
            
            ppu->status &= ~PPUSTATUS_VBLANK; // Reading PPUSTATUS clears VBlank flag
            ppu->addr_latch = 0;              // Resets address latch for $2005/$2006

            // Sprite 0 Hit and Sprite Overflow flags are cleared by the PPU at specific times
            // (dot 1 of pre-render scanline), not directly by reading PPUSTATUS.
            // Some older documentation/emulators might clear them here, but it's less accurate.
            break;

        case 0x0004: // OAMDATA ($2004)
            // Reads from OAMDATA return the value at OAMADDR.
            // During sprite evaluation (cycles 65-256 of rendering scanlines),
            // the OAM aperature might be busy. Reads might return $FF or OAM buffer contents.
            // Outside this, or if OAM is not being accessed by PPU, it reads OAM[OAMADDR].
            // Simplified: return OAM[OAMADDR] unless rendering and PPU is busy with OAM.
            // A common simplification is to return OAM[OAMADDR], as OAMDMA is the primary fill method.
            // Writes to OAMADDR are not inhibited during rendering, but reads can be tricky.
            // If rendering and sprite evaluation is active (cycles 1-256 on visible lines roughly),
            // OAM is read by PPU. Reading $2004 might yield $FF or current internal bus value.
            // For now, return OAM[OAMADDR], assuming reads are not happening during critical internal OAM access.
            data = ppu->oam[ppu->oam_addr];
            // Note: OAMADDR does NOT increment on read.
            break;

        case 0x0007: // PPUDATA ($2007)
            if (ppu->vram_addr <= 0x3EFF) { // VRAM/CHR read (buffered)
                data = ppu->data_buffer; // Return value from previous read
                ppu->data_buffer = ppu_read_vram(ppu, ppu->vram_addr); // Fetch next byte into buffer
            } else { // Palette read ($3F00-$3FFF) (not buffered for return, but buffer still loads)
                data = ppu_read_vram(ppu, ppu->vram_addr);
                // "The VRAM read buffer is simultaneously filled with the byte at PPU $2xxx address
                // that is mirrored by the palette address." - NESDev Wiki
                // This means the buffer gets data from underlying VRAM, not the palette itself.
                ppu->data_buffer = ppu_read_vram(ppu, ppu->vram_addr & 0x2FFF);
            }
            // Increment VRAM address based on PPUCTRL bit 2
            ppu->vram_addr += (ppu->ctrl & PPUCTRL_VRAM_INCREMENT) ? 32 : 1;
            ppu->vram_addr &= 0x3FFF; // Keep within PPU address space (though hardware can go up to 0x7FFF for v)
            break;

        default:
            // Reading from write-only registers ($2000, $2001, $2003, $2005, $2006)
            // typically returns open bus behavior (e.g., last value on data bus, or decaying).
            // A common simplification is to return the last written PPU register value, or parts of PPUSTATUS.
            // For now, returning ppu->data_buffer (last read from PPU bus) is a plausible open bus value.
            data = ppu->data_buffer; // Or (ppu->status & 0x1F) as in original
            DEBUG_WARN("PPU_ReadRegister: Reading from write-only or unhandled PPU register 0x%04X", addr);
            break;
    }
    return data;
}

// Writes to PPU registers ($2000-$2007).
void PPU_WriteRegister(PPU *ppu, uint16_t addr, uint8_t value) {
    // Store last written value for open bus emulation if needed
    // ppu->last_ppu_write = value; 

    // PPU registers are mirrored every 8 bytes from $2008-$3FFF.
    switch (addr & 0x0007) {
        case 0x0000: // PPUCTRL ($2000)
            ppu->ctrl = value;
            ppu->nmi_output = (value & PPUCTRL_NMI_ENABLE) != 0;
            // Update temp_addr's nametable select bits (NN)
            // t: ... NN .. ..... ..... -> value: ...... NN
            ppu->temp_addr = (ppu->temp_addr & 0xF3FF) | ((uint16_t)(value & 0x03) << 10);
            // If NMI is enabled and VBLANK flag is currently set, trigger NMI
            if (ppu->nmi_output && (ppu->status & PPUSTATUS_VBLANK) && ppu->nmi_occured) {
                // nmi_occured ensures NMI is triggered only once per VBLANK edge by this PPUCTRL write
                ppu->nmi_interrupt_line = true; 
            }
            break;

        case 0x0001: // PPUMASK ($2001)
            ppu->mask = value;
            break;

        case 0x0002: // PPUSTATUS ($2002) - Read-only, writes are ignored
            break;

        case 0x0003: // OAMADDR ($2003)
            ppu->oam_addr = value;
            break;

        case 0x0004: // OAMDATA ($2004)
            // Writes to OAMDATA write to OAM at OAMADDR and increment OAMADDR.
            // Writes during rendering (scanlines 0-239, cycles 1-256 roughly) are complex.
            // Some sources say they are ignored, others say they might corrupt OAM or have glitches.
            // Safest basic emulation: allow writes only outside active rendering/sprite eval.
            // OAM DMA ($4014) is the primary method for filling OAM.
            if (!((ppu->scanline >= 0 && ppu->scanline <= 239) && (ppu->cycle >= 1 && ppu->cycle <= 256) && (ppu->mask & (PPUMASK_SHOW_BG | PPUMASK_SHOW_SPRITES)))) {
                 ppu->oam[ppu->oam_addr] = value;
                 ppu->oam_addr++; // OAMADDR increments after each write
            } else {
                // Potentially increment OAMADDR anyway on a buggy PPU during rendering,
                // even if the write to OAM itself is blocked.
                // For simplicity, we only write and increment if not rendering.
            }
            break;

        case 0x0005: // PPUSCROLL ($2005) - Two writes
            if (ppu->addr_latch == 0) { // First write (X scroll)
                // t: ....... ...HGFED <- value: HGFEDCBA
                // t: XXXXXXX XXXXX CBA -> value: XXXXX CBA
                ppu->temp_addr = (ppu->temp_addr & 0xFFE0) | (value >> 3); // Coarse X (5 bits)
                ppu->fine_x = value & 0x07;                               // Fine X (3 bits)
                ppu->addr_latch = 1;
            } else { // Second write (Y scroll)
                // t: CBA..HG FED..... <- value: HGFEDCBA
                // t: CBA..YX YYYYY CBA -> value: YYYYY CBA
                ppu->temp_addr = (ppu->temp_addr & 0x8C1F) | ((uint16_t)(value & 0xF8) << 2); // Coarse Y (5 bits)
                ppu->temp_addr = (ppu->temp_addr & 0xE3FF) | ((uint16_t)(value & 0x07) << 12); // Fine Y (3 bits)
                ppu->addr_latch = 0;
            }
            break;

        case 0x0006: // PPUADDR ($2006) - Two writes
            if (ppu->addr_latch == 0) { // First write (High byte)
                // t: .FEDCBA ........ <- value: ..FEDCBA
                // PPU address is 14-bit, so only use lower 6 bits of value for high byte.
                ppu->temp_addr = (ppu->temp_addr & 0x00FF) | ((uint16_t)(value & 0x3F) << 8);
                ppu->addr_latch = 1;
            } else { // Second write (Low byte)
                // t: ....... HGFEDCBA <- value: HGFEDCBA
                ppu->temp_addr = (ppu->temp_addr & 0xFF00) | value;
                ppu->vram_addr = ppu->temp_addr; // Update current VRAM address (v)
                ppu->vram_addr &= 0x3FFF; // Ensure it's within PPU memory map for direct access
                ppu->addr_latch = 0;
            }
            break;

        case 0x0007: // PPUDATA ($2007)
            ppu_write_vram(ppu, ppu->vram_addr, value);
            // Increment VRAM address based on PPUCTRL bit 2
            ppu->vram_addr += (ppu->ctrl & PPUCTRL_VRAM_INCREMENT) ? 32 : 1;
            ppu->vram_addr &= 0x3FFF; // Keep within PPU address space
            break;
    }
}

// Called by CPU when $4014 is written (OAM DMA).
// This is a simplified model; actual DMA takes ~513-514 CPU cycles.
// The CPU should be stalled during this time.
void PPU_DoOAMDMA(PPU *ppu, const uint8_t *dma_page_data) {
    memcpy(ppu->oam, dma_page_data, 256);
    // Some games rely on OAMADDR being affected by DMA, but it's complex.
    // Most emulators don't change OAMADDR here or assume it's set to 0 after DMA.
    // For simplicity, we don't modify ppu->oam_addr here.
}


// --- NMI signaling ---
// This function just sets a flag; the main emulation loop or CPU must check it.
void PPU_TriggerNMI(PPU *ppu) {
    if (ppu->nmi_output && ppu->nmi_occured) {
        ppu->nmi_interrupt_line = true;
    }
}

// Sets the mirroring mode based on the cartridge's configuration.
void PPU_SetMirroring(PPU *ppu, MirrorMode mode) {
    ppu->mirror_mode = mode;
}

// --- PPU Step (scanline/cycle logic) ---
void PPU_Step(PPU *ppu) {
    bool rendering_enabled = (ppu->mask & PPUMASK_SHOW_BG) || (ppu->mask & PPUMASK_SHOW_SPRITES);

    // --- Pre-render Scanline (261) ---
    if (ppu->scanline == 261) { // Pre-render line
        if (ppu->cycle == 1) {
            ppu->status &= ~PPUSTATUS_VBLANK;         // Clear VBlank flag
            ppu->status &= ~PPUSTATUS_SPRITE_0_HIT;   // Clear Sprite 0 Hit flag
            ppu->status &= ~PPUSTATUS_SPRITE_OVERFLOW; // Clear Sprite Overflow flag
            ppu->nmi_occured = false;
            ppu->nmi_interrupt_line = false; // Lower NMI line
        }
        // VRAM address vertical component is copied from t to v during dots 280-304
        if (rendering_enabled && ppu->cycle >= 280 && ppu->cycle <= 304) {
            copy_vertical_bits(ppu);
        }
    }

    // --- Visible Scanlines (0-239) and Pre-render Scanline (261) ---
    bool is_render_scanline = (ppu->scanline >= 0 && ppu->scanline <= 239) || ppu->scanline == 261;

    if (is_render_scanline && rendering_enabled) {
        // Background Shifter Operations (every cycle during rendering periods)
        if (ppu->cycle >= 1 && ppu->cycle <= 256) { // Visible pixel rendering cycles
            ppu->bg_pattern_shift_low <<= 1;
            ppu->bg_pattern_shift_high <<= 1;
            ppu->bg_attrib_shift_low <<= 1;
            ppu->bg_attrib_shift_high <<= 1;
        } else if (ppu->cycle >= 321 && ppu->cycle <= 336) { // Prefetch cycles for next scanline's first two tiles
            ppu->bg_pattern_shift_low <<= 1;
            ppu->bg_pattern_shift_high <<= 1;
            ppu->bg_attrib_shift_low <<= 1;
            ppu->bg_attrib_shift_high <<= 1;
        }


        // Background Fetching Logic (simplified: fetches happen in 8-cycle blocks)
        // Hardware fetches: NT (cycle 1,2), AT (3,4), PT Low (5,6), PT High (7,8) of an 8-cycle period.
        // Our simplified model: load all data on cycle 1, feed shifters on cycle 2 (or 0 of next block).
        bool is_fetch_cycle_range = (ppu->cycle >= 1 && ppu->cycle <= 256) || (ppu->cycle >= 321 && ppu->cycle <= 336);
        if (is_fetch_cycle_range) {
            switch (ppu->cycle % 8) {
                case 1: // Start of 8-cycle block: Fetch data for the upcoming tile into latches
                    load_background_tile_data(ppu);
                    break;
                case 0: // End of 8-cycle block (e.g. cycle 8, 16, ... 256, or 328, 336)
                        // Feed the shifters with data fetched in the *previous* cycle 1 of this block.
                    feed_background_shifters(ppu);
                    // Increment coarse X scroll after fetching a tile's data (every 8 cycles)
                    increment_coarse_x(ppu);
                    break;
            }
        }
        
        // At cycle 256 of a rendering scanline:
        if (ppu->cycle == 256) {
            increment_fine_y(ppu); // Increment vertical scroll
        }

        // At cycle 257 of a rendering scanline:
        if (ppu->cycle == 257) {
            copy_horizontal_bits(ppu); // Reload horizontal scroll from t to v
            // Sprite evaluation for the *next* scanline (or current if scanline 0)
            // In a more accurate model, OAM is cleared (1-64), then evaluation (65-256)
            // Here, we do it bundled at 257.
            if (ppu->scanline <= 239) { // Only on visible lines for next line's sprites
                 evaluate_sprites(ppu); // Evaluates for sprites on `ppu->scanline`
            }
        }
        
        // Sprite Pattern Fetching (simplified)
        // Hardware: Interleaved during 257-320 for the 8 found sprites.
        // Here: Bundled after evaluation, around cycle 321-340 (or earlier if fewer sprites).
        // This fetches for sprites that will be rendered on `ppu->scanline`.
        if (ppu->cycle == 321 && ppu->scanline <= 239) { // Typically after BG prefetch window starts
            fetch_sprite_patterns(ppu);
        }
    }


    // --- Pixel Rendering ---
    // Happens during cycles 1-256 of visible scanlines (0-239).
    if (ppu->scanline >= 0 && ppu->scanline <= 239 && ppu->cycle >= 1 && ppu->cycle <= 256) {
        int x = ppu->cycle - 1; // Pixel X coordinate (0-255)
        int y = ppu->scanline;  // Pixel Y coordinate (0-239)

        uint8_t bg_palette_idx = 0; // Palette index (0-3 for BG palettes)
        uint8_t bg_pixel_pattern_val = 0; // Pixel value from pattern table (0-3)
        
        // Determine background pixel
        if (ppu->mask & PPUMASK_SHOW_BG) {
            // Don't render leftmost 8 pixels if clipping is enabled
            if (x >= 8 || !(ppu->mask & PPUMASK_CLIP_BG)) {
                // Select bit from 16-bit shifters based on fine_x scroll
                uint16_t bit_selector = 0x8000 >> ppu->fine_x;

                uint8_t pt_bit0 = (ppu->bg_pattern_shift_low & bit_selector) ? 1 : 0;
                uint8_t pt_bit1 = (ppu->bg_pattern_shift_high & bit_selector) ? 1 : 0;
                bg_pixel_pattern_val = (pt_bit1 << 1) | pt_bit0; // 2-bit pattern value (0-3)

                uint8_t attrib_bit0 = (ppu->bg_attrib_shift_low & bit_selector) ? 1 : 0;
                uint8_t attrib_bit1 = (ppu->bg_attrib_shift_high & bit_selector) ? 1 : 0;
                bg_palette_idx = (attrib_bit1 << 1) | attrib_bit0; // 2-bit attribute palette select (0-3)
            }
        }

        uint8_t final_bg_color_entry = 0;
        if (bg_pixel_pattern_val == 0) { // Transparent background pixel
            final_bg_color_entry = ppu_read_vram(ppu, 0x3F00); // Universal background color
        } else {
            final_bg_color_entry = ppu_read_vram(ppu, 0x3F00 + (bg_palette_idx << 2) + bg_pixel_pattern_val);
        }
        final_bg_color_entry &= 0x3F; // Ensure it's a valid 6-bit palette index

        // --- Sprite Pixel ---
        uint8_t spr_pixel_pattern_val = 0; // Pixel value from sprite pattern (0-3)
        uint8_t spr_palette_idx = 0;       // Palette index for sprite (0-3 from attributes)
        uint8_t spr_final_color_entry = 0;
        bool spr_is_opaque = false;
        bool spr_is_foreground = true; // Sprite attribute bit 5: 0=front, 1=behind BG
        int sprite_zero_hit_candidate_idx = -1;


        if (ppu->mask & PPUMASK_SHOW_SPRITES) {
            // Don't render leftmost 8 pixels if clipping is enabled
            if (x >= 8 || !(ppu->mask & PPUMASK_CLIP_SPRITES)) {
                for (int i = 0; i < ppu->sprite_count_current_scanline; ++i) {
                    SpriteShifter* s = &ppu->sprite_shifters[i];
                    if (x >= s->x_pos && x < (s->x_pos + 8)) { // Is pixel X within this sprite's range?
                        int col_in_sprite = x - s->x_pos;
                        if (s->attributes & 0x40) { // Horizontal flip
                            col_in_sprite = 7 - col_in_sprite;
                        }

                        uint8_t spr_pt_bit0 = (s->pattern_low >> (7 - col_in_sprite)) & 1;
                        uint8_t spr_pt_bit1 = (s->pattern_high >> (7 - col_in_sprite)) & 1;
                        uint8_t current_spr_pixel_pattern_val = (spr_pt_bit1 << 1) | spr_pt_bit0;

                        if (current_spr_pixel_pattern_val != 0) { // Opaque sprite pixel
                            spr_pixel_pattern_val = current_spr_pixel_pattern_val;
                            spr_palette_idx = s->attributes & 0x03; // Lower 2 bits of attributes
                            spr_is_opaque = true;
                            spr_is_foreground = !(s->attributes & 0x20); // Bit 5: 0=FG, 1=BG

                            spr_final_color_entry = ppu_read_vram(ppu, 0x3F10 + (spr_palette_idx << 2) + spr_pixel_pattern_val);
                            spr_final_color_entry &= 0x3F;

                            // Sprite 0 Hit Detection
                            // Check if this is sprite 0 (from secondary OAM, which means original OAM index was 0)
                            // and if it's the first opaque sprite pixel at this X.
                            // The `ppu->sprite_zero_on_current_scanline` flag indicates if sprite 0 is *active* on this scanline.
                            // We need to check if `ppu->secondary_oam[i*4+1]` (tile id) corresponds to the original sprite 0.
                            // A simpler way: if `ppu->sprite_zero_on_current_scanline` is true, and `i` corresponds to where sprite 0 landed in secondary_oam.
                            // For simplicity, if sprite 0 is active on this scanline, and this is the *first found sprite (i=0)*
                            // AND it's opaque, it's a candidate.
                            // This check is simplified. A more robust check would trace sprite 0 through secondary OAM.
                            // Assuming sprite 0, if present, is always the first entry in secondary_oam if it's one of the 8.
                            if (ppu->sprite_zero_on_current_scanline && i == 0 && // This sprite is the one at OAM[0]
                                bg_pixel_pattern_val != 0 &&  // Background is opaque
                                x != 255) {                   // Not at the very right edge
                                // Sprite 0 hit flag is set only once per frame.
                                // It's cleared on pre-render line.
                                if (!(ppu->status & PPUSTATUS_SPRITE_0_HIT)) {
                                     ppu->status |= PPUSTATUS_SPRITE_0_HIT;
                                }
                            }
                            break; // Found highest priority sprite for this pixel
                        }
                    }
                }
            }
        }

        // --- Combine BG and Sprite ---
        uint8_t final_color_entry_idx;
        if (spr_is_opaque) {
            if (bg_pixel_pattern_val == 0) { // BG transparent, sprite wins
                final_color_entry_idx = spr_final_color_entry;
            } else { // Both BG and Sprite are opaque
                if (spr_is_foreground) { // Sprite in front
                    final_color_entry_idx = spr_final_color_entry;
                } else { // Sprite behind
                    final_color_entry_idx = final_bg_color_entry;
                }
            }
        } else { // Sprite is transparent, BG wins
            final_color_entry_idx = final_bg_color_entry;
        }
        
        // TODO: Apply color emphasis from PPUMASK bits 5-7 if enabled.
        // This involves modifying the R, G, B components of the color from nes_palette.
        // Example: if (ppu->mask & PPUMASK_EMPHASIZE_RED) { color = emphasize_red(color); }

        ppu->framebuffer[y * 256 + x] = nes_palette[final_color_entry_idx];
    }


    // --- VBLANK Period (Scanlines 241-260) ---
    if (ppu->scanline == 241 && ppu->cycle == 1) {
        ppu->status |= PPUSTATUS_VBLANK; // Set VBlank flag
        ppu->nmi_occured = true;         // Mark that VBlank has started for NMI logic
        if (ppu->nmi_output) {           // If NMI enabled in PPUCTRL
            ppu->nmi_interrupt_line = true; // Signal NMI to CPU
        }
        // TODO: Notify main loop that a frame is ready for rendering to screen
        //if (ppu->nes->frame_ready_callback) {
        //    ppu->nes->frame_ready_callback(ppu->nes);
        //}
    }

    // --- Cycle and Scanline Advancement ---
    ppu->cycle++;
    if (ppu->cycle > 340) { // Each scanline has 341 PPU clocks (0-340)
        ppu->cycle = 0;
        ppu->scanline++;

        if (ppu->scanline == 261 && ppu->frame_odd && rendering_enabled) {
            // Odd frame skip: Skip cycle 0 of scanline 0 (effectively cycle 341 of pre-render)
            ppu->cycle = 1; 
        }
        
        if (ppu->scanline > 261) { // End of frame
            ppu->scanline = 0;     // Wrap to first visible scanline
            ppu->frame_odd = !ppu->frame_odd; // Toggle frame parity
        }
    }
}


// --- CHR ROM/RAM access (for mappers) ---
// These are placeholders; actual access is via BUS_PPU_ReadCHR/WriteCHR
uint8_t PPU_CHR_Read(PPU *ppu, uint16_t addr) {
    return BUS_PPU_ReadCHR(ppu->nes->bus, addr);
}

void PPU_CHR_Write(PPU *ppu, uint16_t addr, uint8_t value) {
    BUS_PPU_WriteCHR(ppu->nes->bus, addr, value);
}

// --- Expose NES palette for UI/PPU viewer ---
const uint32_t* PPU_GetPalette(void) {
    return nes_palette;
}

// --- Expose framebuffer for UI ---
const uint32_t* PPU_GetFramebuffer(PPU* ppu) {
    return ppu->framebuffer;
}

// --- Expose pattern table (CHR ROM/RAM) for UI ---
// This is a simplified view; mappers can bank switch CHR.
// It assumes the bus can provide a direct pointer to current CHR banks.
// For a more robust solution, this would read byte-by-byte via BUS_PPU_ReadCHR.
void PPU_GetPatternTableData(PPU* ppu, int table_idx, uint8_t* buffer_256x128_indexed) {
    // Renders a pattern table (128x128 pixels, 16x16 tiles) into a buffer of palette indices.
    // Each pixel in the buffer will be a 0-3 index into a palette.
    // The caller needs to combine this with palette data to get actual colors.
    // This is for visualization; not direct PPU operation.

    // For simplicity, this example reads directly. A real tool might need to
    // temporarily set PPUCTRL to select the table if that affects BUS_PPU_ReadCHR.
    uint16_t base_addr = (table_idx == 0) ? 0x0000 : 0x1000;

    for (int tile_y = 0; tile_y < 16; ++tile_y) {
        for (int tile_x = 0; tile_x < 16; ++tile_x) {
            uint16_t tile_offset = (tile_y * 16 + tile_x) * 16; // Each tile is 16 bytes
            for (int row = 0; row < 8; ++row) {
                uint8_t pt_low = BUS_PPU_ReadCHR(ppu->nes->bus, base_addr + tile_offset + row);
                uint8_t pt_high = BUS_PPU_ReadCHR(ppu->nes->bus, base_addr + tile_offset + row + 8);
                for (int col = 0; col < 8; ++col) {
                    uint8_t bit0 = (pt_low >> (7 - col)) & 1;
                    uint8_t bit1 = (pt_high >> (7 - col)) & 1;
                    uint8_t pixel_value = (bit1 << 1) | bit0; // 0-3

                    int buffer_x = tile_x * 8 + col;
                    int buffer_y = tile_y * 8 + row;
                    // Assuming buffer is 128 wide for one pattern table.
                    // If buffer_256x128_indexed is for two tables side-by-side:
                    // buffer_x += (table_idx * 128);
                    buffer_256x128_indexed[buffer_y * 128 + buffer_x] = pixel_value;
                }
            }
        }
    }
}


// --- Expose nametable for UI ---
// Returns a pointer to the specified nametable in PPU VRAM, considering mirroring.
// This is a snapshot and doesn't reflect live PPU scrolling effects during rendering.
const uint8_t* PPU_GetNametable(PPU* ppu, int index) {
    if (index < 0 || index > 3) return NULL;
    // Calculate the logical base address of the requested nametable
    uint16_t logical_base_addr = 0x2000 + index * 0x0400;
    // Get the physical VRAM address after mirroring
    uint16_t physical_addr_offset = mirror_vram_addr(ppu, logical_base_addr);
    return &ppu->vram[physical_addr_offset];
}

// --- Expose palette RAM for UI ---
const uint8_t* PPU_GetPaletteRAM(PPU* ppu) {
    return ppu->palette;
}

// --- Expose OAM (sprite RAM) for UI ---
const uint8_t* PPU_GetOAM(PPU* ppu) {
    return ppu->oam;
}

// --- Expose scanline/cycle for UI ---
void PPU_GetScanlineCycle(PPU* ppu, int* scanline, int* cycle) {
    if (scanline) *scanline = ppu->scanline;
    if (cycle) *cycle = ppu->cycle;
}

// --- Dump nametable for debugging ---
void PPU_DumpNametable(PPU* ppu, int index, char* out_buffer, size_t buffer_size) {
    if (!out_buffer || buffer_size < 1) return;
    const uint8_t* nt_data = PPU_GetNametable(ppu, index);
    if (!nt_data) {
        snprintf(out_buffer, buffer_size, "Invalid nametable index %d\n", index);
        return;
    }

    size_t current_pos = 0;
    // A nametable is 32 tiles wide by 30 tiles high (960 bytes).
    for (int y = 0; y < 30; ++y) {
        for (int x = 0; x < 32; ++x) {
            if (current_pos + 4 >= buffer_size) { // Need space for "XX " and null terminator
                out_buffer[current_pos] = '\0';
                strncat(out_buffer, "...", buffer_size - current_pos -1);
                return; // Buffer full
            }
            current_pos += snprintf(out_buffer + current_pos, buffer_size - current_pos, "%02X ", nt_data[y * 32 + x]);
        }
        if (current_pos + 2 >= buffer_size) { // Need space for "\n" and null terminator
            out_buffer[current_pos] = '\0';
            strncat(out_buffer, "...", buffer_size - current_pos -1);
            return; // Buffer full
        }
        current_pos += snprintf(out_buffer + current_pos, buffer_size - current_pos, "\n");
    }
    out_buffer[current_pos] = '\0'; // Ensure null termination
}

// --- Dump palette RAM for debugging ---
void PPU_DumpPaletteRAM(PPU* ppu, char* out_buffer, size_t buffer_size) {
    if (!out_buffer || buffer_size < 1) return;
    size_t current_pos = 0;
    
    current_pos += snprintf(out_buffer + current_pos, buffer_size - current_pos, "BG Palette ($3F00-$3F0F):\n");
    for (int i = 0; i < 16; ++i) {
        if (current_pos + 4 >= buffer_size) goto end_palette_dump;
        current_pos += snprintf(out_buffer + current_pos, buffer_size - current_pos, "%02X ", ppu->palette[i]);
    }
    if (current_pos + 2 >= buffer_size) goto end_palette_dump;
    current_pos += snprintf(out_buffer + current_pos, buffer_size - current_pos, "\n");

    current_pos += snprintf(out_buffer + current_pos, buffer_size - current_pos, "Sprite Palette ($3F10-$3F1F):\n");
    for (int i = 0; i < 16; ++i) {
        if (current_pos + 4 >= buffer_size) goto end_palette_dump;
        current_pos += snprintf(out_buffer + current_pos, buffer_size - current_pos, "%02X ", ppu->palette[0x10 + i]);
    }
    if (current_pos + 2 >= buffer_size) goto end_palette_dump;
    current_pos += snprintf(out_buffer + current_pos, buffer_size - current_pos, "\n");

end_palette_dump:
    if (current_pos < buffer_size) {
        out_buffer[current_pos] = '\0';
    } else if (buffer_size > 0) {
        out_buffer[buffer_size - 1] = '\0'; // Ensure null termination if truncated
    }
}

// --- Dump OAM for debugging ---
void PPU_DumpOAM(PPU* ppu, char* out_buffer, size_t buffer_size) {
    if (!out_buffer || buffer_size < 1) return;
    size_t current_pos = 0;

    for (int i = 0; i < 64; ++i) { // 64 sprites
        // Estimate line length: "Sprite XX: Y:XX Tile:XX Attr:XX X:XX\n" approx 35 chars
        if (current_pos + 40 >= buffer_size) {
             if (current_pos < buffer_size) out_buffer[current_pos] = '\0';
             strncat(out_buffer, "...\n", buffer_size - current_pos -1);
            return; // Buffer full
        }
        current_pos += snprintf(out_buffer + current_pos, buffer_size - current_pos,
                                "Sprite %02d: Y:%02X Tile:%02X Attr:%02X X:%02X\n",
                                i,
                                ppu->oam[i * 4 + 0], // Y
                                ppu->oam[i * 4 + 1], // Tile Index
                                ppu->oam[i * 4 + 2], // Attributes
                                ppu->oam[i * 4 + 3]  // X
                               );
    }
    if (current_pos < buffer_size) {
        out_buffer[current_pos] = '\0'; // Ensure null termination
    } else if (buffer_size > 0) {
        out_buffer[buffer_size-1] = '\0';
    }
}
