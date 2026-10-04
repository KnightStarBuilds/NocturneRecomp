// Compile twice with GAMEPAD_ONLY=0/1. Exercise the recovered configuration,
// control menu, navigation and defaults while replacing rendering/storage I/O.
#include "nocturne.h"
#include "game/gamepad.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

CGame* g_CGamePtr{};
CKeys* g_CKeysPtr{};
CConsole* g_CConsolePtr{};
CDemonMission* g_CDemonMissionPtr{};
CDemonSet* g_CDemonSetPtr{};
CMouse* g_CMousePtr{};
CBitFont* g_ThemeFont{};
CBitFont* g_SmallEditorFont{};
CBitFont* g_MediumFont{};
CMoon g_CMoonInstance{};
char g_CurrentSaveFile[256]{};
char g_RendererDllPath[256]{};
char g_ControlsMenuTextBuffers[9][256]{};
char* g_ControlOptionsMenuPointers[10]{};
int g_MenuLeftRightPressed{}, g_MenuInputHappened{}, g_UseOSFonts{};
int g_UseDirect3D{}, g_AGPTextureMode{}, g_CurrentGraphicsBoard{}, g_FirstTimeFlag{};
char* g_CurrentFilename{};
int g_CurrentLineNumber{};
DWORD g_JoyXPos{}, g_JoyYPos{}, g_JoyButtons{};
DWORD g_JoyZPos{}, g_JoyRPos{};
int g_JoystickPresent{};
char g_KeyboardState[4952]{};

namespace {
int failures = 0;
int keys[600]{};
int ini_mode = CONTROL_MODE_MOUSE;
int ini_fire = DIK_SPACE;
int ini_map = -1;
int stored_mode = -1;
int customizations = 0;
int current_alpha = 0;
int control_alpha = -1;
int map_binding = INPUT_NONE;
int map_zoom = 0;
bool escape_menu = false;
void check(bool condition, const char* message) {
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
int read_key(CKeys*, EInputCodeType code) {
    if (code == DIK_ESCAPE && escape_menu) return 1;
    const int result = keys[code];
    keys[code] = 0;
    return result;
}
}

int _sprintf(void* output, const char* format, ...) {
    va_list args; va_start(args, format);
    const int result = std::vsnprintf(static_cast<char*>(output), 256, format, args);
    va_end(args); return result;
}
char* support_newmsg_cpp_getLocalizedString_FUN_005441f0(char* text) { return text; }
void core_main_c_displayErrorAndQuit_FUN_00506f10(char*, ...) { std::abort(); }
DWORD engine_dosio_cpp_setReadonlyAttribute_FUN_00600c30(char*, DWORD) { return 0; }
CIniFile* engine_ini_cpp_CIniFile_ctor_FUN_004fba70(CIniFile* ini, char*, char*) { return ini; }
void engine_ini_cpp_CIniFile_readIniHeader_FUN_004fbae0(CIniFile*, char*) {}
void engine_ini_cpp_CIniFile_getString_FUN_004fbb20(CIniFile*, char*, char*, int) {}
void engine_ini_cpp_CIniFile_setString_FUN_004fbbb0(CIniFile*, char*, char*) {}
void engine_ini_cpp_CIniFile_getInteger_FUN_004fbc30(CIniFile*, char* name, int* value) {
    if (!std::strncmp(name, "key", 3) && std::strcmp(name, "keyMap")) *value = DIK_SPACE;
    if (!std::strcmp(name, "keyMap") && ini_map >= 0) *value = ini_map;
    if (!std::strcmp(name, "gameControl")) *value = ini_mode;
    if (!std::strcmp(name, "keyFire")) *value = ini_fire;
    if (!std::strcmp(name, "xCenter")) *value = 0;
    if (!std::strcmp(name, "alwaysRun")) *value = 1;
    if (!std::strcmp(name, "xMouseSensitivity")) *value = 12345;
}
void engine_ini_cpp_CIniFile_setInteger_FUN_004fbc90(CIniFile*, char* name, int value) {
    if (!std::strcmp(name, "gameControl")) stored_mode = value;
}
void sound_sndmain_cpp_readIni_FUN_005abf20(CIniFile*) {}
void sound_sndmain_cpp_writeIni_FUN_005ac220(CIniFile*) {}
void core_msnedit_cpp_CDemonMission_readIni_FUN_00537530(CDemonMission*, CIniFile*) {}
void core_msnedit_cpp_CDemonMission_writeIni_FUN_005375d0(CDemonMission*, CIniFile*) {}
void core_setedit_cpp_CDemonSet_readIni_FUN_00584900(CDemonSet*, CIniFile*) {}
void core_setedit_cpp_CDemonSet_writeIni_FUN_00584920(CDemonSet*, CIniFile*) {}
extern "C" void nocturne_window_set_size(int, int) {}
extern "C" int* nocturne_automap_key_binding() { return &map_binding; }
extern "C" int* nocturne_automap_zoom_setting() { return &map_zoom; }
extern "C" void nocturne_automap_apply_default_binding() { map_binding = DIK_TAB; }
extern "C" int nocturne_automap_owns_controls() { return 0; }
std::string watcom_resolve_fs_path(const char* path) { return path; }
void wincore_winrun_cpp_enqueueInput_FUN_005f2f30(int) { std::abort(); }
void core_game_cpp_CGame_saveClockTime_FUN_004d7d80(CGame*) {}
void core_game_cpp_CGame_updateDT_FUN_004d7d90(CGame*) {}
void core_game_cpp_CGame_resetKeyState_FUN_004dbe60(CGame*) {}
void core_moon_cpp_CMoon_update_FUN_00529d60(CMoon*, float) {}
void core_moon_cpp_CMoon_render_FUN_00529ed0(CMoon*) {}
void core_moon_cpp_CMoon_renderJoystickCalibration_FUN_0052a2c0(CMoon*) {}
int core_moon_cpp_CMoon_isAnimationFirstHalf_FUN_0052a3f0(CMoon*) { return 0; }
int engine_font_cpp_CBitFont_getTextWidth_FUN_004cfe80(CBitFont*, char*) { return 8; }
int engine_font_cpp_CBitFont_getTextHeight_FUN_004cff40(CBitFont*, char*) { return 8; }
int engine_font_cpp_CBitFont_getCharHeight_FUN_004d01d0(CBitFont*, int) { return 8; }
int engine_font_cpp_CBitFont_drawText_FUN_004cda80(CBitFont*, char* text, int, int, int, int) {
    if (!std::strncmp(text, "Control :", 9)) control_alpha = current_alpha;
    return 8;
}
void engine_2d_c_drawText_FUN_00401fd0(char*, int, int) {}
int engine_3d_c_setRenderAlpha_FUN_00406d80(int alpha) { current_alpha = alpha; return alpha; }
void engine_ncursfx_cpp_CMouse_render_FUN_005445c0(CMouse*) {}
void wincore_wddvmem_cpp_swapBuffers_FUN_005eda20() {}
void core_menu_cpp_configureCustomKeyBindings_FUN_005138e0() { ++customizations; }
void core_menu_cpp_adjustMouseSensitivity_FUN_005107c0(int*, char*) {}
extern "C" unsigned int nocturne_rng_fx() { return 1; }
extern "C" const char* nocturne_version_short() { return "test"; }

int main() {
    CGame game{};
    CConsole console{};
    CKeys keyboard{};
    CKeys_vtable keyboard_vtable{};
    keyboard_vtable.getAndClearKeyState = read_key;
    keyboard.vtable = &keyboard_vtable;
    g_CKeysPtr = &keyboard;
    g_CGamePtr = &game;
    g_CConsolePtr = &console;
    core_game_cpp_CGame_ctor_FUN_004d7b40(&game);
    check(game.game_control == (GAMEPAD_ONLY ? CONTROL_MODE_GAMEPAD : CONTROL_MODE_KEYBOARD),
          "constructor honors the build's control policy");
    core_inivar_cpp_readIniData_FUN_004fbd90();
    check(game.game_control == (GAMEPAD_ONLY ? CONTROL_MODE_GAMEPAD : CONTROL_MODE_MOUSE),
          "INI mode is constrained only in gamepad-only builds");
    check(game.key_fire == (GAMEPAD_ONLY ? NOCTURNE_PAD_RTRIGGER : DIK_SPACE),
          "imported keyboard map is repaired only for a forced pad");
    check(game.always_run == 1 && game.x_mouse_sensitivity == 12345,
          "binding repair preserves later-loaded control preferences");
    ini_mode = CONTROL_MODE_GAMEPAD;
    ini_fire = NOCTURNE_PAD_BUTTON_BASE + 9;
    core_inivar_cpp_readIniData_FUN_004fbd90();
    check(game.key_fire == ini_fire, "custom controller binding survives INI load");
    ini_fire = DIK_SPACE;
    ini_map = NOCTURNE_PAD_BUTTON_BASE + 4;
    core_inivar_cpp_readIniData_FUN_004fbd90();
    check(game.key_fire == DIK_SPACE && map_binding == ini_map,
          "an explicitly customized mixed Map binding is preserved");
    ini_map = -1;
    game.game_control = CONTROL_MODE_MOUSE;
    core_inivar_cpp_writeIniData_FUN_004fc510();
    check(stored_mode == (GAMEPAD_ONLY ? CONTROL_MODE_GAMEPAD : CONTROL_MODE_MOUSE),
          "saved mode follows the build policy");

    game.game_control = CONTROL_MODE_KEYBOARD;
    game.x_center = 0x8000;
    keys[DIK_RETURN] = 1;
    escape_menu = true;
    core_menu_cpp_configureCustomKeys_FUN_00511890();
    escape_menu = false;
    check(game.game_control == (GAMEPAD_ONLY ? CONTROL_MODE_GAMEPAD : CONTROL_MODE_MOUSE),
          "control selector is locked or cycles according to the flag");
    check(customizations == (GAMEPAD_ONLY ? 1 : 0),
          "Customize Keys remains reachable when the mode row is disabled");
    check(control_alpha == (GAMEPAD_ONLY ? 24000 : 48000),
          "fixed control row renders dimmed");

    int selected = 1;
    keys[DIK_NUMPAD8] = 1;
    core_menu_cpp_renderMenuAndGetChoice_FUN_00510000(g_ControlOptionsMenuPointers, 8,
                                                    &selected, 100, nullptr);
    check(selected == (GAMEPAD_ONLY ? 7 : 0), "up navigation skips only the disabled mode row");
    selected = 7;
    keys[DIK_NUMPAD2] = 1;
    core_menu_cpp_renderMenuAndGetChoice_FUN_00510000(g_ControlOptionsMenuPointers, 8,
                                                    &selected, 100, nullptr);
    check(selected == (GAMEPAD_ONLY ? 1 : 0), "down navigation wraps to the first enabled row");
    char ordinary_first[] = "First : choice", ordinary_second[] = "Second";
    char* ordinary[]{ordinary_first, ordinary_second};
    selected = 0;
    keys[DIK_RETURN] = 1;
    check(core_menu_cpp_renderMenuAndGetChoice_FUN_00510000(ordinary, 2, &selected, 100, nullptr) == 0,
          "other menus retain a selectable first row");

    game.game_control = CONTROL_MODE_GAMEPAD;
    game.x_center = 0;
    keys[DIK_ESCAPE] = 1;
    const int calibrated = core_menu_cpp_calibrateGamepad_FUN_005104d0();
    check(calibrated == (GAMEPAD_ONLY ? 1 : 0) &&
          game.game_control == (GAMEPAD_ONLY ? CONTROL_MODE_GAMEPAD : CONTROL_MODE_KEYBOARD),
          "calibration cannot fall back to keyboard in a locked build");
    game.game_control = CONTROL_MODE_GAMEPAD;
    game.x_center = 0;
    keys[DIK_ESCAPE] = 1;
    const int checked = core_menu_cpp_checkAndCalibrateGamepad_FUN_00511840();
    check(checked == (GAMEPAD_ONLY ? 1 : 0) &&
          game.game_control == (GAMEPAD_ONLY ? CONTROL_MODE_GAMEPAD : CONTROL_MODE_KEYBOARD),
          "startup calibration keeps the same control policy");
    std::printf("GAMEPAD_ONLY=%d: %d failures\n", GAMEPAD_ONLY, failures);
    return failures ? 1 : 0;
}
