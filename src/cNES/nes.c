#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

#include "cNES/bus.h"
#include "cNES/apu.h"
#include "cNES/cpu.h"
#include "cNES/mapper.h"
#include "cNES/ppu.h"
#include "cNES/nes.h"
#include "cNES/rom.h"

NES *NES_Create() 
{
    NES *nes = malloc(sizeof(NES));
    if (!nes) {goto error;}
    memset(nes, 0, sizeof(NES)); // Initialize NES structure to zero

    nes->bus = malloc(sizeof(BUS));
    if (!nes->bus) {goto error;}
    memset(nes->bus, 0, sizeof(BUS)); // Initialize BUS structure to zero

    //TODO: fix later
    if (nes->bus->mapper == 1) {
        nes->bus->mapper_data = malloc(sizeof(Mapper1_State));
        memset(nes->bus->mapper_data, 0, sizeof(Mapper1_State));
        Mapper1_State *mmc1_state = (Mapper1_State *)nes->bus->mapper_data;
        mmc1_state->shift_register = 0x10;
        mmc1_state->control = 0x0C; // default PRG mode (16KB fixed high)
    }

    nes->cpu = CPU_Create(nes);
    if (!nes->cpu) {goto error;}

    nes->ppu = PPU_Create(nes);
    if (!nes->ppu) {goto error;}
    nes->bus->ppu = nes->ppu;

    nes->apu = APU_Create(nes);
    if (!nes->apu) {goto error;}

    // Set default NTSC preset
    static const uint32_t default_nes_palette[64] = {
        0xFF666666, 0xFF882A00, 0xFFA71214, 0xFFA4003B, 0xFF7E005C, 0xFF40006E, 0xFF00066C, 0xFF001D56,
        0xFF003533, 0xFF00480B, 0xFF005200, 0xFF084F00, 0xFF4D4000, 0xFF000000, 0xFF000000, 0xFF000000,
        0xFFADADAD, 0xFFD95F15, 0xFFFF4042, 0xFFFE2775, 0xFFCC1AA0, 0xFF7B1EB7, 0xFF2031B5, 0xFF004E99,
        0xFF006D6B, 0xFF008738, 0xFF00930E, 0xFF328F00, 0xFF8D7C00, 0xFF000000, 0xFF000000, 0xFF000000,
        0xFFFEFFFF, 0xFFFFB064, 0xFFFF9092, 0xFFFF76C6, 0xFFFF6AF3, 0xFFCC6EFE, 0xFF7081FE, 0xFF229EEA,
        0xFF00BEBC, 0xFF00D888, 0xFF30E45C, 0xFF82E045, 0xFFDECD48, 0xFF4F4F4F, 0xFF000000, 0xFF000000,
        0xFFFEFFFF, 0xFFFFDFC0, 0xFFFFD2D3, 0xFFFFC8E8, 0xFFFFC2FB, 0xFFEAC4FE, 0xFFC5CCFE, 0xFFA5D8F7,
        0xFF94E5E4, 0xFF96EECF, 0xFFABF4BD, 0xFFCCF3B3, 0xFFF2EBB5, 0xFFB8B8B8, 0xFF000000, 0xFF000000
    };
    memcpy(nes->settings.video.palette, default_nes_palette, sizeof(default_nes_palette));
    NES_SetRegionPreset(nes, NES_REGION_NTSC);
    nes->settings.audio.sample_rate = 44100;
    nes->settings.audio.volume = 1.0f;

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
        if (nes->apu) APU_Destroy(nes->apu);
        if (nes->ppu) free(nes->ppu);
        if (nes->bus) free(nes->bus);
        if (nes->rom) ROM_Destroy(nes->rom);
        free(nes);
    }
}

int NES_Load(NES* nes, ROM* rom)
{
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
    NES_MapperInfo mapper_info = NES_Mapper_Get(mapper_number);

    DEBUG_INFO("ROM '%s': mapper %u (%s)", rom->path, mapper_number, mapper_info.name);
    if (!mapper_info.supported) {
        DEBUG_WARN("Mapper %u is not implemented yet; ROM may fail or behave incorrectly.", mapper_number);
    }

    // Assuming nes->rom->prg_rom_size and nes->rom->chr_rom_size store sizes in bytes.
    // Using size_t for byte counts is safer for memory operations.
    size_t prg_rom_size_bytes = nes->rom->prg_rom_size;
    size_t chr_rom_size_bytes = nes->rom->chr_rom_size;
    size_t trainer_size_bytes = has_trainer ? 512 : 0;

    uint8_t *current_rom_ptr = nes->rom->data + 16; // Skip iNES header

    if (has_trainer) {
        if (trainer_size_bytes == 0) { // Should not happen if has_trainer is true
             DEBUG_ERROR("ROM '%s': Trainer indicated but size is 0.", rom->path);
             goto error_after_rom_load;
        }
        current_rom_ptr += trainer_size_bytes;
    }

    if (prg_rom_size_bytes == 0) {
        DEBUG_ERROR("ROM '%s': PRG ROM size is zero.", rom->path);
        goto error_after_rom_load;
    }
    if ((size_t)(current_rom_ptr - nes->rom->data) + prg_rom_size_bytes > nes->rom->size) {
        DEBUG_ERROR("ROM '%s': PRG ROM data exceeds file size.", rom->path);
        goto error_after_rom_load;
    }
    nes->bus->prgRomData = current_rom_ptr;
    nes->bus->prgRomDataSize = prg_rom_size_bytes;
    nes->bus->prgBankSelect = 0;
    current_rom_ptr += prg_rom_size_bytes;

    if (chr_rom_size_bytes > 0) {
        if ((size_t)(current_rom_ptr - nes->rom->data) + chr_rom_size_bytes > nes->rom->size) {
            DEBUG_ERROR("ROM '%s': CHR ROM data exceeds file size.", rom->path);
            goto error_after_rom_load;
        }
        nes->bus->chrRomData = current_rom_ptr;
        nes->bus->chrRomDataSize = chr_rom_size_bytes;
        nes->bus->chrBankSelect = 0;
    } else {
        nes->bus->chrRomData = NULL;
        nes->bus->chrRomDataSize = 0;
        memset(nes->bus->chrRam, 0, sizeof(nes->bus->chrRam));
    }

    // Initialize VRAM and palette RAM to zero
    memset(nes->bus->vram, 0, sizeof(nes->bus->vram));
    memset(nes->bus->palette, 0, sizeof(nes->bus->palette));

    // Set mapper, mirroring, and ROM bank counts in the BUS struct
    nes->bus->mapper = mapper_number;
    nes->bus->mirroring = mirroring;
    nes->bus->prgRomSize = prg_rom_banks; // Number of 16KB PRG banks
    nes->bus->chrRomSize = chr_rom_banks; // Number of 8KB CHR banks

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
    DEBUG_ERROR("Failed to load ROM: %s", rom->path); // Generic error from original
    return -1; // Failure
}

// Add NES_Step function to step PPU and CPU, and handle NMI interrupts
void NES_Step(NES *nes)
{
    // Step the CPU
    int cpu_cycles = CPU_Step(nes->cpu);
    if (cpu_cycles == -1) {
        DEBUG_ERROR("CPU execution halted due to error");
    } else if (nes->apu) {
        APU_Clock(nes->apu, (uint32_t)cpu_cycles);
    }

    // Step the PPU three times for every CPU cycle (PPU runs 3x faster)
    for (int i = 0; i < cpu_cycles * 3; i++) {
        PPU_Step(nes->ppu);
    }
    
    // Handle NMI if triggered by PPU
    if (nes->ppu->nmi_interrupt_line) {
        CPU_NMI(nes->cpu);
        nes->ppu->nmi_interrupt_line = false;
        
        // NMI takes 7 CPU cycles
        if (nes->apu) {
            APU_Clock(nes->apu, 7);
        }
        for (int i = 0; i < 7 * 3; i++) {
            PPU_Step(nes->ppu);
        }
    }
}

// Add NES_StepFrame function to run the NES for one frame (OPTIMIZED: avoid frame_odd copy)
void NES_StepFrame(NES *nes)
{
    // Run until PPU frame counter changes (avoid repeated variable reads)
    const int starting_frame = nes->ppu->frame_odd;
    while (nes->ppu->frame_odd == starting_frame) {
        NES_Step(nes);
    }
}

void NES_Reset(NES *nes) 
{
    //TODO: TEMP
    if (nes->bus && nes->bus->mapper_data)
    {
        free(nes->bus->mapper_data);
        nes->bus->mapper_data = NULL;
    }

    if (nes->bus->mapper == 1) {
        nes->bus->mapper_data = malloc(sizeof(Mapper1_State));
        memset(nes->bus->mapper_data, 0, sizeof(Mapper1_State));
        Mapper1_State *mmc1_state = (Mapper1_State *)nes->bus->mapper_data;
        mmc1_state->shift_register = 0x10;
        mmc1_state->control = 0x0C; // default PRG mode (16KB fixed high)
    }

    CPU_Reset(nes->cpu);
    PPU_Reset(nes->ppu);
    if (nes->apu) {
        APU_Reset(nes->apu);
    }
    nes->bus->prgBankSelect = 0;
    nes->bus->chrBankSelect = 0;
    if (nes->bus->mirroring) {
        PPU_SetMirroring(nes->ppu, MIRROR_VERTICAL);
    } else {
        PPU_SetMirroring(nes->ppu, MIRROR_HORIZONTAL);
    }

    // Reset the BUS memory
    memset(nes->bus->memory, 0, sizeof(nes->bus->memory));

    // Reset controller states
    nes->controllers[0] = 0;
    nes->controllers[1] = 0;
}

uint64_t NES_GetFrameCount(NES *nes)
{
    if (!nes || !nes->ppu) {
        return 0;
    }

    return nes->ppu->frame_count;
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
void NES_SetRegionPreset(NES *nes, NES_Region region) {
    if (!nes) return;

    nes->settings.region = region;
    
    switch (region) {
        case NES_REGION_NTSC:
            nes->settings.timing.scanlines_visible = 240;
            nes->settings.timing.scanline_vblank = 241;
            nes->settings.timing.scanline_prerender = 261;
            nes->settings.timing.cycles_per_scanline = 341;
            nes->settings.timing.cpu_clock_rate = 1789773.0f; // Hz
            break;
            
        case NES_REGION_PAL:
            nes->settings.timing.scanlines_visible = 240;
            nes->settings.timing.scanline_vblank = 241;
            nes->settings.timing.scanline_prerender = 311;
            // 341 cycles per scanline. VBlank is much longer in PAL (70 scanlines vs 20).
            nes->settings.timing.cycles_per_scanline = 341;
            nes->settings.timing.cpu_clock_rate = 1662607.0f; // Hz
            break;
            
        case NES_REGION_DENDY:
            nes->settings.timing.scanlines_visible = 240;
            // Dendy: 50Hz, but CPU/PPU timing ratio matches NTSC except for extended VBlank.
            // VBlank starts later in Dendy.
            nes->settings.timing.scanline_vblank = 291;
            nes->settings.timing.scanline_prerender = 311;
            nes->settings.timing.cycles_per_scanline = 341;
            nes->settings.timing.cpu_clock_rate = 1773448.0f; // Hz
            break;

        case NES_REGION_CUSTOM:
            // Do not override user settings
            break;
    }

    if (nes->apu) {
        double cpu_rate = nes->settings.timing.cpu_clock_rate > 0.0f ? (double)nes->settings.timing.cpu_clock_rate : 1789773.0;
        int sample_rate = nes->settings.audio.sample_rate > 0 ? nes->settings.audio.sample_rate : 44100;
        nes->apu->cycles_per_sample = cpu_rate / (double)sample_rate;
    }
}

static const uint32_t default_nes_palette[64] = {
    0xFF666666, 0xFF882A00, 0xFFA71214, 0xFFA4003B, 0xFF7E005C, 0xFF40006E, 0xFF00066C, 0xFF001D56,
    0xFF003533, 0xFF00480B, 0xFF005200, 0xFF084F00, 0xFF4D4000, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFADADAD, 0xFFD95F15, 0xFFFF4042, 0xFFFE2775, 0xFFCC1AA0, 0xFF7B1EB7, 0xFF2031B5, 0xFF004E99,
    0xFF006D6B, 0xFF008738, 0xFF00930E, 0xFF328F00, 0xFF8D7C00, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFFFFEFF, 0xFFFFB064, 0xFFFF9092, 0xFFFF76C6, 0xFFFF6AF3, 0xFFCC6EFE, 0xFF7081FE, 0xFF229EEA,
    0xFF00BEBC, 0xFF00D888, 0xFF30E45C, 0xFF82E045, 0xFFDECD48, 0xFF4F4F4F, 0xFF000000, 0xFF000000,
    0xFFFFFEFF, 0xFFFFDFC0, 0xFFFFD2D3, 0xFFFFC8E8, 0xFFFFC2FB, 0xFFEAC4FE, 0xFFC5CCFE, 0xFFA5D8F7,
    0xFF94E5E4, 0xFF96EECF, 0xFFABF4BD, 0xFFCCF3B3, 0xFFF2EBB5, 0xFFB8B8B8, 0xFF000000, 0xFF000000
};

void NES_LoadPaletteRGBA(NES *nes, const uint32_t* rgba_palette) {
    if (!nes || !rgba_palette) return;

    // Convert RGBA string (0xRRGGBBAA) to ABGR int (0xAABBGGRR) for SDL internal rendering
    for (int i = 0; i < 64; ++i) {
        uint32_t c = rgba_palette[i];
        uint32_t r = (c >> 24) & 0xFF;
        uint32_t g = (c >> 16) & 0xFF;
        uint32_t b = (c >>  8) & 0xFF;
        uint32_t a = (c >>  0) & 0xFF;
        
        nes->settings.video.palette[i] = (a << 24) | (b << 16) | (g << 8) | r;
    }
}
