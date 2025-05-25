#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

#include "cNES/bus.h"
#include "cNES/cpu.h"
#include "cNES/ppu.h"
#include "cNES/nes.h"
#include "cNES/rom.h"

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
        // Note: nes->rom is not freed here. If ROM_Create/Load allocates memory for nes->rom,
        // it should be freed here, e.g., by calling ROM_Destroy(nes->rom).
        free(nes);
    }
}

int NES_Load(NES* nes, ROM* rom)
{
    uint8_t *prg_data_temp = NULL;
    uint8_t *chr_data_temp = NULL;
    uint8_t *trainer_data_temp = NULL;

    nes->rom = rom;
    if (!nes->rom) {
        // ROM_LoadFile should ideally log its own error.
        goto error_rom_load;
    }

    // Extract metadata from the ROM structure and its header
    uint8_t prg_rom_banks = nes->rom->header[4];
    uint8_t chr_rom_banks = nes->rom->header[5];
    uint8_t mirroring = nes->rom->header[6] & 0x01;
    uint8_t has_trainer = (nes->rom->header[6] & 0x04) >> 2;
    uint8_t mapper_number = nes->rom->mapper_id;

    // Assuming nes->rom->prg_rom_size and nes->rom->chr_rom_size store sizes in bytes.
    // Using size_t for byte counts is safer for memory operations.
    size_t prg_rom_size_bytes = nes->rom->prg_rom_size;
    size_t chr_rom_size_bytes = nes->rom->chr_rom_size;
    size_t trainer_size_bytes = has_trainer ? 512 : 0;

    // nes->rom->data points to the entire file content, including the 16-byte header.
    // Content (trainer, PRG, CHR) starts after the header.
    uint8_t *current_rom_ptr = nes->rom->data + 16; // Skip iNES header

    if (has_trainer) {
        if (trainer_size_bytes == 0) { // Should not happen if has_trainer is true
             DEBUG_ERROR("ROM '%s': Trainer indicated but size is 0.", rom->path);
             goto error_after_rom_load;
        }
        trainer_data_temp = malloc(trainer_size_bytes);
        if (!trainer_data_temp) {
            DEBUG_ERROR("ROM '%s': Could not allocate %zu bytes for trainer data.", rom->path, trainer_size_bytes);
            goto error_after_rom_load;
        }
        memcpy(trainer_data_temp, current_rom_ptr, trainer_size_bytes);
        current_rom_ptr += trainer_size_bytes;
    }

    if (prg_rom_size_bytes == 0) {
        DEBUG_ERROR("ROM '%s': PRG ROM size is zero.", rom->path);
        goto error_after_rom_load;
    }
    prg_data_temp = malloc(prg_rom_size_bytes);
    if (!prg_data_temp) {
        DEBUG_ERROR("ROM '%s': Could not allocate %zu bytes for PRG ROM.", rom->path, prg_rom_size_bytes);
        goto error_after_rom_load;
    }
    memcpy(prg_data_temp, current_rom_ptr, prg_rom_size_bytes);
    current_rom_ptr += prg_rom_size_bytes;

    if (chr_rom_size_bytes > 0) {
        chr_data_temp = malloc(chr_rom_size_bytes);
        if (!chr_data_temp) {
            DEBUG_ERROR("ROM '%s': Could not allocate %zu bytes for CHR ROM.", rom->path, chr_rom_size_bytes);
            goto error_after_rom_load;
        }
        memcpy(chr_data_temp, current_rom_ptr, chr_rom_size_bytes);
    }

    // Load PRG ROM data into bus memory (typically for NROM/Mapper 0)
    // nes->bus->prgRom is a 32KB (0x8000 bytes) buffer.
    // This direct copy is suitable for NROM (16KB or 32KB).
    // For larger ROMs/other mappers, mapper logic would handle access.
    if (prg_rom_size_bytes > sizeof(nes->bus->prgRom)) {
        DEBUG_ERROR("ROM '%s': PRG ROM size (%zu bytes) > bus PRG buffer (%zu bytes). This may not be supported without a mapper.",
            rom->path, prg_rom_size_bytes, sizeof(nes->bus->prgRom));
        // To prevent overflow, copy only what fits, or rely on mapper.
        // Original code copied prg_rom_size_bytes, risking overflow.
        // For NROM, prg_rom_size_bytes is 16KB or 32KB, which fits.
        // We proceed with original logic, assuming NROM or mapper handles it.
    }
    memcpy(nes->bus->prgRom, prg_data_temp, prg_rom_size_bytes);

    if (prg_rom_banks == 1) { // 16KB PRG ROM, mirror it
        // prg_rom_size_bytes should be 16384 (0x4000).
        // prg_data_temp contains the 16KB of PRG ROM.
        // The memcpy above loaded it into the first 16KB of nes->bus->prgRom.
        // This mirrors it into the second 16KB.
        memcpy(nes->bus->prgRom + 0x4000, prg_data_temp, 0x4000);
    }

    // Load CHR ROM data into bus memory, or initialize CHR RAM
    // nes->bus->chrRom is an 8KB (0x2000 bytes) buffer.
    if (chr_rom_size_bytes > 0) {
        if (chr_rom_size_bytes > sizeof(nes->bus->chrRom)) {
            DEBUG_WARN("ROM '%s': CHR ROM size (%zu bytes) > bus CHR buffer (%zu bytes). Truncating.",
                rom->path, chr_rom_size_bytes, sizeof(nes->bus->chrRom));
            memcpy(nes->bus->chrRom, chr_data_temp, sizeof(nes->bus->chrRom));
        } else {
            memcpy(nes->bus->chrRom, chr_data_temp, chr_rom_size_bytes);
        }
    } else {
        // No CHR ROM, so initialize CHR RAM (typically 8KB)
        memset(nes->bus->chrRom, 0, sizeof(nes->bus->chrRom));
    }

    // Initialize VRAM and palette RAM to zero
    memset(nes->bus->vram, 0, sizeof(nes->bus->vram));
    memset(nes->bus->palette, 0, sizeof(nes->bus->palette));

    // Set mapper, mirroring, and ROM bank counts in the BUS struct
    nes->bus->mapper = mapper_number;
    nes->bus->mirroring = mirroring;
    nes->bus->prgRomSize = prg_rom_banks; // Number of 16KB PRG banks
    nes->bus->chrRomSize = chr_rom_banks; // Number of 8KB CHR banks

    // Free temporary buffers
    if (prg_data_temp) free(prg_data_temp);
    if (chr_data_temp) free(chr_data_temp);
    if (trainer_data_temp) free(trainer_data_temp);

    NES_Reset(nes);
    return 0; // Success

error_after_rom_load:
    // If ROM_LoadFile succeeded but a subsequent step failed, nes->rom might be valid.
    // It should be cleaned up. Assuming ROM_Destroy is the function for that.
    // if (nes && nes->rom) {
    //     ROM_Destroy(nes->rom); // Requires ROM_Destroy from cNES/rom.h
    //     nes->rom = NULL;
    // }
    // Following original selection's pattern: nes->rom is not freed here.
    // This responsibility might lie with the caller or a higher-level NES_Destroy.

error_rom_load:
    // Free any partially allocated temporary buffers
    if (prg_data_temp) free(prg_data_temp);
    if (chr_data_temp) free(chr_data_temp);
    if (trainer_data_temp) free(trainer_data_temp);

    DEBUG_ERROR("Failed to load ROM: %s", rom->path); // Generic error from original
    return -1; // Failure
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