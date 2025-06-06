#ifndef ROM_H
#define ROM_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct Mapper Mapper;

typedef struct ROM {
    FILE *file;

    uint8_t *data;
    size_t size;

    size_t prg_rom_size; // in 16KB units
    size_t chr_rom_size; // in 8KB units
    uint8_t prg_banks; // Number of 16KB PRG ROM banks
    uint8_t chr_banks; // Number of 8KB CHR ROM banks

    uint8_t* prg_rom_data;
    uint8_t* chr_mem_data;  // Can be CHR ROM or CHR RAM

    char *name;
    char *path;

    uint8_t header[16];

    Mapper *mapper; // Pointer to the mapper structure
    

} ROM;

ROM *ROM_LoadFile(const char *path);
ROM *ROM_LoadMemory(uint8_t *data, size_t size);
void ROM_Destroy(ROM *rom);

#endif // ROM_H