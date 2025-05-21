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
extern bool ui_showCpuWindow;
extern bool ui_showToolbar;
extern bool ui_showDisassembler;
extern bool ui_showGameScreen;
extern bool ui_showProfilerWindow;
extern bool ui_showPpuViewer;
extern bool ui_showLog;
extern bool ui_showMemoryViewer;
extern bool ui_showSettingsWindow;

// Function to apply a theme
void UI_ApplyTheme(UI_Theme theme);

// Function to toggle SDL window fullscreen
void UI_ToggleFullscreen();

// Max number of recent ROMs to store
#define UI_MAX_RECENT_ROMS 5