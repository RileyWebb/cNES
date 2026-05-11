#ifndef FRONTEND_H
#define FRONTEND_H

#include <stdbool.h>
#include <stdint.h>

#include <stddef.h>

typedef struct NES NES;

#define FRONTEND_FONT_PATH_MAX 512
#define FRONTEND_RECENT_ROM_PATH_MAX 256

// New UI theme type
typedef enum {
    FRONTEND_THEME_DEFAULT = 0,
    FRONTEND_THEME_DARK,
    FRONTEND_THEME_LIGHT,
    FRONTEND_THEME_EXCELLENCY
} Frontend_Theme;

// Global variables for UI state
//extern ImGuiIO* ioptr;

typedef struct SDL_Window SDL_Window;
typedef struct SDL_GPUDevice SDL_GPUDevice;
//typedef float ImVec4[4];

extern SDL_GPUDevice *gpu_device; // GPU device for rendering
extern SDL_Window *frontend_window;

extern float frontend_font_size;
extern char frontend_font_path[FRONTEND_FONT_PATH_MAX];

extern bool frontend_fullscreen;
extern bool frontend_paused;
extern float frontend_master_volume;
extern bool frontend_requestReload;

extern bool frontend_showCpuWindow;
extern bool frontend_showToolbar;
extern bool frontend_showDisassembler;
extern bool frontend_showGameScreen;
extern bool frontend_showProfilerWindow;
extern bool frontend_showRomInfoWindow;
extern bool frontend_showPpuViewer;
extern bool frontend_showLog;
extern bool frontend_showMemoryViewer;
extern bool frontend_showSettingsWindow;

extern bool frontend_showAboutWindow;
extern bool frontend_showCreditsWindow;
extern bool frontend_showLicenceWindow;

extern Frontend_Theme frontend_current_theme;

void Frontend_InitStyle(void);
#define Frontend_InitStlye Frontend_InitStyle
void Frontend_Init();
void Frontend_Shutdown();
void Frontend_StartEmulation(NES *nes);
void Frontend_Update(NES *nes);
bool Frontend_CopyFrameBuffer(uint32_t *dest, size_t pixel_count);
void Frontend_AddRecentRom(const char *path);
void Frontend_ClearRecentRoms(void);
int Frontend_GetRecentRomCount(void);
const char *Frontend_GetRecentRom(int index);

// Function to apply a theme
void Frontend_ApplyTheme(Frontend_Theme theme);

// Function to set or toggle SDL window fullscreen
void Frontend_SetFullscreen(bool enabled);
void Frontend_ToggleFullscreen();

// frontend_menus.c
void Frontend_DrawMainMenuBar(NES *nes);
void Frontend_SettingsMenu(NES *nes);
void Frontend_DrawAboutWindow();
void Frontend_ROMInfoWindow(NES *nes);

// Max number of recent ROMs to store
#define FRONTEND_MAX_RECENT_ROMS 5

#endif // FRONTEND_H

