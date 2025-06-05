#ifndef MAPPER_H
#define MAPPER_H

#include <stdint.h>

typedef struct NES NES;
typedef struct ROM ROM;

typedef struct Mapper {
    uint8_t mapper_id; // Mapper ID (0-255)

    uint8_t prg_rom_banks; // Number of PRG ROM banks (16KB each)
    uint8_t chr_rom_banks; // Number of CHR ROM banks (8KB each)
    uint8_t mirroring; // Mirroring type (0: horizontal, 1: vertical, 2: single-screen low, 3: single-screen high, 4: four-screen)
    uint8_t has_trainer; // Whether the ROM has a trainer (1 if present, 0 if not)
    uint8_t has_battery; // Whether the ROM has a battery for saving state (1 if present, 0 if not)

    bool    chr_is_ram;       // True if chr_mem_data is RAM instead of ROM

    // Function pointers for CPU memory access
    uint8_t (*cpu_read)(struct Mapper *mapper, uint16_t address);
    void (*cpu_write)(struct Mapper *mapper, uint16_t address, uint8_t data);

    // Function pointers for PPU memory access
    uint8_t (*ppu_read)(struct Mapper *mapper, uint16_t address);
    void (*ppu_write)(struct Mapper *mapper, uint16_t address, uint8_t data);

} Mapper;

Mapper *Mapper_Create(NES *nes, ROM *rom);
void Mapper_Destroy(Mapper *mapper);

#endif // MAPPER_H