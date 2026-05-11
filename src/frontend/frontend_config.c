#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <cJSON.h>

#include "debug.h"
#include "cNES/nes.h"
#include "frontend/frontend.h"
#include "frontend/frontend_config.h"

#define FRONTEND_SETTINGS_VERSION 2

const char *frontend_nes_button_names[FRONTEND_NES_BUTTON_COUNT] = {
    "A", "B", "Select", "Start", "Up", "Down", "Left", "Right"};

SDL_Keycode frontend_input_keymap[2][FRONTEND_NES_BUTTON_COUNT];
SDL_GamepadButton frontend_input_gamepad_map[FRONTEND_NES_BUTTON_COUNT];
bool frontend_input_enable_gamepad = true;
bool frontend_input_enable_joystick_dpad = true;
int frontend_input_axis_deadzone = 12000;

static int frontend_rebind_controller = -1;
static int frontend_rebind_button = -1;
static char frontend_config_directory[512] = {0};
static char frontend_general_settings_path[512] = {0};
static char frontend_input_settings_path[512] = {0};
static char frontend_recent_roms_path[512] = {0};

static void frontend_config_build_paths(void)
{
    if (frontend_input_settings_path[0] != '\0') {
        return;
    }

    char *pref_path = SDL_GetPrefPath("RileyWebb", "cNES");
    if (pref_path && pref_path[0] != '\0') {
        (void)snprintf(frontend_config_directory, sizeof(frontend_config_directory), "%s", pref_path);
        (void)snprintf(frontend_general_settings_path, sizeof(frontend_general_settings_path), "%s%s", pref_path, "general.json");
        (void)snprintf(frontend_input_settings_path, sizeof(frontend_input_settings_path), "%s%s", pref_path, "input.json");
        (void)snprintf(frontend_recent_roms_path, sizeof(frontend_recent_roms_path), "%s%s", pref_path, "recent_roms.json");
        SDL_free(pref_path);
        return;
    }

    (void)snprintf(frontend_config_directory, sizeof(frontend_config_directory), "%s", "");
    (void)snprintf(frontend_general_settings_path, sizeof(frontend_general_settings_path), "%s", "general.json");
    (void)snprintf(frontend_input_settings_path, sizeof(frontend_input_settings_path), "%s", "input.json");
    (void)snprintf(frontend_recent_roms_path, sizeof(frontend_recent_roms_path), "%s", "recent_roms.json");
}

static void frontend_config_set_string(char *dst, size_t dst_size, const char *value)
{
    if (!dst || dst_size == 0 || !value) {
        return;
    }

    (void)snprintf(dst, dst_size, "%s", value);
}

static void frontend_config_add_bool(cJSON *object, const char *name, bool value)
{
    cJSON_AddBoolToObject(object, name, value);
}

static void frontend_config_add_number(cJSON *object, const char *name, double value)
{
    cJSON_AddNumberToObject(object, name, value);
}

static void frontend_config_add_string(cJSON *object, const char *name, const char *value)
{
    cJSON_AddStringToObject(object, name, value ? value : "");
}

static cJSON *frontend_config_add_section(cJSON *root, const char *name)
{
    cJSON *section = cJSON_CreateObject();
    if (section) {
        cJSON_AddItemToObject(root, name, section);
    }
    return section;
}

const char *FrontendConfig_GetPath(void)
{
    frontend_config_build_paths();
    return frontend_input_settings_path;
}

const char *FrontendConfig_GetDirectory(void)
{
    frontend_config_build_paths();
    return frontend_config_directory;
}

void FrontendConfig_InitDefaults(void)
{
    frontend_input_keymap[0][FRONTEND_NES_BUTTON_A] = SDLK_K;
    frontend_input_keymap[0][FRONTEND_NES_BUTTON_B] = SDLK_J;
    frontend_input_keymap[0][FRONTEND_NES_BUTTON_SELECT] = SDLK_RSHIFT;
    frontend_input_keymap[0][FRONTEND_NES_BUTTON_START] = SDLK_RETURN;
    frontend_input_keymap[0][FRONTEND_NES_BUTTON_UP] = SDLK_W;
    frontend_input_keymap[0][FRONTEND_NES_BUTTON_DOWN] = SDLK_S;
    frontend_input_keymap[0][FRONTEND_NES_BUTTON_LEFT] = SDLK_A;
    frontend_input_keymap[0][FRONTEND_NES_BUTTON_RIGHT] = SDLK_D;

    frontend_input_keymap[1][FRONTEND_NES_BUTTON_A] = SDLK_PERIOD;
    frontend_input_keymap[1][FRONTEND_NES_BUTTON_B] = SDLK_COMMA;
    frontend_input_keymap[1][FRONTEND_NES_BUTTON_SELECT] = SDLK_RALT;
    frontend_input_keymap[1][FRONTEND_NES_BUTTON_START] = SDLK_RCTRL;
    frontend_input_keymap[1][FRONTEND_NES_BUTTON_UP] = SDLK_UP;
    frontend_input_keymap[1][FRONTEND_NES_BUTTON_DOWN] = SDLK_DOWN;
    frontend_input_keymap[1][FRONTEND_NES_BUTTON_LEFT] = SDLK_LEFT;
    frontend_input_keymap[1][FRONTEND_NES_BUTTON_RIGHT] = SDLK_RIGHT;

    frontend_input_gamepad_map[FRONTEND_NES_BUTTON_A] = SDL_GAMEPAD_BUTTON_EAST;
    frontend_input_gamepad_map[FRONTEND_NES_BUTTON_B] = SDL_GAMEPAD_BUTTON_SOUTH;
    frontend_input_gamepad_map[FRONTEND_NES_BUTTON_SELECT] = SDL_GAMEPAD_BUTTON_BACK;
    frontend_input_gamepad_map[FRONTEND_NES_BUTTON_START] = SDL_GAMEPAD_BUTTON_START;
    frontend_input_gamepad_map[FRONTEND_NES_BUTTON_UP] = SDL_GAMEPAD_BUTTON_DPAD_UP;
    frontend_input_gamepad_map[FRONTEND_NES_BUTTON_DOWN] = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    frontend_input_gamepad_map[FRONTEND_NES_BUTTON_LEFT] = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    frontend_input_gamepad_map[FRONTEND_NES_BUTTON_RIGHT] = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;

    frontend_input_enable_gamepad = true;
    frontend_input_enable_joystick_dpad = true;
    frontend_input_axis_deadzone = 12000;
    frontend_rebind_controller = -1;
    frontend_rebind_button = -1;
}

void FrontendConfig_ResetInputSettings(void)
{
    FrontendConfig_InitDefaults();
}

static void frontend_config_serialize_frontend(cJSON *root)
{
    cJSON *frontend = frontend_config_add_section(root, "frontend");
    if (!frontend) {
        return;
    }

    frontend_config_add_number(frontend, "theme", (double)frontend_current_theme);
    frontend_config_add_bool(frontend, "fullscreen", frontend_fullscreen);
    frontend_config_add_number(frontend, "font_size", (double)frontend_font_size);
    frontend_config_add_string(frontend, "font_path", frontend_font_path);
    frontend_config_add_number(frontend, "master_volume", (double)frontend_master_volume);

    frontend_config_add_bool(frontend, "show_cpu_window", frontend_showCpuWindow);
    frontend_config_add_bool(frontend, "show_disassembler", frontend_showDisassembler);
    frontend_config_add_bool(frontend, "show_game_screen", frontend_showGameScreen);
    frontend_config_add_bool(frontend, "show_profiler_window", frontend_showProfilerWindow);
    frontend_config_add_bool(frontend, "show_rom_info_window", frontend_showRomInfoWindow);
    frontend_config_add_bool(frontend, "show_ppu_viewer", frontend_showPpuViewer);
    frontend_config_add_bool(frontend, "show_log", frontend_showLog);
    frontend_config_add_bool(frontend, "show_memory_viewer", frontend_showMemoryViewer);
    frontend_config_add_bool(frontend, "show_settings_window", frontend_showSettingsWindow);
    frontend_config_add_bool(frontend, "show_about_window", frontend_showAboutWindow);
    frontend_config_add_bool(frontend, "show_credits_window", frontend_showCreditsWindow);
    frontend_config_add_bool(frontend, "show_licence_window", frontend_showLicenceWindow);
}

static void frontend_config_serialize_input(cJSON *root)
{
    cJSON *input = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "input", input);

    cJSON *keyboard1 = cJSON_CreateArray();
    cJSON *keyboard2 = cJSON_CreateArray();
    cJSON *gamepad = cJSON_CreateArray();

    for (int i = 0; i < FRONTEND_NES_BUTTON_COUNT; ++i) {
        cJSON_AddItemToArray(keyboard1, cJSON_CreateNumber((double)frontend_input_keymap[0][i]));
        cJSON_AddItemToArray(keyboard2, cJSON_CreateNumber((double)frontend_input_keymap[1][i]));
        cJSON_AddItemToArray(gamepad, cJSON_CreateNumber((double)frontend_input_gamepad_map[i]));
    }

    cJSON_AddItemToObject(input, "controller1_keyboard", keyboard1);
    cJSON_AddItemToObject(input, "controller2_keyboard", keyboard2);
    cJSON_AddItemToObject(input, "gamepad_buttons", gamepad);
    cJSON_AddBoolToObject(input, "enable_gamepad", frontend_input_enable_gamepad);
    cJSON_AddBoolToObject(input, "enable_joystick_dpad", frontend_input_enable_joystick_dpad);
    cJSON_AddNumberToObject(input, "axis_deadzone", (double)frontend_input_axis_deadzone);
}

static void frontend_config_serialize_recent_roms(cJSON *root)
{
    cJSON *recent_roms = cJSON_CreateArray();
    if (!recent_roms) {
        return;
    }

    int recent_count = Frontend_GetRecentRomCount();
    for (int i = 0; i < recent_count; ++i) {
        const char *path = Frontend_GetRecentRom(i);
        if (path && *path) {
            cJSON_AddItemToArray(recent_roms, cJSON_CreateString(path));
        }
    }

    cJSON_AddItemToObject(root, "recent_roms", recent_roms);
}

static void frontend_config_serialize_emulation(cJSON *root, const NES *nes)
{
    if (!nes) {
        return;
    }

    cJSON *emulation = frontend_config_add_section(root, "emulation");
    if (!emulation) {
        return;
    }

    frontend_config_add_number(emulation, "cpu_mode", (double)nes->settings.cpu_mode);
    frontend_config_add_number(emulation, "ppu_mode", (double)nes->settings.ppu_mode);
    frontend_config_add_number(emulation, "region", (double)nes->settings.region);

    cJSON *timing = frontend_config_add_section(emulation, "timing");
    if (timing) {
        frontend_config_add_number(timing, "scanlines_visible", (double)nes->settings.timing.scanlines_visible);
        frontend_config_add_number(timing, "scanline_vblank", (double)nes->settings.timing.scanline_vblank);
        frontend_config_add_number(timing, "scanline_prerender", (double)nes->settings.timing.scanline_prerender);
        frontend_config_add_number(timing, "cycles_per_scanline", (double)nes->settings.timing.cycles_per_scanline);
        frontend_config_add_number(timing, "cpu_clock_rate", (double)nes->settings.timing.cpu_clock_rate);
    }

    cJSON *video = frontend_config_add_section(emulation, "video");
    if (video) {
        cJSON *palette = cJSON_CreateArray();
        if (palette) {
            for (int i = 0; i < 64; ++i) {
                cJSON_AddItemToArray(palette, cJSON_CreateNumber((double)nes->settings.video.palette[i]));
            }
            cJSON_AddItemToObject(video, "palette", palette);
        }
        frontend_config_add_number(video, "saturation", (double)nes->settings.video.saturation);
        frontend_config_add_number(video, "hue", (double)nes->settings.video.hue);
    }

    cJSON *audio = frontend_config_add_section(emulation, "audio");
    if (audio) {
        frontend_config_add_number(audio, "sample_rate", (double)nes->settings.audio.sample_rate);
        frontend_config_add_number(audio, "volume", (double)nes->settings.audio.volume);
    }
}

static bool frontend_config_save_json(const char *path, cJSON *root)
{
    if (!path || !root) {
        return false;
    }

    char *json = cJSON_Print(root);
    if (!json) {
        return false;
    }

    FILE *file = fopen(path, "wb");
    if (!file) {
        DEBUG_WARN("Failed to save settings file: %s", path);
        cJSON_free(json);
        return false;
    }

    size_t len = strlen(json);
    size_t written = fwrite(json, 1, len, file);
    fclose(file);
    cJSON_free(json);

    return written == len;
}

bool FrontendConfig_Save(const NES *nes)
{
    frontend_config_build_paths();

    bool saved_any = false;
    bool success = true;

    cJSON *general_root = cJSON_CreateObject();
    if (general_root) {
        cJSON_AddNumberToObject(general_root, "version", FRONTEND_SETTINGS_VERSION);
        frontend_config_serialize_frontend(general_root);
        frontend_config_serialize_emulation(general_root, nes);
        saved_any = true;
        success = frontend_config_save_json(frontend_general_settings_path, general_root) && success;
        cJSON_Delete(general_root);
    } else {
        success = false;
    }

    cJSON *input_root = cJSON_CreateObject();
    if (input_root) {
        cJSON_AddNumberToObject(input_root, "version", FRONTEND_SETTINGS_VERSION);
        frontend_config_serialize_input(input_root);
        saved_any = true;
        success = frontend_config_save_json(frontend_input_settings_path, input_root) && success;
        cJSON_Delete(input_root);
    } else {
        success = false;
    }

    cJSON *recent_root = cJSON_CreateObject();
    if (recent_root) {
        cJSON_AddNumberToObject(recent_root, "version", FRONTEND_SETTINGS_VERSION);
        frontend_config_serialize_recent_roms(recent_root);
        saved_any = true;
        success = frontend_config_save_json(frontend_recent_roms_path, recent_root) && success;
        cJSON_Delete(recent_root);
    } else {
        success = false;
    }

    if (success)
    {
        DEBUG_INFO("Settings saved successfully to %s", frontend_config_directory);
    }
    else
    {
        DEBUG_WARN("Failed to save settings.");
    }

    return saved_any && success;
}

static void frontend_config_load_key_array(const cJSON *array, SDL_Keycode *dst)
{
    if (!cJSON_IsArray(array)) {
        return;
    }

    int count = cJSON_GetArraySize((cJSON *)array);
    int limit = count < FRONTEND_NES_BUTTON_COUNT ? count : FRONTEND_NES_BUTTON_COUNT;
    for (int i = 0; i < limit; ++i) {
        cJSON *item = cJSON_GetArrayItem((cJSON *)array, i);
        if (cJSON_IsNumber(item)) {
            dst[i] = (SDL_Keycode)item->valueint;
        }
    }
}

static void frontend_config_load_bool(const cJSON *object, const char *name, bool *dst)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive((cJSON *)object, name);
    if (cJSON_IsBool(item)) {
        *dst = cJSON_IsTrue(item);
    }
}

static int frontend_config_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void frontend_config_load_gamepad_array(const cJSON *array)
{
    if (!cJSON_IsArray(array)) {
        return;
    }

    int count = cJSON_GetArraySize((cJSON *)array);
    int limit = count < FRONTEND_NES_BUTTON_COUNT ? count : FRONTEND_NES_BUTTON_COUNT;
    for (int i = 0; i < limit; ++i) {
        cJSON *item = cJSON_GetArrayItem((cJSON *)array, i);
        if (cJSON_IsNumber(item)) {
            frontend_input_gamepad_map[i] = (SDL_GamepadButton)item->valueint;
        }
    }
}

static void frontend_config_load_input(const cJSON *root)
{
    const cJSON *input = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "input");
    if (!cJSON_IsObject(input)) {
        return;
    }

    frontend_config_load_key_array(cJSON_GetObjectItemCaseSensitive((cJSON *)input, "controller1_keyboard"), frontend_input_keymap[0]);
    frontend_config_load_key_array(cJSON_GetObjectItemCaseSensitive((cJSON *)input, "controller2_keyboard"), frontend_input_keymap[1]);
    frontend_config_load_gamepad_array(cJSON_GetObjectItemCaseSensitive((cJSON *)input, "gamepad_buttons"));

    cJSON *enable_gamepad = cJSON_GetObjectItemCaseSensitive((cJSON *)input, "enable_gamepad");
    if (cJSON_IsBool(enable_gamepad)) {
        frontend_input_enable_gamepad = cJSON_IsTrue(enable_gamepad);
    }

    cJSON *enable_joystick_dpad = cJSON_GetObjectItemCaseSensitive((cJSON *)input, "enable_joystick_dpad");
    if (cJSON_IsBool(enable_joystick_dpad)) {
        frontend_input_enable_joystick_dpad = cJSON_IsTrue(enable_joystick_dpad);
    }

    cJSON *axis_deadzone = cJSON_GetObjectItemCaseSensitive((cJSON *)input, "axis_deadzone");
    if (cJSON_IsNumber(axis_deadzone)) {
        int deadzone = axis_deadzone->valueint;
        if (deadzone < 0) {
            deadzone = 0;
        }
        if (deadzone > 32767) {
            deadzone = 32767;
        }
        frontend_input_axis_deadzone = deadzone;
    }
}

static void frontend_config_load_recent_roms(const cJSON *root)
{
    const cJSON *recent_roms = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "recent_roms");
    if (!cJSON_IsArray(recent_roms)) {
        const cJSON *frontend = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "frontend");
        if (cJSON_IsObject(frontend)) {
            recent_roms = cJSON_GetObjectItemCaseSensitive((cJSON *)frontend, "recent_roms");
        }
    }

    if (!cJSON_IsArray(recent_roms)) {
        return;
    }

    Frontend_ClearRecentRoms();
    int count = cJSON_GetArraySize((cJSON *)recent_roms);
    for (int i = count - 1; i >= 0; --i) {
        cJSON *item = cJSON_GetArrayItem((cJSON *)recent_roms, i);
        if (cJSON_IsString(item) && item->valuestring && item->valuestring[0] != '\0') {
            Frontend_AddRecentRom(item->valuestring);
        }
    }
}

static void frontend_config_load_frontend(const cJSON *root)
{
    const cJSON *frontend = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "frontend");
    if (!cJSON_IsObject(frontend)) {
        return;
    }

    cJSON *theme = cJSON_GetObjectItemCaseSensitive((cJSON *)frontend, "theme");
    if (cJSON_IsNumber(theme)) {
        int selected_theme = frontend_config_clamp_int(theme->valueint, FRONTEND_THEME_DEFAULT, FRONTEND_THEME_EXCELLENCY);
        frontend_current_theme = (Frontend_Theme)selected_theme;
        frontend_requestReload = true;
    }

    cJSON *fullscreen = cJSON_GetObjectItemCaseSensitive((cJSON *)frontend, "fullscreen");
    if (cJSON_IsBool(fullscreen)) {
        frontend_fullscreen = cJSON_IsTrue(fullscreen);
    }

    cJSON *font_size = cJSON_GetObjectItemCaseSensitive((cJSON *)frontend, "font_size");
    if (cJSON_IsNumber(font_size)) {
        frontend_font_size = (float)font_size->valuedouble;
        frontend_requestReload = true;
    }

    cJSON *font_path = cJSON_GetObjectItemCaseSensitive((cJSON *)frontend, "font_path");
    if (cJSON_IsString(font_path) && font_path->valuestring && font_path->valuestring[0] != '\0') {
        frontend_config_set_string(frontend_font_path, sizeof(frontend_font_path), font_path->valuestring);
        frontend_requestReload = true;
    }

    cJSON *master_volume = cJSON_GetObjectItemCaseSensitive((cJSON *)frontend, "master_volume");
    if (cJSON_IsNumber(master_volume)) {
        frontend_master_volume = (float)master_volume->valuedouble;
    }

    frontend_config_load_bool(frontend, "show_cpu_window", &frontend_showCpuWindow);
    frontend_config_load_bool(frontend, "show_disassembler", &frontend_showDisassembler);
    frontend_config_load_bool(frontend, "show_game_screen", &frontend_showGameScreen);
    frontend_config_load_bool(frontend, "show_profiler_window", &frontend_showProfilerWindow);
    frontend_config_load_bool(frontend, "show_rom_info_window", &frontend_showRomInfoWindow);
    frontend_config_load_bool(frontend, "show_ppu_viewer", &frontend_showPpuViewer);
    frontend_config_load_bool(frontend, "show_log", &frontend_showLog);
    frontend_config_load_bool(frontend, "show_memory_viewer", &frontend_showMemoryViewer);
    frontend_config_load_bool(frontend, "show_settings_window", &frontend_showSettingsWindow);
    frontend_config_load_bool(frontend, "show_about_window", &frontend_showAboutWindow);
    frontend_config_load_bool(frontend, "show_credits_window", &frontend_showCreditsWindow);
    frontend_config_load_bool(frontend, "show_licence_window", &frontend_showLicenceWindow);
}

static void frontend_config_load_emulation(const cJSON *root, NES *nes)
{
    if (!nes) {
        return;
    }

    const cJSON *emulation = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "emulation");
    if (!cJSON_IsObject(emulation)) {
        return;
    }

    cJSON *region = cJSON_GetObjectItemCaseSensitive((cJSON *)emulation, "region");
    if (cJSON_IsNumber(region)) {
        int selected_region = frontend_config_clamp_int(region->valueint, NES_REGION_NTSC, NES_REGION_CUSTOM);
        NES_SetRegionPreset(nes, (NES_Region)selected_region);
    }

    cJSON *cpu_mode = cJSON_GetObjectItemCaseSensitive((cJSON *)emulation, "cpu_mode");
    if (cJSON_IsNumber(cpu_mode)) {
        nes->settings.cpu_mode = frontend_config_clamp_int(cpu_mode->valueint, 0, 2);
    }

    cJSON *ppu_mode = cJSON_GetObjectItemCaseSensitive((cJSON *)emulation, "ppu_mode");
    if (cJSON_IsNumber(ppu_mode)) {
        nes->settings.ppu_mode = frontend_config_clamp_int(ppu_mode->valueint, 0, 3);
    }

    const cJSON *timing = cJSON_GetObjectItemCaseSensitive((cJSON *)emulation, "timing");
    if (cJSON_IsObject(timing)) {
        cJSON *scanlines_visible = cJSON_GetObjectItemCaseSensitive((cJSON *)timing, "scanlines_visible");
        if (cJSON_IsNumber(scanlines_visible)) {
            nes->settings.timing.scanlines_visible = scanlines_visible->valueint;
        }

        cJSON *scanline_vblank = cJSON_GetObjectItemCaseSensitive((cJSON *)timing, "scanline_vblank");
        if (cJSON_IsNumber(scanline_vblank)) {
            nes->settings.timing.scanline_vblank = scanline_vblank->valueint;
        }

        cJSON *scanline_prerender = cJSON_GetObjectItemCaseSensitive((cJSON *)timing, "scanline_prerender");
        if (cJSON_IsNumber(scanline_prerender)) {
            nes->settings.timing.scanline_prerender = scanline_prerender->valueint;
        }

        cJSON *cycles_per_scanline = cJSON_GetObjectItemCaseSensitive((cJSON *)timing, "cycles_per_scanline");
        if (cJSON_IsNumber(cycles_per_scanline)) {
            nes->settings.timing.cycles_per_scanline = cycles_per_scanline->valueint;
        }

        cJSON *cpu_clock_rate = cJSON_GetObjectItemCaseSensitive((cJSON *)timing, "cpu_clock_rate");
        if (cJSON_IsNumber(cpu_clock_rate)) {
            nes->settings.timing.cpu_clock_rate = (float)cpu_clock_rate->valuedouble;
        }
    }

    const cJSON *video = cJSON_GetObjectItemCaseSensitive((cJSON *)emulation, "video");
    if (cJSON_IsObject(video)) {
        const cJSON *palette = cJSON_GetObjectItemCaseSensitive((cJSON *)video, "palette");
        if (cJSON_IsArray(palette)) {
            int palette_count = cJSON_GetArraySize((cJSON *)palette);
            int limit = palette_count < 64 ? palette_count : 64;
            for (int i = 0; i < limit; ++i) {
                cJSON *entry = cJSON_GetArrayItem((cJSON *)palette, i);
                if (cJSON_IsNumber(entry)) {
                    nes->settings.video.palette[i] = (uint32_t)entry->valuedouble;
                }
            }
        }

        cJSON *saturation = cJSON_GetObjectItemCaseSensitive((cJSON *)video, "saturation");
        if (cJSON_IsNumber(saturation)) {
            nes->settings.video.saturation = (float)saturation->valuedouble;
        }

        cJSON *hue = cJSON_GetObjectItemCaseSensitive((cJSON *)video, "hue");
        if (cJSON_IsNumber(hue)) {
            nes->settings.video.hue = (float)hue->valuedouble;
        }
    }

    const cJSON *audio = cJSON_GetObjectItemCaseSensitive((cJSON *)emulation, "audio");
    if (cJSON_IsObject(audio)) {
        cJSON *sample_rate = cJSON_GetObjectItemCaseSensitive((cJSON *)audio, "sample_rate");
        if (cJSON_IsNumber(sample_rate)) {
            nes->settings.audio.sample_rate = sample_rate->valueint;
        }

        cJSON *volume = cJSON_GetObjectItemCaseSensitive((cJSON *)audio, "volume");
        if (cJSON_IsNumber(volume)) {
            nes->settings.audio.volume = (float)volume->valuedouble;
        }
    }
}

static cJSON *frontend_config_load_json(const char *path)
{
    if (!path || path[0] == '\0') {
        return NULL;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    long size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    char *data = (char *)malloc((size_t)size + 1U);
    if (!data) {
        fclose(file);
        return NULL;
    }

    size_t read_len = fread(data, 1, (size_t)size, file);
    fclose(file);
    data[read_len] = '\0';

    cJSON *root = cJSON_Parse(data);
    free(data);
    return root;
}

bool FrontendConfig_Load(NES *nes)
{
    frontend_config_build_paths();

    bool loaded_any = false;
    bool loaded_general = false;
    bool loaded_recent_roms = false;

    cJSON *general_root = frontend_config_load_json(frontend_general_settings_path);
    if (general_root) {
        frontend_config_load_frontend(general_root);
        frontend_config_load_emulation(general_root, nes);
        loaded_any = true;
        loaded_general = true;
        cJSON_Delete(general_root);
    }

    cJSON *input_root = frontend_config_load_json(frontend_input_settings_path);
    if (input_root) {
        frontend_config_load_input(input_root);

        // Backward compatibility: migrate combined legacy settings from input.json.
        if (!loaded_general) {
            frontend_config_load_frontend(input_root);
            frontend_config_load_emulation(input_root, nes);
            loaded_general = true;
        }

        if (!loaded_recent_roms) {
            frontend_config_load_recent_roms(input_root);
            loaded_recent_roms = true;
        }

        loaded_any = true;
        cJSON_Delete(input_root);
    }

    cJSON *recent_root = frontend_config_load_json(frontend_recent_roms_path);
    if (recent_root) {
        frontend_config_load_recent_roms(recent_root);
        loaded_any = true;
        loaded_recent_roms = true;
        cJSON_Delete(recent_root);
    }

    if (frontend_window) {
        Frontend_SetFullscreen(frontend_fullscreen);
    }

    return loaded_any;
}

void FrontendConfig_BeginKeyRebind(int controller, FrontendNesButton button)
{
    if (controller < 0 || controller > 1) {
        return;
    }
    if (button < 0 || button >= FRONTEND_NES_BUTTON_COUNT) {
        return;
    }

    frontend_rebind_controller = controller;
    frontend_rebind_button = (int)button;
}

bool FrontendConfig_IsWaitingForKey(void)
{
    return frontend_rebind_controller >= 0 && frontend_rebind_button >= 0;
}

int FrontendConfig_GetRebindController(void)
{
    return frontend_rebind_controller;
}

int FrontendConfig_GetRebindButton(void)
{
    return frontend_rebind_button;
}

bool FrontendConfig_CaptureRebindKey(SDL_Keycode key)
{
    if (!FrontendConfig_IsWaitingForKey()) {
        return false;
    }

    if (frontend_rebind_controller < 0 || frontend_rebind_controller > 1) {
        return false;
    }

    if (frontend_rebind_button < 0 || frontend_rebind_button >= FRONTEND_NES_BUTTON_COUNT) {
        return false;
    }

    if (key == SDLK_ESCAPE) {
        frontend_rebind_controller = -1;
        frontend_rebind_button = -1;
        return false;
    }

    frontend_input_keymap[frontend_rebind_controller][frontend_rebind_button] = key;
    frontend_rebind_controller = -1;
    frontend_rebind_button = -1;
    return true;
}
