#include "cNES/nes.h"
#include "cNES/bus.h"

uint8_t BUS_Read(NES* nes, uint16_t address)
{
    // Handle CPU memory map (0x0000 - 0xFFFF)
    if (address < 0x2000) {
        return nes->bus->memory[address & 0x07FF]; // 2KB RAM mirror
    } else if (address < 0x4000) {
        //return nes->ppu->memory[address & 0x0007]; // PPU registers
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
        //nes->ppu->memory[address & 0x0007] = value; // PPU registers
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