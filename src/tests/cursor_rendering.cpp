// Compile the three actual recovered cursor entry points with GAMEPAD_ONLY
// both on and off. Pixel/bitmap services record output without a GPU or SDL.
#include "nocturne.h"
#include <cstdio>
#include <cstdlib>

int g_MouseX = 100, g_MouseY = 100;
int g_GlobalDeltaTimeInt = 1;
int g_CrosshairBrightness = 64, g_BrightnessDirection = 8;
int g_ActiveRenderColor = 0;
int g_ClipLeft = 0, g_ClipTop = 0, g_ClipRight = 640, g_ClipBottom = 480;
int g_CursorSizeHorizontal = 10, g_CursorSizeVertical = 10;
int g_MouseCursorType = 0;
int g_WindowWidth = 640, g_WindowHeight = 480;
uchar g_ColorCubeLookup[32768]{};
CGame* g_CGamePtr = nullptr;
char* g_CurrentFilename = nullptr;
int g_CurrentLineNumber = 0;

namespace {
int pixels, bitmaps, blend_changes;
int bitmap_x, bitmap_y;
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}

void engine_2d_c_plotPixel_FUN_00401140(int, int) { ++pixels; }
void engine_alphabit_cpp_CAlphaBitmap_display_FUN_00410950(CAlphaBitmap*, int x, int y, int) {
    ++bitmaps;
    bitmap_x = x;
    bitmap_y = y;
}
void engine_3d_c_setBlendMode_FUN_00406dd0(int) { ++blend_changes; }
uint shape_edittool_cpp_CEditorTools_getTimeCycledColorByte_FUN_004a1330(CEditorTools*) { return 7; }
void core_main_c_displayErrorAndQuit_FUN_00506f10(char*, ...) { std::abort(); }

int main() {
    CMouse mouse{};
    CGame game{};
    g_CGamePtr = &game;
    for (int effect = 0; effect <= 2; ++effect) {
        mouse.effect_mode = effect;
        mouse.anim_timer = 0;
        bitmaps = blend_changes = 0;
        engine_ncursfx_cpp_CMouse_render_FUN_005445c0(&mouse);
#if GAMEPAD_ONLY
        check(bitmaps == 0 && blend_changes == 0, "controller build drew a menu pointer");
#else
        check(bitmaps == (effect == 1 ? 2 : 1), "desktop menu pointer or effect missing");
        check(bitmap_x == 87 && bitmap_y == 88, "menu pointer hotspot changed");
#endif
    }
    const int modes[] = {CONTROL_MODE_KEYBOARD, CONTROL_MODE_GAMEPAD};
    for (int mode : modes) {
        game.game_control = mode;
        for (int type = 0; type <= 2; ++type) {
            g_MouseCursorType = type;
            pixels = 0;
            shape_edittool_cpp_CEditorTools_drawMousePointer_FUN_004a1380(nullptr, 1);
#if GAMEPAD_ONLY
            check(pixels == 0, "controller build drew a dialog pointer");
#else
            check(pixels > 0, "desktop dialog pointer shape missing");
#endif
        }
    }
    pixels = 0;
    wincore_winrun_cpp_drawCrosshair_FUN_005f2fd0();
#if GAMEPAD_ONLY
    check(pixels == 0 && g_CrosshairBrightness == 64, "controller build drew a cursor crosshair");
#else
    check(pixels == 22 && g_CrosshairBrightness == 72, "desktop cursor crosshair changed");
#endif
    check(g_MouseX == 100 && g_MouseY == 100, "cursor policy changed mouse coordinates");
    std::puts("Recovered cursor rendering policy passed");
}
