#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "debug.h"
#include "cNES/nes.h"
#include "cNES/bus.h"

#include "cNES/ppu.h"

// --- NES master palette (64 colors, RGBA format) ---
static const uint32_t nes_palette[64] = {
    0x666666FF, 0x002A88FF, 0x1412A7FF, 0x3B00A4FF, 0x5C007EFF, 0x6E0040FF, 0x6C0600FF, 0x561D00FF,
    0x333500FF, 0x0B4800FF, 0x005200FF, 0x004F08FF, 0x00404DFF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xADADADFF, 0x155FD9FF, 0x4240FFFF, 0x7527FEFF, 0xA01ACCFF, 0xB71E7BFF, 0xB53120FF, 0x994E00FF,
    0x6B6D00FF, 0x388700FF, 0x0E9300FF, 0x008F32FF, 0x007C8DFF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xFFFEFFFF, 0x64B0FFFF, 0x9290FFFF, 0xC676FFFF, 0xF36AFFFF, 0xFE6ECCFF, 0xFE8170FF, 0xEA9E22FF, // Corrected typo F36AFFff to F36AFFFF
    0xBCBE00FF, 0x88D800FF, 0x5CE430FF, 0x45E082FF, 0x48CDDEFF, 0x4F4F4FFF, 0x000000FF, 0x000000FF,
    0xFFFEFFFF, 0xC0DFFFFF, 0xD3D2FFFF, 0xE8C8FFFF, 0xFBC2FFFF, 0xFEC4EAFF, 0xFECCC5FF, 0xF7D8A5FF,
    0xE4E594FF, 0xCFEE96FF, 0xBDF4ABFF, 0xB3F3CCFF, 0xB5EBF2FF, 0xB8B8B8FF, 0x000000FF, 0x000000FF
};

// --- Helper Functions ---
static uint16_t mirror_vram_addr(NES *nes, uint16_t addr) {
    // addr is PPU address $2000-$2FFF (or mirrors up to $3EFF for nametables)
    addr &= 0x0FFF; // Relative to $2000, mask to 4 nametable region (0x000 - 0xFFF)
    
    // Determine which of the four 1KB nametable areas is being accessed
    uint16_t table_index = addr / 0x0400; // Nametable index (0, 1, 2, or 3)
    uint16_t offset_in_table = addr % 0x0400; // Offset within that 1KB nametable (0x000 - 0x3FF)

    if (nes->bus->mirroring == 0) { // Horizontal mirroring
        // Nametable 0 ($2000) and Nametable 2 ($2800) map to the first 1KB of PPU VRAM.
        // Nametable 1 ($2400) and Nametable 3 ($2C00) map to the second 1KB of PPU VRAM.
        if (table_index == 0 || table_index == 2) {
            return offset_in_table; // Maps to VRAM $0000 - $03FF
        } else { // table_index == 1 || table_index == 3
            return offset_in_table + 0x0400; // Maps to VRAM $0400 - $07FF
        }
    } else { // Vertical mirroring (nes->bus->mirroring == 1)
        // Nametable 0 ($2000) and Nametable 1 ($2400) map to the first 1KB of PPU VRAM.
        // Nametable 2 ($2800) and Nametable 3 ($2C00) map to the second 1KB of PPU VRAM.
        if (table_index == 0 || table_index == 1) {
            return offset_in_table; // Maps to VRAM $0000 - $03FF
        } else { // table_index == 2 || table_index == 3
            return offset_in_table + 0x0400; // Maps to VRAM $0400 - $07FF
        }
    }
    // Note: This assumes ppu->vram is 2KB (0x800 bytes).
    // For other mirroring types (e.g., four-screen), this logic would need extension.
}

static uint8_t ppu_read_vram(PPU *ppu, uint16_t addr) {
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        // CHR ROM/RAM via mapper
        return BUS_PPU_ReadCHR(ppu->nes->bus, addr);
    } else if (addr < 0x3F00) {
        return ppu->vram[mirror_vram_addr(ppu->nes, addr)];
    } else if (addr < 0x4000) {
        uint16_t pal = addr & 0x1F;
        if (pal == 0x10) pal = 0x00;
        if (pal == 0x14) pal = 0x04;
        if (pal == 0x18) pal = 0x08;
        if (pal == 0x1C) pal = 0x0C;
        return ppu->palette[pal];
    }
    return 0;
}

static void ppu_write_vram(PPU *ppu, uint16_t addr, uint8_t value) {
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        BUS_PPU_WriteCHR(ppu->nes->bus, addr, value);
    } else if (addr < 0x3F00) {
        ppu->vram[mirror_vram_addr(ppu->nes, addr)] = value;
    } else if (addr < 0x4000) {
        uint16_t pal = addr & 0x1F;
        if (pal == 0x10) pal = 0x00;
        if (pal == 0x14) pal = 0x04;
        if (pal == 0x18) pal = 0x08;
        if (pal == 0x1C) pal = 0x0C;
        ppu->palette[pal] = value;
    }
}

// --- PPU API Implementation ---

PPU *PPU_Create(NES *nes) {
    PPU *ppu = calloc(1, sizeof(PPU));
    if (!ppu) return NULL; // Allocation check
    ppu->nes = nes;
    // PPU_Reset will initialize all members to their default power-up/reset states.
    PPU_Reset(ppu); 
    // Initial scanline/cycle can be set after reset if specific startup is needed,
    // but PPU_Reset already sets scanline = 241, cycle = 0.
    return ppu;
}

void PPU_Destroy(PPU *ppu) {
    free(ppu);
}

void PPU_Reset(PPU *ppu) {
    memset(ppu->vram, 0, sizeof(ppu->vram));
    memset(ppu->palette, 0, sizeof(ppu->palette));
    memset(ppu->oam, 0, sizeof(ppu->oam));
    ppu->ctrl = ppu->mask = 0;
    ppu->status = 0xA0;
    ppu->oam_addr = 0;
    ppu->scroll_latch = ppu->addr_latch = 0;
    ppu->scroll_x = ppu->scroll_y = 0;
    ppu->vram_addr = ppu->temp_addr = 0;
    ppu->fine_x = 0;
    ppu->data_buffer = 0;
    ppu->scanline = 241;
    ppu->cycle = 0;
    ppu->frame_odd = 0;
    ppu->nmi_occured = ppu->nmi_output = ppu->nmi_previous = ppu->nmi_interrupt = 0;
}

uint8_t PPU_ReadRegister(PPU *ppu, uint16_t addr) {
    uint8_t ret = 0;
    switch (addr & 7) {
        case 2: // PPUSTATUS
            ret = (ppu->status & 0xE0) | (ppu->data_buffer & 0x1F);
            ppu->status &= ~0x80;
            ppu->addr_latch = 0;
            break;
        case 4: // OAMDATA
            ret = ppu->oam[ppu->oam_addr];
            break;
        case 7: // PPUDATA
            ret = ppu->data_buffer;
            ppu->data_buffer = ppu_read_vram(ppu, ppu->vram_addr);
            if (ppu->vram_addr >= 0x3F00) ret = ppu->data_buffer; // Palette reads are not buffered
            ppu->vram_addr += (ppu->ctrl & 0x04) ? 32 : 1;
            break;
        default:
            break;
    }
    return ret;
}

void PPU_WriteRegister(PPU *ppu, uint16_t addr, uint8_t value) {
    switch (addr & 7) {
        case 0: // PPUCTRL
            ppu->ctrl = value;
            ppu->temp_addr = (ppu->temp_addr & 0xF3FF) | ((value & 3) << 10);
            ppu->nmi_output = (value >> 7) & 1;
            break;
        case 1: // PPUMASK
            ppu->mask = value;
            break;
        case 2: // PPUSTATUS (read-only)
            break;
        case 3: // OAMADDR
            ppu->oam_addr = value;
            break;
        case 4: // OAMDATA
            ppu->oam[ppu->oam_addr++] = value;
            break;
        case 5: // PPUSCROLL
            if (!ppu->scroll_latch) {
                ppu->fine_x = value & 7;
                ppu->temp_addr = (ppu->temp_addr & 0xFFE0) | (value >> 3);
                ppu->scroll_latch = 1;
            } else {
                ppu->temp_addr = (ppu->temp_addr & 0x8FFF) | ((value & 7) << 12);
                ppu->temp_addr = (ppu->temp_addr & 0xFC1F) | ((value & 0xF8) << 2);
                ppu->scroll_latch = 0;
            }
            break;
        case 6: // PPUADDR
            if (!ppu->addr_latch) {
                ppu->temp_addr = (ppu->temp_addr & 0x00FF) | ((value & 0x3F) << 8);
                ppu->addr_latch = 1;
            } else {
                ppu->temp_addr = (ppu->temp_addr & 0xFF00) | value;
                ppu->vram_addr = ppu->temp_addr;
                ppu->addr_latch = 0;
            }
            break;
        case 7: // PPUDATA
            ppu_write_vram(ppu, ppu->vram_addr, value);
            ppu->vram_addr += (ppu->ctrl & 0x04) ? 32 : 1;
            break;
    }
}

// --- NMI signaling ---
void PPU_TriggerNMI(PPU *ppu) {
    ppu->nmi_interrupt = 1;
}

// --- PPU Step (scanline/cycle logic, simplified) ---
void PPU_Step(PPU *ppu) {
    // Advance cycle/scanline
    ppu->cycle++;
    if (ppu->cycle > 340) {
        ppu->cycle = 0;
        ppu->scanline++;
        if (ppu->scanline > 261) {
            ppu->scanline = 0;
            ppu->frame_odd ^= 1;
            // Frame done, could signal to host
        }
    }

    // VBLANK logic
    if (ppu->scanline == 241 && ppu->cycle == 1) {
        ppu->status |= 0x80;
        ppu->nmi_occured = 1;
        if (ppu->nmi_output) {
            PPU_TriggerNMI(ppu);
        }
    }
    // End VBLANK
    if (ppu->scanline == 261 && ppu->cycle == 1) {
        ppu->status &= ~0x80;
        ppu->nmi_occured = 0;
        ppu->nmi_interrupt = 0;
    }

    // --- Improved background rendering with attribute table and palette selection ---
    if (ppu->scanline < 240 && ppu->cycle > 0 && ppu->cycle <= 256) {
        int x = ppu->cycle - 1;
        int y = ppu->scanline;

        // Nametable base address (no scrolling)
        uint16_t nt_base = 0x2000 | ((ppu->ctrl & 0x03) * 0x400);
        uint16_t nt_addr = nt_base + ((y / 8) * 32) + (x / 8);
        uint8_t tile_index = ppu_read_vram(ppu, nt_addr);

        // Pattern table base (bit 4 of PPUCTRL)
        uint16_t pt_base = (ppu->ctrl & 0x10) ? 0x1000 : 0x0000;
        uint16_t tile_addr = pt_base + tile_index * 16 + (y % 8);

        uint8_t plane0 = ppu_read_vram(ppu, tile_addr);
        uint8_t plane1 = ppu_read_vram(ppu, tile_addr + 8);

        int bit = 7 - (x % 8);
        uint8_t color_idx = ((plane1 >> bit) & 1) << 1 | ((plane0 >> bit) & 1);

        // Attribute table address and palette selection
        uint16_t at_addr = nt_base + 0x3C0 + ((y / 32) * 8) + (x / 32);
        uint8_t at_byte = ppu_read_vram(ppu, at_addr);
        int shift = (((y % 32) / 16) * 2 + ((x % 32) / 16)) * 2;
        uint8_t palette = (at_byte >> shift) & 0x03;

        // Palette lookup (color 0 is always universal background)
        uint8_t palette_addr = 0x3F00 + (palette << 2) + color_idx;
        if ((color_idx & 0x03) == 0) palette_addr = 0x3F00;
        uint8_t color = ppu_read_vram(ppu, palette_addr);

        // Use NES palette for rendering
        ppu->framebuffer[y * 256 + x] = nes_palette[color & 0x3F];
        // (Optional: remove or comment out debug logging for performance)
        // DEBUG_INFO("PPU: Scanline %d, Cycle %d, Color %02X", ppu->scanline, ppu->cycle, color);
    }
}

// --- CHR ROM/RAM access (for mappers) ---
uint8_t PPU_CHR_Read(PPU *ppu, uint16_t addr) {
    return BUS_PPU_ReadCHR(ppu->nes->bus, addr);
}
void PPU_CHR_Write(PPU *ppu, uint16_t addr, uint8_t value) {
    BUS_PPU_WriteCHR(ppu->nes->bus, addr, value);
}
