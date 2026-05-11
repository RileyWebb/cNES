#include "debug.h"
#include "cNES/rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ROM *ROM_LoadFile(const char *path) 
{
    if (!path || path[0] == '\0') {
        DEBUG_ERROR("Invalid ROM path provided");
        return NULL; 
    }

    FILE *file = fopen(path, "rb");
    if (!file) 
    {
        DEBUG_ERROR("Failed to open ROM file %s", path);
        return NULL; 
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size < 16) {
        fclose(file);
        DEBUG_ERROR("ROM file %s is too small to contain a header", path);
        return NULL;
    }

    size_t size = (size_t)file_size;
    uint8_t *data = malloc(size);
    if (!data)
    {
        fclose(file);
        DEBUG_ERROR("Memory allocation failed for ROM data");
        return NULL; 
    }

    if (fread(data, 1, size, file) != size)
    {
        fclose(file);
        free(data);
        DEBUG_ERROR("Failed to read ROM data from file %s", path);
        return NULL; 
    }
    fclose(file); 

    ROM *rom = ROM_LoadMemory(data, size);
    
    // Free the local file buffer, because ROM_LoadMemory creates its own copy.
    free(data);

    if (!rom) {
        return NULL; // Error will have been logged in ROM_LoadMemory
    }
    
    rom->path = strdup(path);
    if (!rom->path)
    {
        ROM_Destroy(rom);
        DEBUG_ERROR("Memory allocation failed for ROM path");
        return NULL; 
    }

    const char* filename = strrchr(path, '/');
    if (!filename) filename = strrchr(path, '\\');
    
    if (filename) {
        rom->name = strdup(filename + 1);
    } else {
        rom->name = strdup(path);
    }

    if (!rom->name) {
        ROM_Destroy(rom);
        DEBUG_ERROR("Memory allocation failed for ROM name");
        return NULL;
    }

    return rom;
}

ROM *ROM_LoadMemory(uint8_t *data, size_t size)
{
    if (!data || size < 16) {
        DEBUG_ERROR("Invalid ROM data or size");
        return NULL;
    }

    ROM *rom = malloc(sizeof(ROM));
    if (!rom) {
        DEBUG_ERROR("Memory allocation failed for ROM struct");
        return NULL;
    } 
    memset(rom, 0, sizeof(ROM)); 

    rom->data = malloc(size);
    if (!rom->data) {
        DEBUG_ERROR("Memory allocation failed for ROM data buffer");
        goto error; 
    } 
    memcpy(rom->data, data, size); 
    rom->size = size; 

    // Read the NES header (first 16 bytes)
    memcpy(rom->header, data, sizeof(rom->header)); 

    if (memcmp(rom->header, "NES\x1A", 4) != 0) { 
        DEBUG_ERROR("Invalid NES header");
        goto error; 
    } 

    // Check if the format is NES 2.0
    // Byte 7, Bits 2-3 are '10' (i.e. bit 2 is 0, bit 3 is 1)
    int is_nes20 = ((rom->header[7] & 0x0C) == 0x08);

    if (is_nes20) {
        // NES 2.0: Mapper ID uses 12 bits (Flags 8 bits 0-3, Flags 7 bits 4-7, Flags 6 bits 4-7)
        rom->mapper_id = ((rom->header[8] & 0x0F) << 8) | 
                         (rom->header[7] & 0xF0) | 
                         ((rom->header[6] & 0xF0) >> 4);

        // PRG ROM Size
        uint16_t prg_msb = rom->header[9] & 0x0F;
        if (prg_msb == 0x0F) {
            // Exponent-multiplier format
            uint8_t e = (rom->header[4] >> 2) & 0x3F;
            uint8_t m = rom->header[4] & 0x03;
            rom->prg_rom_size = (size_t)((1ULL << e) * (m * 2 + 1));
        } else {
            rom->prg_rom_size = (size_t)(((prg_msb << 8) | rom->header[4]) * 0x4000);
        }

        // CHR ROM Size
        uint16_t chr_msb = (rom->header[9] >> 4) & 0x0F;
        if (chr_msb == 0x0F) {
            // Exponent-multiplier format
            uint8_t e = (rom->header[5] >> 2) & 0x3F;
            uint8_t m = rom->header[5] & 0x03;
            rom->chr_rom_size = (size_t)((1ULL << e) * (m * 2 + 1));
        } else {
            rom->chr_rom_size = (size_t)(((chr_msb << 8) | rom->header[5]) * 0x2000);
        }
    } else {
        // iNES 1.0 (Fixed bit-shifts to put byte 7 in upper nibble and byte 6 in lower nibble)
        rom->mapper_id = (rom->header[7] & 0xF0) | ((rom->header[6] & 0xF0) >> 4);
        rom->prg_rom_size = rom->header[4] * 0x4000; 
        rom->chr_rom_size = rom->header[5] * 0x2000; 
    }



    return rom;

error:
    ROM_Destroy(rom);
    DEBUG_ERROR("Unable to load ROM");
    return NULL;
}

void ROM_Destroy(ROM *rom) 
{
    if (rom) {
        if (rom->file) fclose(rom->file); // Keep just in case it's set elsewhere
        if (rom->path) free(rom->path);
        if (rom->name) free(rom->name);
        if (rom->data) free(rom->data);   // Added to fix internal memory leak
        free(rom);
    }
}