#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <SDL3/SDL.h>
#include <cimgui.h>
#include <cimgui_impl.h>
#include <cimplot.h> // REFACTOR-NOTE: Included, but not explicitly used in UI functions. Ensure it's needed or remove.
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_messagebox.h>
#include <stdio.h> // For vsnprintf, snprintf
#include <string.h> // For strncpy, strcmp, strrchr
#include <stdarg.h> // For va_list
#include <float.h>  // For FLT_MIN

#include "debug.h"
#include "cNES/nes.h"
#include "cNES/cpu.h"
#include "cNES/ppu.h"
#include "cNES/bus.h"
#include "cNES/debugging.h"

#include "ui.h"

SDL_Window *window;
SDL_GLContext glContext;

ImGuiIO* ioptr;

// --- UI State ---
static bool ui_showPpuViewer = true;
static bool ui_showLog = true; // REFACTOR-NOTE: Defaulting to true for better initial layout visibility
static bool ui_paused = true;
//static bool ui_openRomModal = false; // REFACTOR-NOTE: Replaced by SDL file dialog
static bool ui_showMemoryViewer = true;
static bool ui_showSettingsWindow = false;
static char ui_romPath[256] = "";
static char ui_currentRomName[256] = "No ROM Loaded";
static char ui_logBuffer[4096] = ""; // REFACTOR-NOTE: Consider a more dynamic buffer or circular buffer for extensive logging.
static int ui_logLen = 0;
static Uint32 lastFrameTime = 0;
static float ui_fps = 0.0f;
//static float ui_fps_display = 0.0f; // FPS for status bar display
static UI_Theme current_ui_theme = UI_THEME_DARK;
static float ui_master_volume = 0.8f;
static bool ui_sdl_fullscreen = false;

static char ui_recentRoms[UI_MAX_RECENT_ROMS][256];
static int ui_recentRomsCount = 0;

static bool ui_showAboutWindow = false;
static int ui_ppuViewerSelectedPalette = 0;
static bool ui_openSaveStateModal = false;
static bool ui_openLoadStateModal = false;
static int ui_selectedSaveLoadSlot = 0;

static uint8_t nes_input_state[2] = {0, 0};

static bool ui_showCpuWindow = true;
// static bool ui_showPpuWindow = false; // REFACTOR-NOTE: Merged into PPU Viewer
static bool ui_showToolbar = true;
static bool ui_showDisassembler = true;
static bool ui_showGameScreen = true;

// REFACTOR-NOTE: Flag to ensure default docking layout is applied only once.
static bool ui_first_frame = true;

// --- Helper: Append to log ---
void UI_Log(const char* fmt, ...) {
    // REFACTOR-NOTE: Added basic timestamp. Customize as needed.
    // char time_buf[32];
    // time_t now = time(NULL);
    // strftime(time_buf, sizeof(time_buf), "%H:%M:%S ", localtime(&now));
    // int time_len = strlen(time_buf);
    // if (ui_logLen + time_len < (int)sizeof(ui_logBuffer) -1) {
    //     strncpy(ui_logBuffer + ui_logLen, time_buf, sizeof(ui_logBuffer) - ui_logLen -1);
    //     ui_logLen += time_len;
    // }

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(ui_logBuffer + ui_logLen, sizeof(ui_logBuffer) - ui_logLen -1, fmt, args); // -1 for null terminator
    va_end(args);

    if (n > 0) {
        ui_logLen += n;
        // Ensure there's always a newline if the message doesn't have one, and space for it.
        if (ui_logLen > 0 && ui_logBuffer[ui_logLen-1] != '\n' && ui_logLen < (int)sizeof(ui_logBuffer) - 2) {
            ui_logBuffer[ui_logLen++] = '\n';
        }
        ui_logBuffer[ui_logLen] = '\0'; // Ensure null termination
    }
    
    // Simple wrap-around if buffer is full (very basic, could lose messages)
    if (ui_logLen >= (int)sizeof(ui_logBuffer) - (128 * 2) ) { // Keep some buffer if close to full
         DEBUG_WARN("Log buffer full, consider increasing size or implementing circular buffer.");
         // To prevent overflow, one could clear or implement a circular buffer here.
         // For simplicity now, let's just prevent writing past the buffer.
         // A more robust solution would be a circular buffer.
         // For now, just reset if it gets too full to avoid issues.
         // ui_logLen = 0; // Or, shift content.
    }
}

// --- Helper: Add to Recent ROMs ---
static void UI_AddRecentRom(const char* path) {
    for (int i = 0; i < ui_recentRomsCount; ++i) {
        if (strcmp(ui_recentRoms[i], path) == 0) {
            if (i > 0) {
                char temp[256];
                strncpy(temp, ui_recentRoms[i], 255);
                temp[255] = '\0';
                for (int j = i; j > 0; --j) {
                    strncpy(ui_recentRoms[j], ui_recentRoms[j-1], 255);
                    ui_recentRoms[j][255] = '\0';
                }
                strncpy(ui_recentRoms[0], temp, 255);
                ui_recentRoms[0][255] = '\0';
            }
            return;
        }
    }

    if (ui_recentRomsCount < UI_MAX_RECENT_ROMS) {
        for (int i = ui_recentRomsCount; i > 0; --i) {
            strncpy(ui_recentRoms[i], ui_recentRoms[i-1], 255);
            ui_recentRoms[i][255] = '\0';
        }
        ui_recentRomsCount++;
    } else { // Max count reached, shift oldest
        for (int i = UI_MAX_RECENT_ROMS - 1; i > 0; --i) {
            strncpy(ui_recentRoms[i], ui_recentRoms[i-1], 255);
            ui_recentRoms[i][255] = '\0';
        }
    }
    strncpy(ui_recentRoms[0], path, 255);
    ui_recentRoms[0][255] = '\0';
}

void UI_LoadRom(NES* nes, const char* path) {
    if (NES_Load(path, nes) == 0) {
        UI_Log("Successfully loaded ROM: %s", path);
        const char* filename = strrchr(path, '/');
        if (!filename) filename = strrchr(path, '\\'); // Handle Windows paths
        if (filename) strncpy(ui_currentRomName, filename + 1, sizeof(ui_currentRomName) - 1);
        else strncpy(ui_currentRomName, path, sizeof(ui_currentRomName) - 1);
        ui_currentRomName[sizeof(ui_currentRomName) - 1] = '\0';
        NES_Reset(nes);
        UI_AddRecentRom(path);
    } else {
        UI_Log("Failed to load ROM: %s", path);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ROM Load Error", "Failed to load the specified ROM file.", window);
        strncpy(ui_currentRomName, "Failed to load ROM", sizeof(ui_currentRomName) -1);
        ui_currentRomName[sizeof(ui_currentRomName) - 1] = '\0';
    }
}

void UI_Reset(NES* nes) {
    NES_Reset(nes);
    UI_Log("NES Reset");
}

void UI_StepFrame(NES* nes) {
    NES_StepFrame(nes);
    UI_Log("Stepped one frame");
}

void UI_TogglePause(NES* nes) {
    ui_paused = !ui_paused;
    UI_Log(ui_paused ? "Emulation Paused" : "Emulation Resumed");
}

static void UI_HandleInputEvent(const SDL_Event* e) {
    int pressed = (e->type == SDL_EVENT_KEY_DOWN);
    switch (e->key.key) {
        case SDLK_D: nes_input_state[0] = pressed ? (nes_input_state[0] | 0x01) : (nes_input_state[0] & ~0x01); break;
        case SDLK_A: nes_input_state[0] = pressed ? (nes_input_state[0] | 0x02) : (nes_input_state[0] & ~0x02); break;
        case SDLK_S: nes_input_state[0] = pressed ? (nes_input_state[0] | 0x04) : (nes_input_state[0] & ~0x04); break;
        case SDLK_W: nes_input_state[0] = pressed ? (nes_input_state[0] | 0x08) : (nes_input_state[0] & ~0x08); break;
        case SDLK_RETURN: nes_input_state[0] = pressed ? (nes_input_state[0] | 0x10) : (nes_input_state[0] & ~0x10); break;
        case SDLK_RSHIFT: case SDLK_LSHIFT: nes_input_state[0] = pressed ? (nes_input_state[0] | 0x20) : (nes_input_state[0] & ~0x20); break;
        case SDLK_J: nes_input_state[0] = pressed ? (nes_input_state[0] | 0x40) : (nes_input_state[0] & ~0x40); break;
        case SDLK_K: nes_input_state[0] = pressed ? (nes_input_state[0] | 0x80) : (nes_input_state[0] & ~0x80); break;
        default: break;
    }
}

static const SDL_DialogFileFilter filters[] = {
    { "NES ROMs",  "nes;fds;unif;nes2;ines" }, // REFACTOR-NOTE: Combined common ROM types
    { "Archives", "zip;7z;rar" },
    { "All files",   "*" }
};

static void SDLCALL FileDialogCallback(void* userdata, const char* const* filelist, int filter_index) {
    if (!filelist || !*filelist) {
        if (SDL_GetError() && strlen(SDL_GetError()) > 0) { // Check if there's an actual error message
            DEBUG_ERROR("SDL File Dialog Error: %s", SDL_GetError());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "File Dialog Error", SDL_GetError(), window);
        } else {
            DEBUG_WARN("No file selected or dialog cancelled.");
        }
        return;
    }

    const char* selected_file = *filelist; // Only one file due to `allow_many = false`
    if (selected_file) {
        UI_Log("File selected via dialog: %s", selected_file);
        strncpy(ui_romPath, selected_file, sizeof(ui_romPath) -1);
        ui_romPath[sizeof(ui_romPath)-1] = '\0';
        UI_LoadRom((NES *)userdata, ui_romPath);
    }
}

void UI_DrawFileMenu(NES* nes) {
    // REFACTOR-NOTE: Consider adding icons to menu items (e.g., using FontAwesome)
    if (igBeginMenu("File", true)) {
        if (igMenuItem_Bool("Open ROM...", "Ctrl+O", false, true)) {
             SDL_ShowOpenFileDialog(FileDialogCallback, nes, window, filters, SDL_arraysize(filters), NULL, false); // allow_many = false
        }

        if (igBeginMenu("Recent ROMs", ui_recentRomsCount > 0)) {
            for (int i = 0; i < ui_recentRomsCount; ++i) {
                char label[270];
                // REFACTOR-NOTE: Extract just filename for recent menu for brevity
                const char* filename_display = strrchr(ui_recentRoms[i], '/');
                if (!filename_display) filename_display = strrchr(ui_recentRoms[i], '\\');
                filename_display = filename_display ? filename_display + 1 : ui_recentRoms[i];
                snprintf(label, sizeof(label), "%d. %s", i + 1, filename_display);

                if (igMenuItem_Bool(label, NULL, false, true)) {
                    strncpy(ui_romPath, ui_recentRoms[i], sizeof(ui_romPath) -1);
                    ui_romPath[sizeof(ui_romPath)-1] = '\0';
                    UI_LoadRom(nes, ui_romPath);
                }
            }
            igEndMenu();
        }
        igSeparator();
        if (igMenuItem_Bool("Save State...", "Ctrl+S", false, nes != NULL && strlen(ui_currentRomName) > 0 && strcmp(ui_currentRomName, "No ROM Loaded") != 0 && strcmp(ui_currentRomName, "Failed to load ROM") != 0)) {
            ui_openSaveStateModal = true;
        }
        if (igMenuItem_Bool("Load State...", "Ctrl+L", false, nes != NULL && strlen(ui_currentRomName) > 0 && strcmp(ui_currentRomName, "No ROM Loaded") != 0 && strcmp(ui_currentRomName, "Failed to load ROM") != 0)) {
            ui_openLoadStateModal = true;
        }
        igSeparator();
        if (igMenuItem_Bool("Exit", "Alt+F4", false, true)) {
            // REFACTOR-NOTE: This should set a flag that the main loop checks to exit gracefully
            // For now, direct exit for simplicity, but a flag is better for cleanup.
            SDL_Event quit_event;
            quit_event.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit_event);
        }
        igEndMenu();
    }

    if (ui_openSaveStateModal) {
        igOpenPopup_Str("Save State", 0);
        if (igBeginPopupModal("Save State", &ui_openSaveStateModal, ImGuiWindowFlags_AlwaysAutoResize)) {
            igText("Select Slot (0-9):");
            igSameLine(0,0); igSetNextItemWidth(100);
            igInputInt("##SaveSlot", &ui_selectedSaveLoadSlot, 1, 1, 0);
            ui_selectedSaveLoadSlot = ui_selectedSaveLoadSlot < 0 ? 0 : (ui_selectedSaveLoadSlot > 9 ? 9 : ui_selectedSaveLoadSlot);

            // REFACTOR-NOTE: Add confirmation for overwrite if slot is not empty
            if (igButton("Save", (ImVec2){80,0})) {
                UI_Log("Placeholder: Save state to slot %d", ui_selectedSaveLoadSlot);
                // NES_SaveState(nes, ui_selectedSaveLoadSlot); // Actual call
                ui_openSaveStateModal = false;
                igCloseCurrentPopup();
            }
            igSameLine(0, 8);
            if (igButton("Cancel", (ImVec2){80,0})) {
                ui_openSaveStateModal = false;
                igCloseCurrentPopup();
            }
            igEndPopup();
        }
    }
    if (ui_openLoadStateModal) {
        igOpenPopup_Str("Load State", 0);
        if (igBeginPopupModal("Load State", &ui_openLoadStateModal, ImGuiWindowFlags_AlwaysAutoResize)) {
            igText("Select Slot (0-9):");
            igSameLine(0,0); igSetNextItemWidth(100);
            igInputInt("##LoadSlot", &ui_selectedSaveLoadSlot, 1, 1, 0);
            ui_selectedSaveLoadSlot = ui_selectedSaveLoadSlot < 0 ? 0 : (ui_selectedSaveLoadSlot > 9 ? 9 : ui_selectedSaveLoadSlot);

            // REFACTOR-NOTE: Show if slot has data, maybe a timestamp or small screenshot
            if (igButton("Load", (ImVec2){80,0})) {
                UI_Log("Placeholder: Load state from slot %d", ui_selectedSaveLoadSlot);
                // NES_LoadState(nes, ui_selectedSaveLoadSlot); // Actual call
                ui_openLoadStateModal = false;
                igCloseCurrentPopup();
            }
            igSameLine(0, 8);
            if (igButton("Cancel", (ImVec2){80,0})) {
                ui_openLoadStateModal = false;
                igCloseCurrentPopup();
            }
            igEndPopup();
        }
    }
}

void UI_LogWindow() {
    if (!ui_showLog) return;
    // REFACTOR-NOTE: Consider ImGuiWindowFlags_HorizontalScrollbar if lines can be very long
    if (igBegin("Log", &ui_showLog, ImGuiWindowFlags_None)) {
        // REFACTOR-NOTE: Add filter/search, copy to clipboard options
        if (igButton("Clear", (ImVec2){0,0})) {
            ui_logLen = 0;
            ui_logBuffer[0] = '\0';
        }
        igSameLine(0, 10);
        //igCheckbox("Auto-scroll", &ioptr->Log); // Example ImGui built-in auto-scroll

        igSeparator();
        igBeginChild_Str("LogScrollingRegion", (ImVec2){0,0}, false, ImGuiWindowFlags_HorizontalScrollbar);
        igTextUnformatted(ui_logBuffer, ui_logBuffer + ui_logLen);
        //if (ioptr->OptLogAutoScrollToBottom && igGetScrollY() >= igGetScrollMaxY()) {
        //     igSetScrollHereY(1.0f);
        //}
        igEndChild();
    }
    igEnd();
}

static void UI_RenderPatternTable(NES* nes, int table_idx, GLuint* texture_id, uint32_t* pixel_buffer, const uint32_t* palette_to_use) {
    if (!nes || !nes->ppu) return;

    if (*texture_id == 0) {
        glGenTextures(1, texture_id);
        glBindTexture(GL_TEXTURE_2D, *texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindTexture(GL_TEXTURE_2D, 0); // Unbind
    }

    // REFACTOR-NOTE: This temporary buffer for pt_data might be inefficient if called very frequently.
    // Consider if PPU_GetPatternTableData can write directly or if a persistent buffer is better.
    uint8_t pt_data[256 * 128];//[128 * 128 / 8 * 2]; // Each tile is 16 bytes, 16x16 tiles.
    PPU_GetPatternTableData(nes->ppu, table_idx, pt_data); // Assuming this fills pt_data correctly

    for (int tile_y = 0; tile_y < 16; ++tile_y) {
        for (int tile_x = 0; tile_x < 16; ++tile_x) {
            int tile_index_in_pt_data = (tile_y * 16 + tile_x); // Index of the tile
            for (int y = 0; y < 8; ++y) {
                // Each tile is 16 bytes in pt_data: 8 for plane 0, 8 for plane 1
                uint8_t plane0 = pt_data[tile_index_in_pt_data * 16 + y];
                uint8_t plane1 = pt_data[tile_index_in_pt_data * 16 + y + 8];
                for (int x = 0; x < 8; ++x) {
                    int bit = 7 - x;
                    uint8_t color_idx = ((plane1 >> bit) & 1) << 1 | ((plane0 >> bit) & 1);
                    uint32_t pixel_color = palette_to_use[color_idx]; // Already in RGBA for GL
                    pixel_buffer[(tile_y * 8 + y) * 128 + (tile_x * 8 + x)] = pixel_color;
                }
            }
        }
    }
    glBindTexture(GL_TEXTURE_2D, *texture_id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, pixel_buffer);
    glBindTexture(GL_TEXTURE_2D, 0); // Unbind
}


void UI_PPUViewer(NES* nes) {
    if (!ui_showPpuViewer || !nes || !nes->ppu) {
        // If window was open but NES becomes null (e.g. ROM unload failed badly), hide it.
        if (ui_showPpuViewer && (!nes || !nes->ppu)) ui_showPpuViewer = false;
        return;
    }

    // REFACTOR-NOTE: Consider ImGuiWindowFlags_AlwaysAutoResize or set initial size
    if (igBegin("PPU Viewer", &ui_showPpuViewer, ImGuiWindowFlags_None)) {
        static GLuint pt_texture0 = 0, pt_texture1 = 0;
        static uint32_t pt_pixel_buffer[128 * 128];

        const char* palette_names[] = {
            "BG Pal 0", "BG Pal 1", "BG Pal 2", "BG Pal 3",
            "Sprite Pal 0", "Sprite Pal 1", "Sprite Pal 2", "Sprite Pal 3"
        };
        igSetNextItemWidth(150);
        igCombo_Str_arr("PT Palette", &ui_ppuViewerSelectedPalette, palette_names, 8, 4);
        // REFACTOR-NOTE: Add interactivity to pattern tables (e.g., click tile to see info)

        static uint32_t viewer_palette_rgba[4]; // RGBA for GL
        const uint32_t* master_nes_palette_abgr = PPU_GetPalette(); // Master palette (typically 0xAABBGGRR or similar)

        for(int i=0; i<4; ++i) {
            uint8_t palette_ram_base_idx = ui_ppuViewerSelectedPalette * 4;
            // PPU palette RAM: $3F00-$3F0F for BG, $3F10-$3F1F for Sprites
            // Universal BG color at $3F00 (mirrored to $3F04, $3F08, $3F0C for BG palettes, and $3F10, $3F14, $3F18, $3F1C for sprite palettes)
            uint8_t palette_ram_address_offset = (palette_ram_base_idx + i);
            if (i == 0) { // Color 0 of any palette is universal background or transparent for sprites
                palette_ram_address_offset = (ui_ppuViewerSelectedPalette < 4) ? 0x00 : 0x10; // BG palettes use $3F00, Sprite palettes use $3F10 (which is also a mirror of $3F00 if not sprite transparent)
                 // Actually, for display simplicity, let's always use the first color of the specific sub-palette index $3F00,$3F01.. or $3F10,$3F11...
                 // Color 0 of any selected sub-palette always comes from PPU palette RAM index 0 ($3F00) if it's a BG palette,
                 // or PPU palette RAM index 0 ($3F00) if it's a sprite palette and color 0 is used (usually for transparency).
                 // More accurately, viewer_palette[0] should be nes->ppu->palette[0] (master BG color),
                 // and viewer_palette[1,2,3] from nes->ppu->palette[palette_ram_base_idx + 1,2,3].
                 // Let's simplify:
                 // BG palettes: $3F00 (shared), $3F01, $3F02, $3F03 for Pal0; $3F00, $3F05, $3F06, $3F07 for Pal1 etc.
                 // Sprite palettes: $3F00 (shared for transparency), $3F11, $3F12, $3F13 for Pal0; $3F00, $3F15, $3F16, $3F17 for Pal1 etc.
                uint8_t actual_palette_index;
                if (i == 0) { // Universal background color / Sprite transparent color
                    actual_palette_index = nes->ppu->palette[0x00];
                } else {
                    actual_palette_index = nes->ppu->palette[palette_ram_address_offset % 32];
                }
                 uint32_t nes_col_abgr = master_nes_palette_abgr[actual_palette_index % 64];
                 // Convert ABGR (common NES emu format) to RGBA for GL
                 viewer_palette_rgba[i] = ((nes_col_abgr & 0x000000FF) << 24) | // R
                                        ((nes_col_abgr & 0x0000FF00) << 8)  | // G
                                        ((nes_col_abgr & 0x00FF0000) >> 8)  | // B
                                        0xFF000000;                          // A (force opaque for viewer)
            } else { // Colors 1, 2, 3 of the sub-palette
                uint8_t actual_palette_index = nes->ppu->palette[palette_ram_address_offset % 32]; // Use selected palette index
                uint32_t nes_col_abgr = master_nes_palette_abgr[actual_palette_index % 64];
                viewer_palette_rgba[i] = ((nes_col_abgr & 0x000000FF) << 24) | // R
                                       ((nes_col_abgr & 0x0000FF00) << 8)  | // G
                                       ((nes_col_abgr & 0x00FF0000) >> 8)  | // B
                                       0xFF000000;                         // A
            }
        }


        igText("Pattern Table 0 ($0000):");
        UI_RenderPatternTable(nes, 0, &pt_texture0, pt_pixel_buffer, viewer_palette_rgba);
        igImage((ImTextureID)(intptr_t)pt_texture0, (ImVec2){128 * 2.0f, 128 * 2.0f}, (ImVec2){0,0}, (ImVec2){1,1});

        igSameLine(0, 20);
        igBeginGroup();
        igText("Pattern Table 1 ($1000):");
        UI_RenderPatternTable(nes, 1, &pt_texture1, pt_pixel_buffer, viewer_palette_rgba);
        igImage((ImTextureID)(intptr_t)pt_texture1, (ImVec2){128 * 2.0f, 128 * 2.0f}, (ImVec2){0,0}, (ImVec2){1,1});
        igEndGroup();
        
        igSeparator();
        if (igCollapsingHeader_TreeNodeFlags("PPU Registers", ImGuiTreeNodeFlags_DefaultOpen)) {
            igText("CTRL (0x2000): 0x%02X", nes->ppu->ctrl);
            igText("MASK (0x2001): 0x%02X", nes->ppu->mask);
            igText("STATUS (0x2002): 0x%02X", nes->ppu->status);
            igText("OAMADDR (0x2003): 0x%02X", nes->ppu->oam_addr);
            igText("VRAM Addr (v): 0x%04X, Temp Addr (t): 0x%04X", nes->ppu->vram_addr, nes->ppu->temp_addr);
            igText("Fine X: %d, Write Latch (w): %d", nes->ppu->fine_x, nes->ppu->addr_latch); // addr_latch is 'w'
            igText("Scanline: %d, Cycle: %d", nes->ppu->scanline, nes->ppu->cycle);
            igText("Frame Odd: %s", nes->ppu->frame_odd ? "true" : "false");
            igText("NMI Occurred: %s, NMI Output: %s", nes->ppu->nmi_occured ? "true" : "false", nes->ppu->nmi_output ? "true" : "false");

        }

        if (igCollapsingHeader_TreeNodeFlags("Palettes", ImGuiTreeNodeFlags_None)) {
            igText("Current PPU Palette RAM ($3F00 - $3F1F):");
            for (int i = 0; i < 32; ++i) {
                uint8_t palette_idx_val = nes->ppu->palette[i]; // Value from PPU palette RAM (index into master NES palette)
                uint32_t nes_col_abgr = master_nes_palette_abgr[palette_idx_val % 64];
                ImVec4 im_col;
                im_col.x = ((nes_col_abgr & 0x000000FF)) / 255.0f;        // R
                im_col.y = ((nes_col_abgr & 0x0000FF00) >> 8) / 255.0f;   // G
                im_col.z = ((nes_col_abgr & 0x00FF0000) >> 16) / 255.0f;  // B
                im_col.w = 1.0f; // A (fully opaque for display)
                
                if (i > 0 && i % 16 == 0) igNewLine(); // Group BG and Sprite palettes
                if (i > 0 && i % 4 == 0 && i % 16 != 0) igSameLine(0,8); // Space between sub-palettes

                char pal_label[16];
                snprintf(pal_label, sizeof(pal_label), "$3F%02X", i);
                igColorButton(pal_label, im_col, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, (ImVec2){20, 20});
                if (igIsItemHovered(0)) {
                    igBeginTooltip();
                    igText("$3F%02X: Master Index 0x%02X", i, palette_idx_val);
                    igColorButton("##tooltipcol", im_col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel, (ImVec2){64,64});
                    igEndTooltip();
                }
                if ((i+1) % 4 != 0) igSameLine(0,2);
            }
        }


        if (igCollapsingHeader_TreeNodeFlags("OAM (Sprites)", ImGuiTreeNodeFlags_None)) {
            const uint8_t* oam_data = PPU_GetOAM(nes->ppu);
            if (oam_data) {
                 // REFACTOR-NOTE: Display more OAM entries, or a way to scroll through them
                if (igBeginTable("OAMTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit, (ImVec2){0,0}, 0)) {
                    igTableSetupColumn("Sprite #", 0, 0, 0);
                    igTableSetupColumn("Y", 0, 0, 0);
                    igTableSetupColumn("Tile ID", 0, 0, 0);
                    igTableSetupColumn("Attr", 0, 0, 0);
                    igTableSetupColumn("X", 0, 0, 0);
                    igTableHeadersRow();
                    for (int i = 0; i < 8; ++i) { // Display first 8 sprites, more can be added
                        igTableNextRow(0,0);
                        igTableSetColumnIndex(0); igText("%d", i);
                        igTableSetColumnIndex(1); igText("0x%02X (%d)", oam_data[i * 4 + 0], oam_data[i * 4 + 0]);
                        igTableSetColumnIndex(2); igText("0x%02X", oam_data[i * 4 + 1]);
                        igTableSetColumnIndex(3); igText("0x%02X", oam_data[i * 4 + 2]); // REFACTOR-NOTE: Decode attributes (palette, priority, flip)
                        igTableSetColumnIndex(4); igText("0x%02X (%d)", oam_data[i * 4 + 3], oam_data[i * 4 + 3]);
                    }
                    igEndTable();
                }
            }
        }
        // REFACTOR-NOTE: Add Nametable viewer section here
    }
    igEnd();
}


static uint16_t ui_memoryViewerAddress = 0x0000;
static uint8_t ui_memorySnapshot[0x10000]; // REFACTOR-NOTE: This snapshot is for the full 64KB CPU address space.
                                           // For PPU memory, a separate viewer/snapshot might be needed if BUS_Peek doesn't cover it.
static int ui_memoryViewerRows = 16; // Default rows to show

void UI_MemoryViewer(NES* nes) {
    if (!ui_showMemoryViewer || !nes) {
        if (ui_showMemoryViewer && !nes) ui_showMemoryViewer = false;
        return;
    }

    if (igBegin("Memory Viewer (CPU Bus)", &ui_showMemoryViewer, ImGuiWindowFlags_None)) {
        igSetNextItemWidth(100);
        igInputScalar("Base Addr", ImGuiDataType_U16, &ui_memoryViewerAddress, NULL, NULL, "%04X", ImGuiInputTextFlags_CharsHexadecimal);
        igSameLine(0,10);
        igSetNextItemWidth(100);
        igSliderInt("Rows", &ui_memoryViewerRows, 1, 64, "%d", 0); // Display 1 to 64 rows (16 bytes each)

        // REFACTOR-NOTE: Add options for different memory views (e.g., PPU VRAM via PPU debug functions)
        // REFACTOR-NOTE: Add search, goto, data interpretation features

        igBeginChild_Str("MemoryViewerScrollRegion", (ImVec2){0, (float)igGetTextLineHeightWithSpacing() * (ui_memoryViewerRows + 2.5f)}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);

        if (igBeginTable("MemoryTable", 17, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit, (ImVec2){0, 0}, 0)) {
            igTableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, igGetFontSize() * 4.5f, 0); // Auto-adjust based on font size
            for (int i = 0; i < 16; i++) {
                char colName[4];
                snprintf(colName, sizeof(colName), "%X", i);
                igTableSetupColumn(colName, ImGuiTableColumnFlags_WidthFixed, igGetFontSize() * 2.5f, 0);
            }
            igTableHeadersRow();

            for (int row = 0; row < ui_memoryViewerRows; row++) {
                uint16_t base_addr_for_row = ui_memoryViewerAddress + (row * 16);
                // Prevent wrap-around read for display (though BUS_Peek should handle mirroring/mapping)
                if (ui_memoryViewerAddress > 0xFFFF - (row*16)) break;


                igTableNextRow(0, 0);
                igTableSetColumnIndex(0);
                igText("%04X", base_addr_for_row);

                for (int col = 0; col < 16; col++) {
                    uint16_t currentAddr = base_addr_for_row + col;
                    if (currentAddr > 0xFFFF) { // Should not happen with above check but safety
                        igTableSetColumnIndex(col + 1);
                        igText("--");
                        continue;
                    }
                    uint8_t value = BUS_Peek(nes, currentAddr); // Assuming BUS_Peek is safe and handles all mappings
                    uint8_t prevValue = ui_memorySnapshot[currentAddr]; // Snapshot is for CPU view

                    if (nes->cpu && !ui_paused) { // Only update snapshot if running, to see changes
                        ui_memorySnapshot[currentAddr] = value;
                    }

                    igTableSetColumnIndex(col + 1);
                    if (value != prevValue && ui_paused) { // Highlight changes only when paused to be meaningful
                        igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){1.0f, 0.2f, 0.2f, 1.0f});
                        igText("%02X", value);
                        igPopStyleColor(1);
                    } else {
                        igText("%02X", value);
                    }
                }
            }
            igEndTable();
        }
        igEndChild();
    }
    igEnd();
}

void UI_DrawDisassembler(NES* nes) {
    if (!ui_showDisassembler || !nes || !nes->cpu) {
        if (ui_showDisassembler && (!nes || !nes->cpu)) ui_showDisassembler = false;
        return;
    }
    if (igBegin("Disassembler", &ui_showDisassembler, ImGuiWindowFlags_None)) {
        // REFACTOR-NOTE: Add syntax highlighting, breakpoint setting, step controls here too.
        uint16_t pc_to_disassemble = nes->cpu->pc;
        igText("Current PC: 0x%04X", pc_to_disassemble);
        igSameLine(0, 20);
        if(igButton("Step Op", (ImVec2){0,0})) {
            if(ui_paused) NES_Step(nes); // Step one CPU instruction
            else UI_Log("Cannot step instruction while running.");
        }


        igBeginChild_Str("DisassemblyRegion", (ImVec2){0, igGetTextLineHeightWithSpacing() * 18}, ImGuiChildFlags_Borders, 0);
        if (igBeginTable("DisassembledView", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY , (ImVec2){0,0},0)) {
            igTableSetupColumn(" ", ImGuiTableColumnFlags_WidthFixed, igGetFontSize() * 1.5f,0); // For PC indicator
            igTableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, igGetFontSize() * 5.0f,0);
            igTableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch, 0, 0);
            //igTableHeadersRow(); // Optional headers

            char disasm_buf[128];
            uint16_t addr_iter = pc_to_disassemble;

            // Try to center PC in the view a bit by disassembling a few lines before
            // This is a simple approach, more robust would involve calculating actual instruction lengths beforehand.
            for(int i=0; i<8; ++i) {
                // This is tricky as we don't know instruction lengths backwards easily.
                // For now, let's just start a few fixed bytes before.
                // A proper solution needs a more complex lookup or pre-scan.
                // addr_iter -= ???; // This requires knowing previous instruction length.
                // For simplicity, we'll just list from PC onwards for now, or a fixed offset.
            }
            // Let's try to show PC and some instructions around it.
            // This is a placeholder for a more sophisticated disassembler view.
            // For now, just disassemble from current PC for a few lines.
            addr_iter = nes->cpu->pc; // Start disassembly from current PC

            for (int i = 0; i < 16; i++) { // Show 16 instructions
                igTableNextRow(0,0);
                
                igTableSetColumnIndex(0);
                if (addr_iter == nes->cpu->pc) {
                    igText(">"); // PC indicator
                } else {
                    igText(" ");
                }

                igTableSetColumnIndex(1);
                igText("0x%04X", addr_iter);

                igTableSetColumnIndex(2);
                // The disassemble function needs to not rely on internal NES state for 'next' if we are just viewing
                // Assuming disassemble function updates addr_iter to the next instruction's address
                // and returns the length of the current instruction or similar.
                // For this example, let's assume it returns the address of the *next* instruction.
                uint16_t prev_addr_iter = addr_iter;
                addr_iter = disassemble(nes, addr_iter, disasm_buf, sizeof(disasm_buf));
                igTextUnformatted(disasm_buf, NULL);

                if (addr_iter <= prev_addr_iter) { // Stuck or bad disassembly? Prevent infinite loop.
                    igTableNextRow(0,0);
                    igTableSetColumnIndex(1); igText("---");
                    igTableSetColumnIndex(2); igText("Disassembly error or end.");
                    break;
                }
            }
            igEndTable();
        }
        igEndChild();
    }
    igEnd();
}


void UI_ToggleFullscreen() {
    ui_sdl_fullscreen = !ui_sdl_fullscreen;
    SDL_SetWindowFullscreen(window, ui_sdl_fullscreen ? true : false);
    UI_Log("Toggled fullscreen to: %s", ui_sdl_fullscreen ? "ON" : "OFF");
}

void UI_ApplyTheme(UI_Theme theme) {
    current_ui_theme = theme;
    ImGuiStyle* style = igGetStyle();
    ImVec4* colors = style->Colors;

    if (theme == UI_THEME_LIGHT) {
        igStyleColorsLight(NULL);
        colors[ImGuiCol_WindowBg] = (ImVec4){0.94f, 0.94f, 0.94f, 1.00f};
        colors[ImGuiCol_FrameBg] = (ImVec4){1.00f, 1.00f, 1.00f, 1.00f};
        colors[ImGuiCol_TitleBgActive] = (ImVec4){0.82f, 0.82f, 0.82f, 1.00f};
        colors[ImGuiCol_MenuBarBg] = (ImVec4){0.86f, 0.86f, 0.86f, 1.00f};
        colors[ImGuiCol_Header] = (ImVec4){0.90f, 0.90f, 0.90f, 1.00f};
        colors[ImGuiCol_Button] = (ImVec4){0.80f, 0.80f, 0.80f, 1.00f};
        colors[ImGuiCol_ButtonHovered] = (ImVec4){0.70f, 0.70f, 0.70f, 1.00f};
        colors[ImGuiCol_ButtonActive] = (ImVec4){0.60f, 0.60f, 0.60f, 1.00f};
        // REFACTOR-NOTE: Add more color definitions for light theme for completeness
    } else { // UI_THEME_DARK (using your original dark theme)
        igStyleColorsDark(NULL);
        colors[ImGuiCol_Text] = (ImVec4){1.00f, 1.00f, 1.00f, 1.00f};
        colors[ImGuiCol_TextDisabled] = (ImVec4){0.50f, 0.50f, 0.50f, 1.00f}; // Made less dim
        colors[ImGuiCol_WindowBg] = (ImVec4){0.10f, 0.10f, 0.10f, 1.00f}; // Slightly darker
        colors[ImGuiCol_ChildBg] = (ImVec4){0.12f, 0.12f, 0.12f, 1.00f};
        colors[ImGuiCol_PopupBg] = (ImVec4){0.08f, 0.08f, 0.08f, 0.94f};
        colors[ImGuiCol_Border] = (ImVec4){0.30f, 0.30f, 0.33f, 0.50f};
        colors[ImGuiCol_BorderShadow] = (ImVec4){0.00f, 0.00f, 0.00f, 0.00f};
        colors[ImGuiCol_FrameBg] = (ImVec4){0.20f, 0.20f, 0.22f, 0.54f};
        colors[ImGuiCol_FrameBgHovered] = (ImVec4){0.25f, 0.25f, 0.28f, 0.78f};
        colors[ImGuiCol_FrameBgActive] = (ImVec4){0.30f, 0.30f, 0.33f, 1.00f};
        colors[ImGuiCol_TitleBg] = (ImVec4){0.08f, 0.08f, 0.08f, 1.00f};
        colors[ImGuiCol_TitleBgActive] = (ImVec4){0.15f, 0.15f, 0.17f, 1.00f};
        colors[ImGuiCol_TitleBgCollapsed] = (ImVec4){0.08f, 0.08f, 0.08f, 0.75f};
        colors[ImGuiCol_MenuBarBg] = (ImVec4){0.14f, 0.14f, 0.16f, 1.00f};
        colors[ImGuiCol_ScrollbarBg] = (ImVec4){0.02f, 0.02f, 0.02f, 0.53f};
        colors[ImGuiCol_ScrollbarGrab] = (ImVec4){0.31f, 0.31f, 0.31f, 1.00f};
        colors[ImGuiCol_ScrollbarGrabHovered] = (ImVec4){0.41f, 0.41f, 0.41f, 1.00f};
        colors[ImGuiCol_ScrollbarGrabActive] = (ImVec4){0.51f, 0.51f, 0.51f, 1.00f};
        colors[ImGuiCol_CheckMark] = (ImVec4){0.60f, 0.81f, 0.26f, 1.00f}; // Brighter checkmark
        colors[ImGuiCol_SliderGrab] = (ImVec4){0.50f, 0.71f, 0.24f, 1.00f};
        colors[ImGuiCol_SliderGrabActive] = (ImVec4){0.60f, 0.81f, 0.26f, 1.00f};
        colors[ImGuiCol_Button] = (ImVec4){0.22f, 0.40f, 0.15f, 0.60f}; // Greenish hue
        colors[ImGuiCol_ButtonHovered] = (ImVec4){0.28f, 0.50f, 0.20f, 1.00f};
        colors[ImGuiCol_ButtonActive] = (ImVec4){0.32f, 0.58f, 0.22f, 1.00f};
        colors[ImGuiCol_Header] = (ImVec4){0.20f, 0.38f, 0.14f, 0.58f}; // Header for collapsing sections
        colors[ImGuiCol_HeaderHovered] = (ImVec4){0.26f, 0.48f, 0.18f, 0.80f};
        colors[ImGuiCol_HeaderActive] = (ImVec4){0.30f, 0.55f, 0.20f, 1.00f};
        colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
        colors[ImGuiCol_SeparatorHovered] = (ImVec4){0.40f, 0.60f, 0.20f, 0.78f};
        colors[ImGuiCol_SeparatorActive] = (ImVec4){0.50f, 0.70f, 0.25f, 1.00f};
        colors[ImGuiCol_ResizeGrip] = (ImVec4){0.44f, 0.70f, 0.20f, 0.30f}; // More subtle resize grip
        colors[ImGuiCol_ResizeGripHovered] = (ImVec4){0.50f, 0.80f, 0.26f, 0.67f};
        colors[ImGuiCol_ResizeGripActive] = (ImVec4){0.58f, 0.90f, 0.30f, 0.95f};
        colors[ImGuiCol_Tab] = colors[ImGuiCol_Header];
        colors[ImGuiCol_TabHovered] = colors[ImGuiCol_HeaderHovered];
        //colors[ImGuiCol_TabActive] = colors[ImGuiCol_HeaderActive];
        //colors[ImGuiCol_TabUnfocused] = igColorConvertFloat4ToU32(colors[ImGuiCol_Tab]); // ImGui::GetColorU32(ImGuiCol_Tab);
        //colors[ImGuiCol_TabUnfocusedActive] = igColorConvertFloat4ToU32(colors[ImGuiCol_TabActive]); //ImGui::GetColorU32(ImGuiCol_TabActive);
        colors[ImGuiCol_DockingPreview] = colors[ImGuiCol_HeaderActive]; // (ImVec4){0.26f, 0.59f, 0.98f, 0.70f};
        colors[ImGuiCol_DockingEmptyBg] = (ImVec4){0.20f, 0.20f, 0.20f, 1.00f};
        colors[ImGuiCol_PlotLines] = (ImVec4){0.61f, 0.61f, 0.61f, 1.00f};
        colors[ImGuiCol_PlotLinesHovered] = (ImVec4){1.00f, 0.43f, 0.35f, 1.00f};
        colors[ImGuiCol_PlotHistogram] = (ImVec4){0.90f, 0.70f, 0.00f, 1.00f};
        colors[ImGuiCol_PlotHistogramHovered] = (ImVec4){1.00f, 0.60f, 0.00f, 1.00f};
        colors[ImGuiCol_TextSelectedBg] = (ImVec4){0.26f, 0.59f, 0.98f, 0.35f};
        colors[ImGuiCol_DragDropTarget] = (ImVec4){1.00f, 1.00f, 0.00f, 0.90f};
        //colors[ImGuiCol_NavHighlight] = colors[ImGuiCol_HeaderHovered]; // (ImVec4){0.26f, 0.59f, 0.98f, 1.00f};
        colors[ImGuiCol_NavWindowingHighlight] = (ImVec4){1.00f, 1.00f, 1.00f, 0.70f};
        colors[ImGuiCol_NavWindowingDimBg] = (ImVec4){0.80f, 0.80f, 0.80f, 0.20f};
        colors[ImGuiCol_ModalWindowDimBg] = (ImVec4){0.20f, 0.20f, 0.20f, 0.60f}; // Darker modal dim
    }
    // REFACTOR-NOTE: Common style settings for both themes
    style->WindowRounding = 4.0f; // Rounded corners for windows
    style->FrameRounding = 4.0f;  // Rounded corners for frames (buttons, inputs)
    style->GrabRounding = 4.0f;   // Rounded corners for grab handles (sliders)
    style->PopupRounding = 4.0f;
    style->ScrollbarRounding = 4.0f;
    style->TabRounding = 4.0f;
    style->WindowPadding = (ImVec2){8.0f, 8.0f};
    style->FramePadding = (ImVec2){5.0f, 3.0f};
    style->ItemSpacing = (ImVec2){8.0f, 4.0f};
    style->ItemInnerSpacing = (ImVec2){4.0f, 4.0f};
    style->IndentSpacing = 20.0f;
    style->ScrollbarSize = 15.0f;
    style->GrabMinSize = 12.0f;

    // REFACTOR-NOTE: Consider loading a custom font here.
    // ImFontConfig font_config;
    // font_config.FontDataOwnedByAtlas = false; // if using embedded font
    // ioptr->Fonts->AddFontFromFileTTF("path/to/your/font.ttf", 16.0f, &font_config, ioptr->Fonts->GetGlyphRangesDefault());
    // ioptr->Fonts->Build();
}


void UI_InitStlye() { // Legacy name, kept for compatibility if called elsewhere
    UI_ApplyTheme(current_ui_theme);
}

void UI_Init() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        DEBUG_FATAL("Could not initialize SDL: %s", SDL_GetError()); // Use FATAL for critical init errors
        // Consider throwing an exception or returning an error code instead of direct exit
        // For a UI app, showing a message box before exit is good.
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SDL Initialization Error", SDL_GetError(), NULL);
        exit(1);
    }

    // REFACTOR-NOTE: Query display mode for better default window size or fullscreen
    window = SDL_CreateWindow("cNES Emulator", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);
    if (!window) {
        DEBUG_FATAL("Could not create window: %s", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Window Creation Error", SDL_GetError(), NULL);
        SDL_Quit();
        exit(1);
    }

    glContext = SDL_GL_CreateContext(window);
    if (glContext == NULL) {
        DEBUG_FATAL("Could not create OpenGL context: %s", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenGL Context Error", SDL_GetError(), window);
        SDL_DestroyWindow(window);
        SDL_Quit();
        exit(1);
    }
    SDL_GL_MakeCurrent(window, glContext); // Ensure context is current for GLEW
    SDL_GL_SetSwapInterval(1); // Enable vsync

    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) { // Removed GLEW_ERROR_NO_GLX_DISPLAY check as it's usually not fatal if GL context exists
        DEBUG_FATAL("Could not initialize GLEW: %s", glewGetErrorString(err));
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "GLEW Initialization Error", (const char*)glewGetErrorString(err), window);
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        exit(1);
    }

    ImGuiContext *ctx = igCreateContext(NULL);
    ioptr = igGetIO_ContextPtr(ctx); // Get default context's IO
    ioptr->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ioptr->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // If you plan to support gamepads for UI nav
    ioptr->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ioptr->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // For multi-viewport support

    // REFACTOR-NOTE: Tweak style for viewports if default is not desired
    ImGuiStyle* style = igGetStyle();
    if (ioptr->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style->WindowRounding = 0.0f; // Sharp corners for main window when viewports are on
        style->Colors[ImGuiCol_WindowBg].w = 1.0f; // Make main window bg opaque
    }

    ImGui_ImplSDL3_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 130"); // Or your desired GLSL version

    UI_ApplyTheme(UI_THEME_DARK);
    strncpy(ui_currentRomName, "No ROM Loaded", sizeof(ui_currentRomName) -1);
    ui_currentRomName[sizeof(ui_currentRomName)-1] = '\0';

    //ioptr->OptLogAutoScrollToBottom = true; // Default log auto-scroll to true
    UI_Log("cEMU Initialized. Welcome!");
}

void UI_SettingsWindow(NES* nes) {
    if (!ui_showSettingsWindow) return;

    igSetNextWindowSize((ImVec2){480, 400}, ImGuiCond_FirstUseEver);
    if (igBegin("Settings", &ui_showSettingsWindow, ImGuiWindowFlags_None)) {
        if (igBeginTabBar("SettingsTabs", 0)) {
            if (igBeginTabItem("Display", NULL, 0)) {
                igText("Theme:"); igSameLine(0,0);
                if (igRadioButton_Bool("Dark", current_ui_theme == UI_THEME_DARK)) UI_ApplyTheme(UI_THEME_DARK);
                igSameLine(0,0);
                if (igRadioButton_Bool("Light", current_ui_theme == UI_THEME_LIGHT)) UI_ApplyTheme(UI_THEME_LIGHT);
                igSeparator();
                if (igCheckbox("Fullscreen (Window)", &ui_sdl_fullscreen)) UI_ToggleFullscreen();
                igSameLine(0, 10); igTextDisabled("(F11)");
                // REFACTOR-NOTE: Add font scaling options, game screen scaling options here
                igEndTabItem();
            }
            if (igBeginTabItem("Audio", NULL, 0)) {
                // REFACTOR-NOTE: Connect this to actual APU volume control
                if (igSliderFloat("Master Volume", &ui_master_volume, 0.0f, 1.0f, "%.2f", 0)) {
                    // Example: APU_SetMasterVolume(nes->apu, ui_master_volume);
                    UI_Log("Master volume (placeholder) set to: %.2f", ui_master_volume);
                }
                igTextDisabled("Audio settings are placeholders.");
                // REFACTOR-NOTE: Add options for audio buffer size, sample rate (if configurable), APU channel toggles
                igEndTabItem();
            }
            if (igBeginTabItem("Input", NULL, 0)) {
                igText("Controller 1 Mapping (NES):");
                // REFACTOR-NOTE: Implement a visual key mapping UI here.
                // For now, just list current hardcoded keys.
                igText("Up: W, Down: S, Left: A, Right: D");
                igText("A: K, B: J, Select: RShift, Start: Enter");
                igSeparator();
                igText("Controller 2 Mapping:");
                igTextDisabled("TODO: Add key mapping UI for Controller 2.");
                igEndTabItem();
            }
            // REFACTOR-NOTE: Add "Paths" tab for save states, screenshots, ROMs directory.
            // REFACTOR-NOTE: Add "Advanced" tab for emulation tweaks if any.
            igEndTabBar();
        }
        igSeparator();
        // REFACTOR-NOTE: This positioning for close button can be tricky with auto-resize.
        // Simpler: just let it flow, or use a dedicated footer child window.
        // ImVec2 window_size; igGetWindowSize(&window_size); ImVec2 button_size = {80, 0};
        // float offset_x = window_size.x - button_size.x - igGetStyle()->WindowPadding.x;
        // igSetCursorPosX(offset_x > 0 ? offset_x : igGetCursorPosX());
        if (igButton("Close", (ImVec2){-FLT_MIN,0})) ui_showSettingsWindow = false; // Full width
    }
    igEnd();
}

void UI_DrawAboutWindow() {
    if (!ui_showAboutWindow) return;
    igSetNextWindowSize((ImVec2){350, 220}, ImGuiCond_FirstUseEver);
    if (igBegin("About cEMU", &ui_showAboutWindow, ImGuiWindowFlags_AlwaysAutoResize)) {
        igText("cEMU - A NES Emulator"); // REFACTOR-NOTE: Update with your project name/details
        igText("Version: 0.1.alpha (SDL3 + cimgui)"); // REFACTOR-NOTE: Update version
        igText("Author: [Your Name/Handle Here]"); // REFACTOR-NOTE: Fill this in!
        igSeparator();
        igText("Powered by Dear ImGui (cimgui bindings) and SDL3.");
        igText("OpenGL rendering via GLEW.");
        igSeparator();
        // REFACTOR-NOTE: Add a link to your project's GitHub or website if available.
        // if (igButton("Project on GitHub", (ImVec2){-FLT_MIN,0})) { /* SDL_OpenURL("your_link_here"); */ }
        if (igButton("OK", (ImVec2){-FLT_MIN,0})) ui_showAboutWindow = false;
    }
    igEnd();
}


static void UI_ShowAllDebugWindows() {
    ui_showCpuWindow = true;
    ui_showPpuViewer = true;
    ui_showMemoryViewer = true;
    ui_showLog = true;
    ui_showDisassembler = true;
}
static void UI_HideAllDebugWindows() {
    ui_showCpuWindow = false;
    ui_showPpuViewer = false;
    ui_showMemoryViewer = false;
    // ui_showLog = false; // Keep log usually visible or let user decide
    ui_showDisassembler = false;
}

static void UI_DebugToolbar(NES* nes) {
    if (!ui_showToolbar) return;

    // REFACTOR-NOTE: This toolbar could be a dockable window itself or part of the main menu's viewport.
    // For simplicity, keeping it as a separate small window.
    // Using ImGuiWindowFlags_NoFocusOnAppearing to prevent it from stealing focus on startup
    // Consider using icons (e.g. FontAwesome) for a more compact toolbar.
    ImGuiViewport* viewport = igGetMainViewport();
    // Position it below the main menu bar if menu bar is part of the main viewport.
    // This is a bit manual; could be improved with a dedicated toolbar area in docking.
    float menuBarHeight = 0; // Estimate or get actual if possible
    // if (igFindWindowByName("##MainMenuBar")) menuBarHeight = igGetWindowSize(igFindWindowByName("##MainMenuBar")).y;
    
    // A simpler way: just make it a regular small window that can be docked or float.
    // igSetNextWindowPos((ImVec2){viewport->WorkPos.x, viewport->WorkPos.y + menuBarHeight}, ImGuiCond_Always, (ImVec2){0,0});
    // igSetNextWindowSize((ImVec2){viewport->WorkSize.x, 40}, ImGuiCond_Always);

    if (igBegin("Debug Controls", &ui_showToolbar, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking )) {
        // Position manually if not docking it:
        // igSetWindowPos_Vec2((ImVec2){viewport->Pos.x, viewport->Pos.y + 20}, ImGuiCond_Appearing); // Example position


        const char* pause_label = ui_paused ? "Resume (F6)" : "Pause (F6)"; // ICON_FA_PLAY / ICON_FA_PAUSE
        if (igButton(pause_label, (ImVec2){0,0})) UI_TogglePause(nes);
        igSameLine(0, 4);
        if (igButton("Step CPU (F7)", (ImVec2){0,0})) { if (ui_paused && nes && nes->cpu) NES_Step(nes); else UI_Log("Can only step CPU when paused."); }
        igSameLine(0, 4);
        if (igButton("Step Frame (F8)", (ImVec2){0,0})) { if (ui_paused && nes) UI_StepFrame(nes); else UI_Log("Can only step frame when paused.");}
        igSameLine(0, 4);
        if (igButton("Reset (F5)", (ImVec2){0,0})) UI_Reset(nes);
        igSameLine(0,15); // Separator space
        if (igButton("PPU", (ImVec2){0,0})) ui_showPpuViewer = !ui_showPpuViewer;
        igSameLine(0, 4);
        if (igButton("Memory", (ImVec2){0,0})) ui_showMemoryViewer = !ui_showMemoryViewer;
        igSameLine(0, 4);
        if (igButton("Log", (ImVec2){0,0})) ui_showLog = !ui_showLog;
        igSameLine(0, 4);
        if (igButton("CPU", (ImVec2){0,0})) ui_showCpuWindow = !ui_showCpuWindow;
        igSameLine(0, 4);
        if (igButton("Disasm", (ImVec2){0,0})) ui_showDisassembler = !ui_showDisassembler;
    }
    igEnd();
}

static void UI_DrawDebugMenu() {
    if (igBeginMenu("Debug", true)) {
        // REFACTOR-NOTE: Use checkmarks to show visibility
        igMenuItem_Bool("CPU Registers", NULL, &ui_showCpuWindow, true);
        igMenuItem_Bool("PPU Viewer", NULL, &ui_showPpuViewer, true);
        igMenuItem_Bool("Memory Viewer", NULL, &ui_showMemoryViewer, true);
        igMenuItem_Bool("Log Window", NULL, &ui_showLog, true);
        igMenuItem_Bool("Disassembler", NULL, &ui_showDisassembler, true);
        igSeparator();
        igMenuItem_Bool("Debug Toolbar", NULL, &ui_showToolbar, true);
        igSeparator();
        if (igMenuItem_Bool("Show All Debug Windows", NULL, false, true)) UI_ShowAllDebugWindows();
        if (igMenuItem_Bool("Hide All Debug Windows", NULL, false, true)) UI_HideAllDebugWindows();
        igEndMenu();
    }
}

static void UI_CpuWindow(NES* nes) {
    if (!ui_showCpuWindow || !nes || !nes->cpu) {
         if (ui_showCpuWindow && (!nes || !nes->cpu)) ui_showCpuWindow = false;
        return;
    }
    if (igBegin("CPU Registers", &ui_showCpuWindow, ImGuiWindowFlags_None)) { // REFACTOR-NOTE: Consider ImGuiWindowFlags_AlwaysAutoResize
        // igTextColored((ImVec4){1.0f, 0.0f, 0.0f}, "CPU Registers"); // ICON_FA_MICROCHIP
        igText("A:  0x%02X (%d)", nes->cpu->a, nes->cpu->a);
        igText("X:  0x%02X (%d)", nes->cpu->x, nes->cpu->x);
        igText("Y:  0x%02X (%d)", nes->cpu->y, nes->cpu->y);
        igText("SP: 0x01%02X", nes->cpu->sp); // Stack is on page 0x0100
        igText("PC: 0x%04X", nes->cpu->pc);
        
        igText("Status: 0x%02X [", nes->cpu->status);
        igSameLine(0,0);
        // N V - B D I Z C
        const char* flag_names = "NV-BDIZC";
        for (int i = 7; i >= 0; i--) { // Iterate from N (bit 7) down to C (bit 0)
            bool is_set = (nes->cpu->status >> i) & 1;
            // Bit 5 ('-') is always set in status register reads, but can be conceptually clear
            if (i == 5) is_set = true; // Unused flag, often shown as set.
            
            if (is_set) {
                igTextColored((ImVec4){0.2f, 1.0f, 0.2f, 1.0f}, "%c", flag_names[7-i]);
            } else {
                igTextColored((ImVec4){0.8f, 0.2f, 0.2f, 1.0f}, "%c", flag_names[7-i]);
            }
            if (i > 0) igSameLine(0,2);
        }
        igSameLine(0,0); igText("]");
        igNewLine();
        igText("Total Cycles: %llu", (unsigned long long)nes->cpu->total_cycles);
        // REFACTOR-NOTE: Add display for pending interrupts (NMI, IRQ)
        // REFACTOR-NOTE: Add instruction timing/cycle count for current/last instruction
    }
    igEnd();
}

void UI_DrawMainMenuBar(NES* nes) {
    if (igBeginMainMenuBar()) {
        UI_DrawFileMenu(nes);
        if (igBeginMenu("Emulation", true)) {
            bool rom_loaded = (nes != NULL && strlen(ui_currentRomName) > 0 && strcmp(ui_currentRomName, "No ROM Loaded") != 0 && strcmp(ui_currentRomName, "Failed to load ROM") != 0);
            if (igMenuItem_Bool(ui_paused ? "Resume" : "Pause", "F6", false, rom_loaded)) UI_TogglePause(nes);
            if (igMenuItem_Bool("Reset", "F5", false, rom_loaded)) UI_Reset(nes);
            if (igMenuItem_Bool("Step CPU Instruction", "F7", false, rom_loaded && ui_paused)) { if(nes && nes->cpu) NES_Step(nes); }
            if (igMenuItem_Bool("Step Frame", "F8", false, rom_loaded && ui_paused)) UI_StepFrame(nes);
            // REFACTOR-NOTE: Add speed controls (e.g., 50%, 100%, 200%, turbo)
            igEndMenu();
        }
        if (igBeginMenu("View", true)) {
            igMenuItem_Bool("Game Screen", NULL, NULL, true); // Game Screen always conceptually "visible" if part of dock
            igMenuItem_Bool("PPU Viewer", NULL, &ui_showPpuViewer, true);
            igMenuItem_Bool("Log Window", NULL, &ui_showLog, true);
            igMenuItem_Bool("Memory Viewer", NULL, &ui_showMemoryViewer, true);
            igMenuItem_Bool("Disassembler", NULL, &ui_showDisassembler, true);
            igMenuItem_Bool("CPU Registers", NULL, &ui_showCpuWindow, true);
            igMenuItem_Bool("Debug Toolbar", NULL, &ui_showToolbar, true);
            igSeparator();
            if (igMenuItem_Bool("Toggle Fullscreen", "F11", ui_sdl_fullscreen, true)) UI_ToggleFullscreen();
            igEndMenu();
        }
        UI_DrawDebugMenu(); // Keep as separate for more detailed debug toggles
        if (igBeginMenu("Options", true)) {
            if (igMenuItem_Bool("Settings...", "F10", ui_showSettingsWindow, true)) ui_showSettingsWindow = !ui_showSettingsWindow;
            igEndMenu();
        }
        if (igBeginMenu("Help", true)) {
            if (igMenuItem_Bool("About cEMU", NULL, false, true)) ui_showAboutWindow = true;
            // REFACTOR-NOTE: Add "View Controls" or "Help Topics" menu item
            igEndMenu();
        }
        igEndMainMenuBar();
    }
}

static GLuint ppu_game_texture = 0; // Moved to static global for game screen

void UI_GameScreenWindow(NES* nes) {
    // REFACTOR-NOTE: Add ImGuiWindowFlags_NoScrollbar and ImGuiWindowFlags_NoScrollWithMouse if you handle all scaling.
    // Remove ImGuiWindowFlags_NoCollapse if you want it collapsable.
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){0,0}); // No padding for the game screen window
    if (igBegin("Game Screen", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (!nes || !nes->ppu) {
             igTextColored((ImVec4){1.f,1.f,0.f,1.f}, "No NES/PPU context or ROM not loaded.");
        } else {
            if (ppu_game_texture == 0) {
                glGenTextures(1, &ppu_game_texture);
                glBindTexture(GL_TEXTURE_2D, ppu_game_texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // Use GL_LINEAR for smoother scaling if preferred
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                // Framebuffer is ARGB (0xAARRGGBB), GL_RGBA expects RGBA. Need conversion or ensure framebuffer is RGBA.
                // Assuming nes->ppu->framebuffer is already in a format GL_RGBA can use directly (like actual R,G,B,A byte order).
                // If framebuffer is, for example, 0xAARRGGBB (uint32_t), it needs conversion before glTexSubImage2D.
                // For now, assuming framebuffer is raw RGBA pixel data.
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 240, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL); // Use GL_UNSIGNED_BYTE if framebuffer is byte array
                // If framebuffer is uint32_t array of packed colors (e.g. 0xAABBGGRR or 0xRRGGBBAA), use GL_UNSIGNED_INT_8_8_8_8_REV or GL_UNSIGNED_INT_8_8_8_8
                // and ensure byte order matches what OpenGL expects for RGBA.
                // For this example, assuming nes->ppu->framebuffer points to an array of bytes: R, G, B, A, R, G, B, A, ...
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            glBindTexture(GL_TEXTURE_2D, ppu_game_texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 240, GL_RGBA, GL_UNSIGNED_BYTE, nes->ppu->framebuffer); // Or GL_UNSIGNED_INT_8_8_8_8 if uint32_t array
            glBindTexture(GL_TEXTURE_2D, 0);

            // REFACTOR-NOTE: Implement aspect ratio correction and scaling here.
            ImVec2 window_content_region_size;
            igGetContentRegionAvail(&window_content_region_size);

            float aspect_ratio = 256.0f / 240.0f;
            ImVec2 image_size = window_content_region_size;

            if (image_size.x / aspect_ratio > image_size.y) { // Limited by height
                image_size.x = image_size.y * aspect_ratio;
            } else { // Limited by width
                image_size.y = image_size.x / aspect_ratio;
            }
            
            // Center the image
            ImVec2 cursor_pos;
            igGetCursorPos(&cursor_pos);
            cursor_pos.x += (window_content_region_size.x - image_size.x) * 0.5f;
            cursor_pos.y += (window_content_region_size.y - image_size.y) * 0.5f;
            igSetCursorPos(cursor_pos);

            igImage((ImTextureID)(intptr_t)ppu_game_texture, image_size, (ImVec2){0, 0}, (ImVec2){1, 1});
        }
    }
    igEnd();
    igPopStyleVar(1); // WindowPadding
}

void UI_DrawStatusBar(NES* nes) {
    // Flags to make it behave like a status bar but still dockable
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoDecoration;
    float status_bar_height = igGetFrameHeightWithSpacing() + igGetStyle()->WindowPadding.y; // Small padding
    igSetNextWindowSizeConstraints((ImVec2){0, status_bar_height},(ImVec2){FLT_MAX, status_bar_height}, NULL, NULL);


    if (igBegin("Status Bar", NULL, flags)) 
    {
        Uint32 currentTime = SDL_GetTicks();
        //if (currentTime > lastFrameTimeForStatus + 500) {
        //    lastFrameTimeForStatus = currentTime;
        //}
        igText("FPS: %.1f | ROM: %s | %s", ui_fps, ui_currentRomName, ui_paused ? "Paused" : "Running");
        const char* version_text = "cEMU v0.2";
        ImVec2 version_text_size;
        igCalcTextSize(&version_text_size, version_text, NULL, false, 0);
        float version_text_width = version_text_size.x;
        ImVec2 maxRegion;
        igGetContentRegionAvail(&maxRegion);
        igSameLine(maxRegion.x - version_text_width - igGetStyle()->ItemSpacing.x, 0);
        igTextDisabled("%s", version_text);
    }
    igEnd();
}

// REFACTOR-NOTE: Consolidated main drawing logic.
void UI_Draw(NES* nes) {
    ImGuiViewport* viewport = igGetMainViewport();
    igSetNextWindowPos(viewport->WorkPos, ImGuiCond_Always, (ImVec2){0,0}); // Use WorkPos
    igSetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);    // Use WorkSize
    igSetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 0.0f);
    igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 0.0f);
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});
    
    // This is the main host window for the dockspace
    igBegin("cEMU_MainHost", NULL, host_flags);
    igPopStyleVar(3);

    ImGuiID dockspace_id = igGetID_Str("cEMU_DockSpace_Main");
    igDockSpace(dockspace_id, (ImVec2){0.0f, 0.0f}, ImGuiDockNodeFlags_PassthruCentralNode, NULL);

    // REFACTOR-NOTE: Apply default docking layout on the first frame
    if (ui_first_frame) {
        ui_first_frame = false;
        igDockBuilderRemoveNode(dockspace_id); // Clear out existing layout (if any)
        igDockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        igDockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

        ImGuiID dock_main_id = dockspace_id;

        ImGuiID dock_id_statusbar = igDockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.08f, &dock_id_statusbar, &dock_main_id);
        ImGuiID dock_right_id = igDockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, NULL, &dock_main_id);
        ImGuiID dock_left_id = igDockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.22f, NULL, &dock_main_id);
        ImGuiID dock_bottom_id = igDockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.28f, NULL, &dock_main_id);

 // 8% height for status bar
        //igDockBuilderRemoveNode(dock_id_statusbar);

        //igDockBuilderSetNodeSize(dock_id_statusbar, (ImVec2){viewport->WorkSize.x, viewport->WorkSize.y * 0.08f});
        // ImGuiID dock_bottom_right_id = igDockBuilderSplitNode(dock_bottom_id, ImGuiDir_Right, 0.40f, NULL, &dock_bottom_id);

        //igDockBuilderAddNode(dock_id_statusbar, );
        igDockBuilderSetNodeSize(dock_id_statusbar, (ImVec2){viewport->WorkSize.x, igGetFrameHeightWithSpacing() + igGetStyle()->WindowPadding.y});
        
        igDockBuilderDockWindow("Status Bar", dock_id_statusbar); // Dock the status bar

        igDockBuilderDockWindow("Game Screen", dock_main_id); // Central
        igDockBuilderDockWindow("CPU Registers", dock_left_id);
        igDockBuilderDockWindow("Disassembler", dock_left_id); // Tabbed with CPU
        
        igDockBuilderDockWindow("PPU Viewer", dock_right_id);
        igDockBuilderDockWindow("Memory Viewer (CPU Bus)", dock_right_id); // Tabbed with PPU
        
        igDockBuilderDockWindow("Log", dock_bottom_id);
        // igDockBuilderDockWindow("Other Tool", dock_bottom_right_id);

        ImGuiDockNode_SetLocalFlags(igDockBuilderGetNode(dock_id_statusbar), ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoResize);
        
        // Ensure all windows that should be part of default layout are visible initially
        ui_showGameScreen = true; // Assuming a bool for this, or just let it dock
        ui_showCpuWindow = true;
        ui_showDisassembler = true;
        ui_showPpuViewer = true;
        ui_showMemoryViewer = true;
        ui_showLog = true;


        igDockBuilderFinish(dockspace_id);
    }

    UI_DrawMainMenuBar(nes);
    // The DebugToolbar is now a regular window, it will be drawn with others if ui_showToolbar is true.
    // If you want it always above the dockspace but below menu, it needs special handling or to be part of the menubar itself.

    igEnd(); // End of "cEMU_MainHost"

    // --- Draw all dockable windows ---
    // These will now dock into "cEMU_DockSpace_Main"
    UI_GameScreenWindow(nes); // This should be a top-level call to allow docking

    if (ui_showCpuWindow && nes) UI_CpuWindow(nes);
    if (ui_showPpuViewer && nes) UI_PPUViewer(nes);
    if (ui_showLog) UI_LogWindow();
    if (ui_showMemoryViewer && nes) UI_MemoryViewer(nes);
    if (ui_showDisassembler && nes) UI_DrawDisassembler(nes);
    
    if (ui_showToolbar && nes) UI_DebugToolbar(nes); // Draw as a separate window

    // --- Modals and non-docked utility windows ---
    if (ui_showSettingsWindow) UI_SettingsWindow(nes);
    if (ui_showAboutWindow) UI_DrawAboutWindow();

    // --- Status Bar ---
    // REFACTOR-NOTE: Status bar is drawn last to overlay on bottom of viewport.
    // If viewports are enabled, it will correctly be in the main viewport.
    // If not, it might draw over other ImGui windows if they are at the very bottom.
    // A robust status bar is tricky without making it a dockable window itself or using ImGui internal APIs.
    UI_DrawStatusBar(nes);
}

// Main UI loop update function
bool ui_quit_requested = false; // Global flag to signal quit

void UI_Update(NES* nes) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL3_ProcessEvent(&e); // Forward to ImGui

        if (e.type == SDL_EVENT_QUIT) {
            ui_quit_requested = true;
        }
        if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window)) {
             ui_quit_requested = true;
        }

        // Handle global hotkeys not processed by ImGui
        if (e.type == SDL_EVENT_KEY_DOWN && !ioptr->WantCaptureKeyboard) {
            bool ctrl_pressed = (SDL_GetModState() & SDL_KMOD_CTRL);
            // bool alt_pressed = (SDL_GetModState() & KMOD_ALT);

            if (e.key.key == SDLK_F5 && nes) UI_Reset(nes);
            if (e.key.key == SDLK_F6 && nes) UI_TogglePause(nes);
            if (e.key.key == SDLK_F7 && nes && nes->cpu && ui_paused) NES_Step(nes); // Step CPU
            if (e.key.key == SDLK_F8 && nes && ui_paused) UI_StepFrame(nes);     // Step Frame
            // F9 is often ImGui debug metrics, avoid for now unless rebound
            if (e.key.key == SDLK_F10) ui_showSettingsWindow = !ui_showSettingsWindow;
            if (e.key.key == SDLK_F11) UI_ToggleFullscreen();

            if (ctrl_pressed && e.key.key == SDLK_O) {
                 SDL_ShowOpenFileDialog(FileDialogCallback, nes, window, filters, SDL_arraysize(filters), NULL, false);
            }
            bool rom_loaded_for_state = (nes != NULL && strlen(ui_currentRomName) > 0 && strcmp(ui_currentRomName, "No ROM Loaded") != 0 && strcmp(ui_currentRomName, "Failed to load ROM") != 0);
            if (ctrl_pressed && e.key.key == SDLK_S && rom_loaded_for_state) {
                ui_openSaveStateModal = true;
            }
            if (ctrl_pressed && e.key.key == SDLK_L && rom_loaded_for_state) {
                ui_openLoadStateModal = true;
            }
        }
        // NES input is handled here if not captured by ImGui (e.g. typing in an input field)
        if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) && !ioptr->WantCaptureKeyboard) {
            UI_HandleInputEvent(&e);
        }
    }

    if (ui_quit_requested) { // Check flag to exit main application loop
        // Perform any cleanup if necessary before signaling exit
        exit(0);
        return; // Signal to main to break loop
    }


    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    igNewFrame();

    if (nes) { // Update NES controller states from UI input if NES context exists
        NES_SetController(nes, 0, nes_input_state[0]);
        NES_SetController(nes, 1, nes_input_state[1]);

        if(!ui_paused) {
            NES_StepFrame(nes); // Step a full frame if not paused
        }
    }

    // igShowDemoWindow(NULL); // Useful for debugging ImGui features

    UI_Draw(nes); // Central drawing function

    igRender(); // Render ImGui draw data

    // Clear OpenGL viewport (can be outside ImGui rendering if needed, e.g. for 3D scene)
    // glViewport(0, 0, (int)ioptr->DisplaySize.x, (int)ioptr->DisplaySize.y); // Handled by ImGui_ImplOpenGL3_RenderDrawData
    // glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
    // glClear(GL_COLOR_BUFFER_BIT); // Only clear if you're not rendering a game screen that fills everything

    ImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());

    if (ioptr->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        SDL_Window* backup_current_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
        igUpdatePlatformWindows();
        igRenderPlatformWindowsDefault(NULL,NULL);
        SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
    }

    SDL_GL_SwapWindow(window);
}


uint8_t UI_PollInput(int controller) {
    if (controller < 0 || controller > 1) return 0;
    return nes_input_state[controller];
}

void UI_Shutdown() {
    // REFACTOR-NOTE: Save recent ROMs list to a config file here if desired
    // REFACTOR-NOTE: Save window positions/docking layout if ImGuiIniSavingRate > 0 (usually handled by ImGui itself via imgui.ini)

    glDeleteTextures(1, &ppu_game_texture); // Clean up game screen texture
    // REFACTOR-NOTE: Clean up PPU Viewer textures (pt_texture0, pt_texture1) if they were created.
    // Currently, they are function-static, so this needs a more robust cleanup.
    // One way is to make them global like ppu_game_texture or pass pointers to be managed.


    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    igDestroyContext(NULL); // Destroy default context

    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    DEBUG_INFO("UI Shutdown complete.");
}