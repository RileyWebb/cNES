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

    nes->cpu = CPU_Create(nes);
    if (!nes->cpu) {goto error;}

    nes->ppu = PPU_Create(nes);
    if (!nes->ppu) {goto error;}

    nes->bus = malloc(sizeof(BUS));
    if (!nes->bus) {goto error;}
    memset(nes->bus, 0, sizeof(BUS)); // Initialize BUS structure to zero

    NES_Reset(nes);

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
    size_t total_size = (size_t)prg_rom_size + (size_t)chr_rom_size + (size_t)trainer_size; // Total size of the ROM

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

    uint8_t *trainer_data = NULL;
    if (has_trainer) {
        trainer_data = malloc(trainer_size);
        if (!trainer_data) 
        {
            DEBUG_ERROR("Could not allocate memory for trainer data");
            free(prg_data);
            free(chr_data);
            fclose(file);
            return -1;
        }
        // Read the trainer data if present
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
        if (trainer_data) free(trainer_data);
        fclose(file);
        return -1;
    }

    // Read the CHR ROM data
    if (chr_rom_size > 0) {
        if (fread(chr_data, 1, chr_rom_size, file) != chr_rom_size) 
        {
            DEBUG_ERROR("Could not read CHR ROM data from %s", path);
            free(prg_data);
            free(chr_data);
            if (trainer_data) free(trainer_data);
            fclose(file);
            return -1;
        }
    }

    fclose(file); // Close the ROM file after reading

    // Load the PRG ROM into the BUS's PRG ROM area
    memcpy(nes->bus->prgRom, prg_data, prg_rom_size);
    // If only one PRG ROM bank, mirror it into the upper 16KB
    if (prg_rom_banks == 1) {
        memcpy(nes->bus->prgRom + 0x4000, prg_data, 0x4000);
    }

    // --- DO NOT copy PRG ROM into CPU memory map ($8000-$FFFF) ---
    // The bus will always use prgRom for $8000-$FFFF

    // Load the CHR ROM into the BUS's CHR ROM area
    if (chr_rom_size > 0) {
        memcpy(nes->bus->chrRom, chr_data, chr_rom_size);
    } else {
        // If no CHR ROM, allocate CHR RAM (8KB)
        memset(nes->bus->chrRom, 0, 0x2000);
    }

    // Initialize VRAM and palette RAM to zero
    memset(nes->bus->vram, 0, sizeof(nes->bus->vram));
    memset(nes->bus->palette, 0, sizeof(nes->bus->palette));

    // Set mapper, mirroring, and ROM size info in the BUS struct
    nes->bus->mapper = mapper_number;
    nes->bus->mirroring = mirroring;
    nes->bus->prgRomSize = prg_rom_banks;
    nes->bus->chrRomSize = chr_rom_banks;

    // Free allocated memory
    free(prg_data);
    free(chr_data);
    if (trainer_data) free(trainer_data);

    NES_Reset(nes); // Reset the NES after loading the ROM

    return 0;
}

// Add NES_Step function to step PPU and CPU, and handle NMI interrupts
void NES_Step(NES *nes)
{
    // Step the PPU three times for every CPU step (PPU runs 3x faster)
    for (int i = 0; i < 3; ++i) {
        PPU_Step(nes->ppu);
    }

    // Handle NMI if triggered by PPU
    if (nes->ppu->nmi_interrupt_line) {
        CPU_NMI(nes->cpu);
        nes->ppu->nmi_interrupt_line = 0; // Clear the NMI interrupt after CPU services it
    }

    // Step the CPU
    if (CPU_Step(nes->cpu) == -1) {
        DEBUG_ERROR("CPU execution halted due to error");
    }
}

// Add NES_StepFrame function to run the NES for one frame
void NES_StepFrame(NES *nes)
{
    // Run until we enter the next frame
    int current_frame = nes->ppu->frame_odd;
    while (current_frame == nes->ppu->frame_odd) {
        NES_Step(nes);
    }
}

void NES_Reset(NES *nes) 
{
    CPU_Reset(nes->cpu);
    PPU_Reset(nes->ppu);

    // Reset the BUS memory
    memset(nes->bus->memory, 0, sizeof(nes->bus->memory));

    // Reset controller states
    nes->controllers[0] = 0;
    nes->controllers[1] = 0;

}

// Poll controller state (returns the current state of the specified controller)
uint8_t NES_PollController(NES* nes, int controller)
{
    return nes->controllers[controller];
}

// Set controller state (UI or platform layer calls this to update controller state)
void NES_SetController(NES* nes, int controller, uint8_t state)
{
    if (!nes) return;
    if (controller < 0 || controller > 1) return;
    nes->controllers[controller] = state;
}