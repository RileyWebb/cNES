#include <stdio.h>
#include <float.h>
#include <dirent.h>
#include <string.h>
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>
#include <SDL3/SDL.h>
#include "frontend/cimgui_markdown.h"

#include "debug.h"

#include "cNES/version.h"
#include "cNES/apu.h"
#include "cNES/nes.h"
#include "cNES/rom.h"

#include "frontend/frontend.h"
#include "frontend/frontend_config.h"


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
            Frontend_AddRecentRom(selected_file);
            (void)FrontendConfig_Save((NES *)userdata);
            // Frontend_AddRecentRom(path);
        }
        else
        {
            DEBUG_WARN("Failed to load ROM: %s", selected_file);
        }
    }
}

void Frontend_DrawFileMenu(NES *nes)
{
    if (igBeginMenu("File", true))
    {
        if (igMenuItem_Bool("Open ROM...", "Ctrl+O", false, true))
        {
            SDL_ShowOpenFileDialog(FileDialogCallback, nes, frontend_window, filters, SDL_arraysize(filters), NULL, false);
        }

        if (igBeginMenu("Recent ROMs", Frontend_GetRecentRomCount() > 0))
        {
            for (int i = 0; i < Frontend_GetRecentRomCount(); ++i)
            {
                const char *recent_path = Frontend_GetRecentRom(i);
                if (!recent_path || !*recent_path) {
                    continue;
                }

                char label[270];
                const char *filename_display = strrchr(recent_path, '/');
                if (!filename_display)
                    filename_display = strrchr(recent_path, '\\');
                filename_display = filename_display ? filename_display + 1 : recent_path;
                snprintf(label, sizeof(label), "%d. %s", i + 1, filename_display);

                if (igMenuItem_Bool(label, NULL, false, true))
                {
                    if (NES_Load(nes, ROM_LoadFile(recent_path)) == 0)
                    {
                        DEBUG_INFO("Loaded recent ROM: %s", recent_path);
                        NES_Reset(nes);
                        Frontend_AddRecentRom(recent_path);
                        (void)FrontendConfig_Save(nes);
                    }
                    else
                    {
                        DEBUG_WARN("Failed to load recent ROM: %s", recent_path);
                    }
                }
            }
            igEndMenu();
        }
        igSeparator();
        bool rom_loaded_for_state = nes && nes->rom;
        if (igMenuItem_Bool("Save State...", "Ctrl+S", false, rom_loaded_for_state))
        {
            // frontend_openSaveStateModal = true;
        }
        if (igMenuItem_Bool("Load State...", "Ctrl+L", false, rom_loaded_for_state))
        {
            // frontend_openLoadStateModal = true;
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
    if (false) //(frontend_openSaveStateModal)
    {
        /*
        igOpenPopup_Str("Save State", 0);
        if (igBeginPopupModal("Save State", &frontend_openSaveStateModal, ImGuiWindowFlags_AlwaysAutoResize))
        {
            igText("Select Slot (0-9):");
            igSameLine(0, 0);
            igSetNextItemWidth(100);
            igInputInt("##SaveSlot", &frontend_selectedSaveLoadSlot, 1, 1, 0);
            frontend_selectedSaveLoadSlot = frontend_selectedSaveLoadSlot < 0 ? 0 : (frontend_selectedSaveLoadSlot > 9 ? 9 : frontend_selectedSaveLoadSlot);

            if (igButton("Save", (ImVec2){80, 0}))
            {
                Frontend_Log("Placeholder: Save state to slot %d for ROM: %s", frontend_selectedSaveLoadSlot, nes->rom->name);
                // if (nes) NES_SaveState(nes, frontend_selectedSaveLoadSlot); // Actual call
                frontend_openSaveStateModal = false;
                igCloseCurrentPopup();
            }
            igSameLine(0, 8);
            if (igButton("Cancel", (ImVec2){80, 0}))
            {
                frontend_openSaveStateModal = false;
                igCloseCurrentPopup();
            }
            igEndPopup();
        }
            */
    }
    if (false) //(frontend_openLoadStateModal)
    {
        /*
        igOpenPopup_Str("Load State", 0);
        if (igBeginPopupModal("Load State", &frontend_openLoadStateModal, ImGuiWindowFlags_AlwaysAutoResize))
        {
            igText("Select Slot (0-9):");
            igSameLine(0, 0);
            igSetNextItemWidth(100);
            igInputInt("##LoadSlot", &frontend_selectedSaveLoadSlot, 1, 1, 0);
            frontend_selectedSaveLoadSlot = frontend_selectedSaveLoadSlot < 0 ? 0 : (frontend_selectedSaveLoadSlot > 9 ? 9 : frontend_selectedSaveLoadSlot);

            if (igButton("Load", (ImVec2){80, 0}))
            {
                Frontend_Log("Placeholder: Load state from slot %d for ROM: %s", frontend_selectedSaveLoadSlot, nes->rom->name);
                // if (nes) NES_LoadState(nes, frontend_selectedSaveLoadSlot); // Actual call
                frontend_openLoadStateModal = false;
                igCloseCurrentPopup();
            }
            igSameLine(0, 8);
            if (igButton("Cancel", (ImVec2){80, 0}))
            {
                frontend_openLoadStateModal = false;
                igCloseCurrentPopup();
            }
            igEndPopup();
        }
            */
    }
}

static const char *Frontend_InputKeyName(SDL_Keycode key)
{
    const char *name = SDL_GetKeyName(key);
    return (name && name[0] != '\0') ? name : "None";
}

static const char *Frontend_InputGamepadButtonName(SDL_GamepadButton button)
{
    switch (button)
    {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return "South";
    case SDL_GAMEPAD_BUTTON_EAST:
        return "East";
    case SDL_GAMEPAD_BUTTON_WEST:
        return "West";
    case SDL_GAMEPAD_BUTTON_NORTH:
        return "North";
    case SDL_GAMEPAD_BUTTON_BACK:
        return "Back";
    case SDL_GAMEPAD_BUTTON_GUIDE:
        return "Guide";
    case SDL_GAMEPAD_BUTTON_START:
        return "Start";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return "Left Stick";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return "Right Stick";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return "L1";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return "R1";
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return "D-Pad Up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return "D-Pad Down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return "D-Pad Left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return "D-Pad Right";
    default:
        return "Other";
    }
}

static void Frontend_DrawKeyboardBindingTable(int controller)
{
    char table_id[64];
    snprintf(table_id, sizeof(table_id), "KeyboardBindings%d", controller);

    if (!igBeginTable(table_id, 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp, (ImVec2){0, 0}, 0)) {
        return;
    }

    igTableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, 0.0f, 0);
    igTableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
    igTableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 0.0f, 0);
    igTableHeadersRow();

    for (int i = 0; i < FRONTEND_NES_BUTTON_COUNT; ++i)
    {
        char button_id[64];
        snprintf(button_id, sizeof(button_id), "Rebind##kbd_%d_%d", controller, i);

        igTableNextRow(0, 0);
        igTableSetColumnIndex(0);
        igTextUnformatted(frontend_nes_button_names[i], NULL);

        igTableSetColumnIndex(1);
        igTextUnformatted(Frontend_InputKeyName(frontend_input_keymap[controller][i]), NULL);

        igTableSetColumnIndex(2);
        if (igSmallButton(button_id)) {
            FrontendConfig_BeginKeyRebind(controller, (FrontendNesButton)i);
        }
    }

    igEndTable();
}

static void Frontend_DrawGamepadBindingTable(void)
{
    if (!igBeginTable("GamepadBindings", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp, (ImVec2){0, 0}, 0)) {
        return;
    }

    igTableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, 0.0f, 0);
    igTableSetupColumn("Mapping", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
    igTableHeadersRow();

    for (int i = 0; i < FRONTEND_NES_BUTTON_COUNT; ++i)
    {
        igTableNextRow(0, 0);
        igTableSetColumnIndex(0);
        igTextUnformatted(frontend_nes_button_names[i], NULL);

        igTableSetColumnIndex(1);
        igTextUnformatted(Frontend_InputGamepadButtonName(frontend_input_gamepad_map[i]), NULL);
    }

    igEndTable();
}

void Frontend_SettingsMenu(NES *nes)
{
    if (!frontend_showSettingsWindow)
        return;

    ImVec2 viewportSize = igGetMainViewport()->WorkSize;
    ImVec2 windowSize = {viewportSize.x * 2.0f / 3.0f, viewportSize.y * 2.0f / 3.0f};
    igSetNextWindowSize(windowSize, ImGuiCond_Once);
    igSetNextWindowPos((ImVec2){(viewportSize.x - windowSize.x) / 2, (viewportSize.y - windowSize.y) / 2}, ImGuiCond_Once, (ImVec2){0, 0});

    if (igBegin("Settings", &frontend_showSettingsWindow, ImGuiWindowFlags_None))
    {
        if (igBeginTabBar("SettingsTabs", 0))
        {
            
            if (igBeginTabItem("General", NULL, 0))
            {
                const char *themes[] = {"Default", "Dark", "Light", "Excellency"};
                int selected_theme = frontend_current_theme; // Assuming frontend_current_theme is an integer
                if (igCombo_Str_arr("Theme", &selected_theme, themes, SDL_arraysize(themes), 8))
                {
                    switch (selected_theme)
                    {
                    case 0:
                        frontend_current_theme = FRONTEND_THEME_DARK; // Default is dark
                        frontend_requestReload = true; // Flag to reload UI on next frame
                        break;
                    case 1:
                        frontend_current_theme = FRONTEND_THEME_DARK; // Dark theme
                        frontend_requestReload = true; // Flag to reload UI on next frame
                        break;
                    case 2:
                        frontend_current_theme = FRONTEND_THEME_LIGHT; // Light theme
                        frontend_requestReload = true; // Flag to reload UI on next frame
                        break;
                    case 3:
                        frontend_current_theme = FRONTEND_THEME_EXCELLENCY;
                        frontend_requestReload = true;
                        break;
                    }
                    (void)FrontendConfig_Save(nes);
                }
                igSeparator();
                if (igCheckbox("Fullscreen (Window)", &frontend_fullscreen))
                {
                    Frontend_SetFullscreen(frontend_fullscreen);
                    (void)FrontendConfig_Save(nes);
                }
                igSameLine(0, 10);
                igTextDisabled("(F11)");

                igSeparator();
                igText("Font Settings:");
                if (igSliderFloat("Font Size", &frontend_font_size, 8.0f, 32.0f, "%1.0f", 0))
                {
                    frontend_requestReload = true; // Flag to reload fonts on next frame
                    //Frontend_ApplyTheme(frontend_current_theme); // Assuming a function to apply font size
                    (void)FrontendConfig_Save(nes);
                }
                igTextDisabled("Adjust the font size for better readability.");
                igSeparator(); // Visually separate font size from font family selection
                igText("Font Family:");

                // Display current font and a button to reset to default
                const char* display_font_name = "Default";
                if (*frontend_font_path) {
                    // Extract filename from frontend_font_path for display
                    // Handles paths with either '/' or '\'
                    const char *name_part_bslash = strrchr(frontend_font_path, '\\');
                    const char *name_part_fslash = strrchr(frontend_font_path, '/');
                    const char *name_part = name_part_bslash > name_part_fslash ? name_part_bslash : name_part_fslash;
                    display_font_name = name_part ? name_part + 1 : frontend_font_path;
                }
                igText("Current: %s", display_font_name);
                igSameLine(0, 10.0f);
                if (igSmallButton("Use Default")) {
                    snprintf(frontend_font_path, sizeof(frontend_font_path), "%s", "data/fonts/JetBrainsMono.ttf");
                    frontend_requestReload = true; // Signal that font settings changed
                    (void)FrontendConfig_Save(nes);
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


                            if (strcmp(frontend_font_path, full_store_path) == 0)
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
                            snprintf(frontend_font_path, sizeof(frontend_font_path), "%s", font_files[current_font_index]);
                            frontend_requestReload = true;
                            DEBUG_INFO("UI: Font selection changed to: %s", frontend_font_path);
                            (void)FrontendConfig_Save(nes);
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
                if (igCheckbox("Enable VSync", &frontend_vsync_enabled))
                {
                    Frontend_ToggleVSync(frontend_vsync_enabled); // Assuming a function to toggle VSync
                }
                igTextDisabled("Toggle vertical synchronization for smoother visuals.");

                if (igCheckbox("Integer Scaling", &frontend_integer_scaling))
                {
                    Frontend_ToggleIntegerScaling(frontend_integer_scaling); // Assuming a function to toggle integer scaling
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
                    if (igCollapsingHeader_TreeNodeFlags("System & Region", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        const char *region_items[] = {"NTSC", "PAL", "DENDY"};
                        int selected_region_idx = (int)nes->settings.region;
    
                        if (igCombo_Str_arr("Hardware Region", &selected_region_idx, region_items, 3, 3))
                        {
                            if (nes->settings.region != (NES_Region)selected_region_idx)
                            {
                                NES_SetRegionPreset(nes, (NES_Region)selected_region_idx);
                                DEBUG_INFO("Region set to %s.", region_items[selected_region_idx]);
                            }
                        }
                        igTextDisabled("Selects the target hardware region. Affects timing and refresh rate.");
                    }

                    if (igCollapsingHeader_TreeNodeFlags("CPU Settings", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        const char *cpu_modes[] = {"JIT", "Interpreter", "Debug"};
                        int selected_cpu_mode = (int)nes->settings.cpu_mode;
                        if (igCombo_Str_arr("CPU Emulator Mode", &selected_cpu_mode, cpu_modes, 3, 3))
                        {
                            nes->settings.cpu_mode = selected_cpu_mode;
                            (void)FrontendConfig_Save(nes);
                        }
                        igTextDisabled("Execution core for the standard 6502 CPU.");

                        float clock_rate = nes->settings.timing.cpu_clock_rate;
                        if (igInputFloat("CPU Clock Rate (MHz)", &clock_rate, 0.01f, 0.5f, "%.6f", 0))
                        {
                            nes->settings.timing.cpu_clock_rate = clock_rate;
                            (void)FrontendConfig_Save(nes);
                        }
                        igTextDisabled("Base system clock timing. Changes affect core execution speed.");
                    }

                    if (igCollapsingHeader_TreeNodeFlags("PPU Settings", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        const char *ppu_modes[] = {"JIT", "Interpreter", "Accelerated", "Debug"};
                        int selected_ppu_mode = (int)nes->settings.ppu_mode;
                        if (igCombo_Str_arr("PPU Emulator Mode", &selected_ppu_mode, ppu_modes, 4, 4))
                        {
                            nes->settings.ppu_mode = selected_ppu_mode;
                            (void)FrontendConfig_Save(nes);
                        }

                        int scanlines_vis = nes->settings.timing.scanlines_visible;
                        if (igInputInt("Visible Scanlines", &scanlines_vis, 1, 10, 0)) {
                            nes->settings.timing.scanlines_visible = scanlines_vis;
                            (void)FrontendConfig_Save(nes);
                        }

                        int scanline_vblank = nes->settings.timing.scanline_vblank;
                        if (igInputInt("VBlank Scanline", &scanline_vblank, 1, 10, 0)) {
                            nes->settings.timing.scanline_vblank = scanline_vblank;
                            (void)FrontendConfig_Save(nes);
                        }

                        int scanline_prerender = nes->settings.timing.scanline_prerender;
                        if (igInputInt("Prerender Scanline", &scanline_prerender, 1, 10, 0)) {
                            nes->settings.timing.scanline_prerender = scanline_prerender;
                            (void)FrontendConfig_Save(nes);
                        }

                        int cycles_per_scanline = nes->settings.timing.cycles_per_scanline;
                        if (igInputInt("Cycles per Scanline", &cycles_per_scanline, 1, 10, 0)) {
                            nes->settings.timing.cycles_per_scanline = cycles_per_scanline;
                            (void)FrontendConfig_Save(nes);
                        }
                        
                        // Sprite Limit
                        bool sprite_limit_enabled = true; // TODO nes->settings.sprite_limit_enabled
                        if (igCheckbox("Enable Sprite Limit", &sprite_limit_enabled))
                        {
                            DEBUG_INFO("Sprite limit %s.", sprite_limit_enabled ? "enabled" : "disabled");
                        }
                        igTextDisabled("Toggles the original NES 8-sprite-per-scanline hardware limitation.");
                    }

                    if (igCollapsingHeader_TreeNodeFlags("Audio Settings", 0)) // 0 is ImGuiTreeNodeFlags_None
                    {
                        int sample_rate = nes->settings.audio.sample_rate;
                        if (igInputInt("Sample Rate", &sample_rate, 100, 1000, 0))
                        {
                            nes->settings.audio.sample_rate = sample_rate;
                            (void)FrontendConfig_Save(nes);
                        }

                        float volume = nes->settings.audio.volume;
                        if (igSliderFloat("Master Volume", &volume, 0.0f, 1.0f, "%.2f", 0))
                        {
                            nes->settings.audio.volume = volume;
                            (void)FrontendConfig_Save(nes);
                        }
                    }

                    if (igCollapsingHeader_TreeNodeFlags("Video & Colors", 0))
                    {
                        float hue = nes->settings.video.hue;
                        if (igSliderFloat("Hue Offset", &hue, -3.1415f, 3.1415f, "%.2f", 0)) {
                            nes->settings.video.hue = hue;
                            (void)FrontendConfig_Save(nes);
                        }
                        
                        float saturation = nes->settings.video.saturation;
                        if (igSliderFloat("Saturation", &saturation, 0.0f, 2.0f, "%.2f", 0)) {
                            nes->settings.video.saturation = saturation;
                            (void)FrontendConfig_Save(nes);
                        }
                    }
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
                if (igRadioButton_Bool("Dark", frontend_current_theme == FRONTEND_THEME_DARK))
                {
                    frontend_current_theme = FRONTEND_THEME_DARK;
                    frontend_requestReload = true;
                    (void)FrontendConfig_Save(nes);
                }
                igSameLine(0, 5);
                if (igRadioButton_Bool("Light", frontend_current_theme == FRONTEND_THEME_LIGHT))
                {
                    frontend_current_theme = FRONTEND_THEME_LIGHT;
                    frontend_requestReload = true;
                    (void)FrontendConfig_Save(nes);
                }
                igSameLine(0, 5);
                if (igRadioButton_Bool("Excellency", frontend_current_theme == FRONTEND_THEME_EXCELLENCY))
                {
                    frontend_current_theme = FRONTEND_THEME_EXCELLENCY;
                    frontend_requestReload = true;
                    (void)FrontendConfig_Save(nes);
                }
                igSeparator();
                if (igCheckbox("Fullscreen (Window)", &frontend_fullscreen))
                {
                    Frontend_SetFullscreen(frontend_fullscreen);
                    (void)FrontendConfig_Save(nes);
                }
                igSameLine(0, 10);
                igTextDisabled("(F11)");

                // Define data for Present Modes (VSync)
                // These static const arrays are initialized once when the function is first called.
                static const char *present_mode_names_list[] = {"Immediate", "Mailbox", "FIFO (VSync)"};
                static const SDL_GPUPresentMode present_mode_values_list[] = {
                    SDL_GPU_PRESENTMODE_IMMEDIATE,
                    SDL_GPU_PRESENTMODE_MAILBOX,
                    SDL_GPU_PRESENTMODE_VSYNC,
                };
                // Static variable to store the selected index, persists across calls.
                static int selected_present_mode_idx = 2; // Default to FIFO (VSync), which is index 2

                // Define data for Swapchain Composition
                // Typedef is local to this function's scope (C99 and later).
                // If using an older C standard, this typedef might need to be at file scope.
                typedef struct {
                    const char* name;
                    SDL_GPUSwapchainComposition composition;
                } LocalSwapchainModeInfo_t;

                static const LocalSwapchainModeInfo_t swapchain_modes_list[] = {
                    {"SDR", SDL_GPU_SWAPCHAINCOMPOSITION_SDR},
                    {"SDR (Linear)", SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR},
                    {"HDR (Extended Linear)", SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR},
                    {"HDR10 (ST2084)", SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084}
                };
                static int selected_swapchain_mode_idx = 0; // Default to SDR, which is index 0

                // Helper array for swapchain mode names in the ImGui combo box
                // This array's pointers will be filled once.
                static const char* swapchain_mode_combo_names_list[SDL_arraysize(swapchain_modes_list)];
                static bool swapchain_combo_names_list_initialized = false;

                // One-time initialization for swapchain_mode_combo_names_list
                if (!swapchain_combo_names_list_initialized) {
                    for (size_t i = 0; i < SDL_arraysize(swapchain_modes_list); ++i) {
                        swapchain_mode_combo_names_list[i] = swapchain_modes_list[i].name;
                    }
                    swapchain_combo_names_list_initialized = true;
                }

                igSeparator(); 
                igText("Renderer Settings:");

                const LocalSwapchainModeInfo_t selected_mode = swapchain_modes_list[selected_swapchain_mode_idx];

                // VSync (Present Mode) Dropdown
                if (igCombo_Str_arr("VSync (Present Mode)", &selected_present_mode_idx, present_mode_names_list, SDL_arraysize(present_mode_names_list), 4))
                {
                    if (frontend_window) { // Ensure frontend_window is not NULL
                        if (!SDL_SetGPUSwapchainParameters(gpu_device, frontend_window, selected_mode.composition, present_mode_values_list[selected_present_mode_idx]))
                        {
                            DEBUG_WARN("UI: Failed to set Present Mode to %s: %s", present_mode_names_list[selected_present_mode_idx], SDL_GetError());
                            // Optionally, could try to revert selected_present_mode_idx to a previously known good value
                        }
                        else
                        {
                            DEBUG_INFO("UI: Present Mode set to %s", present_mode_names_list[selected_present_mode_idx]);
                        }
                    }
                }
                igTextDisabled("Controls screen tearing and latency. FIFO provides VSync.");

                // Swapchain Composition Dropdown
                if (igCombo_Str_arr("Swapchain Composition", &selected_swapchain_mode_idx, swapchain_mode_combo_names_list, SDL_arraysize(swapchain_modes_list), 4))
                {
                    if (frontend_window) { // Ensure frontend_window is not NULL
                        // Check if the selected composition and colorspace are supported
                        if (SDL_WindowSupportsGPUSwapchainComposition(gpu_device, frontend_window, selected_mode.composition))
                        {
                            if (!SDL_SetGPUSwapchainParameters(gpu_device, frontend_window, selected_mode.composition, present_mode_values_list[selected_present_mode_idx]))
                            {
                                DEBUG_WARN("UI: Failed to set Swapchain Composition to %s: %s", selected_mode.name, SDL_GetError());
                            }
                            else
                            {
                                DEBUG_INFO("UI: Swapchain Composition set to %s", selected_mode.name);
                            }
                        }
                        else
                        {
                            DEBUG_WARN("UI: Swapchain Composition %s is not supported.", selected_mode.name);
                            // Optionally, revert selected_swapchain_mode_idx
                        }
                    }
                }
                igTextDisabled("Determines color space and dynamic range (e.g., SDR, HDR).");
                igSeparator();

                // REFACTOR-NOTE: Add font scaling options, game screen aspect ratio/scaling options here.
                igTextDisabled("More display settings (font, scaling) can be added here.");
                igEndTabItem();
            }
            if (igBeginTabItem("Audio", NULL, 0))
            {
                // REFACTOR-NOTE: Connect this to actual APU volume control.
                if (igSliderFloat("Master Volume", &frontend_master_volume, 0.0f, 1.0f, "%.2f", 0))
                {
                    if (nes && nes->apu) {
                        APU_SetVolume(nes->apu, frontend_master_volume * nes->settings.audio.volume);
                    }
                    DEBUG_INFO("Master volume (placeholder) set to: %.2f", frontend_master_volume);
                }
                // REFACTOR-NOTE: Add options for audio buffer size, sample rate, APU channel toggles.
                igTextDisabled("More audio settings (buffer, channels) can be added here.");
                igEndTabItem();
            }
            if (igBeginTabItem("Input", NULL, 0))
            {
                igTextWrapped("Keyboard, SDL gamepad, and raw joystick D-pad bindings are configured here. Rebinds are captured on the next key press.");

                if (FrontendConfig_IsWaitingForKey())
                {
                    igTextColored((ImVec4){1.0f, 1.0f, 0.2f, 1.0f},
                                  "Capturing Player %d %s. Press ESC to cancel.",
                                  FrontendConfig_GetRebindController() + 1,
                                  frontend_nes_button_names[FrontendConfig_GetRebindButton()]);
                }

                if (igBeginChild_Str("KeyboardSection", (ImVec2){0, 250}, ImGuiChildFlags_Borders, ImGuiWindowFlags_None))
                {
                    igTextUnformatted("Keyboard", NULL);
                    igTextDisabled("Player 1 and Player 2 each have their own key map.");
                    Frontend_DrawKeyboardBindingTable(0);

                    igSeparator();

                    Frontend_DrawKeyboardBindingTable(1);
                    igEndChild();
                }

                if (igBeginChild_Str("ControllerSection", (ImVec2){0, 220}, ImGuiChildFlags_Borders, ImGuiWindowFlags_None))
                {
                    igTextUnformatted("Controllers", NULL);
                    igTextDisabled("Gamepad and joystick input both feed controller 1.");

                    if (igCheckbox("Enable SDL Gamepad", &frontend_input_enable_gamepad)) {
                        (void)FrontendConfig_Save(nes);
                    }

                    if (igCheckbox("Enable Joystick D-Pad", &frontend_input_enable_joystick_dpad)) {
                        (void)FrontendConfig_Save(nes);
                    }

                    if (igSliderInt("Analog Deadzone", &frontend_input_axis_deadzone, 0, 32767, "%d", 0)) {
                        (void)FrontendConfig_Save(nes);
                    }

                    if (frontend_input_enable_gamepad)
                    {
                        igText("Gamepad status: enabled");
                    }
                    else
                    {
                        igTextDisabled("Gamepad status: disabled");
                    }

                    if (frontend_input_enable_joystick_dpad)
                    {
                        int joystick_count = 0;
                        SDL_JoystickID *ids = SDL_GetJoysticks(&joystick_count);
                        if (ids)
                        {
                            const char *selected_name = NULL;
                            for (int i = 0; i < joystick_count; ++i)
                            {
                                if (!SDL_IsGamepad(ids[i]))
                                {
                                    selected_name = SDL_GetJoystickNameForID(ids[i]);
                                    if (selected_name && selected_name[0] != '\0') {
                                        break;
                                    }
                                }
                            }
                            SDL_free(ids);

                            if (selected_name && selected_name[0] != '\0')
                            {
                                igText("Joystick D-Pad: %s", selected_name);
                            }
                            else
                            {
                                igTextDisabled("Joystick D-Pad: waiting for a raw joystick");
                            }
                        }
                        else
                        {
                            igTextDisabled("Joystick D-Pad: no devices detected");
                        }
                    }
                    else
                    {
                        igTextDisabled("Joystick D-Pad: disabled");
                    }

                    igEndChild();
                }

                if (igBeginChild_Str("GamepadSection", (ImVec2){0, 200}, ImGuiChildFlags_Borders, ImGuiWindowFlags_None))
                {
                    igTextUnformatted("SDL Gamepad Mapping", NULL);
                    igTextDisabled("These bindings control controller 1 when an SDL gamepad is available.");
                    Frontend_DrawGamepadBindingTable();
                    igEndChild();
                }

                igSeparator();

                if (igButton("Save Input Settings", (ImVec2){0, 0})) {
                    if (FrontendConfig_Save(nes)) {
                        DEBUG_INFO("Saved input settings to %s", FrontendConfig_GetPath());
                    } else {
                        DEBUG_WARN("Failed to save input settings to %s", FrontendConfig_GetPath());
                    }
                }
                igSameLine(0.0f, 8.0f);
                if (igButton("Restore Input Defaults", (ImVec2){0, 0})) {
                    FrontendConfig_ResetInputSettings();
                    (void)FrontendConfig_Save(nes);
                }
                igEndTabItem();
            }
            // REFACTOR-NOTE: Add "Paths" tab for save states, screenshots, default ROMs directory.
            // REFACTOR-NOTE: Add "Advanced" tab for emulation tweaks (e.g., CPU/PPU cycle accuracy options if available).
            igEndTabBar();
        }
        igSeparator();
        igSetCursorPosY(igGetWindowHeight() - igGetFrameHeightWithSpacing() - igGetStyle()->WindowPadding.y);
        if (igButton("Close", (ImVec2){-FLT_MIN, 0}))
            frontend_showSettingsWindow = false;
    }
    igEnd();
}

void Frontend_DrawMainMenuBar(NES *nes)
{
    if (igBeginMainMenuBar())
    {
        Frontend_DrawFileMenu(nes);
        if (igBeginMenu("Emulation", true))
        {
            bool rom_loaded = nes && nes->rom && nes->rom;
            if (igMenuItem_Bool(frontend_paused ? "Resume " : "Pause", "F6", false, rom_loaded))
                frontend_paused = !frontend_paused;
            if (igMenuItem_Bool("Reset", "F5", false, rom_loaded))
                NES_Reset(nes);
            if (igMenuItem_Bool("Step CPU Instruction", "F7", false, rom_loaded && frontend_paused))
            {
                if (nes && nes->cpu)
                    NES_Step(nes);
            }
            if (igMenuItem_Bool("Step Frame", "F8", false, rom_loaded && frontend_paused))
                NES_StepFrame(nes);
            // REFACTOR-NOTE: Add speed controls (e.g., 50%, 100%, 200%, turbo mode). Would require timing adjustments in main loop.
            igEndMenu();
        }
        if (igBeginMenu("View", true))
        {
            if (igMenuItem_Bool("Game Screen", NULL, frontend_showGameScreen, true))
                frontend_showGameScreen = !frontend_showGameScreen;
            if (igMenuItem_Bool("ROM Info", NULL, frontend_showRomInfoWindow, true))
                frontend_showRomInfoWindow = !frontend_showRomInfoWindow;
            if (igMenuItem_Bool("CPU Registers", NULL, frontend_showCpuWindow, true))
                frontend_showCpuWindow = !frontend_showCpuWindow;
            if (igMenuItem_Bool("PPU Viewer", NULL, frontend_showPpuViewer, true))
                frontend_showPpuViewer = !frontend_showPpuViewer;
            if (igMenuItem_Bool("Memory Viewer", NULL, frontend_showMemoryViewer, true))
                frontend_showMemoryViewer = !frontend_showMemoryViewer;
            if (igMenuItem_Bool("Disassembler", NULL, frontend_showDisassembler, true))
                frontend_showDisassembler = !frontend_showDisassembler;
            if (igMenuItem_Bool("Log Window", NULL, frontend_showLog, true))
                frontend_showLog = !frontend_showLog;
            if (igMenuItem_Bool("Profiler", NULL, frontend_showProfilerWindow, true))
                frontend_showProfilerWindow = !frontend_showProfilerWindow;
            igSeparator();
            if (igMenuItem_Bool("Toggle Fullscreen", "F11", frontend_fullscreen, true))
                Frontend_ToggleFullscreen();
            igEndMenu();
        }

        if (igBeginMenu("Debug", true))
        {
            igMenuItem_Bool("CPU Registers", NULL, &frontend_showCpuWindow, true);
            igMenuItem_Bool("PPU Viewer", NULL, &frontend_showPpuViewer, true);
            igMenuItem_Bool("Memory Viewer", NULL, &frontend_showMemoryViewer, true);
            igMenuItem_Bool("Log Window", NULL, &frontend_showLog, true);
            igMenuItem_Bool("Disassembler", NULL, &frontend_showDisassembler, true);
            igMenuItem_Bool("Profiler", NULL, &frontend_showProfilerWindow, true); // Added Profiler toggle
            igSeparator();
            igMenuItem_Bool("Debug Controls Window", NULL, &frontend_showToolbar, true); // Renamed from Toolbar
            igSeparator();
            if (igMenuItem_Bool("Show All Debug Windows", NULL, false, true))
            {
                frontend_showCpuWindow = true;
                frontend_showPpuViewer = true;
                frontend_showMemoryViewer = true;
                frontend_showLog = true;
                frontend_showDisassembler = true;
                frontend_showProfilerWindow = true; // Show profiler too
            }
            if (igMenuItem_Bool("Hide All Debug Windows", NULL, false, true))
            {
                frontend_showCpuWindow = false;
                frontend_showPpuViewer = false;
                frontend_showMemoryViewer = false;
                // frontend_showLog = false; // Log is often useful to keep visible
                frontend_showDisassembler = false;
                frontend_showProfilerWindow = false; // Hide profiler too
            }
            igEndMenu();
        }

        if (igBeginMenu("Options", true))
        {
            if (igMenuItem_Bool("Settings...", "F10", frontend_showSettingsWindow, true))
                frontend_showSettingsWindow = !frontend_showSettingsWindow;
            igEndMenu();
        }

        if (igBeginMenu("Help", true))
        {
            if (igMenuItem_Bool("About", NULL, false, true))
                frontend_showAboutWindow = true;
            //if (igMenuItem_Bool("Credits", NULL, false, true))
                //frontend_showCreditsWindow = true;
            //if (igMenuItem_Bool("Licence", NULL, false, true))
                //frontend_showLicenceWindow = true;
            // REFACTOR-NOTE: Add "View Controls" or "Help Topics" menu item with keybinds, basic usage.
            igEndMenu();
        }
        igEndMainMenuBar();
    }
}

static ImGuiMarkdown_Config mdConfig;

static char *credits_markdown;
static char *licence_markdown;

static void Frontend_MD_LinkCallback(ImGuiMarkdown_LinkCallbackData link)
{
    if (link.link && link.linkLength > 0)
    {
        char truncated_link[link.linkLength + 1];
        strncpy(truncated_link, link.link, link.linkLength);
        truncated_link[link.linkLength] = '\0';
        SDL_OpenURL(truncated_link);
    }
}

void Frontend_DrawAboutWindow()
{
    if (!frontend_showAboutWindow)
        return;
    
    ImGuiViewport *viewport = igGetMainViewport();
    ImVec2 viewportSize = viewport->WorkSize;
    ImVec2 windowSize = {viewportSize.x * 2.0f / 3.0f, viewportSize.y * 2.0f / 3.0f};
    igSetNextWindowSize(windowSize, ImGuiCond_Always);
    
    //TODO: refactor to popup

    if (igBegin("About cNES", &frontend_showAboutWindow, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove))
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
                mdConfig.linkCallback = Frontend_MD_LinkCallback;
                
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
                mdConfig.linkCallback = Frontend_MD_LinkCallback;
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

