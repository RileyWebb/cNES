#include "cNES/nes.h"
#include "cNES/bus.h"
#include "cNES/ppu.h" // Include for PPU struct definition

// Forward declarations for PPU register access
// These are already in ppu.h, but ensure PPU struct is known if not included above
// uint8_t PPU_ReadRegister(struct PPU* ppu, uint16_t addr);
// void PPU_WriteRegister(struct PPU* ppu, uint16_t addr, uint8_t value);


uint8_t BUS_Read(NES* nes, uint16_t address)
{
    // Handle CPU memory map (0x0000 - 0xFFFF)
    if (address < 0x2000) {
        return nes->bus->memory[address & 0x07FF]; // 2KB RAM mirror
    } else if (address < 0x4000) {
        // PPU registers ($2000-$3FFF), mirrored every 8 bytes
        return PPU_ReadRegister(nes->ppu, 0x2000 + (address & 0x7));
    } else if (address < 0x6000) {
        return nes->bus->memory[address]; // Unused space
    } else if (address < 0x8000) {
        return nes->bus->memory[address]; // PRG ROM mirror
    } else {
        return nes->bus->memory[address]; // PRG ROM
    }

    return 0;
}

void BUS_Write(NES* nes, uint16_t address, uint8_t value)
{
    // Handle CPU memory map (0x0000 - 0xFFFF)
    if (address < 0x2000) {
        nes->bus->memory[address & 0x07FF] = value; // 2KB RAM mirror
    } else if (address < 0x4000) {
        // PPU registers ($2000-$3FFF), mirrored every 8 bytes
        PPU_WriteRegister(nes->ppu, 0x2000 + (address & 0x7), value);
    } else if (address < 0x6000) {
        // Unused space, ignore writes
    } else if (address < 0x8000) {
        nes->bus->memory[address] = value; // PRG ROM mirror
    } else {
        nes->bus->memory[address] = value; // PRG ROM
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