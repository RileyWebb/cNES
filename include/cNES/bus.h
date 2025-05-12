#ifndef BUS_H
#define BUS_H

#include <stdint.h>
#include <stddef.h>

typedef struct NES NES;

typedef struct BUS {
    uint8_t memory[0x10000]; // 64KB BUSory map
    uint8_t prgRom[0x8000];  // 32KB PRG ROM
    uint8_t chrRom[0x2000];  // 8KB CHR ROM
    uint8_t vram[0x1000];    // 4KB VRAM for nametables (mirrored)
    uint8_t palette[0x20];   // 32 bytes palette RAM
    uint8_t mapper;          // Mapper type
    uint8_t mirroring;       // Mirroring type
    uint8_t prgRomSize;      // PRG ROM size in 16KB units
    uint8_t chrRomSize;      // CHR ROM size in 8KB units
} BUS;

// IO functions
uint8_t BUS_Read(NES* nes, uint16_t address);
void BUS_Write(NES* nes, uint16_t address, uint8_t value);
uint16_t BUS_Read16(NES* nes, uint16_t address);
void BUS_Write16(NES* nes, uint16_t address, uint16_t value);

// PPU bus mapping for CHR ROM/RAM
uint8_t BUS_PPU_ReadCHR(struct BUS* bus, uint16_t address);
void BUS_PPU_WriteCHR(struct BUS* bus, uint16_t address, uint8_t value);

// PPU bus mapping for PPU address space
uint8_t BUS_PPU_Read(struct BUS* bus, uint16_t address);
void BUS_PPU_Write(struct BUS* bus, uint16_t address, uint8_t value);

// Peak methods (no PC increment)
uint8_t BUS_Peek(NES* nes, uint16_t address);
uint16_t BUS_Peek16(NES* nes, uint16_t address);

#endif // BUS_H