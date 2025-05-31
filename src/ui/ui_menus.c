#include <stdio.h>
#include <float.h>
#include <dirent.h>
#include <string.h>
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>
#include <SDL3/SDL.h>
#include "ui/cimgui_markdown.h"

#include "debug.h"

#include "cNES/version.h"
#include "cNES/nes.h"
#include "cNES/rom.h"

#include "ui/ui.h"


static const SDL_DialogFileFilter filters[] = {
    {"NES ROMs", "nes;fds;unif;nes2;ines"},
    {"Archives", "zip;7z;rar"}, // REFACTOR-NOTE: Archive handling would need external libraries. This filter is just for selection.
    {"All files", "*"}};

static void SDLCALL FileDialogCallback(void *userdata, const char *const *filelist, int filter_index)
{
    if (!filelist || !*filelist)
    {
        if (SDL_GetError() && strlen(SDL_GetError()) > 0)
        {
            DEBUG_ERROR("SDL File Dialog Error: %s", SDL_GetError());
            // SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "File Dialog Error", SDL_GetError(), window);
        }
        else
        {
            DEBUG_WARN("No file selected or dialog cancelled.");
        }
        return;
    }

    const char *selected_file = *filelist;
    if (selected_file)
    {
        if (NES_Load((NES *)userdata, ROM_LoadFile(selected_file)) == 0)
        {
            DEBUG_INFO("Loaded ROM: %s", selected_file);
            NES_Reset((NES *)userdata);
            // UI_AddRecentRom(path);
        }
        else
        {
            DEBUG_WARN("Failed to load ROM: %s", selected_file);
        }
    }
}

void UI_DrawFileMenu(NES *nes)
{
    if (igBeginMenu("File", true))
    {
        if (igMenuItem_Bool("Open ROM...", "Ctrl+O", false, true))
        {
            SDL_ShowOpenFileDialog(FileDialogCallback, nes, ui_window, filters, SDL_arraysize(filters), NULL, false);
        }

        if (igBeginMenu("Recent ROMs", false)) // ui_recentRomsCount > 0))
        {
            /*
            for (int i = 0; i < ui_recentRomsCount; ++i)
            {
                char label[270];
                const char *filename_display = strrchr(ui_recentRoms[i], '/');
                if (!filename_display)
                    filename_display = strrchr(ui_recentRoms[i], '\\');
                filename_display = filename_display ? filename_display + 1 : ui_recentRoms[i];
                snprintf(label, sizeof(label), "%d. %s", i + 1, filename_display);

                if (igMenuItem_Bool(label, NULL, false, true))
                {
                    strncpy(ui_romPath, ui_recentRoms[i], sizeof(ui_romPath) - 1);
                    ui_romPath[sizeof(ui_romPath) - 1] = '\0';
                    UI_LoadRom(nes, ui_romPath);
                }
            }
                */
            igEndMenu();
        }
        igSeparator();
        bool rom_loaded_for_state = nes && nes->rom;
        if (igMenuItem_Bool("Save State...", "Ctrl+S", false, rom_loaded_for_state))
        {
            // ui_openSaveStateModal = true;
        }
        if (igMenuItem_Bool("Load State...", "Ctrl+L", false, rom_loaded_for_state))
        {
            // ui_openLoadStateModal = true;
        }
        igSeparator();
        if (igMenuItem_Bool("Exit", "Alt+F4", false, true))
        {
            SDL_Event quit_event;
            quit_event.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit_event);
        }
        igEndMenu();
    }

    // REFACTOR-NOTE: Save/Load state modals are placeholders. Actual implementation requires NES_SaveState/NES_LoadState.
    if (false) //(ui_openSaveStateModal)
    {
        /*
        igOpenPopup_Str("Save State", 0);
        if (igBeginPopupModal("Save State", &ui_openSaveStateModal, ImGuiWindowFlags_AlwaysAutoResize))
        {
            igText("Select Slot (0-9):");
            igSameLine(0, 0);
            igSetNextItemWidth(100);
            igInputInt("##SaveSlot", &ui_selectedSaveLoadSlot, 1, 1, 0);
            ui_selectedSaveLoadSlot = ui_selectedSaveLoadSlot < 0 ? 0 : (ui_selectedSaveLoadSlot > 9 ? 9 : ui_selectedSaveLoadSlot);

            if (igButton("Save", (ImVec2){80, 0}))
            {
                UI_Log("Placeholder: Save state to slot %d for ROM: %s", ui_selectedSaveLoadSlot, nes->rom->name);
                // if (nes) NES_SaveState(nes, ui_selectedSaveLoadSlot); // Actual call
                ui_openSaveStateModal = false;
                igCloseCurrentPopup();
            }
            igSameLine(0, 8);
            if (igButton("Cancel", (ImVec2){80, 0}))
            {
                ui_openSaveStateModal = false;
                igCloseCurrentPopup();
            }
            igEndPopup();
        }
            */
    }
    if (false) //(ui_openLoadStateModal)
    {
        /*
        igOpenPopup_Str("Load State", 0);
        if (igBeginPopupModal("Load State", &ui_openLoadStateModal, ImGuiWindowFlags_AlwaysAutoResize))
        {
            igText("Select Slot (0-9):");
            igSameLine(0, 0);
            igSetNextItemWidth(100);
            igInputInt("##LoadSlot", &ui_selectedSaveLoadSlot, 1, 1, 0);
            ui_selectedSaveLoadSlot = ui_selectedSaveLoadSlot < 0 ? 0 : (ui_selectedSaveLoadSlot > 9 ? 9 : ui_selectedSaveLoadSlot);

            if (igButton("Load", (ImVec2){80, 0}))
            {
                UI_Log("Placeholder: Load state from slot %d for ROM: %s", ui_selectedSaveLoadSlot, nes->rom->name);
                // if (nes) NES_LoadState(nes, ui_selectedSaveLoadSlot); // Actual call
                ui_openLoadStateModal = false;
                igCloseCurrentPopup();
            }
            igSameLine(0, 8);
            if (igButton("Cancel", (ImVec2){80, 0}))
            {
                ui_openLoadStateModal = false;
                igCloseCurrentPopup();
            }
            igEndPopup();
        }
            */
    }
}

void UI_SettingsMenu(NES *nes)
{
    if (!ui_showSettingsWindow)
        return;

    ImVec2 viewportSize = igGetMainViewport()->WorkSize;
    ImVec2 windowSize = {viewportSize.x * 2.0f / 3.0f, viewportSize.y * 2.0f / 3.0f};
    igSetNextWindowSize(windowSize, ImGuiCond_Once);
    igSetNextWindowPos((ImVec2){(viewportSize.x - windowSize.x) / 2, (viewportSize.y - windowSize.y) / 2}, ImGuiCond_Once, (ImVec2){0, 0});

    if (igBegin("Settings", &ui_showSettingsWindow, ImGuiWindowFlags_None))
    {
        if (igBeginTabBar("SettingsTabs", 0))
        {
            
            if (igBeginTabItem("General", NULL, 0))
            {
                const char *themes[] = {"Default", "Dark", "Light"};
                int selected_theme = ui_current_theme; // Assuming ui_current_theme is an integer
                if (igCombo_Str_arr("Theme", &selected_theme, themes, SDL_arraysize(themes), 8))
                {
                    switch (selected_theme)
                    {
                    case 0:
                        ui_current_theme = UI_THEME_DARK; // Default is dark
                        ui_requestReload = true; // Flag to reload UI on next frame
                        break;
                    case 1:
                        ui_current_theme = UI_THEME_DARK; // Dark theme
                        ui_requestReload = true; // Flag to reload UI on next frame
                        break;
                    case 2:
                        ui_current_theme = UI_THEME_LIGHT; // Light theme
                        ui_requestReload = true; // Flag to reload UI on next frame
                        break;
                    }
                }
                igSeparator();
                if (igCheckbox("Fullscreen (Window)", &ui_fullscreen))
                    UI_ToggleFullscreen();
                igSameLine(0, 10);
                igTextDisabled("(F11)");

                igSeparator();
                igText("Font Settings:");
                if (igSliderFloat("Font Size", &ui_font_size, 8.0f, 32.0f, "%1.0f", 0))
                {
                    ui_requestReload = true; // Flag to reload fonts on next frame
                    //UI_ApplyTheme(ui_current_theme); // Assuming a function to apply font size
                }
                igTextDisabled("Adjust the font size for better readability.");
                igSeparator(); // Visually separate font size from font family selection
                igText("Font Family:");

                // Display current font and a button to reset to default
                const char* display_font_name = "Default";
                if (ui_font_path && *ui_font_path) {
                    // Extract filename from ui_font_path for display
                    // Handles paths with either '/' or '\'
                    const char *name_part_bslash = strrchr(ui_font_path, '\\');
                    const char *name_part_fslash = strrchr(ui_font_path, '/');
                    const char *name_part = name_part_bslash > name_part_fslash ? name_part_bslash : name_part_fslash;
                    display_font_name = name_part ? name_part + 1 : ui_font_path;
                }
                igText("Current: %s", display_font_name);
                igSameLine(0, 10.0f);
                if (igSmallButton("Use Default")) {
                    if (ui_font_path) {
                        free(ui_font_path); // Assuming ui_font_path is allocated with strdup/malloc
                        ui_font_path = NULL; // Assuming NULL tells UI_LoadFont to use default
                    }
                    ui_requestReload = true; // Signal that font settings changed
                }

                // Prepare for font dropdown
                const char *font_dir_scan_path = "data/fonts/";
                const char *font_dir_store_prefix = "data\\fonts\\";
                char *font_files[128]; // Max 128 font files
                char *font_display_names[128]; // Display names without extension
                int font_count = 0;
                int current_font_index = -1;

                DIR *dir;
                struct dirent *entry;

                if ((dir = opendir(font_dir_scan_path)) != NULL)
                {
                    while ((entry = readdir(dir)) != NULL && font_count < (int)SDL_arraysize(font_files))
                    {
                        #if defined(_DIRENT_HAVE_D_TYPE) && defined(DT_REG) && defined(DT_UNKNOWN)
                        if (entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN) {
                            continue;
                        }
                        #endif

                        const char *ext = strrchr(entry->d_name, '.');
                        if (ext && (strcmp(ext, ".ttf") == 0))
                        {
                            char full_store_path[512];
                            snprintf(full_store_path, sizeof(full_store_path), "%s%s", font_dir_scan_path, entry->d_name);
                            font_files[font_count] = strdup(full_store_path);

                            // Create display name without extension
                            size_t name_len = strlen(entry->d_name);
                            size_t ext_len = strlen(ext);
                            font_display_names[font_count] = (char*)malloc(name_len - ext_len + 1);
                            if (font_display_names[font_count]) {
                                strncpy(font_display_names[font_count], entry->d_name, name_len - ext_len);
                                font_display_names[font_count][name_len - ext_len] = '\0';
                            } else {
                                // Fallback if malloc fails, though unlikely for small strings
                                font_display_names[font_count] = strdup(entry->d_name);
                            }


                            if (ui_font_path && strcmp(ui_font_path, full_store_path) == 0)
                            {
                                current_font_index = font_count;
                            }
                            font_count++;
                        }
                    }
                    closedir(dir);
                }
                else
                {
                    igTextDisabled("Error: Cannot open font directory at '%s'.", font_dir_scan_path);
                }

                if (font_count > 0)
                {
                    // Convert char* array to const char* array for igCombo
                    const char *combo_font_display_names[128];
                    for(int i=0; i < font_count; ++i) {
                        combo_font_display_names[i] = font_display_names[i];
                    }

                    if (igCombo_Str_arr("Select Font", &current_font_index, combo_font_display_names, font_count, 6))
                    {
                        if (current_font_index >= 0 && current_font_index < font_count)
                        {
                            if (ui_font_path) {
                                //free(ui_font_path);
                            }
                            ui_font_path = strdup(font_files[current_font_index]);
                            ui_requestReload = true;
                            DEBUG_INFO("UI: Font selection changed to: %s", ui_font_path);
                        }
                    }
                } else if (dir != NULL) { // dir was opened but no fonts found
                     igTextDisabled("No .ttf fonts found in '%s'.", font_dir_scan_path);
                }


                // Free allocated memory for font names
                for (int i = 0; i < font_count; ++i)
                {
                    free(font_files[i]);
                    free(font_display_names[i]);
                }
                
                /*
                igText("Display Settings:");
                if (igCheckbox("Enable VSync", &ui_vsync_enabled))
                {
                    UI_ToggleVSync(ui_vsync_enabled); // Assuming a function to toggle VSync
                }
                igTextDisabled("Toggle vertical synchronization for smoother visuals.");

                if (igCheckbox("Integer Scaling", &ui_integer_scaling))
                {
                    UI_ToggleIntegerScaling(ui_integer_scaling); // Assuming a function to toggle integer scaling
                }
                igTextDisabled("Enable integer scaling for pixel-perfect rendering.");
                */
                igSeparator();
                igTextDisabled("More display settings (aspect ratio, scaling) can be added here.");
                igEndTabItem();
            }
            
            if (igBeginTabItem("Emulation", NULL, 0))
            {
                if (nes) // Ensure nes context and settings are valid
                {
                    // --- Region Setting ---
                    const char *region_items[] = {"NTSC", "PAL"}; // Add DENDY or other regions if supported by the core
                    // Assuming nes->settings->region is an enum NES_Region where NTSC=0, PAL=1
                    int selected_region_idx = (int)nes->settings.cpu_region;

                    if (igCombo_Str_arr("System Region", &selected_region_idx, region_items, SDL_arraysize(region_items), (int)SDL_arraysize(region_items)))
                    {
                        if (nes->settings.cpu_region != (NES_Region)selected_region_idx) // NES_Region cast assumes it's an enum
                        {
                            nes->settings.cpu_region = (NES_Region)selected_region_idx;
                            DEBUG_INFO("Region set to %s. A system reset (F5) is required for changes to take full effect.", region_items[selected_region_idx]);
                            // Consider adding a flag: ui_emulation_settings_changed_requires_reset = true;
                        }
                    }
                    igTextDisabled("Selects the target hardware region. Affects timing, refresh rate, and potentially color palette. Requires emulator reset.");
                    igSeparator();

                    // --- Sprite Limit Setting ---
                    // Assuming nes->settings->sprite_limit_enabled is a boolean field
                    bool sprite_limit_enabled = true;//nes->settings->sprite_limit_enabled;
                    if (igCheckbox("Enable Sprite Limit", &sprite_limit_enabled))
                    {
                        //nes->settings->sprite_limit_enabled = sprite_limit_enabled;
                        DEBUG_INFO("Sprite limit %s.", sprite_limit_enabled ? "enabled" : "disabled");
                        // This setting can often be changed on-the-fly without a full reset.
                    }
                    igTextDisabled("Toggles the original NES 8-sprite-per-scanline hardware limitation. Disabling may reduce sprite flicker in some games.");
                    igSeparator();
                    
                    // --- Four Score / Multi-tap Support ---
                    // Assuming nes->settings->four_score_enabled is a boolean field
                    /*
                    bool four_score_enabled = nes->settings->four_score_enabled;
                    if (igCheckbox("Enable Four Score (4-Player Adapter)", &four_score_enabled))
                    {
                        nes->settings->four_score_enabled = four_score_enabled;
                        UI_Log("Four Score adapter %s. Reset may be required.", four_score_enabled ? "enabled" : "disabled");
                    }
                    igTextDisabled("Enables support for 4-player games via the Four Score adapter. Requires reset.");
                    igSeparator();
                    */

                    // --- Zapper Connected ---
                    // Assuming nes->settings->zapper_connected is a boolean field
                    /*
                    bool zapper_connected = nes->settings->zapper_connected;
                    if (igCheckbox("Zapper Connected (Port 2)", &zapper_connected)) {
                        nes->settings->zapper_connected = zapper_connected;
                        UI_Log("Zapper %s on Port 2.", zapper_connected ? "connected" : "disconnected");
                    }
                    igTextDisabled("Simulates a Zapper light gun connected to controller port 2.");
                    igSeparator();
                    */

                    igTextDisabled("More emulation settings (e.g., initial RAM state, CPU/APU tweaks, mapper-specific options) can be added here.");
                }
                else
                {
                    igTextDisabled("Emulation settings are unavailable. Load a ROM and ensure the emulator context is active.");
                }
                igEndTabItem();
            }

            if (igBeginTabItem("Display", NULL, 0))
            {
                igText("Theme:");
                igSameLine(0, 5);
                if (igRadioButton_Bool("Dark", ui_current_theme == UI_THEME_DARK))
                    UI_ApplyTheme(UI_THEME_DARK);
                igSameLine(0, 5);
                if (igRadioButton_Bool("Light", ui_current_theme == UI_THEME_LIGHT))
                    UI_ApplyTheme(UI_THEME_LIGHT);
                igSeparator();
                if (igCheckbox("Fullscreen (Window)", &ui_fullscreen))
                    UI_ToggleFullscreen();
                igSameLine(0, 10);
                igTextDisabled("(F11)");
                // REFACTOR-NOTE: Add font scaling options, game screen aspect ratio/scaling options here.
                igTextDisabled("More display settings (font, scaling) can be added here.");
                igEndTabItem();
            }
            if (igBeginTabItem("Audio", NULL, 0))
            {
                // REFACTOR-NOTE: Connect this to actual APU volume control.
                if (igSliderFloat("Master Volume", &ui_master_volume, 0.0f, 1.0f, "%.2f", 0))
                {
                    // if (nes && nes->apu) APU_SetMasterVolume(nes->apu, ui_master_volume);
                    DEBUG_INFO("Master volume (placeholder) set to: %.2f", ui_master_volume);
                }
                // REFACTOR-NOTE: Add options for audio buffer size, sample rate, APU channel toggles.
                igTextDisabled("More audio settings (buffer, channels) can be added here.");
                igEndTabItem();
            }
            if (igBeginTabItem("Input", NULL, 0))
            {
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
        igSetCursorPosY(igGetWindowHeight() - igGetFrameHeightWithSpacing() - igGetStyle()->WindowPadding.y);
        if (igButton("Close", (ImVec2){-FLT_MIN, 0}))
            ui_showSettingsWindow = false;
    }
    igEnd();
}

void UI_DrawMainMenuBar(NES *nes)
{
    if (igBeginMainMenuBar())
    {
        UI_DrawFileMenu(nes);
        if (igBeginMenu("Emulation", true))
        {
            bool rom_loaded = nes && nes->rom && nes->rom;
            if (igMenuItem_Bool(ui_paused ? "Resume " : "Pause", "F6", false, rom_loaded))
                ui_paused = !ui_paused;
            if (igMenuItem_Bool("Reset", "F5", false, rom_loaded))
                NES_Reset(nes);
            if (igMenuItem_Bool("Step CPU Instruction", "F7", false, rom_loaded && ui_paused))
            {
                if (nes && nes->cpu)
                    NES_Step(nes);
            }
            if (igMenuItem_Bool("Step Frame", "F8", false, rom_loaded && ui_paused))
                NES_StepFrame(nes);
            // REFACTOR-NOTE: Add speed controls (e.g., 50%, 100%, 200%, turbo mode). Would require timing adjustments in main loop.
            igEndMenu();
        }
        if (igBeginMenu("View", true))
        {
            if (igMenuItem_Bool("Game Screen", NULL, ui_showGameScreen, true))
                ui_showGameScreen = !ui_showGameScreen;
            if (igMenuItem_Bool("CPU Registers", NULL, ui_showCpuWindow, true))
                ui_showCpuWindow = !ui_showCpuWindow;
            if (igMenuItem_Bool("PPU Viewer", NULL, ui_showPpuViewer, true))
                ui_showPpuViewer = !ui_showPpuViewer;
            if (igMenuItem_Bool("Memory Viewer", NULL, ui_showMemoryViewer, true))
                ui_showMemoryViewer = !ui_showMemoryViewer;
            if (igMenuItem_Bool("Disassembler", NULL, ui_showDisassembler, true))
                ui_showDisassembler = !ui_showDisassembler;
            if (igMenuItem_Bool("Log Window", NULL, ui_showLog, true))
                ui_showLog = !ui_showLog;
            if (igMenuItem_Bool("Debug Controls", NULL, ui_showToolbar, true))
                ui_showToolbar = !ui_showToolbar;
            if (igMenuItem_Bool("Profiler", NULL, ui_showProfilerWindow, true))
                ui_showProfilerWindow = !ui_showProfilerWindow;
            igSeparator();
            if (igMenuItem_Bool("Toggle Fullscreen", "F11", ui_fullscreen, true))
                UI_ToggleFullscreen();
            igEndMenu();
        }

        if (igBeginMenu("Debug", true))
        {
            igMenuItem_Bool("CPU Registers", NULL, &ui_showCpuWindow, true);
            igMenuItem_Bool("PPU Viewer", NULL, &ui_showPpuViewer, true);
            igMenuItem_Bool("Memory Viewer", NULL, &ui_showMemoryViewer, true);
            igMenuItem_Bool("Log Window", NULL, &ui_showLog, true);
            igMenuItem_Bool("Disassembler", NULL, &ui_showDisassembler, true);
            igMenuItem_Bool("Profiler", NULL, &ui_showProfilerWindow, true); // Added Profiler toggle
            igSeparator();
            igMenuItem_Bool("Debug Controls Window", NULL, &ui_showToolbar, true); // Renamed from Toolbar
            igSeparator();
            if (igMenuItem_Bool("Show All Debug Windows", NULL, false, true))
            {
                ui_showCpuWindow = true;
                ui_showPpuViewer = true;
                ui_showMemoryViewer = true;
                ui_showLog = true;
                ui_showDisassembler = true;
                ui_showProfilerWindow = true; // Show profiler too
            }
            if (igMenuItem_Bool("Hide All Debug Windows", NULL, false, true))
            {
                ui_showCpuWindow = false;
                ui_showPpuViewer = false;
                ui_showMemoryViewer = false;
                // ui_showLog = false; // Log is often useful to keep visible
                ui_showDisassembler = false;
                ui_showProfilerWindow = false; // Hide profiler too
            }
            igEndMenu();
        }

        if (igBeginMenu("Options", true))
        {
            if (igMenuItem_Bool("Settings...", "F10", ui_showSettingsWindow, true))
                ui_showSettingsWindow = !ui_showSettingsWindow;
            igEndMenu();
        }

        if (igBeginMenu("Help", true))
        {
            if (igMenuItem_Bool("About", NULL, false, true))
                ui_showAboutWindow = true;
            //if (igMenuItem_Bool("Credits", NULL, false, true))
                //ui_showCreditsWindow = true;
            //if (igMenuItem_Bool("Licence", NULL, false, true))
                //ui_showLicenceWindow = true;
            // REFACTOR-NOTE: Add "View Controls" or "Help Topics" menu item with keybinds, basic usage.
            igEndMenu();
        }
        igEndMainMenuBar();
    }
}

static ImGuiMarkdown_Config mdConfig;

static char *credits_markdown;
static char *licence_markdown;

static void UI_MD_LinkCallback(ImGuiMarkdown_LinkCallbackData link)
{
    if (link.link && link.linkLength > 0)
    {
        char truncated_link[link.linkLength + 1];
        strncpy(truncated_link, link.link, link.linkLength);
        truncated_link[link.linkLength] = '\0';
        SDL_OpenURL(truncated_link);
    }
}

void UI_DrawAboutWindow()
{
    if (!ui_showAboutWindow)
        return;
    
    ImGuiViewport *viewport = igGetMainViewport();
    ImVec2 viewportSize = viewport->WorkSize;
    ImVec2 windowSize = {viewportSize.x * 2.0f / 3.0f, viewportSize.y * 2.0f / 3.0f};
    igSetNextWindowSize(windowSize, ImGuiCond_Always);
    
    //TODO: refactor to popup

    if (igBegin("About cNES", &ui_showAboutWindow, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove))
    {
        if (igBeginTabBar("AboutTabs", 0))
        {
            if (igBeginTabItem("About", NULL, 0))
            {
                // Center the content
                ImVec2 content_region;
                igGetContentRegionAvail(&content_region);
                float window_width = content_region.x;
                
                // Title section
                ImVec2 title_size; 
                igCalcTextSize(&title_size, "cNES", NULL, false, 0.0f);
                igPushFont(NULL); // Use default font for now, could use larger font if available
                float title_width = title_size.x;
                igSetCursorPosX((window_width - title_width) * 0.5f);
                igText("cNES");
                igPopFont();
                
                // Subtitle
                ImVec2 subtitle_size;
                igCalcTextSize(&subtitle_size, "Nintendo Entertainment System Emulator", NULL, false, 0.0f);
                float subtitle_width = subtitle_size.x;
                igSetCursorPosX((window_width - subtitle_width) * 0.5f);
                igTextColored((ImVec4){0.7f, 0.7f, 0.7f, 1.0f}, "Nintendo Entertainment System Emulator");
                
                igSpacing();
                igSeparator();
                igSpacing();
                
                // Version information in a clean layout
                igColumns(2, "AboutColumns", false);
                igSetColumnWidth(0, 140.0f);
                
                igText("Version:");
                igNextColumn();
                igText("%s", CNES_VERSION_STRING);
                igNextColumn();
                
                igText("Build Date:");
                igNextColumn();
                igText("%s", CNES_VERSION_BUILD_DATE);
                igNextColumn();
                
                igText("Author:");
                igNextColumn();
                igText("Riley Webb");
                igColumns(1, NULL, false);
                
                igSpacing();
                igSeparator();
                igSpacing();
                
                // Description
                igTextWrapped("A fast and versatile Nintendo Entertainment System emulator written in C. "
                             "Features accurate CPU and PPU emulation with comprehensive debugging tools "
                             "and a modern user interface.");
                
                igSpacing();
                
                // Technology stack
                igText("Built with:");
                igBulletText("SDL3 with GPU acceleration");
                igBulletText("Dear ImGui (cimgui bindings)");
                igBulletText("Cycle-accurate NES core");
                
                igSpacing();
                igSeparator();
                igSpacing();
                
                // Center the GitHub button
                float button_width = 200.0f;
                igSetCursorPosX((window_width - button_width) * 0.5f);
                
                if (igButton("View on GitHub", (ImVec2){button_width, 0}))
                {
                    SDL_OpenURL("https://github.com/RileyWebb/cNES");
                }
                
                igEndTabItem();
            }
            
            if (igBeginTabItem("License", NULL, 0))
            {
                ImGuiMarkdown_Config_Init(&mdConfig);
                mdConfig.linkCallback = UI_MD_LinkCallback;
                
                if (licence_markdown)
                {
                    ImGuiMarkdown(licence_markdown, strlen(licence_markdown), &mdConfig);
                }
                else
                {
                    FILE *file = fopen("LICENCE", "r");
                    if (file)
                    {
                        fseek(file, 0, SEEK_END);
                        long length = ftell(file);
                        fseek(file, 0, SEEK_SET);
                        if (length > 0)
                        {
                            licence_markdown = (char *)malloc((size_t)length + 1);
                            if (licence_markdown)
                            {
                                fread(licence_markdown, 1, (size_t)length, file);
                                licence_markdown[length] = '\0';
                            }
                        }
                        fclose(file);
                    }
                    else
                    {
                        igText("Error loading licence file.");
                    }
                }
                igEndTabItem();
            }

            if (igBeginTabItem("Credits", NULL, 0))
            {
                ImGuiMarkdown_Config_Init(&mdConfig);
                mdConfig.linkCallback = UI_MD_LinkCallback;
                mdConfig.linkIcon = "\uf08e";
                
                if (credits_markdown)
                {
                    ImGuiMarkdown(credits_markdown, strlen(credits_markdown), &mdConfig);
                }
                else
                {
                    FILE *file = fopen("CREDITS", "r");
                    if (file)
                    {
                        fseek(file, 0, SEEK_END);
                        long length = ftell(file);
                        fseek(file, 0, SEEK_SET);
                        if (length > 0)
                        {
                            credits_markdown = (char *)malloc((size_t)length + 1);
                            if (credits_markdown)
                            {
                                fread(credits_markdown, 1, (size_t)length, file);
                                credits_markdown[length] = '\0';
                            }
                        }
                        fclose(file);
                    }
                    else
                    {
                        igText("Error loading credits file.");
                    }
                }
                igEndTabItem();
            }
            
            if (igBeginTabItem("Commits", NULL, 0))
            {
                igText("Recent Git Commits:");
                igText("TODO: Add git commit history display here.");
                igEndTabItem();
            }
            igEndTabBar();
        }
    }
    
    ImVec2 viewport_center;
    ImGuiViewport_GetCenter(&viewport_center, viewport);
    ImVec2 window_size;
    igGetWindowSize(&window_size);
    igSetWindowPos_WindowPtr(igGetCurrentWindow(), (ImVec2){viewport_center.x - window_size.x * 0.5f, viewport_center.y - window_size.y * 0.5f}, ImGuiCond_Always);
    
    igEnd();
}