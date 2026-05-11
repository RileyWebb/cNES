#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h> // Primary header for SDL_gpu
#include <SDL3/SDL_thread.h>
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
#include <stdarg.h>

#include "debug.h"
#include "profiler.h"

#include "cNES/nes.h"
#include "cNES/cpu.h"
#include "cNES/apu.h"
#include "cNES/ppu.h"
#include "cNES/bus.h"
#include "cNES/rom.h"
#include "cNES/debugging.h"
#include "cNES/version.h"

#include "frontend/cimgui_markdown.h"

#include "frontend/frontend.h"
#include "frontend/frontend_internal.h"
#include "frontend/frontend_style.h"
#include "frontend/frontend_profiler.h"
#include "frontend/frontend_config.h"

// --- SDL and ImGui Globals ---
SDL_Window *frontend_window;
SDL_GPUDevice *gpu_device;
ImGuiIO *ioptr;
static SDL_AudioStream *frontend_audio_stream = NULL;

// --- UI State ---
bool frontend_paused = true;
frontend_debug_log frontend_log_buffer[8192];
size_t frontend_log_count = 0;
static float frontend_fps = 0.0f;
static uint64_t frontend_frame_start = 0;
static double frontend_frame_time = 0.0;
float frontend_master_volume = 0.8f;
bool frontend_fullscreen = false;

static char frontend_recentRoms[FRONTEND_MAX_RECENT_ROMS][256];
static int frontend_recentRomsCount = 0;
Frontend_Theme frontend_current_theme = FRONTEND_THEME_DARK;

uint16_t frontend_memoryViewerAddress = 0x0000;
uint8_t frontend_memorySnapshot[0x10000] = {0};
int frontend_memoryViewerRows = 16;

int frontend_ppuViewerSelectedPalette = 0;
static bool frontend_openSaveStateModal = false;
static bool frontend_openLoadStateModal = false;
static int frontend_selectedSaveLoadSlot = 0;

uint8_t nes_input_state[2] = {0, 0};
static uint8_t frontend_keyboard_input_state[2] = {0, 0};
static uint8_t frontend_gamepad_input_state = 0;
static SDL_Gamepad *frontend_gamepad = NULL;
static uint8_t frontend_joystick_input_state = 0;
static SDL_Joystick *frontend_joystick = NULL;

bool frontend_showCpuWindow = true;
bool frontend_showDisassembler = true;
bool frontend_showGameScreen = true;
bool frontend_showProfilerWindow = true;
bool frontend_showRomInfoWindow = true;
bool frontend_showPpuViewer = false;
bool frontend_showLog = true;
bool frontend_showMemoryViewer = false;
bool frontend_showSettingsWindow = false;

bool frontend_showAboutWindow = false;
bool frontend_showCreditsWindow = false;
bool frontend_showLicenceWindow = false;

float frontend_font_size = 24.0f;
char frontend_font_path[FRONTEND_FONT_PATH_MAX] = "data/fonts/JetBrainsMono.ttf";
bool frontend_requestReload = false;
static float frontend_last_font_dpi_scale = 1.0f;

static bool frontend_first_frame = true;

// --- PPU Viewer SDL_gpu Resources ---
SDL_GPUTexture *pt_texture0 = NULL;
SDL_GPUTexture *pt_texture1 = NULL;
SDL_GPUSampler *pt_sampler = NULL;
SDL_GPUTransferBuffer *pt_transfer_buffer = NULL;

// --- Game Screen SDL_gpu Resources ---
SDL_GPUTexture *ppu_game_texture = NULL;
SDL_GPUSampler *ppu_game_sampler = NULL;
SDL_GPUTransferBuffer *ppu_game_transfer_buffer = NULL;
SDL_GPUTextureSamplerBinding ppu_game_texture_sampler_binding = {0};

typedef struct FrontendEmulationState {
    NES *nes;
    SDL_Thread *thread;
    SDL_Mutex *mutex;
    SDL_Condition *condition;
    bool thread_running;
    bool stop_requested;
    bool paused;
    bool reset_requested;
    bool step_cpu_requested;
    bool step_frame_requested;
    uint8_t controllers[2];
    uint32_t frame_buffer[PPU_FRAMEBUFFER_WIDTH * PPU_FRAMEBUFFER_HEIGHT];
    bool frame_buffer_valid;
} FrontendEmulationState;

static FrontendEmulationState frontend_emulation = {0};

static void Frontend_DestroyAudioStream(void)
{
    if (frontend_audio_stream) {
        SDL_DestroyAudioStream(frontend_audio_stream);
        frontend_audio_stream = NULL;
    }
}

static void Frontend_EnsureAudioStream(NES *nes)
{
    if (!nes || !nes->apu) {
        Frontend_DestroyAudioStream();
        return;
    }

    if (frontend_audio_stream) {
        return;
    }

    SDL_AudioSpec spec = {
        .format = SDL_AUDIO_F32,
        .channels = 1,
        .freq = nes->settings.audio.sample_rate > 0 ? nes->settings.audio.sample_rate : 44100,
    };

    frontend_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!frontend_audio_stream) {
        DEBUG_WARN("Audio stream creation failed: %s", SDL_GetError());
        return;
    }

    SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(frontend_audio_stream));
}

static void Frontend_PumpAudio(NES *nes)
{
    if (!nes || !nes->apu || !frontend_audio_stream) {
        return;
    }

    float audio_buffer[2048];
    size_t samples_read = APU_ReadSamples(nes->apu, audio_buffer, SDL_arraysize(audio_buffer));
    if (samples_read > 0) {
        (void)SDL_PutAudioStreamData(frontend_audio_stream, audio_buffer, (int)(samples_read * sizeof(float)));
    }
}

void Frontend_RequestStepFrame(void)
{
    if (!frontend_emulation.mutex || !frontend_emulation.condition) {
        return;
    }

    SDL_LockMutex(frontend_emulation.mutex);
    frontend_emulation.step_frame_requested = true;
    frontend_emulation.paused = true;
    frontend_paused = true;
    SDL_BroadcastCondition(frontend_emulation.condition);
    SDL_UnlockMutex(frontend_emulation.mutex);
}

void Frontend_RequestPause(bool paused)
{
    if (!frontend_emulation.mutex || !frontend_emulation.condition) {
        frontend_paused = paused;
        return;
    }

    SDL_LockMutex(frontend_emulation.mutex);
    frontend_emulation.paused = paused;
    frontend_paused = paused;
    SDL_BroadcastCondition(frontend_emulation.condition);
    SDL_UnlockMutex(frontend_emulation.mutex);
}

void Frontend_RequestReset(void)
{
    if (!frontend_emulation.mutex || !frontend_emulation.condition) {
        return;
    }

    SDL_LockMutex(frontend_emulation.mutex);
    frontend_emulation.reset_requested = true;
    SDL_BroadcastCondition(frontend_emulation.condition);
    SDL_UnlockMutex(frontend_emulation.mutex);
}

void Frontend_RequestStepCpu(void)
{
    if (!frontend_emulation.mutex || !frontend_emulation.condition) {
        return;
    }

    SDL_LockMutex(frontend_emulation.mutex);
    frontend_emulation.step_cpu_requested = true;
    frontend_emulation.paused = true;
    frontend_paused = true;
    SDL_BroadcastCondition(frontend_emulation.condition);
    SDL_UnlockMutex(frontend_emulation.mutex);
}

void Frontend_AddRecentRom(const char *path)
{
    if (!path || !path[0]) {
        return;
    }

    int existing_index = -1;
    for (int i = 0; i < frontend_recentRomsCount; ++i)
    {
        if (strncmp(frontend_recentRoms[i], path, sizeof(frontend_recentRoms[i])) == 0) {
            existing_index = i;
            break;
        }
    }

    if (existing_index > 0)
    {
        char temp[FRONTEND_RECENT_ROM_PATH_MAX];
        (void)snprintf(temp, sizeof(temp), "%s", frontend_recentRoms[existing_index]);
        for (int i = existing_index; i > 0; --i)
        {
            (void)snprintf(frontend_recentRoms[i], sizeof(frontend_recentRoms[i]), "%s", frontend_recentRoms[i - 1]);
        }
        (void)snprintf(frontend_recentRoms[0], sizeof(frontend_recentRoms[0]), "%s", temp);
    }
    else if (existing_index < 0)
    {
        if (frontend_recentRomsCount < FRONTEND_MAX_RECENT_ROMS) {
            ++frontend_recentRomsCount;
        }
        for (int i = frontend_recentRomsCount - 1; i > 0; --i)
        {
            (void)snprintf(frontend_recentRoms[i], sizeof(frontend_recentRoms[i]), "%s", frontend_recentRoms[i - 1]);
        }
        (void)snprintf(frontend_recentRoms[0], sizeof(frontend_recentRoms[0]), "%s", path);
    }
}

void Frontend_ClearRecentRoms(void)
{
    memset(frontend_recentRoms, 0, sizeof(frontend_recentRoms));
    frontend_recentRomsCount = 0;
}

int Frontend_GetRecentRomCount(void)
{
    return frontend_recentRomsCount;
}

const char *Frontend_GetRecentRom(int index)
{
    if (index < 0 || index >= frontend_recentRomsCount) {
        return NULL;
    }
    return frontend_recentRoms[index];
}

void Frontend_ApplyTheme(Frontend_Theme theme)
{
    frontend_current_theme = theme;
    FrontendStyle_Apply(theme);
}

static float Frontend_GetWindowDpiScale(void)
{
    if (!frontend_window) {
        return 1.0f;
    }

    float scale = SDL_GetWindowDisplayScale(frontend_window);
    if (scale <= 0.0f) {
        scale = SDL_GetWindowPixelDensity(frontend_window);
    }
    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    return scale;
}

static bool Frontend_RebuildFontsForDpi(float dpi_scale)
{
    if (!ioptr || !ioptr->Fonts) {
        return false;
    }

    if (dpi_scale <= 0.0f) {
        dpi_scale = 1.0f;
    }

    float size_pixels = frontend_font_size * dpi_scale;
    if (size_pixels < 8.0f) {
        size_pixels = 8.0f;
    }

    ImFontAtlas *atlas = ioptr->Fonts;
    ImFontAtlas_Clear(atlas);

    const ImWchar *glyph_ranges = ImFontAtlas_GetGlyphRangesDefault(atlas);
    ImFontConfig *font_cfg = ImFontConfig_ImFontConfig();
    ImFont *font = NULL;

    if (font_cfg) {
        font_cfg->OversampleH = 2;
        font_cfg->OversampleV = 2;
        font_cfg->RasterizerDensity = dpi_scale;
        font = ImFontAtlas_AddFontFromFileTTF(atlas, frontend_font_path, size_pixels, font_cfg, glyph_ranges);
    }

    if (!font) {
        font = ImFontAtlas_AddFontDefault(atlas, NULL);
        DEBUG_WARN("UI font load failed for '%s'; using ImGui default font", frontend_font_path);
    }

    if (font_cfg) {
        ImFontConfig_destroy(font_cfg);
    }

    ioptr->FontDefault = font;
    ioptr->FontGlobalScale = 1.0f / dpi_scale;

    ImGui_ImplSDLGPU3_DestroyFontsTexture();
    ImGui_ImplSDLGPU3_CreateFontsTexture();

    frontend_last_font_dpi_scale = dpi_scale;
    return true;
}

static void Frontend_ApplyPendingUiReload(void)
{
    if (!frontend_requestReload) {
        return;
    }

    frontend_requestReload = false;
    Frontend_ApplyTheme(frontend_current_theme);

    if (!Frontend_RebuildFontsForDpi(Frontend_GetWindowDpiScale())) {
        DEBUG_WARN("Failed to rebuild UI fonts");
    }
}

void Frontend_SetFullscreen(bool enabled)
{
    if (!frontend_window) {
        frontend_fullscreen = enabled;
        return;
    }

    if (!SDL_SetWindowFullscreen(frontend_window, enabled)) {
        DEBUG_WARN("Failed to set fullscreen: %s", SDL_GetError());
        return;
    }

    frontend_fullscreen = enabled;
}

void Frontend_ToggleFullscreen(void)
{
    Frontend_SetFullscreen(!frontend_fullscreen);
}

void Frontend_SyncControllerState(void)
{
    if (!frontend_emulation.mutex) {
        return;
    }

    SDL_LockMutex(frontend_emulation.mutex);
    frontend_emulation.controllers[0] = nes_input_state[0];
    frontend_emulation.controllers[1] = nes_input_state[1];
    SDL_UnlockMutex(frontend_emulation.mutex);
}

bool Frontend_CopyFrameBuffer(uint32_t *dest, size_t pixel_count)
{
    if (!dest || pixel_count < (size_t)(PPU_FRAMEBUFFER_WIDTH * PPU_FRAMEBUFFER_HEIGHT) || !frontend_emulation.mutex) {
        return false;
    }

    SDL_LockMutex(frontend_emulation.mutex);
    if (frontend_emulation.frame_buffer_valid) {
        memcpy(dest, frontend_emulation.frame_buffer, sizeof(frontend_emulation.frame_buffer));
        SDL_UnlockMutex(frontend_emulation.mutex);
        return true;
    }
    SDL_UnlockMutex(frontend_emulation.mutex);
    return false;
}

static void Frontend_RunOneFrame(NES *nes)
{
    if (!nes || !nes->ppu || !nes->cpu)
    {
        return;
    }

    FrontendProfiler_Step(nes);
}

static void Frontend_TryOpenFirstGamepad(void)
{
    if (frontend_gamepad || !frontend_input_enable_gamepad) {
        return;
    }

    int count = 0;
    SDL_JoystickID *gamepads = SDL_GetGamepads(&count);
    if (!gamepads || count <= 0) {
        SDL_free(gamepads);
        return;
    }

    frontend_gamepad = SDL_OpenGamepad(gamepads[0]);
    SDL_free(gamepads);
}

static void Frontend_CloseJoystick(void)
{
    if (frontend_joystick) {
        SDL_CloseJoystick(frontend_joystick);
        frontend_joystick = NULL;
    }
    frontend_joystick_input_state = 0;
}

static void Frontend_TryOpenFirstJoystick(void)
{
    if (frontend_joystick || !frontend_input_enable_joystick_dpad) {
        return;
    }

    int count = 0;
    SDL_JoystickID *joysticks = SDL_GetJoysticks(&count);
    if (!joysticks || count <= 0) {
        SDL_free(joysticks);
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        if (frontend_gamepad && SDL_GetGamepadID(frontend_gamepad) == joysticks[i]) {
            continue;
        }

        frontend_joystick = SDL_OpenJoystick(joysticks[i]);
        if (frontend_joystick) {
            break;
        }
    }

    SDL_free(joysticks);
}

static void Frontend_UpdateKeyboardInputState(void)
{
    frontend_keyboard_input_state[0] = 0;
    frontend_keyboard_input_state[1] = 0;

    int key_count = 0;
    const bool *keys = SDL_GetKeyboardState(&key_count);
    if (!keys) {
        return;
    }

    for (int player = 0; player < 2; ++player)
    {
        uint8_t state = 0;
        for (int button = 0; button < FRONTEND_NES_BUTTON_COUNT; ++button)
        {
            SDL_Scancode scancode = SDL_GetScancodeFromKey(frontend_input_keymap[player][button], NULL);
            if (scancode != SDL_SCANCODE_UNKNOWN && (int)scancode < key_count && keys[scancode]) {
                state |= (uint8_t)(1u << button);
            }
        }
        frontend_keyboard_input_state[player] = state;
    }
}

static void Frontend_UpdateGamepadInputState(void)
{
    frontend_gamepad_input_state = 0;

    if (!frontend_input_enable_gamepad || !frontend_gamepad) {
        return;
    }

    for (int button = 0; button < FRONTEND_NES_BUTTON_COUNT; ++button)
    {
        SDL_GamepadButton mapped = frontend_input_gamepad_map[button];
        if (mapped != SDL_GAMEPAD_BUTTON_INVALID && SDL_GetGamepadButton(frontend_gamepad, mapped)) {
            frontend_gamepad_input_state |= (uint8_t)(1u << button);
        }
    }

    Sint16 axis_x = SDL_GetGamepadAxis(frontend_gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    Sint16 axis_y = SDL_GetGamepadAxis(frontend_gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    int deadzone = frontend_input_axis_deadzone;

    if (axis_x <= -deadzone) frontend_gamepad_input_state |= (uint8_t)(1u << FRONTEND_NES_BUTTON_LEFT);
    if (axis_x >= deadzone) frontend_gamepad_input_state |= (uint8_t)(1u << FRONTEND_NES_BUTTON_RIGHT);
    if (axis_y <= -deadzone) frontend_gamepad_input_state |= (uint8_t)(1u << FRONTEND_NES_BUTTON_UP);
    if (axis_y >= deadzone) frontend_gamepad_input_state |= (uint8_t)(1u << FRONTEND_NES_BUTTON_DOWN);
}

static void Frontend_HandleGamepadButtonEvent(SDL_Event *event)
{
    (void)event;
    Frontend_UpdateGamepadInputState();
}

static void Frontend_HandleGamepadAxisEvent(SDL_Event *event)
{
    (void)event;
    Frontend_UpdateGamepadInputState();
}

static void Frontend_UpdateJoystickInputState(void)
{
    frontend_joystick_input_state = 0;

    if (!frontend_input_enable_joystick_dpad || !frontend_joystick) {
        return;
    }

    if (SDL_GetNumJoystickHats(frontend_joystick) > 0)
    {
        Uint8 hat = SDL_GetJoystickHat(frontend_joystick, 0);
        if (hat & SDL_HAT_UP) frontend_joystick_input_state |= (uint8_t)(1u << FRONTEND_NES_BUTTON_UP);
        if (hat & SDL_HAT_DOWN) frontend_joystick_input_state |= (uint8_t)(1u << FRONTEND_NES_BUTTON_DOWN);
        if (hat & SDL_HAT_LEFT) frontend_joystick_input_state |= (uint8_t)(1u << FRONTEND_NES_BUTTON_LEFT);
        if (hat & SDL_HAT_RIGHT) frontend_joystick_input_state |= (uint8_t)(1u << FRONTEND_NES_BUTTON_RIGHT);
    }

    if (frontend_joystick_input_state == 0 && SDL_GetNumJoystickAxes(frontend_joystick) >= 2)
    {
        Sint16 axis_x = SDL_GetJoystickAxis(frontend_joystick, 0);
        Sint16 axis_y = SDL_GetJoystickAxis(frontend_joystick, 1);
        int deadzone = frontend_input_axis_deadzone;

        if (axis_x <= -deadzone) frontend_joystick_input_state |= (uint8_t)(1u << FRONTEND_NES_BUTTON_LEFT);
        if (axis_x >= deadzone) frontend_joystick_input_state |= (uint8_t)(1u << FRONTEND_NES_BUTTON_RIGHT);
        if (axis_y <= -deadzone) frontend_joystick_input_state |= (uint8_t)(1u << FRONTEND_NES_BUTTON_UP);
        if (axis_y >= deadzone) frontend_joystick_input_state |= (uint8_t)(1u << FRONTEND_NES_BUTTON_DOWN);
    }
}

static int SDLCALL Frontend_EmulationThread(void *data)
{
    FrontendEmulationState *state = (FrontendEmulationState *)data;
    Profiler_SetThreadName("Emulation Thread");

    while (true)
    {
        bool do_reset = false;
        bool do_step_cpu = false;
        bool do_step_frame = false;
        bool paused = false;
        uint8_t controllers[2] = {0, 0};

        SDL_LockMutex(state->mutex);
        while (!state->stop_requested && state->paused && !state->reset_requested && !state->step_cpu_requested && !state->step_frame_requested)
        {
            SDL_WaitCondition(state->condition, state->mutex);
        }

        if (state->stop_requested)
        {
            SDL_UnlockMutex(state->mutex);
            break;
        }

        do_reset = state->reset_requested;
        do_step_cpu = state->step_cpu_requested;
        do_step_frame = state->step_frame_requested;
        paused = state->paused;
        controllers[0] = state->controllers[0];
        controllers[1] = state->controllers[1];
        state->reset_requested = false;
        state->step_cpu_requested = false;
        state->step_frame_requested = false;
        SDL_UnlockMutex(state->mutex);

        if (do_reset && state->nes)
        {
            NES_Reset(state->nes);
        }

        if (!state->nes)
        {
            SDL_Delay(1);
            continue;
        }

        NES_SetController(state->nes, 0, controllers[0]);
        NES_SetController(state->nes, 1, controllers[1]);

        if (do_step_cpu)
        {
            if (state->nes->cpu && CPU_Step(state->nes->cpu) == -1)
            {
                DEBUG_ERROR("CPU execution halted due to error");
            }
        }
        else if (do_step_frame || !paused)
        {
            Frontend_RunOneFrame(state->nes);

            SDL_LockMutex(state->mutex);
            memcpy(state->frame_buffer, state->nes->ppu->framebuffer, sizeof(state->frame_buffer));
            state->frame_buffer_valid = true;
            SDL_UnlockMutex(state->mutex);
        }
    }

    SDL_LockMutex(state->mutex);
    state->thread_running = false;
    SDL_UnlockMutex(state->mutex);
    return 0;
}

void Frontend_StartEmulation(NES *nes)
{
    if (!nes)
    {
        return;
    }

    if (!frontend_emulation.mutex)
    {
        frontend_emulation.mutex = SDL_CreateMutex();
    }
    if (!frontend_emulation.condition)
    {
        frontend_emulation.condition = SDL_CreateCondition();
    }
    if (!frontend_emulation.mutex || !frontend_emulation.condition)
    {
        DEBUG_FATAL("Failed to create emulation synchronization primitives: %s", SDL_GetError());
        return;
    }

    SDL_LockMutex(frontend_emulation.mutex);
    frontend_emulation.nes = nes;
    frontend_emulation.stop_requested = false;
    frontend_emulation.paused = frontend_paused;
    frontend_emulation.reset_requested = false;
    frontend_emulation.step_cpu_requested = false;
    frontend_emulation.step_frame_requested = false;
    frontend_emulation.frame_buffer_valid = false;
    frontend_emulation.controllers[0] = nes_input_state[0];
    frontend_emulation.controllers[1] = nes_input_state[1];
    SDL_UnlockMutex(frontend_emulation.mutex);

    if (!frontend_emulation.thread_running)
    {
        Frontend_DestroyAudioStream();
        Frontend_EnsureAudioStream(nes);
        if (nes->apu) {
            APU_SetVolume(nes->apu, frontend_master_volume * nes->settings.audio.volume);
        }
        frontend_emulation.thread_running = true;
        frontend_emulation.thread = SDL_CreateThread(Frontend_EmulationThread, "cNES Emulation", &frontend_emulation);
        if (!frontend_emulation.thread)
        {
            SDL_LockMutex(frontend_emulation.mutex);
            frontend_emulation.thread_running = false;
            SDL_UnlockMutex(frontend_emulation.mutex);
            DEBUG_FATAL("Failed to create emulation thread: %s", SDL_GetError());
        }
    }
}

static void Frontend_StopEmulation(void)
{
    if (!frontend_emulation.thread || !frontend_emulation.mutex || !frontend_emulation.condition)
    {
        return;
    }

    SDL_LockMutex(frontend_emulation.mutex);
    frontend_emulation.stop_requested = true;
    SDL_BroadcastCondition(frontend_emulation.condition);
    SDL_UnlockMutex(frontend_emulation.mutex);

    SDL_WaitThread(frontend_emulation.thread, NULL);
    frontend_emulation.thread = NULL;

    SDL_LockMutex(frontend_emulation.mutex);
    frontend_emulation.thread_running = false;
    SDL_UnlockMutex(frontend_emulation.mutex);
}

void Frontend_Init()
{
    if (!SDL_Init(SDL_INIT_VIDEO| SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS))
    {
        DEBUG_FATAL("Could not initialize SDL: %s", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SDL Initialization Error", SDL_GetError(), NULL);
        exit(1);
    }

    FrontendConfig_InitDefaults();
    (void)FrontendConfig_Load(NULL);

    frontend_window = SDL_CreateWindow("cNES", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!frontend_window)
    {
        DEBUG_FATAL("Could not create window: %s", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Window Creation Error", SDL_GetError(), NULL);
        SDL_Quit();
        exit(1);
    }

    DEBUG_DEBUG("SDL Window created");

    gpu_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV,
#ifdef DEBUG
                                     true,
#else
                                     false,
#endif
                                     NULL);

    if (!gpu_device)
    {
        DEBUG_FATAL("Unable to create GPU Device: %s", SDL_GetError());
        SDL_DestroyWindow(frontend_window);
        SDL_Quit();
        exit(1);
    }

    DEBUG_DEBUG("GPU Device created with %s driver", SDL_GetGPUDeviceDriver(gpu_device));

    if (!SDL_ClaimWindowForGPUDevice(gpu_device, frontend_window))
    {
        DEBUG_FATAL("Unable to claim window for GPU: %s", SDL_GetError());
        SDL_DestroyGPUDevice(gpu_device);
        SDL_DestroyWindow(frontend_window);
        SDL_Quit();
        exit(1);
    }
    
    SDL_SetGPUSwapchainParameters(gpu_device, frontend_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_IMMEDIATE);

    if (frontend_fullscreen) {
        Frontend_SetFullscreen(true);
    }

    //ImGui context
    ImGuiContext *ctx = igCreateContext(NULL);
    ioptr = igGetIO_ContextPtr(ctx);
    ioptr->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ioptr->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ioptr->ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;
    ioptr->ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports;
    ioptr->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // ioptr->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImPlot_SetCurrentContext(ImPlot_CreateContext());

    DEBUG_DEBUG("ImGui context created");

    ImGuiStyle *style = igGetStyle();
    if (ioptr->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style->WindowRounding = 0.0f;
        style->Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Renderer backends for SDL_gpu
    ImGui_ImplSDL3_InitForSDLGPU(frontend_window);
    ImGui_ImplSDLGPU3_InitInfo init_info = 
    {
        .Device = gpu_device,
        .ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(gpu_device, frontend_window),
        .MSAASamples = SDL_GPU_SAMPLECOUNT_1, // No MSAA
    };

    ImGui_ImplSDLGPU3_Init(&init_info);

    frontend_requestReload = true;
    Frontend_ApplyPendingUiReload();
    Frontend_TryOpenFirstGamepad();


    FrontendProfiler_Init();
    DEBUG_RegisterCallback(Frontend_Log);

    DEBUG_INFO("cNES Initialized");
}

static float frontend_status_bar_height = 0;

void Frontend_DrawStatusBar(NES *nes)
{
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking;

    const char *version_text = CNES_VERSION_BUILD_STRING; // REFACTOR-NOTE: Consistent versioning
    ImVec2 version_text_size;
    igCalcTextSize(&version_text_size, version_text, NULL, false, 0);

    // Calculate status bar height based on current style (font size + frame padding)
    // igGetFrameHeight() = FontSize + style.FramePadding.y * 2
    frontend_status_bar_height = igGetFrameHeight(); 

    ImGuiViewport *viewport = igGetMainViewport();
    // Ensure the status bar height is at least the text line height with spacing, plus a little extra if frame padding is small
    float min_height = igGetTextLineHeightWithSpacing() + igGetStyle()->WindowPadding.y; // Ensure text fits comfortably
    if (frontend_status_bar_height < min_height) {
        frontend_status_bar_height = min_height;
    }

    igSetNextWindowSize((ImVec2){viewport->WorkSize.x, frontend_status_bar_height}, ImGuiCond_Always);
    igSetNextWindowPos((ImVec2){viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - frontend_status_bar_height}, ImGuiCond_Always, (ImVec2){0, 0});

    if (igBegin("Status Bar", NULL, flags))
    {
        // FPS calculation is now done in Frontend_Update. frontend_fps is updated there.
        igText("FPS: %.1f | ROM: %s | %s", frontend_fps, nes->rom ? nes->rom->name : "No ROM Loaded", frontend_paused ? "Paused" : "Running");

        //const char *version_text = CNES_VERSION_BUILD_STRING; // REFACTOR-NOTE: Consistent versioning
        //ImVec2 version_text_size;
        //igCalcTextSize(&version_text_size, version_text, NULL, false, 0);

        ImVec2 content_avail;
        igGetContentRegionAvail(&content_avail);

        // Align version text to the right
        float version_pos_x = content_avail.x - version_text_size.x - igGetStyle()->ItemSpacing.x;
        if (version_pos_x > igGetCursorPosX())
        { // Ensure it doesn't overlap with left text
            igSameLine(version_pos_x, 0);
        }
        else
        { // Not enough space, just put it on the same line if possible
            igSameLine(0, igGetStyle()->ItemSpacing.x * 2);
        }
        igTextDisabled("%s", version_text);
    }
    igEnd();

}

void Frontend_Draw(NES *nes)
{
    ImGuiViewport *viewport = igGetMainViewport();
    igSetNextWindowPos(viewport->WorkPos, ImGuiCond_Always, (ImVec2){0, 0});
    igSetNextWindowSize((ImVec2){viewport->WorkSize.x, viewport->WorkSize.y - frontend_status_bar_height}, ImGuiCond_Always);
    igSetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking;

    igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 0.0f);
    igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 0.0f);
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});

    igBegin("cEMU_MainHost", NULL, host_flags);
    igPopStyleVar(3);

    ImGuiID dockspace_id = igGetID_Str("cEMU_DockSpace_Main");
    igDockSpace(dockspace_id, (ImVec2){0.0f, 0.0f}, ImGuiDockNodeFlags_PassthruCentralNode, NULL);

    if (frontend_first_frame)
    {
        frontend_first_frame = false;
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
        dock_id_log = igDockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.15f, NULL, &dock_main_id);    // Log takes 15% of remaining
        dock_id_right = igDockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, NULL, &dock_main_id); // Right panel 25%
        dock_id_left = igDockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.22f, NULL, &dock_main_id);   // Left panel 22%
        // Center panel is what remains of dock_main_id

        igDockBuilderDockWindow("Status Bar", dock_id_statusbar);
        igDockBuilderDockWindow("Log", dock_id_log);
        igDockBuilderDockWindow("Game Screen", dock_main_id);
        igDockBuilderDockWindow("ROM Info", dock_id_right);
        igDockBuilderDockWindow("CPU Registers", dock_id_left);
        igDockBuilderDockWindow("Disassembler", dock_id_left);
        igDockBuilderDockWindow("PPU Viewer", dock_id_right);
        igDockBuilderDockWindow("Memory Viewer (CPU Bus)", dock_id_right);
        igDockBuilderDockWindow("Profiler", dock_id_right);
        igDockBuilderDockWindow("Debug Controls", dock_id_left); // Add debug controls to left panel too

        // Make status bar non-interactive for docking/resizing
        ImGuiDockNode *status_node = igDockBuilderGetNode(dock_id_statusbar);
        if (status_node)
            ImGuiDockNode_SetLocalFlags(status_node, ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoResize | ImGuiDockNodeFlags_NoWindowMenuButton);
        ImGuiDockNode *game_node = igDockBuilderGetNode(dock_main_id);
        if (game_node)
            ImGuiDockNode_SetLocalFlags(game_node, ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoUndocking);

        igDockBuilderFinish(dockspace_id);

        // Ensure windows part of the default layout are initially set to visible
        frontend_showGameScreen = true;
        frontend_showRomInfoWindow = true;
        frontend_showCpuWindow = false;
        frontend_showDisassembler = false;
        frontend_showPpuViewer = false;
        frontend_showMemoryViewer = false;
        frontend_showLog = true;
        frontend_showProfilerWindow = false; // Profiler window is not shown by default
    }

    Frontend_DrawMainMenuBar(nes);

    igEnd(); // End of "cEMU_MainHost"

    // --- Draw all dockable windows ---
    if (frontend_showGameScreen)
        Frontend_GameScreenWindow(nes); // Must be called for docking to work, even if hidden by user later
    if (frontend_showRomInfoWindow)
        Frontend_ROMInfoWindow(nes);
    if (frontend_showCpuWindow)
        Frontend_CpuWindow(nes);
    //if (frontend_showPpuViewer) 
        //Frontend_PPUViewer(nes);
    if (frontend_showLog)
        Frontend_LogWindow();
    if (frontend_showMemoryViewer)
        Frontend_MemoryViewer(nes);
    if (frontend_showDisassembler)
        Frontend_DrawDisassembler(nes);
    if (frontend_showProfilerWindow)
        FrontendProfiler_DrawWindow(Profiler_GetInstance()); // Draw Profiler
        
    Frontend_DrawStatusBar(nes);

    // --- Modals and non-docked utility windows ---
    if (frontend_showSettingsWindow)
        Frontend_SettingsMenu(nes);
    if (frontend_showAboutWindow)
        Frontend_DrawAboutWindow();

    // For debugging ImGui itself
    //igShowDemoWindow(NULL);
    // ImPlot_ShowDemoWindow(NULL); // For ImPlot debugging
}

bool frontend_quit_requested = false;

void Frontend_Update(NES *nes)
{
    static int profiler_input_section = -1;
    static int profiler_draw_section = -1;
    static int profiler_present_section = -1;

    if (profiler_input_section < 0) {
        profiler_input_section = Profiler_CreateSection("SDL Input");
    }
    if (profiler_draw_section < 0) {
        profiler_draw_section = Profiler_CreateSection("Frontend Draw");
    }
    if (profiler_present_section < 0) {
        profiler_present_section = Profiler_CreateSection("GPU Present");
    }

    frontend_frame_start = SDL_GetPerformanceCounter();

    if (profiler_input_section >= 0) {
        Profiler_BeginSectionByID(profiler_input_section);
    }

    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        ImGui_ImplSDL3_ProcessEvent(&e);

        if (e.type == SDL_EVENT_QUIT)
        {
            frontend_quit_requested = true;
        }
        if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(frontend_window))
        {
            frontend_quit_requested = true;
        }

        if (e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED && e.window.windowID == SDL_GetWindowID(frontend_window))
        {
            frontend_requestReload = true;
        }

        if (e.type == SDL_EVENT_KEY_DOWN && !ioptr->WantCaptureKeyboard)
        {
            bool ctrl_pressed = (SDL_GetModState() & SDL_KMOD_CTRL);
            // bool alt_pressed = (SDL_GetModState() & KMOD_ALT); // For Alt+F4, handled by SDL_EVENT_QUIT usually

            if (e.key.key == SDLK_F5)
                Frontend_RequestReset();
            if (e.key.key == SDLK_F6)
                Frontend_RequestPause(!frontend_paused);
            if (e.key.key == SDLK_F7 && frontend_paused)
                Frontend_RequestStepCpu();
            if (e.key.key == SDLK_F8 && frontend_paused)
                Frontend_RequestStepFrame();
            if (e.key.key == SDLK_F10)
                frontend_showSettingsWindow = !frontend_showSettingsWindow;
            if (e.key.key == SDLK_F11)
                Frontend_ToggleFullscreen();

            if (ctrl_pressed && e.key.key == SDLK_O)
            {
                //SDL_ShowOpenFileDialog(FileDialogCallback, nes, window, filters, SDL_arraysize(filters), NULL, false);
            }
            bool rom_loaded_for_state = nes && nes->rom;
            if (ctrl_pressed && e.key.key == SDLK_S && rom_loaded_for_state)
            {
                frontend_openSaveStateModal = true;
            }
            if (ctrl_pressed && e.key.key == SDLK_L && rom_loaded_for_state)
            {
                frontend_openLoadStateModal = true;
            }
        }
        if (e.type == SDL_EVENT_KEY_DOWN && !ioptr->WantCaptureKeyboard)
        {
            if (FrontendConfig_IsWaitingForKey())
            {
                if (FrontendConfig_CaptureRebindKey(e.key.key)) {
                    (void)FrontendConfig_Save(nes);
                }
            }
        }

        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_UP)
        {
            Frontend_HandleGamepadButtonEvent(&e);
        }

        if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
        {
            Frontend_HandleGamepadAxisEvent(&e);
        }

        if (e.type == SDL_EVENT_GAMEPAD_ADDED)
        {
            Frontend_TryOpenFirstGamepad();
        }

        if (e.type == SDL_EVENT_GAMEPAD_REMOVED)
        {
            if (frontend_gamepad) {
                SDL_CloseGamepad(frontend_gamepad);
                frontend_gamepad = NULL;
            }
            frontend_gamepad_input_state = 0;
            Frontend_TryOpenFirstGamepad();
        }

        if (e.type == SDL_EVENT_JOYSTICK_ADDED)
        {
            Frontend_TryOpenFirstJoystick();
        }

        if (e.type == SDL_EVENT_JOYSTICK_REMOVED)
        {
            if (frontend_joystick && SDL_GetJoystickID(frontend_joystick) == e.jdevice.which) {
                Frontend_CloseJoystick();
            }

            Frontend_TryOpenFirstJoystick();
        }
    }

    if (profiler_input_section >= 0) {
        Profiler_EndSection(profiler_input_section);
    }

    if (frontend_input_enable_gamepad) {
        Frontend_TryOpenFirstGamepad();
    } else if (frontend_gamepad) {
        SDL_CloseGamepad(frontend_gamepad);
        frontend_gamepad = NULL;
        frontend_gamepad_input_state = 0;
    }

    if (frontend_input_enable_joystick_dpad) {
        Frontend_TryOpenFirstJoystick();
        Frontend_UpdateJoystickInputState();
    } else {
        Frontend_CloseJoystick();
    }

    if (frontend_quit_requested)
    {
        Frontend_Shutdown(); // Shutdown is called by main application loop before exit
        exit(0);       // Let main loop handle exit
        return;
    }

    if (!frontend_requestReload)
    {
        float current_scale = Frontend_GetWindowDpiScale();
        if (SDL_fabsf(current_scale - frontend_last_font_dpi_scale) > 0.01f) {
            frontend_requestReload = true;
        }
    }

    Frontend_ApplyPendingUiReload();

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    igNewFrame();

    Frontend_UpdateKeyboardInputState();
    Frontend_UpdateGamepadInputState();

    nes_input_state[0] = (uint8_t)(frontend_keyboard_input_state[0] |
                                   (frontend_input_enable_gamepad ? frontend_gamepad_input_state : 0) |
                                   (frontend_input_enable_joystick_dpad ? frontend_joystick_input_state : 0));
    nes_input_state[1] = frontend_keyboard_input_state[1];
    Frontend_SyncControllerState();

    if (profiler_draw_section >= 0) {
        Profiler_BeginSectionByID(profiler_draw_section);
    }

    Frontend_Draw(nes);

    if (profiler_draw_section >= 0) {
        Profiler_EndSection(profiler_draw_section);
    }

    if (nes && nes->apu) {
        APU_SetVolume(nes->apu, frontend_master_volume * nes->settings.audio.volume);
        Frontend_PumpAudio(nes);
    }

    if (profiler_present_section >= 0) {
        Profiler_BeginSectionByID(profiler_present_section);
    }
    igRender();

    ImDrawData *draw_data = igGetDrawData();
    const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(gpu_device);

    if (command_buffer)
    {
        SDL_GPUTexture *swapchain_texture = NULL; // Must be initialized to NULL
        SDL_AcquireGPUSwapchainTexture(command_buffer, frontend_window, &swapchain_texture, NULL, NULL);

        if (swapchain_texture != NULL && !is_minimized)
        {
            Imgui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);
            SDL_GPUColorTargetInfo target_info = {0}; // Important to zero-initialize
            target_info.texture = swapchain_texture;
            target_info.clear_color.r = 0; // Uses global clear_color set by theme
            target_info.clear_color.g = 0;
            target_info.clear_color.b = 0;
            target_info.clear_color.a = 1;
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

            SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(command_buffer, &target_info, 1, NULL);
            if (render_pass)
            { // Check if render pass began successfully
                ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass, NULL);
                SDL_EndGPURenderPass(render_pass);
            }
            else
            {
                DEBUG_ERROR("Failed to begin GPU render pass: %s", SDL_GetError());
            }
        }
        else if (is_minimized)
        {
            // Window is minimized, nothing to render to swapchain.
            // SDL_gpu handles this internally, but good to be aware.
        }
        else if (swapchain_texture == NULL)
        {
            // DEBUG_ERROR("Failed to acquire swapchain texture: %s", SDL_GetError());
        }

        SDL_SubmitGPUCommandBuffer(command_buffer); // This also presents the frame
    }

    // Update and Render additional Platform Windows (for multi-viewport support)
    if (ioptr->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        igUpdatePlatformWindows();
        igRenderPlatformWindowsDefault(NULL, NULL); // This should work with SDL_gpu backend
    }

    if (profiler_present_section >= 0) {
        Profiler_EndSection(profiler_present_section);
    }

    uint64_t frame_end = SDL_GetPerformanceCounter();
    frontend_frame_time = (float)((frame_end - frontend_frame_start) * 1000.0 / SDL_GetPerformanceFrequency()); // Convert to milliseconds
    frontend_fps = 1000.0f / frontend_frame_time; // Calculate FPS
}

uint8_t Frontend_PollInput(int controller)
{
    if (controller < 0 || controller > 1)
        return 0;
    return nes_input_state[controller];
}

void Frontend_Shutdown()
{
    // REFACTOR-NOTE: Save recent ROMs list, window positions/docking layout (imgui.ini handles docking if enabled).
    // Consider saving settings (theme, volume) to a config file.
    Frontend_StopEmulation();
    Profiler_Shutdown();

    DEBUG_INFO("Shutting down UI");

    (void)FrontendConfig_Save(frontend_emulation.nes);

    if (frontend_gamepad)
    {
        SDL_CloseGamepad(frontend_gamepad);
        frontend_gamepad = NULL;
    }

    Frontend_CloseJoystick();

    // Wait for GPU to finish any pending operations
    if (gpu_device)
    {
        SDL_WaitForGPUIdle(gpu_device);
    }

    Frontend_DestroyAudioStream();

    // Shutdown ImGui backends
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    igDestroyContext(NULL); // Pass the context if you stored it, NULL for default

    // Release Game Screen GPU resources
    if (gpu_device)
    {
        if (ppu_game_transfer_buffer)
        {
            SDL_ReleaseGPUTransferBuffer(gpu_device, ppu_game_transfer_buffer);
            ppu_game_transfer_buffer = NULL;
        }
        if (ppu_game_sampler)
        {
            SDL_ReleaseGPUSampler(gpu_device, ppu_game_sampler);
            ppu_game_sampler = NULL;
        }
        if (ppu_game_texture)
        {
            SDL_ReleaseGPUTexture(gpu_device, ppu_game_texture);
            ppu_game_texture = NULL;
        }

        // Release PPU Viewer GPU resources
        if (pt_transfer_buffer)
        {
            SDL_ReleaseGPUTransferBuffer(gpu_device, pt_transfer_buffer);
            pt_transfer_buffer = NULL;
        }
        if (pt_sampler)
        {
            SDL_ReleaseGPUSampler(gpu_device, pt_sampler);
            pt_sampler = NULL;
        }
        if (pt_texture0)
        {
            SDL_ReleaseGPUTexture(gpu_device, pt_texture0);
            pt_texture0 = NULL;
        }
        if (pt_texture1)
        {
            SDL_ReleaseGPUTexture(gpu_device, pt_texture1);
            pt_texture1 = NULL;
        }

        // Destroy GPU device
        SDL_DestroyGPUDevice(gpu_device);
        gpu_device = NULL;
    }

    // Destroy window and quit SDL
    if (frontend_window)
    {
        SDL_DestroyWindow(frontend_window);
        frontend_window = NULL;
    }

    if (frontend_emulation.condition)
    {
        SDL_DestroyCondition(frontend_emulation.condition);
        frontend_emulation.condition = NULL;
    }
    if (frontend_emulation.mutex)
    {
        SDL_DestroyMutex(frontend_emulation.mutex);
        frontend_emulation.mutex = NULL;
    }

    SDL_Quit();

    DEBUG_INFO("UI Shutdown complete");
}


