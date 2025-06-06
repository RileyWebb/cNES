#ifndef BUS_H
#define BUS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h> // For bool type

#include "cNES/mapper.h"

// Forward declaration
typedef struct NES NES;

typedef struct BUS {
    // Dynamically allocated memory regions
    uint8_t* cpu_ram;       // CPU RAM ($0000-$07FF, mirrored up to $1FFF), typically 2KB
    uint8_t* vram;          // PPU VRAM for nametables ($2000-$2FFF in PPU space), typically 2KB
    uint8_t* palette_ram;   // PPU Palette RAM ($3F00-$3F1F in PPU space), 32 bytes

    // Cartridge properties
    Mapper *mapper; // Pointer to the mapper structure for cartridge handling

    // Mapper function pointers
    // Called for CPU access to cartridge address space (e.g., $4020-$FFFF)
    uint8_t (*cpu_read)(struct BUS* bus, uint16_t address);
    void    (*cpu_write)(struct BUS* bus, uint16_t address, uint8_t value);

    // Called for PPU access to CHR address space (Pattern Tables, e.g., $0000-$1FFF)
    uint8_t (*ppu_read)(struct BUS* bus, uint16_t address);
    void    (*ppu_write)(struct BUS* bus, uint16_t address, uint8_t value);
    
    NES* nes; // Pointer to the main NES structure for inter-component communication
} BUS;

// IO functions
uint8_t BUS_Read(NES* nes, uint16_t address);
void BUS_Write(NES* nes, uint16_t address, uint8_t value);
uint16_t BUS_Read16(NES* nes, uint16_t address);
void BUS_Write16(NES* nes, uint16_t address, uint16_t value);

// PPU bus mapping for PPU address space
uint8_t BUS_PPU_Read(struct BUS* bus, uint16_t address);
void BUS_PPU_Write(struct BUS* bus, uint16_t address, uint8_t value);

// PPU bus mapping for CHR ROM/RAM
uint8_t BUS_PPU_ReadCHR(struct BUS* bus, uint16_t address);
void BUS_PPU_WriteCHR(struct BUS* bus, uint16_t address, uint8_t value);

#endif // BUS_H