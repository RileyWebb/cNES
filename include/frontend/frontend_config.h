#ifndef FRONTEND_CONFIG_H
#define FRONTEND_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>

typedef struct NES NES;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FrontendNesButton {
    FRONTEND_NES_BUTTON_A = 0,
    FRONTEND_NES_BUTTON_B,
    FRONTEND_NES_BUTTON_SELECT,
    FRONTEND_NES_BUTTON_START,
    FRONTEND_NES_BUTTON_UP,
    FRONTEND_NES_BUTTON_DOWN,
    FRONTEND_NES_BUTTON_LEFT,
    FRONTEND_NES_BUTTON_RIGHT,
    FRONTEND_NES_BUTTON_COUNT
} FrontendNesButton;

extern const char *frontend_nes_button_names[FRONTEND_NES_BUTTON_COUNT];
extern SDL_Keycode frontend_input_keymap[2][FRONTEND_NES_BUTTON_COUNT];
extern SDL_GamepadButton frontend_input_gamepad_map[FRONTEND_NES_BUTTON_COUNT];
extern bool frontend_input_enable_gamepad;
extern bool frontend_input_enable_joystick_dpad;
extern int frontend_input_axis_deadzone;

void FrontendConfig_InitDefaults(void);
void FrontendConfig_ResetInputSettings(void);
bool FrontendConfig_Load(NES *nes);
bool FrontendConfig_Save(const NES *nes);
const char *FrontendConfig_GetPath(void);
const char *FrontendConfig_GetDirectory(void);

void FrontendConfig_BeginKeyRebind(int controller, FrontendNesButton button);
bool FrontendConfig_IsWaitingForKey(void);
int FrontendConfig_GetRebindController(void);
int FrontendConfig_GetRebindButton(void);
bool FrontendConfig_CaptureRebindKey(SDL_Keycode key);

#ifdef __cplusplus
}
#endif

#endif
