#pragma once

#include "frontend/frontend.h"
#include "cNES/nes.h"

#include "debug.h"
#include "profiler.h"
#include "cNES/ppu.h"
#include "cNES/cpu.h"
#include "cNES/bus.h"
#include "cNES/rom.h"
#include "cNES/version.h"
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <SDL3/SDL_gpu.h>

typedef struct frontend_debug_log {
    debug_log log; // The log entry
    char *formatted; // Formatted string for display
} frontend_debug_log;

extern frontend_debug_log frontend_log_buffer[8192];
extern size_t frontend_log_count;
extern bool frontend_requestReload;
extern bool frontend_showAboutWindow;
extern bool frontend_showCreditsWindow;
extern bool frontend_showLicenceWindow;
extern bool frontend_showCpuWindow;
extern bool frontend_showToolbar;
extern bool frontend_showDisassembler;
extern bool frontend_showGameScreen;
extern bool frontend_showProfilerWindow;
extern bool frontend_showRomInfoWindow;
extern bool frontend_showPpuViewer;
extern bool frontend_showSettings;
extern bool frontend_showLog;
extern bool frontend_showMemoryViewer;
extern bool frontend_showSettingsWindow;

extern SDL_Window *frontend_window;

extern SDL_GPUDevice *gpu_device;
extern ImGuiIO *ioptr;

extern bool frontend_paused;
extern float frontend_master_volume;
extern bool frontend_fullscreen;
extern float frontend_font_size;
extern char frontend_font_path[FRONTEND_FONT_PATH_MAX];

extern SDL_GPUTexture *ppu_game_texture;
extern SDL_GPUSampler *ppu_game_sampler;
extern SDL_GPUTextureSamplerBinding ppu_game_texture_sampler_binding;

extern SDL_GPUTexture *pt_texture0;
extern SDL_GPUTexture *pt_texture1;
extern SDL_GPUSampler *pt_sampler;
extern SDL_GPUTransferBuffer *pt_transfer_buffer;
extern SDL_GPUTransferBuffer *ppu_game_transfer_buffer;

extern int frontend_ppuViewerSelectedPalette;
extern uint16_t frontend_memoryViewerAddress;
extern uint8_t frontend_memorySnapshot[0x10000];
extern int frontend_memoryViewerRows;

extern uint8_t nes_input_state[2];

void Frontend_RequestPause(bool paused);
void Frontend_RequestReset(void);
void Frontend_RequestStepCpu(void);
void Frontend_RequestStepFrame(void);
void Frontend_SyncControllerState(void);


// Logs
void Frontend_LogWindow();
void Frontend_Log(debug_log log);

// CPU/Debugger
void Frontend_CpuWindow(NES *nes);
void Frontend_DebugToolbar(NES *nes);
void Frontend_MemoryViewer(NES *nes);
void Frontend_DrawDisassembler(NES *nes);
void Frontend_ROMInfoWindow(NES *nes);

// PPU
void Frontend_PPUViewer(NES *nes);
void Frontend_RenderPatternTable_GPU(SDL_GPUDevice *device, NES *nes, int table_idx,
                               SDL_GPUTexture *texture, SDL_GPUTransferBuffer *transfer_buffer,
                               uint32_t *pixel_buffer_rgba32, const uint32_t *palette_to_use_rgba32);

// Game Screen
void Frontend_GameScreenWindow(NES *nes);

// Menus
void Frontend_DrawMainMenuContents(NES *nes);

// Profiler
void Frontend_Profiler_DrawWindow(Profiler *profiler);


