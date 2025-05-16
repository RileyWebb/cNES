#include "cNES/nes.h"
#include "cNES/bus.h"
#include "cNES/ppu.h"

// Controller variables
static uint8_t controller_shift[2] = {0, 0}; // Shift registers for controllers
static uint8_t controller_strobe = 0;        // Controller strobe flag

uint8_t BUS_Read(NES* nes, uint16_t address)
{
    // Handle CPU memory map (0x0000 - 0xFFFF)
    if (address < 0x2000) {
        return nes->bus->memory[address & 0x07FF]; // 2KB RAM mirror
    } else if (address == 0x4016 || address == 0x4017) {
        // Controller input registers ($4016, $4017)
        int controller_idx = address - 0x4016; // 0 for $4016, 1 for $4017
        
        // Get the bit 0 of the controller shift register
        uint8_t result = controller_shift[controller_idx] & 0x01;
        
        // If strobe is 1, continuously reload from controller state
        if (controller_strobe) {
            result = nes->controllers[controller_idx] & 0x01;
        } else {
            // Shift right the controller shift register for next read
            controller_shift[controller_idx] >>= 1;
        }
        
        // On real hardware, bits 1-7 come from open bus, but we'll return them as 0
        return result;
    } else if (address < 0x4000) {
        // PPU registers ($2000-$3FFF), mirrored every 8 bytes
        return PPU_ReadRegister(nes->ppu, 0x2000 + (address & 0x7));
    } else if (address < 0x6000) {
        return 0; // Unused space, open bus
    } else if (address < 0x8000) {
        // PRG RAM (if present, not implemented here)
        return 0;
    } else {
        // $8000-$FFFF: PRG ROM (always read from prgRom)
        return nes->bus->prgRom[address - 0x8000];
    }
}

uint8_t BUS_Peek(NES* nes, uint16_t address)
{
    // Similar to BUS_Read but does not modify state (e.g., controller shift registers)
    if (address < 0x2000) {
        return nes->bus->memory[address & 0x07FF]; // 2KB RAM mirror
    } else if (address == 0x4016 || address == 0x4017) {
        // Controller input registers ($4016, $4017)
        int controller_idx = address - 0x4016; // 0 for $4016, 1 for $4017
        return controller_shift[controller_idx] & 0x01; // Return current state without shifting
    } else if (address >= 0x2000 && address < 0x4000) {
        // PPU registers ($2000-$2007), mirrored every 8 bytes up to $3FFF.
        // All reads from PPU registers should go through the PPU_ReadRegister function.
        // This function handles specific behaviors like:
        // - PPUSTATUS ($2002): Clears VBlank flag, resets address latch.
        // - OAMDATA ($2004): Reads from OAM at OAMADDR, with specific behavior during rendering.
        // - PPUDATA ($2007): Handles buffered reads for VRAM and direct reads for palette.
        // - Write-only registers: Return open bus behavior (handled by PPU_ReadRegister).
        //
        // PPU_ReadRegister itself will handle the mirroring (addr & 0x0007) internally.
        return PPU_ReadRegister(nes->ppu, address);
    } else if (address < 0x6000) {
        return 0; // Unused space, open bus
    } else if (address < 0x8000) {
        // PRG RAM (if present, not implemented here)
        return 0;
    } else {
        // $8000-$FFFF: PRG ROM (always read from prgRom)
        return nes->bus->prgRom[address - 0x8000];
    }
}

// Helper for OAM DMA: read a byte from CPU memory (RAM/ROM/IO) for DMA
static uint8_t BUS_DMA_CPU_Read(void* cpu_ptr, uint16_t addr) {
    NES* nes = (NES*)cpu_ptr;
    return BUS_Read(nes, addr);
}

void BUS_Write(NES* nes, uint16_t address, uint8_t value)
{
    // Handle CPU memory map (0x0000 - 0xFFFF)
    if (address < 0x2000) {
        nes->bus->memory[address & 0x07FF] = value; // 2KB RAM mirror
    } else if (address == 0x4016) {
        // Controller Strobe ($4016)
        // Update strobe flag based on bit 0
        controller_strobe = value & 0x01;
        
        // When strobe goes low, update shift registers with current controller state
        if (!controller_strobe) {
            controller_shift[0] = nes->controllers[0];
            controller_shift[1] = nes->controllers[1];
        }
    } else if (address == 0x4014) {
        // OAM DMA ($4014)
        // Value is the page number (high 8 bits of address)
        uint16_t base = ((uint16_t)value) << 8;
        uint8_t start = nes->ppu->oam_addr;
        for (uint16_t i = 0; i < 256; ++i) {
            uint8_t val = BUS_Read(nes, base + i);
            nes->ppu->oam[(start + i) & 0xFF] = val;
        }
        // OAMADDR is NOT incremented after DMA on real hardware.
    } else if (address < 0x4000) {
        // PPU registers ($2000-$3FFF), mirrored every 8 bytes
        PPU_WriteRegister(nes->ppu, 0x2000 + (address & 0x7), value);
    } else if (address < 0x6000) {
        // Unused space, ignore writes
    } else if (address < 0x8000) {
        // PRG RAM (if present, not implemented here)
    } else {
        // $8000-$FFFF: PRG ROM (writes ignored)
    }
}

uint16_t BUS_Read16(NES* nes, uint16_t address)
{
    uint8_t lo = BUS_Read(nes, address);
    uint8_t hi = BUS_Read(nes, address + 1);
    return (uint16_t)lo | ((uint16_t)hi << 8);
}

void BUS_Write16(NES* nes, uint16_t address, uint16_t value)
{
    uint8_t lo = (uint8_t)(value & 0xFF);
    uint8_t hi = (uint8_t)(value >> 8);
    BUS_Write(nes, address, lo);
    BUS_Write(nes, address + 1, hi);
}

// --- PPU Bus Mapping for CHR ROM/RAM ---
uint8_t BUS_PPU_ReadCHR(struct BUS* bus, uint16_t address)
{
    // CHR ROM/RAM is mapped at $0000-$1FFF in PPU address space
    address &= 0x1FFF;
    return bus->chrRom[address];
}

void BUS_PPU_WriteCHR(struct BUS* bus, uint16_t address, uint8_t value)
{
    address &= 0x1FFF;
    if (bus->chrRomSize == 0) { // CHR RAM is used if chrRomSize is 0
        bus->chrRom[address] = value;
    }
    // If CHR ROM, writes are ignored
}

// --- PPU Bus Mapping for PPU Address Space ($0000-$3FFF) ---

uint8_t BUS_PPU_Read(struct BUS* bus, uint16_t address)
{
    address &= 0x3FFF;
    if (address < 0x2000) {
        // Pattern tables (CHR ROM/RAM)
        return BUS_PPU_ReadCHR(bus, address);
    } else if (address < 0x3F00) {
        // Nametables and mirrors ($2000-$2FFF, mirrored to $3EFF)
        // 2KB internal VRAM, mirrored every 0x1000
        uint16_t vram_addr = (address - 0x2000) & 0x0FFF;
        return bus->vram[vram_addr];
    } else if (address < 0x4000) {
        // Palette RAM indexes ($3F00-$3FFF), mirrored every 32 bytes
        uint16_t pal_addr = (address - 0x3F00) & 0x1F;
        return bus->palette[pal_addr];
    }
    return 0;
}

void BUS_PPU_Write(struct BUS* bus, uint16_t address, uint8_t value)
{
    address &= 0x3FFF;
    if (address < 0x2000) {
        // Pattern tables (CHR RAM only; ignore if CHR ROM)
        BUS_PPU_WriteCHR(bus, address, value);
    } else if (address < 0x3F00) {
        // Nametables and mirrors ($2000-$2FFF, mirrored to $3EFF)
        uint16_t vram_addr = (address - 0x2000) & 0x0FFF;
        bus->vram[vram_addr] = value;
    } else if (address < 0x4000) {
        // Palette RAM indexes ($3F00-$3FFF), mirrored every 32 bytes
        uint16_t pal_addr = (address - 0x3F00) & 0x1F;
        bus->palette[pal_addr] = value;
    }
}
