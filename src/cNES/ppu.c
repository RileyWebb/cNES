/*
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h" // Assuming you have a debug.h for logging

#include "cNES/nes.h"
#include "cNES/bus.h"
#include "cNES/ppu.h"

// Forward declaration for internal PPU memory access
uint8_t PPU_ReadMemory(PPU* ppu, uint16_t address);
void PPU_WriteMemory(PPU* ppu, uint16_t address, uint8_t value);

// Function to create and initialize the PPU
PPU* PPU_Create()
{
    PPU* ppu = malloc(sizeof(PPU));
    if (!ppu) {
        DEBUG_ERROR("Failed to allocate memory for PPU");
        return NULL;
    }
    memset(ppu, 0, sizeof(PPU)); // Initialize PPU structure to zero

    // Initialize PPU registers to their power-up state
    ppu->ctrl = 0x00;
    ppu->mask = 0x00;
    ppu->status = 0xA0; // Status register power-up state (bit 5 and 7 set)
    ppu->scroll = 0x00;
    ppu->oam_addr = 0x00;
    ppu->oam_data = 0x00;
    ppu->addr = 0x0000;
    ppu->data = 0x00;

    // Initialize internal state
    ppu->scanline = 241; // Start at post-render scanline
    ppu->cycle = 0;
    ppu->frame_odd = 0; // Start with an even frame

    // Initialize internal VRAM address and temporary VRAM address
    ppu->vram_addr = 0x0000;
    ppu->temp_vram_addr = 0x0000;

    // Initialize fine X scroll
    ppu->fine_x_scroll = 0;

    // Initialize PPU data buffer
    ppu->data_buffer = 0x00;

    // Initialize NMI flag
    ppu->nmi_occured = 0;
    ppu->nmi_output = 0;
    ppu->nmi_previous = 0;
    ppu->nmi_interrupt = 0;


    DEBUG_INFO("PPU created and initialized");
    return ppu;
}

// Function to destroy the PPU
void PPU_Destroy(PPU* ppu)
{
    if (ppu) {
        free(ppu);
        DEBUG_INFO("PPU destroyed");
    }
}

// Function to reset the PPU (similar to power-up state)
void PPU_Reset(PPU* ppu)
{
    ppu->ctrl = 0x00;
    ppu->mask = 0x00;
    ppu->status = 0xA0;
    ppu->scroll = 0x00;
    ppu->oam_addr = 0x00;

    ppu->scanline = 241;
    ppu->cycle = 0;
    ppu->frame_odd = 0;

    ppu->vram_addr = 0x0000;
    ppu->temp_vram_addr = 0x0000;
    ppu->fine_x_scroll = 0;
    ppu->data_buffer = 0x00;

    ppu->nmi_occured = 0;
    ppu->nmi_output = 0;
    ppu->nmi_previous = 0;
    ppu->nmi_interrupt = 0;

    DEBUG_INFO("PPU reset");
}

// Function to read from PPU registers (CPU side)
uint8_t PPU_ReadRegister(NES* nes, uint16_t address)
{
    PPU* ppu = nes->ppu;
    uint8_t data = 0x00;

    // PPU registers are mirrored every 8 bytes from 0x2000 to 0x3FFF
    uint16_t register_address = 0x2000 + (address & 0x0007);

    switch (register_address) {
        case 0x2002: // PPUSTATUS
            // Reading PPUSTATUS clears bit 7 (VBLANK) and the address latch
            data = ppu->status;
            ppu->status &= ~0x80; // Clear VBLANK flag
            ppu->address_latch = 0; // Clear address latch
            ppu->nmi_occured = 0; // Reading status also clears the NMI interrupt flag
            break;

        case 0x2004: // OAMDATA
            // Reading OAMDATA reads from OAM at the address specified by OAMADDR
            data = ppu->oam[ppu->oam_addr];
            // Reads from OAMDATA during rendering (scanlines 0-239) increment OAMADDR
            // Reads outside of rendering do not increment OAMADDR
            // This is a simplification; actual behavior depends on PPU state
            // For now, we'll always increment for simplicity in this basic structure
            // ppu->oam_addr++; // Auto-increment OAMADDR (simplified)
            break;

        case 0x2007: // PPUDATA
            // Reading PPUDATA reads from VRAM at the address specified by PPUADDR
            // The first read after setting PPUADDR is buffered, subsequent reads get the actual data
            // This is a common NES PPU behavior
            data = ppu->data_buffer; // Return the buffered data
            ppu->data_buffer = PPU_ReadMemory(ppu, ppu->vram_addr); // Read the actual data for the next read

            // Auto-increment PPUADDR based on PPUCTRL bit 2
            if (ppu->ctrl & 0x04) { // VRAM address increment (0: add 1, 1: add 32)
                ppu->vram_addr += 32;
            } else {
                ppu->vram_addr += 1;
            }
            break;

        default:
            // Reading other registers is generally not allowed or has specific side effects
            // For this basic implementation, we'll just return 0
            DEBUG_WARN("Attempted to read from unimplemented PPU register 0x%04X", register_address);
            break;
    }

    // The lower 5 bits of PPUSTATUS are not connected and return the last value on the data bus.
    // This is a simplification; a full emulator would need to capture the last data bus value.
    // For now, we'll just mask the status bits.
    // data = (data & 0xE0) | (ppu->status & 0x1F); // Simplified: combine read data with lower status bits

    return data;
}

// Function to write to PPU registers (CPU side)
void PPU_WriteRegister(NES* nes, uint16_t address, uint8_t value)
{
    PPU* ppu = nes->ppu;

    // PPU registers are mirrored every 8 bytes from 0x2000 to 0x3FFF
    uint16_t register_address = 0x2000 + (address & 0x0007);

    switch (register_address) {
        case 0x2000: // PPUCTRL
            ppu->ctrl = value;
            // Update the base nametable address in the temporary VRAM address
            ppu->temp_vram_addr = (ppu->temp_vram_addr & 0xF3FF) | ((uint16_t)(value & 0x03) << 10);
            // Update NMI enable flag
            ppu->nmi_output = (value >> 7) & 0x01;
            ppu->nmi_change(); // Trigger NMI change check
            break;

        case 0x2001: // PPUMASK
            ppu->mask = value;
            break;

        case 0x2003: // OAMADDR
            ppu->oam_addr = value;
            break;

        case 0x2004: // OAMDATA
            // Writing to OAMDATA writes to OAM at the address specified by OAMADDR
            ppu->oam[ppu->oam_addr] = value;
            ppu->oam_addr++; // Auto-increment OAMADDR after write
            break;

        case 0x2005: // PPUSCROLL
            // Writing to PPUSCROLL is a two-step process
            if (ppu->address_latch == 0) {
                // First write: horizontal scroll and fine X scroll
                ppu->scroll = value;
                ppu->temp_vram_addr = (ppu->temp_vram_addr & 0xFFE0) | (uint16_t)(value >> 3); // Coarse X
                ppu->fine_x_scroll = value & 0x07; // Fine X
                ppu->address_latch = 1;
            } else {
                // Second write: vertical scroll
                ppu->scroll = value;
                ppu->temp_vram_addr = (ppu->temp_vram_addr & 0x8FFF) | ((uint16_t)(value & 0x07) << 12); // Fine Y
                ppu->temp_vram_addr = (ppu->temp_vram_addr & 0xFC1F) | ((uint16_t)(value & 0xF8) << 2); // Coarse Y
                ppu->address_latch = 0;
            }
            break;

        case 0x2006: // PPUADDR
            // Writing to PPUADDR is a two-step process
            if (ppu->address_latch == 0) {
                // First write: high byte of the VRAM address
                ppu->temp_vram_addr = (ppu->temp_vram_addr & 0x00FF) | ((uint16_t)(value & 0x3F) << 8);
                ppu->address_latch = 1;
            } else {
                // Second write: low byte of the VRAM address
                ppu->temp_vram_addr = (ppu->temp_vram_addr & 0xFF00) | (uint16_t)value;
                ppu->vram_addr = ppu->temp_vram_addr; // Update the actual VRAM address
                ppu->address_latch = 0;
            }
            break;

        case 0x2007: // PPUDATA
            // Writing to PPUDATA writes the value to VRAM at the address specified by PPUADDR
            PPU_WriteMemory(ppu, ppu->vram_addr, value);

            // Auto-increment PPUADDR based on PPUCTRL bit 2
            if (ppu->ctrl & 0x04) { // VRAM address increment (0: add 1, 1: add 32)
                ppu->vram_addr += 32;
            } else {
                ppu->vram_addr += 1;
            }
            break;

        default:
            // Writing to other registers is generally not allowed or has specific side effects
            // For this basic implementation, we'll just ignore the write
            DEBUG_WARN("Attempted to write to unimplemented PPU register 0x%04X with value 0x%02X", register_address, value);
            break;
    }
}

// Internal function to read from PPU memory (VRAM, Pattern Tables, etc.)
uint8_t PPU_ReadMemory(PPU* ppu, uint16_t address)
{
    uint8_t data = 0x00;
    uint16_t mirrored_address = address & 0x3FFF; // PPU memory is mirrored every 0x4000 bytes

    if (mirrored_address < 0x2000) {
        // 0x0000 - 0x1FFF: Pattern Tables (usually CHR ROM)
        // In this basic structure, we'll assume CHR ROM is loaded directly into ppu->vram for simplicity.
        // A real emulator would need to handle mappers and potentially CHR RAM.
        data = ppu->vram[mirrored_address];
    } else if (mirrored_address < 0x3F00) {
        // 0x2000 - 0x3EFF: Nametables and Attribute Tables
        // Handle mirroring based on the cartridge's mirroring type (horizontal or vertical)
        // This is a simplification; proper mirroring requires mapper implementation.
        uint16_t nametable_address = mirrored_address & 0x0FFF; // Mask to get address within nametable space
        // Basic mirroring (assuming horizontal for now)
        if (nes->bus->mirroring == 0) { // Horizontal mirroring
             if (nametable_address >= 0x0800 && nametable_address < 0x0C00) nametable_address -= 0x0800;
             if (nametable_address >= 0x0C00 && nametable_address < 0x1000) nametable_address -= 0x0800;
        } else { // Vertical mirroring
             if (nametable_address >= 0x0400 && nametable_address < 0x0800) nametable_address -= 0x0400;
             if (nametable_address >= 0x0C00 && nametable_address < 0x1000) nametable_address -= 0x0400;
        }
        data = ppu->vram[0x2000 + nametable_address];

    } else if (mirrored_address < 0x4000) {
        // 0x3F00 - 0x3FFF: Palettes
        uint16_t palette_address = mirrored_address & 0x001F; // Mask to get address within palette space
        // Palette mirroring: 0x3F10, 0x3F14, 0x3F18, 0x3F1C are mirrors of 0x3F00, 0x3F04, 0x3F08, 0x3F0C
        if (palette_address == 0x10) palette_address = 0x00;
        else if (palette_address == 0x14) palette_address = 0x04;
        else if (palette_address == 0x18) palette_address = 0x08;
        else if (palette_address == 0x1C) palette_address = 0x0C;

        data = ppu->vram[0x3F00 + palette_address];
    }

    return data;
}

// Internal function to write to PPU memory (VRAM, Pattern Tables, etc.)
void PPU_WriteMemory(PPU* ppu, uint16_t address, uint8_t value)
{
     uint16_t mirrored_address = address & 0x3FFF; // PPU memory is mirrored every 0x4000 bytes

    if (mirrored_address < 0x2000) {
        // 0x0000 - 0x1FFF: Pattern Tables (usually CHR ROM, read-only)
        // If the cartridge has CHR RAM, writes would be allowed here.
        // For simplicity, we'll allow writes to vram in this range, assuming it's CHR RAM or for testing.
        ppu->vram[mirrored_address] = value;
    } else if (mirrored_address < 0x3F00) {
        // 0x2000 - 0x3EFF: Nametables and Attribute Tables
        // Handle mirroring based on the cartridge's mirroring type (horizontal or vertical)
        // This is a simplification; proper mirroring requires mapper implementation.
         uint16_t nametable_address = mirrored_address & 0x0FFF; // Mask to get address within nametable space
        // Basic mirroring (assuming horizontal for now)
        if (nes->bus->mirroring == 0) { // Horizontal mirroring
             if (nametable_address >= 0x0800 && nametable_address < 0x0C00) nametable_address -= 0x0800;
             if (nametable_address >= 0x0C00 && nametable_address < 0x1000) nametable_address -= 0x0800;
        } else { // Vertical mirroring
             if (nametable_address >= 0x0400 && nametable_address < 0x0800) nametable_address -= 0x0400;
             if (nametable_address >= 0x0C00 && nametable_address < 0x1000) nametable_address -= 0x0400;
        }
        ppu->vram[0x2000 + nametable_address] = value;

    } else if (mirrored_address < 0x4000) {
        // 0x3F00 - 0x3FFF: Palettes
        uint16_t palette_address = mirrored_address & 0x001F; // Mask to get address within palette space
         // Palette mirroring: 0x3F10, 0x3F14, 0x3F18, 0x3F1C are mirrors of 0x3F00, 0x3F04, 0x3F08, 0x3F0C
        if (palette_address == 0x10) palette_address = 0x00;
        else if (palette_address == 0x14) palette_address = 0x04;
        else if (palette_address == 0x18) palette_address = 0x08;
        else if (palette_address == 0x1C) palette_address = 0x0C;

        ppu->vram[0x3F00 + palette_address] = value;
        // Writing to the palette also updates the mirrored addresses
        if (palette_address == 0x00) ppu->vram[0x3F10] = value;
        else if (palette_address == 0x04) ppu->vram[0x3F14] = value;
        else if (palette_address == 0x08) ppu->vram[0x3F18] = value;
        else if (palette_address == 0x0C) ppu->vram[0x3F1C] = value;
    }
}

// Function to simulate one PPU cycle
void PPU_Step(NES* nes)
{
    PPU* ppu = nes->ppu;

    // --- PPU Rendering Simulation (Simplified) ---
    // This is a highly simplified representation of PPU rendering.
    // A full implementation would involve fetching nametable bytes, attribute bytes,
    // pattern table bytes, sprite data, and rendering pixels based on PPUCTRL and PPUMASK.

    // Increment cycle and scanline
    ppu->cycle++;

    // Handle end of scanline
    if (ppu->cycle > 340) {
        ppu->cycle = 0;
        ppu->scanline++;

        // Handle end of frame
        if (ppu->scanline > 261) {
            ppu->scanline = 0;
            ppu->frame_odd ^= 1; // Toggle odd/even frame

            // Clear VBLANK flag at the start of the pre-render scanline (scanline 261)
            ppu->status &= ~0x80;
            ppu->nmi_occured = 0; // Clear NMI flag at the start of the frame
        }
    }

    // --- PPU Status Updates and NMI ---

    // VBLANK starts at scanline 241, cycle 1
    if (ppu->scanline == 241 && ppu->cycle == 1) {
        ppu->status |= 0x80; // Set VBLANK flag
        ppu->nmi_occured = 1; // Indicate NMI occurred
        ppu->nmi_change(); // Trigger NMI change check
    }

    // NMI logic
    // NMI is generated if VBLANK is enabled (PPUCTRL bit 7) and VBLANK occurs
    // The NMI signal is level-sensitive, so it stays high as long as VBLANK is set and NMI is enabled.
    // The CPU reads the NMI vector when it detects a falling edge on the NMI line.
    // A proper emulator needs to synchronize the PPU NMI output with the CPU's NMI input.

    // Simplified NMI logic:
    // The 'nmi_interrupt' flag is set when an NMI should occur and is cleared by the CPU reading PPUSTATUS.
    if (ppu->nmi_output && ppu->nmi_occured) {
        ppu->nmi_interrupt = 1;
    } else {
        ppu->nmi_interrupt = 0;
    }

    // Check for NMI change (rising edge) to signal the CPU
    // This is a simplified check; a real emulator would need more precise timing.
    // if (ppu->nmi_interrupt && !ppu->nmi_previous) {
    //     // Signal CPU for NMI
    //     // nes->cpu->nmi_pending = 1; // Assuming a flag in the CPU structure
    // }
    // ppu->nmi_previous = ppu->nmi_interrupt;


    // --- Rendering (Placeholder) ---
    // The actual pixel rendering logic would go here, based on the current scanline and cycle.
    // This involves fetching tile data, palette data, sprite data, and drawing to the frame buffer.
    // This is a complex process involving many internal PPU components and timings.

    // Example: Drawing a single pixel (placeholder)
    // if (ppu->scanline < 240 && ppu->cycle < 256) {
    //     // Calculate pixel index in the frame buffer
    //     int pixel_index = ppu->scanline * 256 + ppu->cycle;
    //     // Determine pixel color based on rendering logic (placeholder: black)
    //     ppu->frame_buffer[pixel_index] = 0; // Example: set pixel to black
    // }
}

// Function to handle NMI logic changes
void PPU_nmi_change(PPU* ppu) {
    // This function is called when PPUCTRL bit 7 (NMI enable) or PPUSTATUS bit 7 (VBLANK) changes.
    // It determines the state of the PPU's NMI output line.

    int nmi_now = ppu->nmi_output && ppu->nmi_occured;

    if (nmi_now && !ppu->nmi_previous) {
        // Rising edge detected, trigger NMI interrupt in the CPU
        // This requires calling a function in your CPU emulation
        // For example: nes->cpu->trigger_nmi();
         DEBUG_INFO("PPU: NMI rising edge detected");
         // In a real emulator, you would signal the CPU here.
         // For this basic structure, we'll just set a flag.
         ppu->nmi_interrupt = 1;
    }

    ppu->nmi_previous = nmi_now;
}

// Function to get the frame buffer for display
uint8_t* PPU_GetFrameBuffer(PPU* ppu)
{
    return ppu->frame_buffer;
}
*/