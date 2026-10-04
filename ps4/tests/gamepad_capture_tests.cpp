// Firmware button samples pass through the real PS4 driver, its mapping, the
// gamepad pump and recovered key-capture loop. Only UI/storage services are fake.
#include "nocturne.h"
#include "game/gamepad.h"
#include "pad_driver.h"
#include <SDL.h>
#include <orbis/Pad.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

char g_KeyboardState[4952]{};
DWORD g_JoyXPos{}, g_JoyYPos{}, g_JoyZPos{}, g_JoyRPos{}, g_JoyButtons{};
int g_JoystickPresent{}, g_InputKeyMask{};
CGame* g_CGamePtr{};
CKeys* g_CKeysPtr{};
CEditorTools* g_CEditorToolsPtr{};
CConsole* g_CConsolePtr{};
CDemonMission* g_CDemonMissionPtr{};
CDemonSet* g_CDemonSetPtr{};
char g_CustomKeyNames[30][40]{};
int* g_CustomKeyPointers[31]{};
int g_CustomKeyCount{};
char* g_CurrentFilename{};
int g_CurrentLineNumber{};
int g_UseDirect3D{}, g_AGPTextureMode{}, g_CurrentGraphicsBoard{}, g_FirstTimeFlag{};
char g_RendererDllPath[256]{};

namespace {
CGame game{};
int map_binding = INPUT_NONE, map_zoom{};
int* selected_action{};
enum class Capture { idle, pressing, released };
Capture capture = Capture::idle;
unsigned frames{}, checks{};
bool picked{};
std::unordered_map<std::string, int> saved_ini;

void check(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void sample(uint32_t physical) {
    ps4_test_pad_sample(physical);
    nocturne_gamepad_pump();
}
void load_ini() {
    game = {};
    core_inivar_cpp_readIniData_FUN_004fbd90();
}
void test_ini_migration() {
    // Every persisted action can carry a desktop/native SDL Touchpad code.
    const struct { const char* name; int CGame::*field; } bindings[] = {
        {"keyWalk", &CGame::key_walk}, {"keyBackup", &CGame::key_backup},
        {"keyRun", &CGame::key_run}, {"keyStrafe", &CGame::key_strafe},
        {"keyStrafeLeft", &CGame::key_strafe_left}, {"keyStrafeRight", &CGame::key_strafe_right},
        {"keyRight", &CGame::key_right}, {"keyLeft", &CGame::key_left},
        {"keyFire", &CGame::key_fire}, {"keyUseItem", &CGame::key_use_item},
        {"keyLight", &CGame::key_light}, {"keyInfrared", &CGame::key_infrared},
        {"keyDraw", &CGame::key_draw}, {"keyJump", &CGame::key_jump},
        {"keyPointUp", &CGame::key_point_up}, {"keyPointDown", &CGame::key_point_down},
        {"keyWeapon1", &CGame::key_weapon_1}, {"keyWeapon2", &CGame::key_weapon_2},
        {"keyWeapon3", &CGame::key_weapon_3}, {"keyWeapon4", &CGame::key_weapon_4},
        {"keyWeapon5", &CGame::key_weapon_5}, {"keyNextWeapon", &CGame::key_next_weapon},
        {"keyPrevWeapon", &CGame::key_prev_weapon}, {"keyNextItem", &CGame::key_next_item},
        {"keyPrevItem", &CGame::key_prev_item}, {"keyItemDesc", &CGame::key_item_desc},
        {"keyNextAmmo", &CGame::key_next_ammo}
    };
    constexpr int canonical = NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_BACK);
    constexpr int legacy = NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_TOUCHPAD);
    for (const auto& binding : bindings) {
        saved_ini = {{binding.name, legacy}, {"keyMap", canonical},
                     {"alwaysRun", 1}, {"xMouseSensitivity", 12345}, {"mapZoom", 2}};
        load_ini();
        check(game.*binding.field == canonical, "loaded action retains its physical Touchpad binding");
        check(map_binding == canonical, "migration retains both actions when both aliases were saved");
        check(game.always_run == 1 && game.x_mouse_sensitivity == 12345 && map_zoom == 2,
              "binding migration preserves unrelated loaded preferences");
        sample(ORBIS_PAD_BUTTON_TOUCH_PAD);
        check(g_KeyboardState[game.*binding.field] != 0, "migrated action receives the physical press");
        sample(0);
    }
    saved_ini = {{"keyMap", legacy}, {"keyFire", DIK_F1}, {"keyDraw", INPUT_NONE}};
    load_ini();
    check(map_binding == canonical && game.key_fire == DIK_F1 && game.key_draw == INPUT_NONE,
          "custom Map alias migrates without replacing mixed/unassigned controls");
    const CGame before = game;
    nocturne_gamepad_reseed_for_mode(&game);
    check(!std::memcmp(&before, &game, sizeof(game)) && map_binding == canonical,
          "migration is idempotent");
    saved_ini.clear();
    load_ini();
}
void capture_touchpad(int* action) {
    sample(0);
    selected_action = action;
    capture = Capture::idle;
    frames = 0;
    picked = false;
    core_menu_cpp_configureCustomKeyBindings_FUN_005138e0();
    check(picked && capture == Capture::released, "real binding UI accepts and releases Touchpad");
    check(*action == NOCTURNE_PAD_BUTTON(SDL_CONTROLLER_BUTTON_BACK),
          "capture persists the canonical Touchpad code");
    check(g_KeyboardState[*action] == 0, "button release clears the captured code");
}
}

// Rendering and pick-list interaction are scripted; key capture itself is real.
int _sprintf(void* output, const char* format, ...) {
    va_list args; va_start(args, format);
    const int result = std::vsnprintf(static_cast<char*>(output), 200, format, args);
    va_end(args); return result;
}
char* support_newmsg_cpp_getLocalizedString_FUN_005441f0(char* s) { return s; }
void core_main_c_displayErrorAndQuit_FUN_00506f10(char*, ...) { std::abort(); }
void engine_special_cpp_clearScreen_FUN_005b3e70() {}
void wincore_wddvmem_cpp_swapBuffers_FUN_005eda20() {
    check(++frames < 30, "capture must finish rather than retrying two alias inputs forever");
    sample(capture == Capture::pressing ? ORBIS_PAD_BUTTON_TOUCH_PAD : 0);
}
CPickList* shape_edittool_cpp_CPickList_ctor_FUN_004a3b90(CPickList* list) { return list; }
CPickList* shape_edittool_cpp_CPickList_dtor_FUN_004a3c80(CPickList* list, uint) { return list; }
void shape_edittool_cpp_CStrList_add_FUN_004a2b80(CStrList*, char*) {}
void shape_edittool_cpp_CPickList_setSelectedResult_FUN_004a3e10(CPickList*, int) {}
int shape_edittool_cpp_CPickList_displayChoicesAndWaitForInput_FUN_004a3e20(CPickList*, char*, int, uint) {
    if (picked) return -1;
    picked = true;
    for (int i = 0; i < g_CustomKeyCount; ++i)
        if (g_CustomKeyPointers[i] == selected_action) return i + 1;
    std::abort();
}
void shape_edittool_cpp_CEditorTools_displayCenteredStatusMessage_FUN_0049e790(CEditorTools*, char* format, ...) {
    if (!std::strncmp(format, "Press new key or button for ", 28)) capture = Capture::pressing;
    else if (capture == Capture::pressing) capture = Capture::released;
}
void shape_edittool_cpp_CEditorTools_showError_FUN_0049e740(CEditorTools*, char*, ...) { std::abort(); }
char* core_menu_cpp_getKeyDisplayName_FUN_005134e0(EInputCodeType) { return const_cast<char*>("test binding"); }
int core_menu_cpp_calibrateGamepad_FUN_005104d0() { std::abort(); }
void engine_2d_c_clearInputAndWait_FUN_00403260() { std::memset(g_KeyboardState, 0, sizeof(g_KeyboardState)); }
void wincore_winrun_cpp_enqueueInput_FUN_005f2f30(int) { std::abort(); }
std::string watcom_resolve_fs_path(const char* path) { return path; }
extern "C" int nocturne_automap_owns_controls() { return 0; }
extern "C" int* nocturne_automap_key_binding() { return &map_binding; }
extern "C" int* nocturne_automap_zoom_setting() { return &map_zoom; }
extern "C" char* nocturne_automap_key_label() { return const_cast<char*>("Map"); }
extern "C" void nocturne_automap_apply_default_binding() { map_binding = DIK_TAB; }
extern "C" void nocturne_window_set_size(int, int) {}
DWORD engine_dosio_cpp_setReadonlyAttribute_FUN_00600c30(char*, DWORD) { return 0; }
CIniFile* engine_ini_cpp_CIniFile_ctor_FUN_004fba70(CIniFile* ini, char*, char*) { return ini; }
void engine_ini_cpp_CIniFile_readIniHeader_FUN_004fbae0(CIniFile*, char*) {}
void engine_ini_cpp_CIniFile_getString_FUN_004fbb20(CIniFile*, char*, char*, int) {}
void engine_ini_cpp_CIniFile_getInteger_FUN_004fbc30(CIniFile*, char* name, int* value) {
    const auto found = saved_ini.find(name);
    if (found != saved_ini.end()) *value = found->second;
}
void sound_sndmain_cpp_readIni_FUN_005abf20(CIniFile*) {}
void core_msnedit_cpp_CDemonMission_readIni_FUN_00537530(CDemonMission*, CIniFile*) {}
void core_setedit_cpp_CDemonSet_readIni_FUN_00584900(CDemonSet*, CIniFile*) {}

int main() {
    CConsole console{};
    CKeys keys{};
    CKeys_vtable vtable{};
    vtable.getKeyState = engine_keys_cpp_CKeys_getKeyState_FUN_00502420;
    keys.vtable = &vtable;
    g_CKeysPtr = &keys;
    g_CGamePtr = &game;
    g_CConsolePtr = &console;
    nocturne_gamepad_init();
    check(nocturne_gamepad_present(), "production PS4 driver opened the fixture device");
    test_ini_migration();
    capture_touchpad(&map_binding);
    capture_touchpad(&game.key_light);
    check(map_binding == INPUT_NONE, "real duplicate-binding cleanup releases the previous Map action");
    sample(ORBIS_PAD_BUTTON_TOUCH_PAD);
    const int code = game.key_light;
    check(g_KeyboardState[code] != 0, "remapped action receives the physical press");
    g_KeyboardState[code] = 0;
    sample(ORBIS_PAD_BUTTON_TOUCH_PAD);
    check(g_KeyboardState[code] == 0, "held Touchpad does not create another edge");
    sample(0);
    sample(ORBIS_PAD_BUTTON_TOUCH_PAD);
    check(g_KeyboardState[code] != 0, "releasing and pressing creates a new edge");
    sample(0);
    std::printf("PS4 backend and recovered key capture: %u checks passed\n", checks);
}
