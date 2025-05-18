#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h> // Primary header for SDL_gpu
#include <cimgui.h>
#include <cimgui_impl.h> // For ImGui_ImplSDL3_ and ImGui_ImplSDLGPU3_
#include <cimplot.h>     // REFACTOR-NOTE: Included, but not explicitly used in UI functions. Kept for now in case of future use.
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_messagebox.h>
#include <stdio.h>  // For vsnprintf, snprintf
#include <string.h> // For strncpy, strcmp, strrchr
#include <stdarg.h> // For va_list
#include <float.h>  // For FLT_MIN
#include <time.h>   // For logging timestamp

#include "debug.h"
#include "ui/cimgui_markdown.h"

#include "cNES/nes.h"
#include "cNES/cpu.h"
#include "cNES/ppu.h"
#include "cNES/bus.h"
#include "cNES/debugging.h"

#include "ui.h"

// --- SDL and ImGui Globals ---
SDL_Window *window;
SDL_GPUDevice* gpu_device;
ImGuiIO* ioptr;
ImVec4 clear_color; // Clear color for the swapchain

// --- UI State ---
static bool ui_showPpuViewer = true;
static bool ui_showLog = true;
static bool ui_paused = false;
static bool ui_showMemoryViewer = true;
static bool ui_showSettingsWindow = false;
static char ui_romPath[256] = "";
static char ui_currentRomName[256] = "No ROM Loaded";
static char ui_logBuffer[8192] = ""; // REFACTOR-NOTE: Increased buffer size. Consider a circular buffer for very extensive logging.
static int ui_logLen = 0;
static float ui_fps = 0.0f;
static UI_Theme current_ui_theme = UI_THEME_DARK;
static float ui_master_volume = 0.8f;
static bool ui_sdl_fullscreen = false;

static char ui_recentRoms[UI_MAX_RECENT_ROMS][256];
static int ui_recentRomsCount = 0;

static bool ui_showAboutWindow = false;
static bool ui_showCreditsWindow = false;
static bool ui_showLicenceWindow = false;
static int ui_ppuViewerSelectedPalette = 0;
static bool ui_openSaveStateModal = false;
static bool ui_openLoadStateModal = false;
static int ui_selectedSaveLoadSlot = 0;

static uint8_t nes_input_state[2] = {0, 0};

static bool ui_showCpuWindow = true;
static bool ui_showToolbar = true;
static bool ui_showDisassembler = true;
static bool ui_showGameScreen = true; // REFACTOR-NOTE: Game screen is now a dockable window.

static bool ui_first_frame = true; // Flag for applying default docking layout

// --- PPU Viewer SDL_gpu Resources ---
static SDL_GPUTexture* pt_texture0 = NULL;
static SDL_GPUTexture* pt_texture1 = NULL;
static SDL_GPUSampler* pt_sampler = NULL; // Single sampler can be used for both pattern table textures
static SDL_GPUTransferBuffer* pt_transfer_buffer = NULL; // Single transfer buffer can be reused

// --- Game Screen SDL_gpu Resources ---
static SDL_GPUTexture* ppu_game_texture = NULL;
static SDL_GPUSampler* ppu_game_sampler = NULL;
static SDL_GPUTransferBuffer* ppu_game_transfer_buffer = NULL; // Renamed to avoid conflict
static SDL_GPUTextureSamplerBinding ppu_game_texture_sampler_binding = {0};

// --- Helper: Append to log ---
void UI_Log(const char* fmt, ...) {
    char time_buf[32];
    time_t now_time = time(NULL);
    struct tm *tm_info = localtime(&now_time); // REFACTOR-NOTE: Using localtime for human-readable time.
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S ", tm_info);
    int time_len = strlen(time_buf);

    int remaining_buf_space = sizeof(ui_logBuffer) - ui_logLen - 1; // -1 for null terminator

    if (time_len < remaining_buf_space) {
        strncpy(ui_logBuffer + ui_logLen, time_buf, remaining_buf_space);
        ui_logLen += time_len;
        remaining_buf_space -= time_len;
    }

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(ui_logBuffer + ui_logLen, remaining_buf_space, fmt, args);
    va_end(args);

    if (n > 0) {
        ui_logLen += n;
        if (ui_logLen > 0 && ui_logBuffer[ui_logLen-1] != '\n' && ui_logLen < (int)sizeof(ui_logBuffer) - 2) {
            ui_logBuffer[ui_logLen++] = '\n';
        }
        ui_logBuffer[ui_logLen] = '\0';
    }
    
    if (ui_logLen >= (int)sizeof(ui_logBuffer) - 512 ) { // Keep some buffer if close to full
         DEBUG_WARN("Log buffer nearing capacity. Consider increasing size or implementing circular buffer.");
         // Basic wrap-around: clear half the log to make space
         int half_len = ui_logLen / 2;
         memmove(ui_logBuffer, ui_logBuffer + half_len, ui_logLen - half_len);
         ui_logLen -= half_len;
         ui_logBuffer[ui_logLen] = '\0';
         UI_Log("Log buffer was cleared partially to make space.");
    }
}

// --- Helper: Add to Recent ROMs ---
static void UI_AddRecentRom(const char* path) {
    // Check if path already exists
    for (int i = 0; i < ui_recentRomsCount; ++i) {
        if (strcmp(ui_recentRoms[i], path) == 0) {
            // If it exists and not at the top, move to top
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
            return; // Done
        }
    }

    // Path not found, add to top
    if (ui_recentRomsCount < UI_MAX_RECENT_ROMS) {
        // Shift existing entries down
        for (int i = ui_recentRomsCount; i > 0; --i) {
            strncpy(ui_recentRoms[i], ui_recentRoms[i-1], 255);
            ui_recentRoms[i][255] = '\0';
        }
        ui_recentRomsCount++;
    } else { // Max count reached, shift oldest out
        for (int i = UI_MAX_RECENT_ROMS - 1; i > 0; --i) {
            strncpy(ui_recentRoms[i], ui_recentRoms[i-1], 255);
            ui_recentRoms[i][255] = '\0';
        }
    }
    // Add new path at the top
    strncpy(ui_recentRoms[0], path, 255);
    ui_recentRoms[0][255] = '\0';
}

void UI_LoadRom(NES* nes, const char* path) {
    if (NES_Load(path, nes) == 0) {
        UI_Log("Successfully loaded ROM: %s", path);
        const char* filename = strrchr(path, '/');
        if (!filename) filename = strrchr(path, '\\');
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
    if (nes) { // REFACTOR-NOTE: Added null check for safety
        NES_Reset(nes);
        UI_Log("NES Reset");
    } else {
        UI_Log("Cannot reset: No NES context.");
    }
}

void UI_StepFrame(NES* nes) {
    if (nes) { // REFACTOR-NOTE: Added null check
        NES_StepFrame(nes);
        UI_Log("Stepped one frame");
    } else {
        UI_Log("Cannot step frame: No NES context.");
    }
}

void UI_TogglePause(NES* nes) { // REFACTOR-NOTE: NES context not strictly needed here, but good for consistency if actions depend on it.
    ui_paused = !ui_paused;
    UI_Log(ui_paused ? "Emulation Paused" : "Emulation Resumed");
}

static void UI_HandleInputEvent(const SDL_Event* e) {
    int pressed = (e->type == SDL_EVENT_KEY_DOWN);
    // Standard NES controller mapping (A, B, Select, Start, Up, Down, Left, Right)
    switch (e->key.key) {
        // Player 1
        case SDLK_K:      nes_input_state[0] = pressed ? (nes_input_state[0] | 0x01) : (nes_input_state[0] & ~0x01); break; // A
        case SDLK_J:      nes_input_state[0] = pressed ? (nes_input_state[0] | 0x02) : (nes_input_state[0] & ~0x02); break; // B
        case SDLK_RSHIFT: nes_input_state[0] = pressed ? (nes_input_state[0] | 0x04) : (nes_input_state[0] & ~0x04); break; // Select
        case SDLK_RETURN: nes_input_state[0] = pressed ? (nes_input_state[0] | 0x08) : (nes_input_state[0] & ~0x08); break; // Start
        case SDLK_W:      nes_input_state[0] = pressed ? (nes_input_state[0] | 0x10) : (nes_input_state[0] & ~0x10); break; // Up
        case SDLK_S:      nes_input_state[0] = pressed ? (nes_input_state[0] | 0x20) : (nes_input_state[0] & ~0x20); break; // Down
        case SDLK_A:      nes_input_state[0] = pressed ? (nes_input_state[0] | 0x40) : (nes_input_state[0] & ~0x40); break; // Left
        case SDLK_D:      nes_input_state[0] = pressed ? (nes_input_state[0] | 0x80) : (nes_input_state[0] & ~0x80); break; // Right
        // REFACTOR-NOTE: Add Player 2 controls if desired
        default: break;
    }
}

static const SDL_DialogFileFilter filters[] = {
    { "NES ROMs",  "nes;fds;unif;nes2;ines" },
    { "Archives", "zip;7z;rar" }, // REFACTOR-NOTE: Archive handling would need external libraries. This filter is just for selection.
    { "All files",   "*" }
};

static void SDLCALL FileDialogCallback(void* userdata, const char* const* filelist, int filter_index) {
    if (!filelist || !*filelist) {
        if (SDL_GetError() && strlen(SDL_GetError()) > 0) {
            DEBUG_ERROR("SDL File Dialog Error: %s", SDL_GetError());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "File Dialog Error", SDL_GetError(), window);
        } else {
            DEBUG_WARN("No file selected or dialog cancelled.");
        }
        return;
    }

    const char* selected_file = *filelist;
    if (selected_file) {
        UI_Log("File selected via dialog: %s", selected_file);
        strncpy(ui_romPath, selected_file, sizeof(ui_romPath) -1);
        ui_romPath[sizeof(ui_romPath)-1] = '\0';
        UI_LoadRom((NES *)userdata, ui_romPath);
    }
}

void UI_DrawFileMenu(NES* nes) {
    if (igBeginMenu("File", true)) {
        if (igMenuItem_Bool("Open ROM...", "Ctrl+O", false, true)) {
             SDL_ShowOpenFileDialog(FileDialogCallback, nes, window, filters, SDL_arraysize(filters), NULL, false);
        }

        if (igBeginMenu("Recent ROMs", ui_recentRomsCount > 0)) {
            for (int i = 0; i < ui_recentRomsCount; ++i) {
                char label[270];
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
        bool rom_loaded_for_state = (nes != NULL && strlen(ui_currentRomName) > 0 && strcmp(ui_currentRomName, "No ROM Loaded") != 0 && strcmp(ui_currentRomName, "Failed to load ROM") != 0);
        if (igMenuItem_Bool("Save State...", "Ctrl+S", false, rom_loaded_for_state)) {
            ui_openSaveStateModal = true;
        }
        if (igMenuItem_Bool("Load State...", "Ctrl+L", false, rom_loaded_for_state)) {
            ui_openLoadStateModal = true;
        }
        igSeparator();
        if (igMenuItem_Bool("Exit", "Alt+F4", false, true)) {
            SDL_Event quit_event;
            quit_event.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit_event);
        }
        igEndMenu();
    }

    // REFACTOR-NOTE: Save/Load state modals are placeholders. Actual implementation requires NES_SaveState/NES_LoadState.
    if (ui_openSaveStateModal) {
        igOpenPopup_Str("Save State", 0);
        if (igBeginPopupModal("Save State", &ui_openSaveStateModal, ImGuiWindowFlags_AlwaysAutoResize)) {
            igText("Select Slot (0-9):");
            igSameLine(0,0); igSetNextItemWidth(100);
            igInputInt("##SaveSlot", &ui_selectedSaveLoadSlot, 1, 1, 0);
            ui_selectedSaveLoadSlot = ui_selectedSaveLoadSlot < 0 ? 0 : (ui_selectedSaveLoadSlot > 9 ? 9 : ui_selectedSaveLoadSlot);

            if (igButton("Save", (ImVec2){80,0})) {
                UI_Log("Placeholder: Save state to slot %d for ROM: %s", ui_selectedSaveLoadSlot, ui_currentRomName);
                // if (nes) NES_SaveState(nes, ui_selectedSaveLoadSlot); // Actual call
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

            if (igButton("Load", (ImVec2){80,0})) {
                UI_Log("Placeholder: Load state from slot %d for ROM: %s", ui_selectedSaveLoadSlot, ui_currentRomName);
                // if (nes) NES_LoadState(nes, ui_selectedSaveLoadSlot); // Actual call
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
    if (igBegin("Log", &ui_showLog, ImGuiWindowFlags_None)) {
        if (igButton("Clear", (ImVec2){0,0})) {
            ui_logLen = 0;
            ui_logBuffer[0] = '\0';
        }
        // REFACTOR-NOTE: Auto-scroll can be managed manually if needed, ImGui might have built-in options too.
        // igSameLine(0, 10);
        // static bool auto_scroll = true;
        // igCheckbox("Auto-scroll", &auto_scroll);

        igSeparator();
        igBeginChild_Str("LogScrollingRegion", (ImVec2){0,0}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        igTextUnformatted(ui_logBuffer, ui_logBuffer + ui_logLen);
        // if (auto_scroll && igGetScrollY() >= igGetScrollMaxY()) { // Basic auto-scroll
        //     igSetScrollHereY(1.0f);
        // }
        if (igGetScrollY() >= igGetScrollMaxY()) // Scroll to bottom if at the end
             igSetScrollHereY(1.0f);
        igEndChild();
    }
    igEnd();
}


// Helper to render a pattern table to a texture using SDL_gpu
static void UI_RenderPatternTable_GPU(SDL_GPUDevice* device, NES* nes, int table_idx, 
                                   SDL_GPUTexture* texture, SDL_GPUTransferBuffer* transfer_buffer, 
                                   uint32_t* pixel_buffer_rgba32, const uint32_t* palette_to_use_rgba32) {
    if (!device || !nes || !nes->ppu || !texture || !transfer_buffer || !pixel_buffer_rgba32 || !palette_to_use_rgba32) return;

    // PPU_GetPatternTableData provides raw tile data.
    // Each tile is 8x8 pixels. A pattern table is 16x16 tiles (128x128 pixels).
    // Each tile is 16 bytes: 8 bytes for plane 0, 8 bytes for plane 1.
    uint8_t pt_data[16 * 16 * 16]; // Max size for one pattern table (256 tiles * 16 bytes/tile = 4096 bytes)
    PPU_GetPatternTableData(nes->ppu, table_idx, pt_data);

    for (int tile_y = 0; tile_y < 16; ++tile_y) {
        for (int tile_x = 0; tile_x < 16; ++tile_x) {
            int tile_offset_in_pt_data = (tile_y * 16 + tile_x) * 16; // Offset to the current tile's data
            for (int y = 0; y < 8; ++y) { // Pixel row within a tile
                uint8_t plane0_byte = pt_data[tile_offset_in_pt_data + y];
                uint8_t plane1_byte = pt_data[tile_offset_in_pt_data + y + 8];
                for (int x = 0; x < 8; ++x) { // Pixel column within a tile
                    int bit = 7 - x; // NES pixels are drawn left-to-right from MSB
                    uint8_t color_idx_in_palette = ((plane1_byte >> bit) & 1) << 1 | ((plane0_byte >> bit) & 1);
                    uint32_t final_pixel_color_rgba = palette_to_use_rgba32[color_idx_in_palette];
                    
                    // Calculate position in the 128x128 pixel_buffer_rgba32
                    int buffer_x = tile_x * 8 + x;
                    int buffer_y = tile_y * 8 + y;
                    pixel_buffer_rgba32[buffer_y * 128 + buffer_x] = final_pixel_color_rgba;
                }
            }
        }
    }

    // Upload to GPU Texture
    SDL_MapGPUTransferBuffer(device, transfer_buffer, 0);

    SDL_GPUCommandBuffer* cmd_buffer = SDL_AcquireGPUCommandBuffer(device);
    if (cmd_buffer) {
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd_buffer);
        if (copy_pass) {
            SDL_GPUTextureTransferInfo source_transfer_info = {0};
            source_transfer_info.transfer_buffer = transfer_buffer;
            source_transfer_info.offset = 0;
            source_transfer_info.pixels_per_row = 128 * 4; // Bytes per row
            source_transfer_info.rows_per_layer = 128;    // Number of rows

            SDL_GPUTextureRegion destination_region = {0};
            destination_region.texture = texture;
            destination_region.w = 128;
            destination_region.h = 128;
            destination_region.d = 1;

            SDL_UploadToGPUTexture(copy_pass, &source_transfer_info, &destination_region, false);
            SDL_EndGPUCopyPass(copy_pass);
        } else {
            UI_Log("PPUViewer: Failed to begin GPU copy pass: %s", SDL_GetError());
        }
        SDL_SubmitGPUCommandBuffer(cmd_buffer);
    } else {
        UI_Log("PPUViewer: Failed to acquire GPU command buffer: %s", SDL_GetError());
    }
}


void UI_PPUViewer(NES* nes) {
    if (!ui_showPpuViewer) return;
    if (!nes || !nes->ppu) { // If window was open but NES becomes null, hide it.
        if (ui_showPpuViewer && (!nes || !nes->ppu)) ui_showPpuViewer = false;
        return;
    }

    if (igBegin("PPU Viewer", &ui_showPpuViewer, ImGuiWindowFlags_None)) {
        // Ensure GPU resources for PPU Viewer are created
        if (!pt_sampler) { // Create sampler once
            SDL_GPUSamplerCreateInfo sampler_info = {0};
            sampler_info.min_filter = SDL_GPU_FILTER_NEAREST; // For pixel art
            sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;
            sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            pt_sampler = SDL_CreateGPUSampler(gpu_device, &sampler_info);
            if (!pt_sampler) UI_Log("PPUViewer: Failed to create sampler: %s", SDL_GetError());
        }
        if (!pt_transfer_buffer) { // Create transfer buffer once (can be reused)
             SDL_GPUTransferBufferCreateInfo transfer_create_info = {0};
             transfer_create_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
             transfer_create_info.size = 128 * 128 * 4 * 8 * 8; // For one 128x128 RGBA texture
             pt_transfer_buffer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_create_info);
             if (!pt_transfer_buffer) UI_Log("PPUViewer: Failed to create transfer buffer: %s", SDL_GetError());
        }
        if (!pt_texture0 && gpu_device) {
            SDL_GPUTextureCreateInfo tex_info = {0};
            tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            tex_info.width = 128; tex_info.height = 128; tex_info.layer_count_or_depth = 1;
            tex_info.num_levels = 1; tex_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
            tex_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            pt_texture0 = SDL_CreateGPUTexture(gpu_device, &tex_info);
            if (!pt_texture0) UI_Log("PPUViewer: Failed to create pt_texture0: %s", SDL_GetError());
        }
        if (!pt_texture1 && gpu_device) {
            SDL_GPUTextureCreateInfo tex_info = {0};
            tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            tex_info.width = 128; tex_info.height = 128; tex_info.layer_count_or_depth = 1;
            tex_info.num_levels = 1; tex_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
            tex_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            pt_texture1 = SDL_CreateGPUTexture(gpu_device, &tex_info);
            if (!pt_texture1) UI_Log("PPUViewer: Failed to create pt_texture1: %s", SDL_GetError());
        }
        
        static uint32_t pt_pixel_buffer_rgba32[128 * 128]; // CPU-side buffer for pixel data

        const char* palette_names[] = {
            "BG Pal 0", "BG Pal 1", "BG Pal 2", "BG Pal 3",
            "Sprite Pal 0", "Sprite Pal 1", "Sprite Pal 2", "Sprite Pal 3"
        };
        igSetNextItemWidth(150);
        igCombo_Str_arr("PT Palette", &ui_ppuViewerSelectedPalette, palette_names, 8, 4);

        static uint32_t viewer_palette_rgba[4]; // RGBA for display
        const uint32_t* master_nes_palette_abgr = PPU_GetPalette(); 

        // Determine the 4 colors for the selected sub-palette
        for(int i=0; i<4; ++i) {
            uint8_t palette_ram_idx; // Index into PPU's $3F00-$3F1F
            if (i == 0) { // Color 0 is universal background or transparent for sprites
                 palette_ram_idx = 0x00; // Always use $3F00 for the first color in any displayed palette
            } else {
                // ui_ppuViewerSelectedPalette: 0-3 for BG, 4-7 for Sprites
                // BG palettes: $3F00, $3F01, $3F02, $3F03 (for pal 0)
                //              $3F00, $3F05, $3F06, $3F07 (for pal 1)
                // Sprite palettes: $3F00 (or $3F10), $3F11, $3F12, $3F13 (for pal 0 / overall pal 4)
                //                  $3F00 (or $3F10), $3F15, $3F16, $3F17 (for pal 1 / overall pal 5)
                int base_offset = (ui_ppuViewerSelectedPalette < 4) ? 
                                  (ui_ppuViewerSelectedPalette * 4) :  // BG palettes start at 0x00, 0x04, 0x08, 0x0C
                                  (0x10 + (ui_ppuViewerSelectedPalette - 4) * 4); // Sprite palettes start at 0x10, 0x14, 0x18, 0x1C
                palette_ram_idx = base_offset + i;
            }
            // Ensure mirroring for palette addresses (e.g. $3F04 -> $3F00, $3F10 -> $3F00 etc.)
            if (palette_ram_idx % 4 == 0) palette_ram_idx = 0x00; // All palette[0] colors mirror $3F00

            uint8_t master_palette_color_index = nes->ppu->palette[palette_ram_idx % 32]; // Read from PPU palette RAM
            uint32_t nes_col_abgr = master_nes_palette_abgr[master_palette_color_index % 64];
            
            // Convert ABGR (0xAABBGGRR) to RGBA (0xRRGGBBAA)
            viewer_palette_rgba[i] = ((nes_col_abgr & 0x000000FF) << 24) | // R to R0000000
                                   ((nes_col_abgr & 0x0000FF00) << 8)  | // G to 00GG0000
                                   ((nes_col_abgr & 0x00FF0000) >> 8)  | // B to 0000BB00
                                   0x000000FF;                          // A (force opaque FF)
        }


        if (pt_texture0 && pt_sampler && pt_transfer_buffer) {
            igText("Pattern Table 0 ($0000):");
            UI_RenderPatternTable_GPU(gpu_device, nes, 0, pt_texture0, pt_transfer_buffer, pt_pixel_buffer_rgba32, viewer_palette_rgba);
            SDL_GPUTextureSamplerBinding binding0 = {pt_texture0, pt_sampler};
            igImage((ImTextureID)(uintptr_t)&binding0, (ImVec2){128 * 2.0f, 128 * 2.0f}, (ImVec2){0,0}, (ImVec2){1,1});
        }

        igSameLine(0, 20);
        igBeginGroup();
        if (pt_texture1 && pt_sampler && pt_transfer_buffer) {
            igText("Pattern Table 1 ($1000):");
            UI_RenderPatternTable_GPU(gpu_device, nes, 1, pt_texture1, pt_transfer_buffer, pt_pixel_buffer_rgba32, viewer_palette_rgba);
            SDL_GPUTextureSamplerBinding binding1 = {pt_texture1, pt_sampler};
            igImage((ImTextureID)(uintptr_t)&binding1, (ImVec2){128 * 2.0f, 128 * 2.0f}, (ImVec2){0,0}, (ImVec2){1,1});
        }
        igEndGroup();
        
        igSeparator();
        if (igCollapsingHeader_TreeNodeFlags("PPU Registers", ImGuiTreeNodeFlags_DefaultOpen)) {
            igText("CTRL (0x2000): 0x%02X", nes->ppu->ctrl);
            igText("MASK (0x2001): 0x%02X", nes->ppu->mask);
            igText("STATUS (0x2002): 0x%02X", nes->ppu->status);
            igText("OAMADDR (0x2003): 0x%02X", nes->ppu->oam_addr);
            igText("VRAM Addr (v): 0x%04X, Temp Addr (t): 0x%04X", nes->ppu->vram_addr, nes->ppu->temp_addr);
            igText("Fine X: %d, Write Latch (w): %d", nes->ppu->fine_x, nes->ppu->addr_latch);
            igText("Scanline: %d, Cycle: %d", nes->ppu->scanline, nes->ppu->cycle);
            igText("Frame Odd: %s", nes->ppu->frame_odd ? "true" : "false");
            igText("NMI Occurred: %s, NMI Output: %s", nes->ppu->nmi_occured ? "true" : "false", nes->ppu->nmi_output ? "true" : "false");
        }

        if (igCollapsingHeader_TreeNodeFlags("Palettes", ImGuiTreeNodeFlags_None)) {
            igText("Current PPU Palette RAM ($3F00 - $3F1F):");
            for (int i = 0; i < 32; ++i) {
                uint8_t palette_idx_val = nes->ppu->palette[i];
                uint32_t nes_col_abgr = master_nes_palette_abgr[palette_idx_val % 64];
                ImVec4 im_col; // RGBA for ImGui
                im_col.x = ((nes_col_abgr & 0x000000FF)) / 255.0f;        // R
                im_col.y = ((nes_col_abgr & 0x0000FF00) >> 8) / 255.0f;   // G
                im_col.z = ((nes_col_abgr & 0x00FF0000) >> 16) / 255.0f;  // B
                im_col.w = 1.0f;
                
                if (i > 0 && i % 16 == 0) igNewLine();
                if (i > 0 && i % 4 == 0 && i % 16 != 0) igSameLine(0,8);

                char pal_label[16];
                snprintf(pal_label, sizeof(pal_label), "$3F%02X", i);
                igColorButton(pal_label, im_col, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, (ImVec2){20, 20});
                if (igIsItemHovered(0)) {
                    igBeginTooltip();
                    igText("$3F%02X: Master Idx 0x%02X", i, palette_idx_val);
                    igColorButton("##tooltipcol", im_col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel, (ImVec2){64,64});
                    igEndTooltip();
                }
                if ((i+1) % 4 != 0) igSameLine(0,2);
            }
        }

        if (igCollapsingHeader_TreeNodeFlags("OAM (Sprites)", ImGuiTreeNodeFlags_None)) {
            const uint8_t* oam_data = PPU_GetOAM(nes->ppu);
            if (oam_data) {
                if (igBeginTable("OAMTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit, (ImVec2){0,0}, 0)) {
                    igTableSetupColumn("Sprite #", 0, 0, 0);
                    igTableSetupColumn("Y", 0, 0, 0);
                    igTableSetupColumn("Tile ID", 0, 0, 0);
                    igTableSetupColumn("Attr", 0, 0, 0); // REFACTOR-NOTE: Decode attributes (palette, priority, flip) for better display.
                    igTableSetupColumn("X", 0, 0, 0);
                    igTableHeadersRow();
                    for (int i = 0; i < 8; ++i) { // Display first 8 sprites
                        igTableNextRow(0,0);
                        igTableSetColumnIndex(0); igText("%d", i);
                        igTableSetColumnIndex(1); igText("0x%02X (%d)", oam_data[i * 4 + 0], oam_data[i * 4 + 0]);
                        igTableSetColumnIndex(2); igText("0x%02X", oam_data[i * 4 + 1]);
                        igTableSetColumnIndex(3); igText("0x%02X", oam_data[i * 4 + 2]);
                        igTableSetColumnIndex(4); igText("0x%02X (%d)", oam_data[i * 4 + 3], oam_data[i * 4 + 3]);
                    }
                    igEndTable();
                }
            }
        }
        // REFACTOR-NOTE: Add Nametable viewer section here (complex, involves rendering tiles based on nametable, attribute table, and current PPU scroll).
    }
    igEnd();
}


static uint16_t ui_memoryViewerAddress = 0x0000;
// REFACTOR-NOTE: Snapshot is for CPU view. PPU memory has its own address space and would need a separate viewer or mode.
static uint8_t ui_memorySnapshot[0x10000]; 
static int ui_memoryViewerRows = 16;

void UI_MemoryViewer(NES* nes) {
    if (!ui_showMemoryViewer) return;
    if (!nes) {
        if (ui_showMemoryViewer) ui_showMemoryViewer = false;
        return;
    }

    if (igBegin("Memory Viewer (CPU Bus)", &ui_showMemoryViewer, ImGuiWindowFlags_None)) {
        igSetNextItemWidth(100);
        igInputScalar("Base Addr", ImGuiDataType_U16, &ui_memoryViewerAddress, NULL, NULL, "%04X", ImGuiInputTextFlags_CharsHexadecimal);
        igSameLine(0,10);
        igSetNextItemWidth(100);
        igSliderInt("Rows", &ui_memoryViewerRows, 1, 64, "%d", 0);

        // REFACTOR-NOTE: Add search, goto, data interpretation (e.g., as text, 16-bit words) features for advanced debugging.

        igBeginChild_Str("MemoryViewerScrollRegion", (ImVec2){0, (float)igGetTextLineHeightWithSpacing() * (ui_memoryViewerRows + 2.5f)}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);

        if (igBeginTable("MemoryTable", 17, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit, (ImVec2){0, 0}, 0)) {
            igTableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, igGetFontSize() * 4.5f, 0);
            for (int i = 0; i < 16; i++) {
                char colName[4];
                snprintf(colName, sizeof(colName), "%X", i);
                igTableSetupColumn(colName, ImGuiTableColumnFlags_WidthFixed, igGetFontSize() * 2.5f, 0);
            }
            igTableHeadersRow();

            for (int row = 0; row < ui_memoryViewerRows; row++) {
                uint16_t base_addr_for_row = ui_memoryViewerAddress + (row * 16);
                if (ui_memoryViewerAddress > 0xFFFF - (row*16)) break; // Prevent wrap for display address

                igTableNextRow(0, 0);
                igTableSetColumnIndex(0);
                igText("%04X", base_addr_for_row);

                for (int col = 0; col < 16; col++) {
                    uint16_t currentAddr = base_addr_for_row + col;
                    if (currentAddr > 0xFFFF) { // Should not happen due to outer break, but defensive.
                        igTableSetColumnIndex(col + 1);
                        igText("--");
                        continue;
                    }
                    uint8_t value = BUS_Peek(nes, currentAddr); 
                    uint8_t prevValue = ui_memorySnapshot[currentAddr];

                    if (nes->cpu && !ui_paused) { 
                        ui_memorySnapshot[currentAddr] = value;
                    }

                    igTableSetColumnIndex(col + 1);
                    if (value != prevValue && ui_paused) { 
                        igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){1.0f, 0.3f, 0.3f, 1.0f}); // Highlight changed values when paused
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
    if (!ui_showDisassembler) return;
    if (!nes || !nes->cpu) {
        if (ui_showDisassembler && (!nes || !nes->cpu)) ui_showDisassembler = false;
        return;
    }
    if (igBegin("Disassembler", &ui_showDisassembler, ImGuiWindowFlags_None)) {
        // REFACTOR-NOTE: Add breakpoint setting, step-over/step-out controls, and syntax highlighting for a richer debugger.
        uint16_t pc_to_disassemble = nes->cpu->pc;
        igText("Current PC: 0x%04X", pc_to_disassemble);
        igSameLine(0, 20);
        if(igButton("Step Op (F7)", (ImVec2){0,0})) { // REFACTOR-NOTE: F7 mapping added here for local control
            if(ui_paused) NES_Step(nes); 
            else UI_Log("Cannot step instruction while running. Pause first (F6).");
        }

        igBeginChild_Str("DisassemblyRegion", (ImVec2){0, igGetTextLineHeightWithSpacing() * 18}, ImGuiChildFlags_Borders, ImGuiWindowFlags_None); // Removed ImGuiWindowFlags_HorizontalScrollbar as table handles it
        if (igBeginTable("DisassembledView", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY , (ImVec2){0,0},0)) {
            igTableSetupColumn(" ", ImGuiTableColumnFlags_WidthFixed, igGetFontSize() * 1.5f,0); 
            igTableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, igGetFontSize() * 5.0f,0);
            igTableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch, 0, 0);
            
            char disasm_buf[128];
            uint16_t addr_iter = nes->cpu->pc; 

            // Attempt to show a few lines before PC (very simplified, true back-disassembly is complex)
            // This naive approach just starts a bit earlier and hopes instructions align.
            uint16_t start_addr = nes->cpu->pc;
            for(int pre_lines = 0; pre_lines < 8; ++pre_lines) {
                if (start_addr < 5) { start_addr = 0; break; } // Avoid underflow by too much
                start_addr -= 3; // Guess average instruction length
            }
            if (start_addr > nes->cpu->pc) start_addr = nes->cpu->pc; // Safety if PC is very low

            addr_iter = start_addr;

            for (int i = 0; i < 32; i++) { // Show more lines
                igTableNextRow(0,0);
                
                igTableSetColumnIndex(0);
                if (addr_iter == nes->cpu->pc) {
                    igText(">"); 
                } else {
                    igText(" ");
                }

                igTableSetColumnIndex(1);
                igText("0x%04X", addr_iter);

                igTableSetColumnIndex(2);
                uint16_t prev_addr_iter = addr_iter;
                addr_iter = disassemble(nes, addr_iter, disasm_buf, sizeof(disasm_buf)); // disassemble should return next instruction's address
                igTextUnformatted(disasm_buf, NULL);

                if (addr_iter <= prev_addr_iter && i < 31) { // Prevent infinite loop if disassembly stalls, but allow last line
                    igTableNextRow(0,0);
                    igTableSetColumnIndex(1); igText("----");
                    igTableSetColumnIndex(2); igText("Disassembly error or end of known code.");
                    break;
                }
                 if (addr_iter == 0 && i < 31) break; // Stop if disassemble returns 0 (e.g. invalid opcode sequence)
            }
            // Auto-scroll to PC if it's not visible
            // This is a bit tricky with tables, might need manual scroll management or ImGuiListClipper
            igEndTable();
        }
        igEndChild();
    }
    igEnd();
}


void UI_ToggleFullscreen() {
    ui_sdl_fullscreen = !ui_sdl_fullscreen;
    // SDL_SetWindowFullscreen takes SDL_bool (SDL_TRUE/SDL_FALSE)
    if (SDL_SetWindowFullscreen(window, ui_sdl_fullscreen ? true : false) != 0) {
        UI_Log("Error toggling fullscreen: %s", SDL_GetError());
        ui_sdl_fullscreen = !ui_sdl_fullscreen; // Revert state on error
    } else {
        UI_Log("Toggled fullscreen to: %s", ui_sdl_fullscreen ? "ON" : "OFF");
    }
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
        colors[ImGuiCol_CheckMark] = (ImVec4){0.26f, 0.59f, 0.98f, 1.00f}; // A more distinct checkmark for light theme
        colors[ImGuiCol_SliderGrab] = (ImVec4){0.24f, 0.52f, 0.88f, 1.00f};
        colors[ImGuiCol_SliderGrabActive] = (ImVec4){0.26f, 0.59f, 0.98f, 1.00f};
        clear_color = (ImVec4){0.90f, 0.90f, 0.90f, 1.00f}; // Lighter clear color for light theme
    } else { // UI_THEME_DARK
        igStyleColorsDark(NULL); // Start with default dark
        colors[ImGuiCol_Text]                   = (ImVec4){1.00f, 1.00f, 1.00f, 1.00f};
        colors[ImGuiCol_TextDisabled]           = (ImVec4){0.50f, 0.50f, 0.50f, 1.00f};
        colors[ImGuiCol_WindowBg]               = (ImVec4){0.10f, 0.10f, 0.11f, 1.00f}; // Slightly bluish dark
        colors[ImGuiCol_ChildBg]                = (ImVec4){0.12f, 0.12f, 0.13f, 1.00f};
        colors[ImGuiCol_PopupBg]                = (ImVec4){0.08f, 0.08f, 0.09f, 0.94f};
        colors[ImGuiCol_Border]                 = (ImVec4){0.43f, 0.43f, 0.50f, 0.50f};
        colors[ImGuiCol_BorderShadow]           = (ImVec4){0.00f, 0.00f, 0.00f, 0.00f};
        colors[ImGuiCol_FrameBg]                = (ImVec4){0.20f, 0.21f, 0.22f, 0.54f}; // Controls background
        colors[ImGuiCol_FrameBgHovered]         = (ImVec4){0.25f, 0.26f, 0.28f, 0.78f};
        colors[ImGuiCol_FrameBgActive]          = (ImVec4){0.30f, 0.31f, 0.33f, 1.00f};
        colors[ImGuiCol_TitleBg]                = (ImVec4){0.08f, 0.08f, 0.09f, 1.00f}; // Window title bar
        colors[ImGuiCol_TitleBgActive]          = (ImVec4){0.15f, 0.16f, 0.18f, 1.00f}; // Active window title bar
        colors[ImGuiCol_TitleBgCollapsed]       = (ImVec4){0.08f, 0.08f, 0.09f, 0.75f};
        colors[ImGuiCol_MenuBarBg]              = (ImVec4){0.14f, 0.15f, 0.16f, 1.00f};
        colors[ImGuiCol_ScrollbarBg]            = (ImVec4){0.02f, 0.02f, 0.02f, 0.53f};
        colors[ImGuiCol_ScrollbarGrab]          = (ImVec4){0.31f, 0.31f, 0.33f, 1.00f};
        colors[ImGuiCol_ScrollbarGrabHovered]   = (ImVec4){0.41f, 0.41f, 0.43f, 1.00f};
        colors[ImGuiCol_ScrollbarGrabActive]    = (ImVec4){0.51f, 0.51f, 0.53f, 1.00f};
        colors[ImGuiCol_CheckMark]              = (ImVec4){0.50f, 0.75f, 0.25f, 1.00f}; // Greenish checkmark
        colors[ImGuiCol_SliderGrab]             = (ImVec4){0.40f, 0.65f, 0.20f, 1.00f};
        colors[ImGuiCol_SliderGrabActive]       = (ImVec4){0.50f, 0.75f, 0.25f, 1.00f};
        colors[ImGuiCol_Button]                 = (ImVec4){0.22f, 0.40f, 0.18f, 0.60f}; // Darker Greenish buttons
        colors[ImGuiCol_ButtonHovered]          = (ImVec4){0.28f, 0.50f, 0.24f, 1.00f};
        colors[ImGuiCol_ButtonActive]           = (ImVec4){0.32f, 0.58f, 0.28f, 1.00f};
        colors[ImGuiCol_Header]                 = (ImVec4){0.20f, 0.38f, 0.16f, 0.58f}; // Collapsing header
        colors[ImGuiCol_HeaderHovered]          = (ImVec4){0.26f, 0.48f, 0.22f, 0.80f};
        colors[ImGuiCol_HeaderActive]           = (ImVec4){0.30f, 0.55f, 0.26f, 1.00f};
        colors[ImGuiCol_Separator]              = colors[ImGuiCol_Border];
        colors[ImGuiCol_SeparatorHovered]       = (ImVec4){0.40f, 0.60f, 0.20f, 0.78f};
        colors[ImGuiCol_SeparatorActive]        = (ImVec4){0.50f, 0.70f, 0.25f, 1.00f};
        colors[ImGuiCol_ResizeGrip]             = (ImVec4){0.44f, 0.70f, 0.20f, 0.30f};
        colors[ImGuiCol_ResizeGripHovered]      = (ImVec4){0.50f, 0.80f, 0.26f, 0.67f};
        colors[ImGuiCol_ResizeGripActive]       = (ImVec4){0.58f, 0.90f, 0.30f, 0.95f};
        colors[ImGuiCol_Tab]                    = colors[ImGuiCol_Header];
        colors[ImGuiCol_TabHovered]             = colors[ImGuiCol_HeaderHovered];
        //colors[ImGuiCol_TabActive]              = colors[ImGuiCol_HeaderActive];
        //colors[ImGuiCol_TabUnfocused]           = igColorConvertU32ToFloat4(igGetColorU32_Col(ImGuiCol_Tab,1.0f) & 0x00FFFFFF | 0xDD000000); // More transparent
        //colors[ImGuiCol_TabUnfocusedActive]     = igColorConvertU32ToFloat4(igGetColorU32_Col(ImGuiCol_TabActive,1.0f) & 0x00FFFFFF | 0xDD000000);
        colors[ImGuiCol_DockingPreview]         = colors[ImGuiCol_HeaderActive]; 
        colors[ImGuiCol_DockingEmptyBg]         = (ImVec4){0.20f, 0.20f, 0.20f, 1.00f};
        colors[ImGuiCol_PlotLines]              = (ImVec4){0.61f, 0.61f, 0.61f, 1.00f};
        colors[ImGuiCol_PlotLinesHovered]       = (ImVec4){1.00f, 0.43f, 0.35f, 1.00f};
        colors[ImGuiCol_PlotHistogram]          = (ImVec4){0.90f, 0.70f, 0.00f, 1.00f};
        colors[ImGuiCol_PlotHistogramHovered]   = (ImVec4){1.00f, 0.60f, 0.00f, 1.00f};
        colors[ImGuiCol_TextSelectedBg]         = (ImVec4){0.26f, 0.59f, 0.98f, 0.35f};
        colors[ImGuiCol_DragDropTarget]         = (ImVec4){1.00f, 1.00f, 0.00f, 0.90f};
        //colors[ImGuiCol_NavHighlight]           = colors[ImGuiCol_HeaderHovered];
        colors[ImGuiCol_NavWindowingHighlight]  = (ImVec4){1.00f, 1.00f, 1.00f, 0.70f};
        colors[ImGuiCol_NavWindowingDimBg]      = (ImVec4){0.80f, 0.80f, 0.80f, 0.20f};
        colors[ImGuiCol_ModalWindowDimBg]       = (ImVec4){0.20f, 0.20f, 0.20f, 0.60f};
        clear_color = (ImVec4){0.05f, 0.05f, 0.055f, 1.00f}; // Dark clear color for dark theme
    }
    
    style->WindowRounding = 4.0f; 
    style->FrameRounding = 4.0f;  
    style->GrabRounding = 4.0f;   
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

    // REFACTOR-NOTE: Font loading. If you have a preferred font, load it here.
    // Example:
    // char* fontPath = SDL_GetBasePath(); // Or a specific path
    // if (fontPath) {
    //     char fullFontPath[512];
    //     snprintf(fullFontPath, sizeof(fullFontPath), "%smyfont.ttf", fontPath); // Assuming font in base path
    //     SDL_free(fontPath);
    //     ioptr->Fonts->AddFontFromFileTTF(fullFontPath, 16.0f, NULL, NULL);
    //     // ImGui_ImplSDLGPU3_DestroyFontsTexture(); // If fonts are changed after init
    //     // ImGui_ImplSDLGPU3_CreateFontsTexture();
    // }
}


void UI_InitStlye() { // Legacy name, kept for compatibility.
    UI_ApplyTheme(current_ui_theme); // Apply default theme
}

void UI_Init() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { // REFACTOR-NOTE: Added SDL_INIT_GAMEPAD for ImGui gamepad nav
        DEBUG_FATAL("Could not initialize SDL: %s", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SDL Initialization Error", SDL_GetError(), NULL);
        exit(1);
    }

    // REFACTOR-NOTE: Window flags: Removed SDL_WINDOW_OPENGL. SDL_WINDOW_RESIZABLE and SDL_WINDOW_MAXIMIZED are good.
    // Consider SDL_WINDOW_HIGH_PIXEL_DENSITY for HiDPI displays if desired.
    window = SDL_CreateWindow("cNES Emulator (SDL3_gpu)", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);
    if (!window) {
        DEBUG_FATAL("Could not create window: %s", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Window Creation Error", SDL_GetError(), NULL);
        SDL_Quit();
        exit(1);
    }

    gpu_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV| SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_METALLIB,
#ifdef DEBUG
        true,
#else
        false,
#endif
        NULL);

    if (!gpu_device) {
        DEBUG_FATAL("Unable to create GPU Device: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        exit(1);
    }

    // Claim window for GPU Device
    if (!SDL_ClaimWindowForGPUDevice(gpu_device, window)) {
        DEBUG_FATAL("Unable to claim window for GPU: %s", SDL_GetError());
        SDL_DestroyGPUDevice(gpu_device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        exit(1);
    }
    // REFACTOR-NOTE: SDL_GPU_PRESENTMODE_MAILBOX is good for low latency. FIFO is vsync. IMMEDIATE is no vsync (tearing).
    SDL_SetGPUSwapchainParameters(gpu_device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_MAILBOX);

    // Setup Dear ImGui context
    ImGuiContext *ctx = igCreateContext(NULL);
    ioptr = igGetIO_ContextPtr(ctx);
    ioptr->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ioptr->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; 
    ioptr->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    //ioptr->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; 

    ImPlot_SetCurrentContext(ImPlot_CreateContext()); // Initialize ImPlot context

    ImGuiStyle* style = igGetStyle();
    if (ioptr->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style->WindowRounding = 0.0f; 
        style->Colors[ImGuiCol_WindowBg].w = 1.0f; 
    }

    // Setup Platform/Renderer backends for SDL_gpu
    ImGui_ImplSDL3_InitForSDLGPU(window); // SDL3 platform backend
    ImGui_ImplSDLGPU3_InitInfo init_info = {0};
    init_info.Device = gpu_device;
    init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(gpu_device, window);
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1; // No MSAA for ImGui by default
    ImGui_ImplSDLGPU3_Init(&init_info);          // SDL_gpu renderer backend

    UI_ApplyTheme(UI_THEME_DARK); // Apply initial theme (also sets clear_color)
    strncpy(ui_currentRomName, "No ROM Loaded", sizeof(ui_currentRomName) -1);
    ui_currentRomName[sizeof(ui_currentRomName)-1] = '\0';

    UI_Log("cEMU Initialized with SDL3_gpu. Welcome!");
}

void UI_SettingsWindow(NES* nes) {
    if (!ui_showSettingsWindow) return;

    igSetNextWindowSize((ImVec2){480, 400}, ImGuiCond_FirstUseEver);
    if (igBegin("Settings", &ui_showSettingsWindow, ImGuiWindowFlags_None)) {
        if (igBeginTabBar("SettingsTabs", 0)) {
            if (igBeginTabItem("Display", NULL, 0)) {
                igText("Theme:"); igSameLine(0,5);
                if (igRadioButton_Bool("Dark", current_ui_theme == UI_THEME_DARK)) UI_ApplyTheme(UI_THEME_DARK);
                igSameLine(0,5);
                if (igRadioButton_Bool("Light", current_ui_theme == UI_THEME_LIGHT)) UI_ApplyTheme(UI_THEME_LIGHT);
                igSeparator();
                if (igCheckbox("Fullscreen (Window)", &ui_sdl_fullscreen)) UI_ToggleFullscreen();
                igSameLine(0, 10); igTextDisabled("(F11)");
                // REFACTOR-NOTE: Add font scaling options, game screen aspect ratio/scaling options here.
                igTextDisabled("More display settings (font, scaling) can be added here.");
                igEndTabItem();
            }
            if (igBeginTabItem("Audio", NULL, 0)) {
                // REFACTOR-NOTE: Connect this to actual APU volume control.
                if (igSliderFloat("Master Volume", &ui_master_volume, 0.0f, 1.0f, "%.2f", 0)) {
                    // if (nes && nes->apu) APU_SetMasterVolume(nes->apu, ui_master_volume);
                    UI_Log("Master volume (placeholder) set to: %.2f", ui_master_volume);
                }
                // REFACTOR-NOTE: Add options for audio buffer size, sample rate, APU channel toggles.
                igTextDisabled("More audio settings (buffer, channels) can be added here.");
                igEndTabItem();
            }
            if (igBeginTabItem("Input", NULL, 0)) {
                igText("Controller 1 Mapping (NES):");
                igText("Up: W, Down: S, Left: A, Right: D");
                igText("A: K, B: J, Select: RShift, Start: Enter");
                igSeparator();
                igTextDisabled("TODO: Add remappable key bindings UI for Controller 1 & 2.");
                // REFACTOR-NOTE: Implement a visual key mapping UI.
                igEndTabItem();
            }
            // REFACTOR-NOTE: Add "Paths" tab for save states, screenshots, default ROMs directory.
            // REFACTOR-NOTE: Add "Advanced" tab for emulation tweaks (e.g., CPU/PPU cycle accuracy options if available).
            igEndTabBar();
        }
        igSeparator();
        if (igButton("Close", (ImVec2){-FLT_MIN,0})) ui_showSettingsWindow = false;
    }
    igEnd();
}

void UI_DrawAboutWindow() {
    if (!ui_showAboutWindow) return;
    igSetNextWindowSize((ImVec2){400, 250}, ImGuiCond_FirstUseEver); // REFACTOR-NOTE: Slightly larger for more info
    if (igBegin("About cEMU", &ui_showAboutWindow, ImGuiWindowFlags_AlwaysAutoResize)) {
        igText("cEMU - A NES Emulator Project");
        igText("Version: 0.2.sdl3gpu (SDL3 + cimgui + SDL_gpu)"); // REFACTOR-NOTE: Updated version
        igText("Author: Your Name/Handle Here"); // REFACTOR-NOTE: Fill this in!
        igSeparator();
        igText("Powered by Dear ImGui (cimgui bindings) and SDL3 with SDL_gpu.");
        igText("This project is for demonstration and learning purposes.");
        igSeparator();
        // REFACTOR-NOTE: Add a link to your project's GitHub or website.
        if (igButton("Project on GitHub (Example)", (ImVec2){-FLT_MIN,0})) { 
            SDL_OpenURL("https://github.com/RileyWebb/cNES"); // Example URL
        }
        if (igButton("OK", (ImVec2){-FLT_MIN,0})) ui_showAboutWindow = false;
    }
    igEnd();
}

static ImGuiMarkdown_Config mdConfig;

static char* credits_markdown;
static char* licence_markdown;

static void UI_MD_LinkCallback(ImGuiMarkdown_LinkCallbackData link) 
{
    if (link.link && link.linkLength > 0) {
        char truncated_link[link.linkLength + 1];
        strncpy(truncated_link, link.link, link.linkLength);
        truncated_link[link.linkLength] = '\0';
        SDL_OpenURL(truncated_link);
    }
}

void UI_DrawCreditsWindow() {
    if (!ui_showCreditsWindow) return;
    igSetNextWindowSize((ImVec2){400, 250}, ImGuiCond_FirstUseEver);
    if (igBegin("Credits", &ui_showCreditsWindow, ImGuiWindowFlags_None)) {
        ImGuiMarkdown_Config_Init(&mdConfig); // Initialize with defaults

        // Customize config
        mdConfig.linkCallback = UI_MD_LinkCallback;
        // mdConfig.linkIcon = ICON_FA_LINK; // If using FontAwesome

        //if (myH1Font) mdConfig.headingFormats[0].font = myH1Font;
        //mdConfig.headingFormats[0].separator = true;

        //if (myH2Font) mdConfig.headingFormats[1].font = myH2Font;
        //mdConfig.headingFormats[1].separator = true;
        
        // H3 uses default font but no separator
        //mdConfig.headingFormats[2].font = NULL; // Or igGetDefaultFont()
        //mdConfig.headingFormats[2].separator = false;

        // mdConfig.formatCallback = MyCustomFormatCallback; // If you have one

        if (credits_markdown) {
            ImGuiMarkdown(credits_markdown, strlen(credits_markdown), &mdConfig);
        } else {
            FILE* file = fopen("CREDITS", "r");
            if (file) {
                fseek(file, 0, SEEK_END);
                long length = ftell(file);
                fseek(file, 0, SEEK_SET);
                credits_markdown = (char*)malloc(length + 1);
                if (credits_markdown) {
                    fread(credits_markdown, 1, length, file);
                    credits_markdown[length] = '\0'; // Null-terminate
                }
                fclose(file);
            } else {
                igText("Error loading credits.");
            }
        }
    }
    igEnd();
}

void UI_DrawLicenceWindow() {
    if (!ui_showLicenceWindow) return;
    igSetNextWindowSize((ImVec2){400, 250}, ImGuiCond_FirstUseEver);
    if (igBegin("Licence", &ui_showLicenceWindow, ImGuiWindowFlags_None)) {
        ImGuiMarkdown_Config_Init(&mdConfig); // Initialize with defaults

        // Customize config
        mdConfig.linkCallback = UI_MD_LinkCallback;
        // mdConfig.linkIcon = ICON_FA_LINK; // If using FontAwesome

        //if (myH1Font) mdConfig.headingFormats[0].font = myH1Font;
        //mdConfig.headingFormats[0].separator = true;

        //if (myH2Font) mdConfig.headingFormats[1].font = myH2Font;
        //mdConfig.headingFormats[1].separator = true;
        
        // H3 uses default font but no separator
        //mdConfig.headingFormats[2].font = NULL; // Or igGetDefaultFont()
        //mdConfig.headingFormats[2].separator = false;

        // mdConfig.formatCallback = MyCustomFormatCallback; // If you have one

        if (licence_markdown) {
            ImGuiMarkdown(licence_markdown, strlen(licence_markdown), &mdConfig);
        } else {
            FILE* file = fopen("LICENCE", "r");
            if (file) {
                fseek(file, 0, SEEK_END);
                long length = ftell(file);
                fseek(file, 0, SEEK_SET);
                licence_markdown = (char*)malloc(length + 1);
                if (licence_markdown) {
                    fread(licence_markdown, 1, length, file);
                    licence_markdown[length] = '\0'; // Null-terminate
                }
                fclose(file);
            } else {
                igText("Error loading licence.");
            }
        }
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
    // ui_showLog = false; // Log is often useful to keep visible
    ui_showDisassembler = false;
}

static void UI_DebugToolbar(NES* nes) {
    if (!ui_showToolbar) return;

    // This toolbar is a regular dockable window.
    if (igBegin("Debug Controls", &ui_showToolbar, ImGuiWindowFlags_None )) { // Removed restrictive flags to allow docking/resizing
        const char* pause_label = ui_paused ? "Resume (F6)" : "Pause (F6)";
        if (igButton(pause_label, (ImVec2){0,0})) UI_TogglePause(nes);
        igSameLine(0, 4);
        if (igButton("Step CPU (F7)", (ImVec2){0,0})) { if (ui_paused && nes && nes->cpu) NES_Step(nes); else UI_Log("Can only step CPU when paused."); }
        igSameLine(0, 4);
        if (igButton("Step Frame (F8)", (ImVec2){0,0})) { if (ui_paused && nes) UI_StepFrame(nes); else UI_Log("Can only step frame when paused.");}
        igSameLine(0, 4);
        if (igButton("Reset (F5)", (ImVec2){0,0})) UI_Reset(nes);
        
        igSeparator(); // Visually separate control groups
        
        // Toggles for debug windows (can also be in View menu)
        igCheckbox("CPU", &ui_showCpuWindow); igSameLine(0,4);
        igCheckbox("PPU", &ui_showPpuViewer); igSameLine(0,4);
        igCheckbox("Memory", &ui_showMemoryViewer); igSameLine(0,4);
        igCheckbox("Log", &ui_showLog); igSameLine(0,4);
        igCheckbox("Disasm", &ui_showDisassembler);
    }
    igEnd();
}

static void UI_DrawDebugMenu() {
    if (igBeginMenu("Debug", true)) {
        igMenuItem_Bool("CPU Registers", NULL, &ui_showCpuWindow, true);
        igMenuItem_Bool("PPU Viewer", NULL, &ui_showPpuViewer, true);
        igMenuItem_Bool("Memory Viewer", NULL, &ui_showMemoryViewer, true);
        igMenuItem_Bool("Log Window", NULL, &ui_showLog, true);
        igMenuItem_Bool("Disassembler", NULL, &ui_showDisassembler, true);
        igSeparator();
        igMenuItem_Bool("Debug Controls Window", NULL, &ui_showToolbar, true); // Renamed from Toolbar
        igSeparator();
        if (igMenuItem_Bool("Show All Debug Windows", NULL, false, true)) UI_ShowAllDebugWindows();
        if (igMenuItem_Bool("Hide All Debug Windows", NULL, false, true)) UI_HideAllDebugWindows();
        igEndMenu();
    }
}

static void UI_CpuWindow(NES* nes) {
    if (!ui_showCpuWindow) return;
    if (!nes || !nes->cpu) {
         if (ui_showCpuWindow && (!nes || !nes->cpu)) ui_showCpuWindow = false;
        return;
    }
    if (igBegin("CPU Registers", &ui_showCpuWindow, ImGuiWindowFlags_None)) { 
        igText("A:  0x%02X (%3d)", nes->cpu->a, nes->cpu->a); // %3d for consistent spacing
        igText("X:  0x%02X (%3d)", nes->cpu->x, nes->cpu->x);
        igText("Y:  0x%02X (%3d)", nes->cpu->y, nes->cpu->y);
        igText("SP: 0x01%02X", nes->cpu->sp); 
        igText("PC: 0x%04X", nes->cpu->pc);
        
        igText("Status: 0x%02X [", nes->cpu->status);
        igSameLine(0,0);
        const char* flag_names = "NV-BDIZC"; // Bit 5 is often shown as '-', though it has a value in the register
        for (int i = 7; i >= 0; i--) { 
            bool is_set = (nes->cpu->status >> i) & 1;
            // Bit 5 ('-') is conventionally shown as set if its bit in the status byte is 1.
            // The B flag (bit 4) has two meanings depending on context (interrupt vs PHP/BRK).
            // Here we just show the raw status register bits.
            
            if (is_set) {
                igTextColored((ImVec4){0.3f, 1.0f, 0.3f, 1.0f}, "%c", flag_names[7-i]); // Green for set
            } else {
                igTextColored((ImVec4){1.0f, 0.4f, 0.4f, 1.0f}, "%c", flag_names[7-i]); // Red for clear
            }
            if (i > 0) igSameLine(0,2);
        }
        igSameLine(0,0); igText("]");
        igNewLine();
        igText("Total Cycles: %llu", (unsigned long long)nes->cpu->total_cycles);
        // REFACTOR-NOTE: Add display for pending interrupts (NMI, IRQ lines state from bus/CPU).
        // REFACTOR-NOTE: Add instruction timing/cycle count for current/last instruction (requires more detailed CPU state).
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
            // REFACTOR-NOTE: Add speed controls (e.g., 50%, 100%, 200%, turbo mode). Would require timing adjustments in main loop.
            igEndMenu();
        }
        if (igBeginMenu("View", true)) {
            igMenuItem_Bool("Game Screen", NULL, &ui_showGameScreen, true); // Toggle visibility of game screen window
            igMenuItem_Bool("CPU Registers", NULL, &ui_showCpuWindow, true);
            igMenuItem_Bool("PPU Viewer", NULL, &ui_showPpuViewer, true);
            igMenuItem_Bool("Memory Viewer", NULL, &ui_showMemoryViewer, true);
            igMenuItem_Bool("Disassembler", NULL, &ui_showDisassembler, true);
            igMenuItem_Bool("Log Window", NULL, &ui_showLog, true);
            igMenuItem_Bool("Debug Controls", NULL, &ui_showToolbar, true); // Matches window title
            igSeparator();
            if (igMenuItem_Bool("Toggle Fullscreen", "F11", ui_sdl_fullscreen, true)) UI_ToggleFullscreen();
            igEndMenu();
        }
        UI_DrawDebugMenu(); 
        if (igBeginMenu("Options", true)) {
            if (igMenuItem_Bool("Settings...", "F10", ui_showSettingsWindow, true)) ui_showSettingsWindow = !ui_showSettingsWindow;
            igEndMenu();
        }
        if (igBeginMenu("Help", true)) {
            if (igMenuItem_Bool("About", NULL, false, true)) ui_showAboutWindow = true;
            if (igMenuItem_Bool("Credits", NULL, false, true)) ui_showCreditsWindow = true;
            if (igMenuItem_Bool("Licence", NULL, false, true)) ui_showLicenceWindow = true;
            // REFACTOR-NOTE: Add "View Controls" or "Help Topics" menu item with keybinds, basic usage.
            igEndMenu();
        }
        igEndMainMenuBar();
    }
}


void UI_GameScreenWindow(NES* nes)
{
    if (!ui_showGameScreen) return;

    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){0,0});
    if (igBegin("Game Screen", &ui_showGameScreen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (!gpu_device) {
            igTextColored((ImVec4){1.f,1.f,0.f,1.f}, "Error: No SDL_GPUDevice available.");
        } else if (!nes || !nes->ppu || !nes->ppu->framebuffer) {
            ImVec2 avail_size; 
            igGetContentRegionAvail(&avail_size);
            const char* msg = "No ROM loaded or PPU not ready.";
            ImVec2 text_size; 
            igCalcTextSize(&text_size, msg, NULL, false, 0.0f);
            igSetCursorPosX((avail_size.x - text_size.x) * 0.5f);
            igSetCursorPosY((avail_size.y - text_size.y) * 0.5f);
            igTextDisabled("%s", msg);
        } else {
            // Create texture if needed
            if (ppu_game_texture == NULL) {
                SDL_GPUTextureCreateInfo texture_info = {0};
                texture_info.type = SDL_GPU_TEXTURETYPE_2D;
                texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                texture_info.width = 256;
                texture_info.height = 240;
                texture_info.layer_count_or_depth = 1;
                texture_info.num_levels = 1;
                texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
                ppu_game_texture = SDL_CreateGPUTexture(gpu_device, &texture_info);
                if (!ppu_game_texture) {
                    UI_Log("GameScreen: Failed to create PPU game texture: %s", SDL_GetError());
                    igTextColored((ImVec4){1.f,0.f,0.f,1.f}, "Failed to create PPU game texture.");
                    igEnd(); igPopStyleVar(1); return;
                }

                ppu_game_texture_sampler_binding.texture = ppu_game_texture;
            }

            // Create sampler if needed
            if (ppu_game_sampler == NULL) {
                SDL_GPUSamplerCreateInfo sampler_info = {0};
                sampler_info.min_filter = SDL_GPU_FILTER_NEAREST;
                sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;
                sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
                sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                ppu_game_sampler = SDL_CreateGPUSampler(gpu_device, &sampler_info);
                if (!ppu_game_sampler) {
                    UI_Log("GameScreen: Failed to create PPU game sampler: %s", SDL_GetError());
                    igTextColored((ImVec4){1.f,0.f,0.f,1.f}, "Failed to create PPU game sampler.");
                    igEnd(); igPopStyleVar(1); return;
                }

                ppu_game_texture_sampler_binding.sampler = ppu_game_sampler;
            }

            // Create transfer buffer if needed
            if (ppu_game_transfer_buffer == NULL) {
                SDL_GPUTransferBufferCreateInfo transfer_create_info = {0};
                transfer_create_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                transfer_create_info.size = 256 * 240 * 4; // Only need space for one frame
                ppu_game_transfer_buffer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_create_info);
                if (!ppu_game_transfer_buffer) {
                    UI_Log("GameScreen: Failed to create PPU transfer buffer: %s", SDL_GetError());
                    igTextColored((ImVec4){1.f,0.f,0.f,1.f}, "Failed to create PPU transfer buffer.");
                    igEnd(); igPopStyleVar(1); return;
                }
            }

            // Update texture if we have new frame data
            if (ppu_game_texture && nes->ppu->framebuffer) {
                // Map transfer buffer
                void* mapped_memory = SDL_MapGPUTransferBuffer(gpu_device, ppu_game_transfer_buffer, false);
                if (!mapped_memory) {
                    UI_Log("GameScreen: Failed to map GPU transfer buffer: %s", SDL_GetError());
                } else {
                    // Copy frame data to transfer buffer
                    memcpy(mapped_memory, nes->ppu->framebuffer, 256 * 240 * sizeof(uint32_t));
                    SDL_UnmapGPUTransferBuffer(gpu_device, ppu_game_transfer_buffer);

                    // Create command buffer for the copy operation
                    SDL_GPUCommandBuffer* cmd_buffer = SDL_AcquireGPUCommandBuffer(gpu_device);
                    if (cmd_buffer) {
                        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd_buffer);
                        if (copy_pass) {
                            SDL_UploadToGPUTexture(
                                copy_pass,
                                &(SDL_GPUTextureTransferInfo){
                                    .transfer_buffer = ppu_game_transfer_buffer,
                                    .offset = 0,
                                    .pixels_per_row = 256,
                                    .rows_per_layer = 240
                                },
                                &(SDL_GPUTextureRegion){
                                    .texture = ppu_game_texture,
                                    .x = 0,
                                    .y = 0,
                                    .z = 0,
                                    .w = 256,
                                    .h = 240,
                                    .d = 1
                                },
                                false
                            );
                            SDL_EndGPUCopyPass(copy_pass);
                        } else {
                            UI_Log("GameScreen: Failed to begin GPU copy pass: %s", SDL_GetError());
                        }
                        SDL_SubmitGPUCommandBuffer(cmd_buffer);
                    } else {
                        UI_Log("GameScreen: Failed to acquire GPU command buffer for texture upload: %s", SDL_GetError());
                    }
                }
            }

            // Calculate display size maintaining aspect ratio
            ImVec2 window_content_region_size;
            igGetContentRegionAvail(&window_content_region_size);
            float aspect_ratio = 256.0f / 240.0f;
            ImVec2 image_size = window_content_region_size;

            if (window_content_region_size.x / aspect_ratio > window_content_region_size.y) {
                image_size.x = window_content_region_size.y * aspect_ratio;
            } else {
                image_size.y = window_content_region_size.x / aspect_ratio;
            }

            // Center the image
            float offset_x = (window_content_region_size.x - image_size.x) * 0.5f;
            float offset_y = (window_content_region_size.y - image_size.y) * 0.5f;
            igSetCursorPos((ImVec2){igGetCursorPosX() + offset_x, igGetCursorPosY() + offset_y});

            // Display the texture
            if (ppu_game_texture_sampler_binding.texture && ppu_game_texture_sampler_binding.sampler) {
                igImage((ImTextureID)(uintptr_t)&ppu_game_texture_sampler_binding, image_size, (ImVec2){0, 0}, (ImVec2){1, 1});
            } else {
                igText("Game texture or sampler not ready.");
            }
        }
    }
    igEnd();
    igPopStyleVar(1);
}

void UI_DrawStatusBar(NES* nes) 
{
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize;

    ImGuiViewport* viewport = igGetMainViewport();
    igSetNextWindowSize((ImVec2){viewport->WorkSize.x, 28}, ImGuiCond_Always);
    igSetNextWindowPos((ImVec2){viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - 28}, ImGuiCond_Always, (ImVec2){0,0});

    if (igBegin("Status Bar", NULL, flags))  {
        // Calculate FPS (simple moving average or per-second updates)
        static uint32_t frame_count = 0;
        static uint32_t last_fps_time = 0;
        uint32_t current_ticks = SDL_GetTicks();
        frame_count++;
        if (current_ticks - last_fps_time >= 1000) {
            ui_fps = (float)frame_count / ((current_ticks - last_fps_time)/1000.0f);
            last_fps_time = current_ticks;
            frame_count = 0;
        }
        
        igText("FPS: %.1f | ROM: %s | %s", ui_fps, ui_currentRomName, ui_paused ? "Paused" : "Running");
        
        const char* version_text = "cNES v0.2"; // REFACTOR-NOTE: Consistent versioning
        ImVec2 version_text_size;
        igCalcTextSize(&version_text_size, version_text, NULL, false, 0);
        
        ImVec2 content_avail;
        igGetContentRegionAvail(&content_avail);
        
        // Align version text to the right
        float version_pos_x = content_avail.x - version_text_size.x - igGetStyle()->ItemSpacing.x;
        if (version_pos_x > igGetCursorPosX()) { // Ensure it doesn't overlap with left text
             igSameLine(version_pos_x, 0);
        } else { // Not enough space, just put it on the same line if possible
             igSameLine(0, igGetStyle()->ItemSpacing.x * 2);
        }
        igTextDisabled("%s", version_text);
    }
    igEnd();

    //igSetWindowSize_Str("Status Bar", (ImVec2){igGetMainViewport()->WorkSize.x, 124}, ImGuiCond_Always);
}

void UI_Draw(NES* nes) {
    ImGuiViewport* viewport = igGetMainViewport();
    igSetNextWindowPos(viewport->WorkPos, ImGuiCond_Always, (ImVec2){0,0});
    igSetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);   
    igSetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 0.0f);
    igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 0.0f);
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});
    
    igBegin("cEMU_MainHost", NULL, host_flags);
    igPopStyleVar(3);

    ImGuiID dockspace_id = igGetID_Str("cEMU_DockSpace_Main");
    igDockSpace(dockspace_id, (ImVec2){0.0f, 0.0f}, ImGuiDockNodeFlags_PassthruCentralNode, NULL);

    if (ui_first_frame) {
        ui_first_frame = false;
        igDockBuilderRemoveNode(dockspace_id); 
        igDockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        igDockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

        ImGuiID dock_main_id = dockspace_id; 
        ImGuiID dock_id_statusbar;
        ImGuiID dock_id_log;
        ImGuiID dock_id_right;
        ImGuiID dock_id_left;

        // Split layout: Status bar at bottom, then Log above status, then main area split left/center/right
        dock_id_statusbar = igDockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.0f, NULL, &dock_main_id);
        dock_id_log = igDockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.15f, NULL, &dock_main_id); // Log takes 15% of remaining
        dock_id_right = igDockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, NULL, &dock_main_id); // Right panel 25%
        dock_id_left = igDockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.22f, NULL, &dock_main_id);  // Left panel 22%
        // Center panel is what remains of dock_main_id

        igDockBuilderDockWindow("Status Bar", dock_id_statusbar);
        igDockBuilderDockWindow("Log", dock_id_log);
        igDockBuilderDockWindow("Game Screen", dock_main_id); 
        igDockBuilderDockWindow("CPU Registers", dock_id_left);
        igDockBuilderDockWindow("Disassembler", dock_id_left); 
        igDockBuilderDockWindow("PPU Viewer", dock_id_right);
        igDockBuilderDockWindow("Memory Viewer (CPU Bus)", dock_id_right);
        igDockBuilderDockWindow("Debug Controls", dock_id_left); // Add debug controls to left panel too

        // Make status bar non-interactive for docking/resizing
        ImGuiDockNode* status_node = igDockBuilderGetNode(dock_id_statusbar);
        if (status_node) ImGuiDockNode_SetLocalFlags(status_node, ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoResize | ImGuiDockNodeFlags_NoWindowMenuButton);
        ImGuiDockNode* game_node = igDockBuilderGetNode(dock_main_id);
        if (game_node) ImGuiDockNode_SetLocalFlags(game_node, ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoUndocking);

        igDockBuilderFinish(dockspace_id);
        
        // Ensure windows part of the default layout are initially set to visible
        ui_showGameScreen = true;
        ui_showCpuWindow = true;
        ui_showDisassembler = true;
        ui_showPpuViewer = true;
        ui_showMemoryViewer = true;
        ui_showLog = true;
        ui_showToolbar = true; // For Debug Controls window
    }

    UI_DrawMainMenuBar(nes);

    igEnd(); // End of "cEMU_MainHost"

    // --- Draw all dockable windows ---
    if (ui_showGameScreen) UI_GameScreenWindow(nes); // Must be called for docking to work, even if hidden by user later
    if (ui_showCpuWindow) UI_CpuWindow(nes);
    //if (ui_showPpuViewer) UI_PPUViewer(nes);
    if (ui_showLog) UI_LogWindow();
    if (ui_showMemoryViewer) UI_MemoryViewer(nes);
    if (ui_showDisassembler) UI_DrawDisassembler(nes);
    if (ui_showToolbar) UI_DebugToolbar(nes); // Debug Controls window
    
    UI_DrawStatusBar(nes);

    // --- Modals and non-docked utility windows ---
    if (ui_showSettingsWindow) UI_SettingsWindow(nes);
    if (ui_showAboutWindow) UI_DrawAboutWindow();
    if (ui_showCreditsWindow) UI_DrawCreditsWindow();
    if (ui_showLicenceWindow) UI_DrawLicenceWindow();
}


bool ui_quit_requested = false; 

void UI_Update(NES* nes) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL3_ProcessEvent(&e); 

        if (e.type == SDL_EVENT_QUIT) {
            ui_quit_requested = true;
        }
        if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window)) {
             ui_quit_requested = true;
        }

        if (e.type == SDL_EVENT_KEY_DOWN && !ioptr->WantCaptureKeyboard) {
            bool ctrl_pressed = (SDL_GetModState() & SDL_KMOD_CTRL);
            // bool alt_pressed = (SDL_GetModState() & KMOD_ALT); // For Alt+F4, handled by SDL_EVENT_QUIT usually

            if (e.key.key == SDLK_F5 && nes) UI_Reset(nes);
            if (e.key.key == SDLK_F6 && nes) UI_TogglePause(nes);
            if (e.key.key == SDLK_F7 && nes && nes->cpu && ui_paused) NES_Step(nes);
            if (e.key.key == SDLK_F8 && nes && ui_paused) UI_StepFrame(nes);    
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
        if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) && !ioptr->WantCaptureKeyboard) {
            UI_HandleInputEvent(&e);
        }
    }

    if (ui_quit_requested) { 
        UI_Shutdown(); // Shutdown is called by main application loop before exit
        exit(0); // Let main loop handle exit
        return; 
    }

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    igNewFrame();

    if (nes) { 
        NES_SetController(nes, 0, nes_input_state[0]);
        NES_SetController(nes, 1, nes_input_state[1]);

        if(!ui_paused) {
            NES_StepFrame(nes); 
        }
    }

    // igShowDemoWindow(NULL); // Uncomment for ImGui debugging

    UI_Draw(nes); 

    ImPlot_ShowDemoWindow(NULL); // Uncomment for ImPlot debugging

    // Rendering with SDL_gpu
    igRender();
    ImDrawData* draw_data = igGetDrawData();
    const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

    SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(gpu_device); 

    SDL_GPUTexture* swapchain_texture = NULL; // Must be initialized to NULL
    SDL_AcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, NULL, NULL); 

    if (swapchain_texture != NULL && !is_minimized) 
    {
        Imgui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);
        SDL_GPUColorTargetInfo target_info = {0}; // Important to zero-initialize
        target_info.texture = swapchain_texture;
        target_info.clear_color.r = clear_color.x; // Uses global clear_color set by theme
        target_info.clear_color.r = clear_color.x; // Uses global clear_color set by theme
        target_info.clear_color.g = clear_color.y;
        target_info.clear_color.b = clear_color.z;
        target_info.clear_color.a = clear_color.w;
        target_info.load_op = SDL_GPU_LOADOP_CLEAR;
        target_info.store_op = SDL_GPU_STOREOP_STORE;

        target_info.mip_level = 0;
        target_info.layer_or_depth_plane = 0;
        target_info.cycle = false;
        target_info.resolve_texture = NULL;
        target_info.resolve_mip_level = 0;
        target_info.resolve_layer = 0;
        target_info.cycle_resolve_texture = false;
        target_info.padding1 = 0;
        target_info.padding2 = 0;
        // mip_level, layer_or_depth_plane, cycle, resolve_texture etc. are 0/false/NULL by default from zero-init

        SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(command_buffer, &target_info, 1, NULL);
        if (render_pass) { // Check if render pass began successfully
            ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass, NULL);
            SDL_EndGPURenderPass(render_pass);
        } else {
            UI_Log("Failed to begin GPU render pass: %s", SDL_GetError());
        }
    } else if (is_minimized) {
        // Window is minimized, nothing to render to swapchain.
        // SDL_gpu handles this internally, but good to be aware.
    } else if (swapchain_texture == NULL) {
         UI_Log("Failed to acquire swapchain texture: %s", SDL_GetError());
    }

    SDL_SubmitGPUCommandBuffer(command_buffer); // This also presents the frame

    // Update and Render additional Platform Windows (for multi-viewport support)
    if (ioptr->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        igUpdatePlatformWindows();
        igRenderPlatformWindowsDefault(NULL,NULL); // This should work with SDL_gpu backend
    }
}


uint8_t UI_PollInput(int controller) {
    if (controller < 0 || controller > 1) return 0;
    return nes_input_state[controller];
}

void UI_Shutdown() {
    // REFACTOR-NOTE: Save recent ROMs list, window positions/docking layout (imgui.ini handles docking if enabled).
    // Consider saving settings (theme, volume) to a config file.

    DEBUG_INFO("Shutting down UI");

    // Wait for GPU to finish any pending operations
    if (gpu_device) {
        SDL_WaitForGPUIdle(gpu_device);
    }

    // Shutdown ImGui backends
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    igDestroyContext(NULL); // Pass the context if you stored it, NULL for default

    // Release Game Screen GPU resources
    if (gpu_device) { 
        if (ppu_game_transfer_buffer) {
            SDL_ReleaseGPUTransferBuffer(gpu_device, ppu_game_transfer_buffer);
            ppu_game_transfer_buffer = NULL;
        }
        if (ppu_game_sampler) {
            SDL_ReleaseGPUSampler(gpu_device, ppu_game_sampler);
            ppu_game_sampler = NULL;
        }
        if (ppu_game_texture) {
            SDL_ReleaseGPUTexture(gpu_device, ppu_game_texture);
            ppu_game_texture = NULL;
        }

        // Release PPU Viewer GPU resources
        if (pt_transfer_buffer) {
            SDL_ReleaseGPUTransferBuffer(gpu_device, pt_transfer_buffer);
            pt_transfer_buffer = NULL;
        }
        if (pt_sampler) {
            SDL_ReleaseGPUSampler(gpu_device, pt_sampler);
            pt_sampler = NULL;
        }
        if (pt_texture0) {
            SDL_ReleaseGPUTexture(gpu_device, pt_texture0);
            pt_texture0 = NULL;
        }
        if (pt_texture1) {
            SDL_ReleaseGPUTexture(gpu_device, pt_texture1);
            pt_texture1 = NULL;
        }
        
        // Destroy GPU device
        SDL_DestroyGPUDevice(gpu_device);
        gpu_device = NULL;
    }

    // Destroy window and quit SDL
    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    SDL_Quit();

    DEBUG_INFO("UI Shutdown complete");
}
