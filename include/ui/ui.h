#ifndef UI_H
#define UI_H

#include <stdbool.h>

typedef struct NES NES;

void UI_InitStlye(); // This will be repurposed for applying themes
void UI_Init();
void UI_Shutdown();
void UI_Update(NES *nes);


// New UI helper function
void UI_Log(const char* fmt, ...); // Declaration for UI_Log if not already public

// New UI theme type
typedef enum {
    UI_THEME_DARK,
    UI_THEME_LIGHT
} UI_Theme;

// Global variables for UI state
//extern ImGuiIO* ioptr;

typedef struct SDL_Window SDL_Window;
extern SDL_Window *ui_window;

extern float ui_font_size;
extern char *ui_font_path;

extern bool ui_fullscreen;
extern bool ui_paused;
extern float ui_master_volume;
extern bool ui_requestReload;

extern bool ui_showCpuWindow;
extern bool ui_showToolbar;
extern bool ui_showDisassembler;
extern bool ui_showGameScreen;
extern bool ui_showProfilerWindow;
extern bool ui_showPpuViewer;
extern bool ui_showLog;
extern bool ui_showMemoryViewer;
extern bool ui_showSettingsWindow;

extern bool ui_showAboutWindow;
extern bool ui_showCreditsWindow;
extern bool ui_showLicenceWindow;

extern UI_Theme ui_current_theme;

// Function to apply a theme
void UI_ApplyTheme(UI_Theme theme);

// Function to toggle SDL window fullscreen
void UI_ToggleFullscreen();

// ui_menus.c
void UI_DrawMainMenuBar(NES *nes);
void UI_SettingsMenu(NES *nes);

// Max number of recent ROMs to store
#define UI_MAX_RECENT_ROMS 5

#endif // UI_H