/* SDL device-boundary double. The production backend owns button numbering and
 * mappings; this fixture only applies generic SDL button/axis mapping rules. */
#include "SDL_internal.h"
#include "SDL.h"
#include "joystick/SDL_sysjoystick.h"
#include "pad_driver.h"
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/Sysmodule.h>
#include <assert.h>

extern SDL_JoystickDriver SDL_PS4_JoystickDriver;
static SDL_Joystick joystick;
static SDL_GamepadMapping mapping;
static Uint8 raw_buttons[32];
static Sint16 raw_axes[6];
static Uint32 ticks, physical_buttons;
static int controller_token, window_token;
static SDL_bool initialized;

int32_t scePadInit(void) { return 0; }
int32_t scePadOpen(int32_t user, int32_t type, int32_t index, void *p)
{ (void)user; (void)type; (void)index; (void)p; return 1; }
int32_t scePadClose(int32_t handle) { (void)handle; return 0; }
int32_t scePadReadState(int32_t handle, OrbisPadData *data)
{
    (void)handle;
    SDL_zero(*data);
    data->connected = 1;
    data->leftStick.x = data->leftStick.y = 128;
    data->rightStick.x = data->rightStick.y = 128;
    data->buttons = physical_buttons;
    return 0;
}
int32_t scePadSetVibration(int32_t handle, const OrbisPadVibeParam *vibration)
{ (void)handle; (void)vibration; return 0; }
int32_t sceUserServiceInitialize(void *params) { (void)params; return 0; }
int32_t sceUserServiceGetInitialUser(int32_t *user) { *user = 1; return 0; }
uint32_t sceSysmoduleLoadModuleInternal(enum OrbisSysModuleInternal module)
{ (void)module; return 0; }

SDL_JoystickID SDL_GetNextJoystickInstanceID(void) { return 1; }
void SDL_PrivateJoystickAdded(SDL_JoystickID id) { (void)id; }
void SDL_PrivateJoystickRemoved(SDL_JoystickID id) { (void)id; }
int SDL_PrivateJoystickButton(SDL_Joystick *j, Uint8 button, Uint8 state)
{ (void)j; assert(button < SDL_arraysize(raw_buttons)); raw_buttons[button] = state; return 1; }
int SDL_PrivateJoystickAxis(SDL_Joystick *j, Uint8 axis, Sint16 value)
{ (void)j; assert(axis < SDL_arraysize(raw_axes)); raw_axes[axis] = value; return 1; }
SDL_JoystickGUID SDL_CreateJoystickGUID(Uint16 bus, Uint16 vendor, Uint16 product,
    Uint16 version, const char *vendor_name, const char *product_name, Uint8 signature, Uint8 data)
{
    SDL_JoystickGUID guid = {{0}};
    (void)bus; (void)vendor; (void)product; (void)version;
    (void)vendor_name; (void)product_name; (void)signature; (void)data;
    return guid;
}
int SDL_InitSubSystem(Uint32 flags)
{
    (void)flags;
    if (initialized) return 0;
    if (SDL_PS4_JoystickDriver.Init() < 0) return -1;
    initialized = SDL_TRUE;
    return 0;
}
int SDL_NumJoysticks(void) { return SDL_PS4_JoystickDriver.GetCount(); }
SDL_bool SDL_IsGameController(int index)
{ return SDL_PS4_JoystickDriver.GetGamepadMapping(index, &mapping); }
SDL_GameController *SDL_GameControllerOpen(int index)
{
    if (!SDL_PS4_JoystickDriver.GetGamepadMapping(index, &mapping) ||
        SDL_PS4_JoystickDriver.Open(&joystick, index) < 0) return NULL;
    SDL_PS4_JoystickDriver.Update(&joystick);
    return (SDL_GameController *)&controller_token;
}
void SDL_GameControllerClose(SDL_GameController *pad)
{ (void)pad; SDL_PS4_JoystickDriver.Close(&joystick); }
SDL_bool SDL_GameControllerGetAttached(SDL_GameController *pad)
{ (void)pad; return SDL_NumJoysticks() ? SDL_TRUE : SDL_FALSE; }
SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *pad)
{ (void)pad; return &joystick; }
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *j)
{ (void)j; return SDL_PS4_JoystickDriver.GetDeviceInstanceID(0); }
SDL_GameControllerType SDL_GameControllerGetType(SDL_GameController *pad)
{ (void)pad; return SDL_CONTROLLER_TYPE_PS4; }
SDL_Window *SDL_GetKeyboardFocus(void) { return (SDL_Window *)&window_token; }
Uint8 SDL_GameControllerGetButton(SDL_GameController *pad, SDL_GameControllerButton button)
{
    const SDL_InputMapping *buttons[] = {
        &mapping.a, &mapping.b, &mapping.x, &mapping.y, &mapping.back, &mapping.guide,
        &mapping.start, &mapping.leftstick, &mapping.rightstick,
        &mapping.leftshoulder, &mapping.rightshoulder, &mapping.dpup, &mapping.dpdown,
        &mapping.dpleft, &mapping.dpright, &mapping.misc1, &mapping.paddle1,
        &mapping.paddle2, &mapping.paddle3, &mapping.paddle4, &mapping.touchpad
    };
    (void)pad;
    assert(button >= 0 && button < SDL_arraysize(buttons));
    const SDL_InputMapping *input = buttons[button];
    assert(input->kind == EMappingKind_None || input->kind == EMappingKind_Button);
    return input->kind == EMappingKind_Button ? raw_buttons[input->target] : 0;
}
Sint16 SDL_GameControllerGetAxis(SDL_GameController *pad, SDL_GameControllerAxis axis)
{
    const SDL_InputMapping *axes[] = { &mapping.leftx, &mapping.lefty,
        &mapping.rightx, &mapping.righty, &mapping.lefttrigger, &mapping.righttrigger };
    (void)pad;
    assert(axis >= 0 && axis < SDL_arraysize(axes));
    const SDL_InputMapping *input = axes[axis];
    assert(input->kind == EMappingKind_Axis);
    const int value = raw_axes[input->target];
    /* SDL maps a raw full-range trigger axis to the controller's 0..32767. */
    return axis >= SDL_CONTROLLER_AXIS_TRIGGERLEFT ? (value + 32768) / 2 : value;
}
Uint32 SDL_GetTicks(void) { return ticks; }
SDL_RWops *SDL_RWFromFile(const char *path, const char *mode)
{ (void)path; (void)mode; return NULL; }
int SDL_GameControllerAddMappingsFromRW(SDL_RWops *rw, int free_rw)
{ (void)rw; (void)free_rw; return 0; }
void ps4_test_pad_sample(uint32_t buttons)
{
    physical_buttons = buttons;
    ticks += 16;
    if (joystick.hwdata) SDL_PS4_JoystickDriver.Update(&joystick);
}
