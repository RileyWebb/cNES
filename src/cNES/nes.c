#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

#include "cNES/bus.h"
#include "cNES/cpu.h"
#include "cNES/ppu.h"
#include "cNES/nes.h"

NES *NES_Create() 
{
    NES *nes = malloc(sizeof(NES));
    if (!nes) {goto error;}
    memset(nes, 0, sizeof(NES)); // Initialize NES structure to zero

    nes->cpu = malloc(sizeof(CPU));
    if (!nes->cpu) {goto error;}
    memset(nes->cpu, 0, sizeof(CPU)); // Initialize CPU structure to zero
    nes->cpu->nes = nes;

    //nes->ppu = malloc(sizeof(PPU));
    //if (!nes->ppu) {goto error;}

    nes->bus = malloc(sizeof(BUS));
    if (!nes->bus) {goto error;}
    memset(nes->bus, 0, sizeof(BUS)); // Initialize BUS structure to zero

    CPU_Reset(nes->cpu);

    return nes;

error:
    NES_Destroy(nes);
    DEBUG_ERROR("Failed to create NES instance");

    return NULL;
}

void NES_Destroy(NES* nes) 
{
    if (nes) {
        if (nes->cpu) free(nes->cpu);
        if (nes->ppu) free(nes->ppu);
        if (nes->bus) free(nes->bus);
        free(nes);
    }
}

int NES_Load(const char* path, NES* nes) 
{
    FILE *file = fopen(path, "rb");
    if (!file) 
    {
        DEBUG_ERROR("Unable to open ROM file %s", path);
        return -1;
    }

    // Read the NES header (first 16 bytes)
    uint8_t header[16];
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) 
    {
        DEBUG_ERROR("Could not read NES header from %s", path);
        fclose(file);
        return -1;
    }

    // Check for valid NES header
    if (header[0] != 'N' || header[1] != 'E' || header[2] != 'S' || header[3] != 0x1A) 
    {
        DEBUG_ERROR("Invalid NES ROM file format");
        fclose(file);
        return -1;
    }

    // Read PRG ROM size (in 16KB units)
    uint8_t prg_rom_banks = header[4];
    uint8_t chr_rom_banks = header[5]; // Not used in nestest, but read for completeness
    uint8_t mapper_info = header[6]; // Mapper info byte
    uint8_t mirroring = header[6] & 0x01; // Mirroring info (0: horizontal, 1: vertical)
    uint8_t has_trainer = (header[6] & 0x04) >> 2; // Trainer presence (0: no trainer, 1: trainer present)
    uint8_t has_battery = (header[6] & 0x02) >> 1; // Battery presence (0: no battery, 1: battery present)
    uint8_t mapper_number = ((header[7] & 0xF0) >> 4) | (mapper_info & 0xF0); // Mapper number

    uint16_t prg_rom_size = prg_rom_banks * 0x4000; // Total PRG ROM size in bytes
    uint16_t chr_rom_size = chr_rom_banks * 0x2000; // Total CHR ROM size in bytes
    uint16_t trainer_size = has_trainer ? 512 : 0; // Trainer size in bytes
    uint16_t total_size = prg_rom_size + chr_rom_size + trainer_size; // Total size of the ROM

    uint8_t *prg_data = malloc(prg_rom_size);
    if (!prg_data) 
    {
        DEBUG_ERROR("Could not allocate memory for PRG ROM");
        fclose(file);
        return -1;
    }

    uint8_t *chr_data = malloc(chr_rom_size);
    if (!chr_data) 
    {
        DEBUG_ERROR("Could not allocate memory for CHR ROM");
        free(prg_data);
        fclose(file);
        return -1;
    }

    uint8_t *trainer_data = malloc(trainer_size);
    if (has_trainer && !trainer_data) 
    {
        DEBUG_ERROR("Could not allocate memory for trainer data");
        free(prg_data);
        free(chr_data);
        fclose(file);
        return -1;
    }
    
    // Read the trainer data if present
    if (has_trainer) 
    {
        if (fread(trainer_data, 1, trainer_size, file) != trainer_size) 
        {
            DEBUG_ERROR("Could not read trainer data from %s", path);
            free(prg_data);
            free(chr_data);
            free(trainer_data);
            fclose(file);
            return -1;
        }
    }

    // Read the PRG ROM data
    if (fread(prg_data, 1, prg_rom_size, file) != prg_rom_size) 
    {
        DEBUG_ERROR("Could not read PRG ROM data from %s", path);
        free(prg_data);
        free(chr_data);
        free(trainer_data);
        fclose(file);
        return -1;
    }

    // Read the CHR ROM data
    if (fread(chr_data, 1, chr_rom_size, file) != chr_rom_size) 
    {
        DEBUG_ERROR("Could not read CHR ROM data from %s", path);
        free(prg_data);
        free(chr_data);
        free(trainer_data);
        fclose(file);
        return -1;
    }

    fclose(file); // Close the ROM file after reading

    // Load the PRG ROM into the CPU's memory map (0x8000 - 0xFFFF)
    memcpy(nes->bus->memory + 0x8000, prg_data, prg_rom_size); // Load PRG ROM into $8000-$FFFF
    memcpy(nes->bus->memory + 0xC000, prg_data, prg_rom_size); // Mirror PRG ROM into $C000-$FFFF
    // Load the CHR ROM into the PPU's memory map (0x0000 - 0x1FFF)
    memcpy(nes->bus->memory + 0x0000, chr_data, chr_rom_size); // Load CHR ROM into $0000-$1FFF
    memcpy(nes->bus->memory + 0x2000, chr_data, chr_rom_size); // Mirror CHR ROM into $2000-$3FFF
    
    // Free allocated memory
    free(prg_data);
    free(chr_data);
    if (has_trainer) free(trainer_data);

    return 0;
}