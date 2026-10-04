/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
/* PS4 pad backend for SDL 2.30.0. Driver table adapted from SDL's dummy
 * joystick backend; altered for Nocturne. See LICENSE.txt. */
#include "SDL_internal.h"
#include "SDL_events.h"
#include "joystick/SDL_sysjoystick.h"
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include "presentation.h"
#include "modules.h"

/* The game uses the controller belonging to the title's initial user. Keep
 * the native port open across disconnects; SDL gets a fresh instance on each
 * reconnection, so stale controller objects cannot control the new instance. */
static int pad_handle = -1;
static SDL_bool pad_connected;
static SDL_JoystickID pad_instance = -1;
struct joystick_hwdata { SDL_JoystickID instance; };

static void PS4_Detect(void)
{
    OrbisPadData data;
    SDL_bool connected;
    SDL_zero(data);
    connected = pad_handle >= 0 && scePadReadState(pad_handle, &data) == 0 && data.connected;
    if (connected == pad_connected) return;
    pad_connected = connected;
    if (connected) {
        pad_instance = SDL_GetNextJoystickInstanceID();
        SDL_PrivateJoystickAdded(pad_instance);
    } else {
        SDL_PrivateJoystickRemoved(pad_instance);
    }
}
static int PS4_Init(void)
{
    int result, user;
    OrbisUserServiceInitializeParams params = { ORBIS_KERNEL_PRIO_FIFO_NORMAL };
    if (ps4_load_module(ORBIS_SYSMODULE_INTERNAL_PAD, "Pad") < 0 ||
        ps4_load_module(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE, "UserService") < 0)
        return -1;
    result = sceUserServiceInitialize(&params);
    if (result < 0 && (Uint32)result != ORBIS_USER_SERVICE_ERROR_ALREADY_INITIALIZED)
        return SDL_SetError("sceUserServiceInitialize failed: 0x%08x", (unsigned)result);
    result = sceUserServiceGetInitialUser(&user);
    if (result < 0) return SDL_SetError("sceUserServiceGetInitialUser failed: 0x%08x", (unsigned)result);
    result = scePadInit();
    if (result < 0) return SDL_SetError("scePadInit failed: 0x%08x", (unsigned)result);
    pad_handle = scePadOpen(user, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    if (pad_handle < 0) return SDL_SetError("scePadOpen failed: 0x%08x", (unsigned)pad_handle);
    PS4_Detect();
    return 0;
}
static int PS4_Count(void) { return pad_connected ? 1 : 0; }
static const char *PS4_Name(int index) { (void)index; return "PS4 DualShock 4"; }
static const char *PS4_Path(int index) { (void)index; return NULL; }
static int PS4_SteamSlot(int index) { (void)index; return -1; }
static int PS4_Player(int index) { (void)index; return 0; }
static void PS4_SetPlayer(int index, int player) { (void)index; (void)player; }
static SDL_JoystickGUID PS4_GUID(int index)
{
    (void)index;
    /* Unique native-port GUID lets our mapping describe the ScePad layout. */
    return SDL_CreateJoystickGUID(SDL_HARDWARE_BUS_VIRTUAL, 0, 0, 1, NULL, "PS4 DualShock 4", 'p', 0);
}
static SDL_JoystickID PS4_Instance(int index) { (void)index; return pad_instance; }
static int PS4_Open(SDL_Joystick *joystick, int index)
{
    (void)index;
    if (!pad_connected) return SDL_SetError("PS4 controller disconnected");
    joystick->hwdata = SDL_malloc(sizeof(*joystick->hwdata));
    if (!joystick->hwdata) return SDL_OutOfMemory();
    joystick->hwdata->instance = pad_instance;
    joystick->naxes = 6;
    joystick->nbuttons = 14;
    joystick->nhats = 0;
    return 0;
}
static int PS4_Rumble(SDL_Joystick *joystick, Uint16 low, Uint16 high)
{
    OrbisPadVibeParam vibration = { low >> 8, high >> 8 };
    if (!pad_connected || joystick->hwdata->instance != pad_instance)
        return SDL_SetError("PS4 controller disconnected");
    if (scePadSetVibration(pad_handle, &vibration) < 0) return SDL_SetError("scePadSetVibration failed");
    return 0;
}
static int PS4_RumbleTriggers(SDL_Joystick *joystick, Uint16 a, Uint16 b)
{ (void)joystick; (void)a; (void)b; return SDL_Unsupported(); }
static Uint32 PS4_Capabilities(SDL_Joystick *joystick) { (void)joystick; return SDL_JOYCAP_RUMBLE; }
static int PS4_LED(SDL_Joystick *joystick, Uint8 r, Uint8 g, Uint8 b)
{ (void)joystick; (void)r; (void)g; (void)b; return SDL_Unsupported(); }
static int PS4_Effect(SDL_Joystick *joystick, const void *data, int size)
{ (void)joystick; (void)data; (void)size; return SDL_Unsupported(); }
static int PS4_Sensors(SDL_Joystick *joystick, SDL_bool enabled)
{ (void)joystick; (void)enabled; return SDL_Unsupported(); }
static void PS4_Update(SDL_Joystick *joystick)
{
    static const Uint32 buttons[] = {
        ORBIS_PAD_BUTTON_CROSS, ORBIS_PAD_BUTTON_CIRCLE,
        ORBIS_PAD_BUTTON_SQUARE, ORBIS_PAD_BUTTON_TRIANGLE,
        ORBIS_PAD_BUTTON_L1, ORBIS_PAD_BUTTON_R1,
        ORBIS_PAD_BUTTON_OPTIONS, ORBIS_PAD_BUTTON_L3,
        ORBIS_PAD_BUTTON_R3, ORBIS_PAD_BUTTON_TOUCH_PAD,
        ORBIS_PAD_BUTTON_UP, ORBIS_PAD_BUTTON_DOWN,
        ORBIS_PAD_BUTTON_LEFT, ORBIS_PAD_BUTTON_RIGHT
    };
    OrbisPadData data;
    unsigned i;
    if (!pad_connected || joystick->hwdata->instance != pad_instance) return;
    if (scePadReadState(pad_handle, &data) < 0 || !data.connected) return;
    for (i = 0; i < SDL_arraysize(buttons); ++i)
        SDL_PrivateJoystickButton(joystick, i, data.buttons & buttons[i] ? SDL_PRESSED : SDL_RELEASED);
    /* Publish neutral samples too, so returning the stick releases movement. */
    SDL_PrivateJoystickAxis(joystick, 0, nocturne_ps4_stick_axis(data.leftStick.x));
    SDL_PrivateJoystickAxis(joystick, 1, nocturne_ps4_stick_axis(data.leftStick.y));
    SDL_PrivateJoystickAxis(joystick, 2, nocturne_ps4_stick_axis(data.rightStick.x));
    SDL_PrivateJoystickAxis(joystick, 3, nocturne_ps4_stick_axis(data.rightStick.y));
    SDL_PrivateJoystickAxis(joystick, 4, (Sint16)((int)data.analogButtons.l2 * 257 - 32768));
    SDL_PrivateJoystickAxis(joystick, 5, (Sint16)((int)data.analogButtons.r2 * 257 - 32768));
}
static void PS4_Close(SDL_Joystick *joystick)
{
    if (pad_connected && joystick->hwdata && joystick->hwdata->instance == pad_instance)
        PS4_Rumble(joystick, 0, 0);
    SDL_free(joystick->hwdata);
    joystick->hwdata = NULL;
}
static void PS4_Quit(void)
{
    if (pad_handle >= 0) scePadClose(pad_handle);
    pad_handle = -1;
    pad_connected = SDL_FALSE;
    pad_instance = -1;
}
static SDL_bool PS4_Mapping(int index, SDL_GamepadMapping *out)
{
    (void)index;
    SDL_zero(*out);
#define BUTTON(field, number) out->field.kind = EMappingKind_Button; out->field.target = number
#define AXIS(field, number) out->field.kind = EMappingKind_Axis; out->field.target = number
    BUTTON(a, 0); BUTTON(b, 1); BUTTON(x, 2); BUTTON(y, 3);
    BUTTON(leftshoulder, 4); BUTTON(rightshoulder, 5); BUTTON(start, 6);
    /* Back is the persisted Touchpad binding. Publishing a second alias makes
     * the recovered one-key capture see two simultaneous inputs. */
    BUTTON(leftstick, 7); BUTTON(rightstick, 8); BUTTON(back, 9);
    BUTTON(dpup, 10); BUTTON(dpdown, 11); BUTTON(dpleft, 12); BUTTON(dpright, 13);
    AXIS(leftx, 0); AXIS(lefty, 1); AXIS(rightx, 2); AXIS(righty, 3);
    AXIS(lefttrigger, 4); AXIS(righttrigger, 5);
#undef BUTTON
#undef AXIS
    return SDL_TRUE;
}
SDL_JoystickDriver SDL_PS4_JoystickDriver = {
    PS4_Init, PS4_Count, PS4_Detect, PS4_Name, PS4_Path, PS4_SteamSlot,
    PS4_Player, PS4_SetPlayer, PS4_GUID, PS4_Instance, PS4_Open,
    PS4_Rumble, PS4_RumbleTriggers, PS4_Capabilities, PS4_LED, PS4_Effect,
    PS4_Sensors, PS4_Update, PS4_Close, PS4_Quit, PS4_Mapping
};
