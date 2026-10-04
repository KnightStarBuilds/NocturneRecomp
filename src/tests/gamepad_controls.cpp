// Exercise the real joystick -> keyboard bindings -> analog pipeline. SDL's
// device boundary is controlled here; no window, physical pad or event timing
// is involved. Weapon/camera services are unrelated and must not be reached.
#include "nocturne.h"
#include "game/gamepad.h"
#include <SDL.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

char g_KeyboardState[4952]{};
char g_PrevKeyboardState[600]{};
DWORD g_JoyXPos{}, g_JoyYPos{}, g_JoyZPos{}, g_JoyRPos{}, g_JoyButtons{};
int g_JoystickPresent{}, g_LocalHeroIndex{};
CGame* g_CGamePtr{};
CKeys* g_CKeysPtr{};
CHero* g_HeroActors[4]{};
CNetGame* g_CNetGamePtr{};
CDemonSet* g_CDemonSetPtr{};
CScript* g_CScriptPtr{};

namespace {
bool attached = true;
bool focused = true;
bool map_owns_controls = false;
int map_binding = INPUT_NONE;
char pad_token{}, joystick_token{}, window_token{};
std::array<Sint16, SDL_CONTROLLER_AXIS_MAX> axes{};
std::array<Uint8, SDL_CONTROLLER_BUTTON_MAX> buttons{};
unsigned checks = 0;
unsigned failures = 0;
unsigned item_description_requests = 0;
CInventory* described_inventory = nullptr;

void check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

bool near(float actual, float expected) {
    return std::fabs(actual - expected) < 0.0001f;
}

[[noreturn]] void unexpected_service() {
    std::fputs("Unexpected weapon/camera service in movement test\n", stderr);
    std::abort();
}

int read_and_clear(CKeys*, EInputCodeType code) {
    const int pressed = g_KeyboardState[code];
    g_KeyboardState[code] = 0;
    return pressed;
}

EDeathState alive(CCharacter*) { return DEATH_STATE_ALIVE; }

CGame game{};
CHero hero{};
CCharacter_full_vtable hero_vtable{};
CKeys_vtable keys_vtable{};
CKeys keys{};

void reset_frame() {
    axes.fill(0);
    buttons.fill(0);
    focused = true;
    attached = true;
    map_owns_controls = false;
    // Release the prior frame through the real edge-state logic first.
    nocturne_gamepad_pump();
    std::memset(g_KeyboardState, 0, sizeof(g_KeyboardState));
    std::memset(g_PrevKeyboardState, 0, sizeof(g_PrevKeyboardState));
    game = {};
    game.game_control = CONTROL_MODE_GAMEPAD;
    nocturne_gamepad_apply_defaults(&game);
    game.x_mouse_sensitivity = 0x10000;
    game.y_mouse_sensitivity = 0x10000;
    game.delta_time_float = 0.02f;
    item_description_requests = 0;
    described_inventory = nullptr;
}

void test_platform_bindings() {
    reset_frame();
    const int map_button = SDL_CONTROLLER_BUTTON_BACK;
#if defined(NOCTURNE_PS4)
    const int description_button = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    const int unused_description_button = SDL_CONTROLLER_BUTTON_GUIDE;
    const char* map_label = "Touchpad";
#else
    const int description_button = SDL_CONTROLLER_BUTTON_GUIDE;
    const int unused_description_button = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    const char* map_label = "Share";
#endif
    check(game.key_item_desc == NOCTURNE_PAD_BUTTON(description_button),
          "item description uses the platform's default controller button");
    check(map_binding == NOCTURNE_PAD_BUTTON(map_button),
          "map retains the saved SDL BACK input code");
    check(std::strcmp(nocturne_gamepad_code_name(map_binding), map_label) == 0,
          "map label identifies the physical button on this platform");
    buttons[map_button] = 1;
    nocturne_gamepad_pump();
    check(g_KeyboardState[map_binding] != 0,
          "physical map button reaches its configured engine input code");

    reset_frame();
    buttons[description_button] = 1;
    nocturne_gamepad_pump();
    SPlayerInput input{};
    core_game_cpp_CGame_processJoystickControls_FUN_004dc230(&game, &input);
    check(item_description_requests == 1 && described_inventory == &hero.inventory,
          "description button reaches the current hero's inventory through recovered controls");
    nocturne_gamepad_pump();
    core_game_cpp_CGame_processJoystickControls_FUN_004dc230(&game, &input);
    check(item_description_requests == 1, "held description button toggles only once");

    reset_frame();
    buttons[unused_description_button] = 1;
    nocturne_gamepad_pump();
    core_game_cpp_CGame_processJoystickControls_FUN_004dc230(&game, &input);
    check(item_description_requests == 0,
          "the other platform's description default does not activate this binding");
}

void test_saved_binding_migration() {
    reset_frame();
    game.key_item_desc = NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_GUIDE);
    game.key_fire = NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    game.always_run = 1;
    game.x_mouse_sensitivity = 12345;
    map_binding = NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_X);
    CGame expected = game;
#if defined(NOCTURNE_PS4)
    expected.key_item_desc = NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
#endif
    nocturne_gamepad_reseed_for_mode(&game);
    check(std::memcmp(&game, &expected, sizeof(game)) == 0,
          "only the obsolete PS4 Guide description binding migrates; desktop and custom settings survive");
    check(map_binding == NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_X),
          "saved custom map binding survives description migration");

    const int custom_bindings[] = {
        NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSTICK),
        NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_Y), DIK_F1, INPUT_NONE
    };
    for (const int binding : custom_bindings) {
        game.key_item_desc = binding;
        const CGame saved = game;
        nocturne_gamepad_reseed_for_mode(&game);
        check(std::memcmp(&game, &saved, sizeof(game)) == 0,
              "custom or unassigned item description binding remains user-owned");
    }

    game.key_item_desc = NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_GUIDE);
    game.key_run = NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    CGame collision = game;
    nocturne_gamepad_reseed_for_mode(&game);
    check(std::memcmp(&game, &collision, sizeof(game)) == 0,
          "description migration does not steal a custom R3 action binding");
    game.key_run = NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSTICK);
    map_binding = NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    collision = game;
    nocturne_gamepad_reseed_for_mode(&game);
    check(std::memcmp(&game, &collision, sizeof(game)) == 0 &&
              map_binding == NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSTICK),
          "description migration does not steal a custom R3 map binding");

    reset_frame();
    game.key_fire = NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_TOUCHPAD);
    map_binding = NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_TOUCHPAD);
    expected = game;
#if defined(NOCTURNE_PS4)
    expected.key_fire = NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_BACK);
    const int expected_map = NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_BACK);
#else
    const int expected_map = map_binding;
#endif
    nocturne_gamepad_reseed_for_mode(&game);
    check(std::memcmp(&game, &expected, sizeof(game)) == 0 && map_binding == expected_map,
          "Touchpad aliases normalize only on PS4; desktop bindings retain their codes");
}

void test_run_bindings() {
    struct Binding { const char* name; int code; int button; };
    const Binding bindings[] = {
        {"L3", NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSTICK), SDL_CONTROLLER_BUTTON_LEFTSTICK},
        {"remapped R3", NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSTICK), SDL_CONTROLLER_BUTTON_RIGHTSTICK},
        {"keyboard Shift", DIK_LSHIFT, -1},
        {"unassigned", INPUT_NONE, -1},
    };
    // Raw 60% deflection becomes 50% after the real deadzone, above movement
    // activation and below the former automatic-run threshold.
    const Sint16 deflections[] = {-32768, -19660, 19660, 32767};
    for (const auto& binding : bindings) {
        for (const auto deflection : deflections) {
            for (int always_run : {0, 1}) {
                for (int held : {0, 1}) {
                    if (binding.code == INPUT_NONE && held) continue;
                    reset_frame();
                    game.key_run = binding.code;
                    game.always_run = always_run;
                    axes[SDL_CONTROLLER_AXIS_LEFTY] = deflection;
                    if (binding.button >= 0) buttons[binding.button] = held;
                    else if (binding.code != INPUT_NONE) g_KeyboardState[binding.code] = held;
                    nocturne_gamepad_pump();
                    SPlayerInput input{};
                    core_game_cpp_CGame_processJoystickControls_FUN_004dc230(&game, &input);
                    const int expected = always_run ? !held : held;
                    ++checks;
                    if (input.action_state.run != expected) {
                        ++failures;
                        std::fprintf(stderr, "FAIL: %s axis=%d always_run=%d held=%d: run=%d, expected %d\n",
                                     binding.name, int(deflection), always_run, held,
                                     input.action_state.run, expected);
                    }
                    check(input.action_state.walk == (deflection < 0), "forward stick resolves walk binding");
                    check(input.action_state.backup == (deflection > 0), "backward stick resolves backup binding");
                }
            }
        }
    }
}

void test_analog_ownership() {
    for (const Sint16 deflection : {-32768, -19660, 19660, 32767}) {
        for (int resolved_run : {0, 1}) {
            reset_frame();
            axes[SDL_CONTROLLER_AXIS_LEFTY] = deflection;
            SPlayerInput input{};
            input.action_state.run = resolved_run;
            input.action_state.walk = 1;
            input.action_state.backup = 1;
            check(nocturne_gamepad_apply_analog(&game, &input) == 1, "connected pad handles analog input");
            check(input.action_state.run == resolved_run, "analog input preserves resolved run state");
            check(input.action_state.walk == 1 && input.action_state.backup == 1,
                  "analog input preserves movement bindings");
        }
    }

    reset_frame();
    SPlayerInput input{};
    input.action_state.run = 1;
    input.strafe_speed = 0.3f;
    input.turn_speed = -0.2f;
    input.look_up_down_speed = 0.1f;
    const auto resolved = input;
    nocturne_gamepad_apply_analog(&game, &input);
    check(std::memcmp(&input, &resolved, sizeof(input)) == 0, "centered sticks preserve digital controls");

    axes[SDL_CONTROLLER_AXIS_LEFTX] = 19660;
    axes[SDL_CONTROLLER_AXIS_RIGHTX] = 32767;
    game.x_mouse_sensitivity = 0x20000;
    nocturne_gamepad_apply_analog(&game, &input);
    check(near(input.strafe_speed, 0.5f), "partial left stick still controls analog strafe");
    check(near(input.turn_speed, 0.5f), "right stick turn still honors X sensitivity");
    check(near(input.look_up_down_speed, 0.1f), "centered look axis preserves digital look");
    check(input.action_state.run == 1, "strafe and turn preserve run binding");

    axes[SDL_CONTROLLER_AXIS_RIGHTX] = 0;
    axes[SDL_CONTROLLER_AXIS_RIGHTY] = -32768;
    game.y_mouse_sensitivity = 0x8000;
    nocturne_gamepad_apply_analog(&game, &input);
    check(near(input.look_up_down_speed, -0.125f), "right stick look still honors Y sensitivity");

    focused = false;
    input = resolved;
    nocturne_gamepad_apply_analog(&game, &input);
    check(std::memcmp(&input, &resolved, sizeof(input)) == 0, "unfocused pad leaves resolved input alone");
    focused = true;
    map_owns_controls = true;
    input = resolved;
    check(nocturne_gamepad_apply_analog(&game, &input) == 1, "automap retains controller ownership");
    check(std::memcmp(&input, &resolved, sizeof(input)) == 0, "analog does not change automap-owned input");
    core_game_cpp_CGame_processJoystickControls_FUN_004dc230(&game, &input);
    const SPlayerInput empty{};
    check(std::memcmp(&input, &empty, sizeof(input)) == 0, "automap clears input through full pipeline");

    map_owns_controls = false;
    attached = false;
    nocturne_gamepad_pump();
    input = resolved;
    check(nocturne_gamepad_apply_analog(&game, &input) == 0, "missing pad leaves digital path in charge");
    check(std::memcmp(&input, &resolved, sizeof(input)) == 0, "missing pad leaves resolved input alone");
}
} // namespace

// Opaque SDL handles never leave these doubles. Pure SDL math remains linked
// normally; controller enumeration and focus cannot reach the host's devices.
extern "C" int SDL_InitSubSystem(Uint32) { return 0; }
extern "C" int SDL_NumJoysticks() { return attached ? 1 : 0; }
extern "C" SDL_bool SDL_IsGameController(int) { return SDL_TRUE; }
extern "C" SDL_GameController* SDL_GameControllerOpen(int) {
    return reinterpret_cast<SDL_GameController*>(&pad_token);
}
extern "C" void SDL_GameControllerClose(SDL_GameController*) {}
extern "C" SDL_bool SDL_GameControllerGetAttached(SDL_GameController*) { return attached ? SDL_TRUE : SDL_FALSE; }
extern "C" SDL_Joystick* SDL_GameControllerGetJoystick(SDL_GameController*) {
    return reinterpret_cast<SDL_Joystick*>(&joystick_token);
}
extern "C" SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick*) { return 1; }
extern "C" SDL_GameControllerType SDL_GameControllerGetType(SDL_GameController*) {
    return SDL_CONTROLLER_TYPE_PS4;
}
extern "C" SDL_Window* SDL_GetKeyboardFocus() {
    return focused ? reinterpret_cast<SDL_Window*>(&window_token) : nullptr;
}
extern "C" Sint16 SDL_GameControllerGetAxis(SDL_GameController*, SDL_GameControllerAxis axis) { return axes[axis]; }
extern "C" Uint8 SDL_GameControllerGetButton(SDL_GameController*, SDL_GameControllerButton button) { return buttons[button]; }
extern "C" SDL_RWops* SDL_RWFromFile(const char*, const char*) { return nullptr; }
extern "C" int SDL_GameControllerAddMappingsFromRW(SDL_RWops*, int) { return 0; }
std::string watcom_resolve_fs_path(const char* path) { return path; }
extern "C" int nocturne_automap_owns_controls() { return map_owns_controls; }
extern "C" int* nocturne_automap_key_binding() { return &map_binding; }
extern "C" void nocturne_net_weapon_request(int, int, int) { unexpected_service(); }
extern "C" void nocturne_goggle_look_reset() { unexpected_service(); }
void core_game_cpp_CGame_restoreDefaultControls_FUN_004dbbc0(CGame*) { unexpected_service(); }
void wincore_winrun_cpp_enqueueInput_FUN_005f2f30(int) { unexpected_service(); }
void core_inv_cpp_CInventory_toggleDetailView_FUN_00501500(CInventory* inventory) {
    ++item_description_requests;
    described_inventory = inventory;
}
int core_setdir_cpp_CDemonSet_evaluateVirtualDirector_FUN_005751d0(CDemonSet*, CDemonActor*, int) { unexpected_service(); }
void core_set_cpp_CDemonSet_setCameraView_FUN_0056ae50(CDemonSet*, int) { unexpected_service(); }

int main() {
    g_CGamePtr = &game;
    hero_vtable._uc.getDeathState = alive;
    hero.base.base.vtable._uc = &hero_vtable;
    g_HeroActors[0] = &hero;
    keys_vtable.getAndClearKeyState = read_and_clear;
    keys.vtable = &keys_vtable;
    g_CKeysPtr = &keys;
    nocturne_gamepad_suppress_nav(1);
    nocturne_gamepad_init();
    check(nocturne_gamepad_present() != 0, "fixture controller was opened");
    test_platform_bindings();
    test_saved_binding_migration();
    test_run_bindings();
    test_analog_ownership();
    std::printf("Gamepad controls: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
