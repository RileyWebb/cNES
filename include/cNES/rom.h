#ifndef ROM_H
#define ROM_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct ROM {
    FILE *file;

    uint8_t *data;
    size_t size;

    char *name;
    char *path;

    uint8_t header[16];

    uint8_t mapper_id;
    size_t prg_rom_size; // in 16KB units
    size_t chr_rom_size; // in 8KB units
} ROM;

ROM *ROM_LoadFile(const char *path);
ROM *ROM_LoadMemory(uint8_t *data, size_t size);
void ROM_Destroy(ROM *rom);

#endif // ROM_H