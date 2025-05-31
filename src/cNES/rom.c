#include "debug.h"

#include "cNES/rom.h"

ROM *ROM_LoadFile(const char *path) 
{
    if (!path || strlen(path) == 0) {
        DEBUG_ERROR("Invalid ROM path provided");
        return NULL; // Return NULL if path is invalid
    }

    size_t size = 0;
    FILE *file = fopen(path, "rb");
    uint8_t *data = NULL;

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);

    data = malloc(size);
    if (!data)
    {
        fclose(file);
        DEBUG_ERROR("Memory allocation failed for ROM data");
        return NULL; // Return NULL if memory allocation fails
    }
    fseek(file, 0, SEEK_SET); // Reset file pointer to beginning
    if (fread(data, 1, size, file) != size)
    {
        fclose(file);
        free(data);
        DEBUG_ERROR("Failed to read ROM data from file %s", path);
        return NULL; // Return NULL if reading fails
    }
    fclose(file); // Close the file after reading

    ROM *rom = ROM_LoadMemory(data, size);
    
    rom->path = strdup(path);
    if (!rom->path)
    {
        free(data);
        ROM_Destroy(rom);
        DEBUG_ERROR("Memory allocation failed for ROM path");
        return NULL; // Return NULL if memory allocation for path fails
    }

    const char* filename = strrchr(path, '/');
    if (!filename) filename = strrchr(path, '\\');
    if (filename) {
        rom->name = malloc(strlen(filename));
        if (rom->name) strcpy(rom->name, filename + 1);
    } else {
        rom->name = malloc(strlen(path) + 1);
        if (rom->name) strcpy(rom->name, path);
    }

    return rom; // Return the loaded ROM structure

/*
    ROM *rom = malloc(sizeof(ROM));
    if (!rom) { goto error; } // Check for memory allocation failure
    memset(rom, 0, sizeof(ROM)); // Initialize ROM structure to zero

    rom->path = strdup(path);
    if (!rom->path) { goto error; } // Check for memory allocation failure

    rom->file = fopen(path, "rb");
    if (!rom->file) { goto error; } // Check for file opening failure

    fseek(rom->file, 0, SEEK_END);
    rom->size = ftell(rom->file);
    fseek(rom->file, 0, SEEK_SET);

    if (rom->size < 16) { goto error; } // Check for valid file size

    // Read the NES header (first 16 bytes)
    if (fread(rom->header, 1, sizeof(rom->header), rom->file) != sizeof(rom->header)) { goto error; } // Check for read failure

    rom->data = malloc(rom->size);
    if (!rom->data) { goto error; } // Check for memory allocation failure
    fseek(rom->file, 0, SEEK_SET); // Reset file pointer to beginning
    if (fread(rom->data, 1, rom->size, rom->file) != rom->size) { goto error; } // Check for read failure
    if (memcmp(rom->header, "NES\x1A", 4) != 0) { goto error; } // Check for valid NES header
    rom->mapper_id = ((rom->header[7] & 0xF0) >> 4) | (rom->header[6] & 0xF0); // Mapper number
    rom->prg_rom_size = rom->header[4] * 0x4000; // Total PRG ROM size in bytes
    rom->chr_rom_size = rom->header[5] * 0x2000; // Total CHR ROM size in bytes

    const char* filename = strrchr(path, '/');
    if (!filename) filename = strrchr(path, '\\');
    if (filename) {
        rom->name = malloc(strlen(filename));
        if (rom->name) strcpy(rom->name, filename + 1);
    } else {
        rom->name = malloc(strlen(path) + 1);
        if (rom->name) strcpy(rom->name, path);
    }

    return rom; // Return the loaded ROM structure

error:
    ROM_Destroy(rom);

    DEBUG_ERROR("Unable to load ROM file %s", path);

    return NULL;
*/
}

ROM *ROM_LoadMemory(uint8_t *data, size_t size)
{
    ROM *rom = malloc(sizeof(ROM));
    if (!rom) { goto error; } // Check for memory allocation failure
    memset(rom, 0, sizeof(ROM)); // Initialize ROM structure to zero

    rom->data = malloc(size);
    if (!rom->data) { goto error; } // Check for memory allocation failure
    memcpy(rom->data, data, size); // Copy the ROM data into the structure
    rom->size = size; // Set the size of the ROM data
    if (size < 16) { goto error; } // Check for valid size

    // Read the NES header (first 16 bytes)
    memcpy(rom->header, data, sizeof(rom->header)); // Copy the header from the data
    if (size < sizeof(rom->header)) { goto error; } // Check if data is large enough for header

    if (memcmp(rom->header, "NES\x1A", 4) != 0) { goto error; } // Check for valid NES header
    rom->mapper_id = ((rom->header[7] & 0xF0) >> 4) | (rom->header[6] & 0xF0); // Mapper number
    rom->prg_rom_size = rom->header[4] * 0x4000; // Total PRG ROM size in bytes
    rom->chr_rom_size = rom->header[5] * 0x2000; // Total CHR ROM size in bytes

    return rom;

error:
    ROM_Destroy(rom);

    DEBUG_ERROR("Unable to load ROM");

    return NULL;
}

void ROM_Destroy(ROM *rom) 
{
    if (rom) {
        if (rom->file) fclose(rom->file);
        if (rom->path) free(rom->path);
        if (rom->name) free(rom->name);
        free(rom);
    }
}