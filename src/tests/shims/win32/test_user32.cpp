// Exercise the real SDL -> Win32 message bridge through its wired entry points.
// The dummy video driver needs no display or GL context. Only unrelated window
// decoration, rendering, controller and media services are replaced below.
#include "nocturne_test.h"
#include "system/user32.h"
#include "globals/globals_610000.h"
#include "win32/shim_init.h"
#include <SDL.h>
#include <cstdint>
#include <stdexcept>

extern SDL_Window* g_sdlWindow;

namespace {
constexpr unsigned kSetCursor = 0x0020;
constexpr unsigned kActivateApp = 0x001c;
constexpr unsigned kMouseMove = 0x0200;
constexpr unsigned kKeyUp = 0x0101;

struct Observed {
    HWND window;
    unsigned message;
    WPARAM wparam;
    LPARAM lparam;
    int cursor_visible;
};
std::vector<Observed> received;
int mapping_calls = 0;
int mapped_x = 0;
int mapped_y = 0;
int focus_requests = 0;

LRESULT window_proc(HWND window, unsigned message, WPARAM wparam, LPARAM lparam) {
    // This is the recovered mainWindowProc's cursor contract: its software
    // cursor owns the pixels, so a native cursor request is answered with null.
    if (message == kSetCursor) g_SetCursorFunc(nullptr);
    received.push_back({window, message, wparam, lparam, SDL_ShowCursor(SDL_QUERY)});
    return 0;
}

void dispatch_pending() {
    MSG message{};
    unsigned count = 0;
    while (g_PeekMessageAFunc(&message, 0, 0, 0, 1 /* PM_REMOVE */)) {
        if (++count > 100) throw std::runtime_error("message queue did not drain");
        g_DispatchMessageAFunc(&message);
    }
}

struct Window {
    HWND handle{};
    HCURSOR arrow{};

    Window() {
        SDL_SetMainReady();
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
        shims_init_user32();
        arrow = g_LoadCursorAFunc(nullptr, reinterpret_cast<LPCSTR>(32512));
        g_SetCursorFunc(arrow);
        SDL_ShowCursor(SDL_ENABLE);
        received.clear();
        mapping_calls = focus_requests = 0;
        WNDCLASSA window_class{};
        window_class.lpfnWndProc = window_proc;
        window_class.lpszClassName = "cursor-test";
        g_RegisterClassAFunc(&window_class);
        handle = g_CreateWindowExAFunc(0, "cursor-test", "cursor-test", 0,
                                      0, 0, 640, 480, 0, nullptr, nullptr, nullptr);
        if (!handle) {
            SDL_Quit();
            throw std::runtime_error("dummy window creation failed");
        }
        // Native driver events are irrelevant; the shim's synthetic initial
        // messages remain queued and must be delivered through Peek/Dispatch.
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    }

    ~Window() {
        MSG message{};
        while (g_PeekMessageAFunc(&message, 0, 0, 0, 1)) {}
        SDL_DestroyWindow(g_sdlWindow);
        g_sdlWindow = nullptr;
        SDL_Quit();
        received.clear();
    }

    void after_initial_messages() {
        dispatch_pending();
        received.clear();
        mapping_calls = focus_requests = 0;
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    }

    void push(SDL_Event event) const {
        if (SDL_PushEvent(&event) != 1) throw std::runtime_error(SDL_GetError());
    }

    void window_event(Uint8 kind) const {
        SDL_Event event{};
        event.type = SDL_WINDOWEVENT;
        event.window.windowID = SDL_GetWindowID(g_sdlWindow);
        event.window.event = kind;
        push(event);
    }
};

void check_cursor_request(const Observed& event, HWND window) {
    CHECK_EQ(event.message, kSetCursor);
    CHECK_EQ(event.window, window);
    CHECK_EQ(event.wparam, static_cast<WPARAM>(window));
    CHECK_EQ(static_cast<unsigned>(event.lparam) & 0xffffu, 1u); // HTCLIENT
    CHECK_EQ(event.cursor_visible, SDL_DISABLE);
}
} // namespace

extern "C" void nocturne_apply_window_icon(SDL_Window*) {}
extern "C" void nocturne_gamepad_pump() {}
extern "C" int mci_video_pump_frame() { return 0; }
extern "C" int mci_video_is_playing() { return 0; }
extern "C" void nocturne_focus_trace_arm() { ++focus_requests; }
extern "C" void nocturne_gl_window_to_logical(int x, int y, int* out_x, int* out_y) {
    ++mapping_calls;
    mapped_x = x;
    mapped_y = y;
    *out_x = x / 2 + 7;
    *out_y = y / 2 + 11;
}
extern "C" void nocturne_gl_logical_to_window(int x, int y, int* out_x, int* out_y) {
    *out_x = (x - 7) * 2;
    *out_y = (y - 11) * 2;
}

NOCTURNE_TEST(set_cursor_hides_shows_and_returns_the_previous_handle) {
    Window window;
    // Do not dispatch the initial request: this case drives the cursor API itself.
    CHECK(window.arrow != nullptr);
    CHECK_EQ(g_SetCursorFunc(nullptr), window.arrow);
    CHECK_EQ(SDL_ShowCursor(SDL_QUERY), SDL_DISABLE);
    CHECK_EQ(g_SetCursorFunc(nullptr), nullptr);
    CHECK_EQ(SDL_ShowCursor(SDL_QUERY), SDL_DISABLE);
    CHECK_EQ(g_SetCursorFunc(window.arrow), nullptr);
    CHECK_EQ(SDL_ShowCursor(SDL_QUERY), SDL_ENABLE);
    CHECK_EQ(g_SetCursorFunc(window.arrow), window.arrow);
    CHECK_EQ(SDL_ShowCursor(SDL_QUERY), SDL_ENABLE);
    CHECK_EQ(g_SetCursorFunc(nullptr), window.arrow);
    CHECK_EQ(SDL_ShowCursor(SDL_QUERY), SDL_DISABLE);
}

NOCTURNE_TEST(initial_cursor_request_precedes_window_activation) {
    Window window;
    dispatch_pending();
    CHECK_EQ(received.size(), 2u);
    if (received.size() != 2) return;
    check_cursor_request(received[0], window.handle);
    CHECK_EQ(received[1].message, kActivateApp);
    CHECK_EQ(received[1].wparam, 1u);
    CHECK_EQ(received[1].cursor_visible, SDL_DISABLE);
}

NOCTURNE_TEST(mouse_motion_reapplies_hidden_cursor_before_preserving_logical_position) {
    Window window;
    window.after_initial_messages();
    // A driver can reveal its cursor while our remembered Win32 handle is still
    // null. A repeated null request must therefore reach SDL again.
    SDL_ShowCursor(SDL_ENABLE);
    SDL_Event event{};
    event.type = SDL_MOUSEMOTION;
    event.motion.windowID = SDL_GetWindowID(g_sdlWindow);
    event.motion.x = 123;
    event.motion.y = 87;
    window.push(event);
    dispatch_pending();
    CHECK_EQ(mapping_calls, 1);
    CHECK_EQ(mapped_x, 123);
    CHECK_EQ(mapped_y, 87);
    CHECK_EQ(received.size(), 2u);
    if (received.size() != 2) return;
    check_cursor_request(received[0], window.handle);
    CHECK_EQ(received[1].message, kMouseMove);
    CHECK_EQ(received[1].window, window.handle);
    CHECK_EQ(received[1].lparam, (54 << 16) | 68);
    CHECK_EQ(received[1].cursor_visible, SDL_DISABLE);
}

NOCTURNE_TEST(window_enter_requests_cursor_before_the_next_keyboard_message) {
    Window window;
    window.after_initial_messages();
    SDL_ShowCursor(SDL_ENABLE);
    window.window_event(SDL_WINDOWEVENT_ENTER);
    SDL_Event key{};
    key.type = SDL_KEYUP;
    key.key.windowID = SDL_GetWindowID(g_sdlWindow);
    key.key.keysym.scancode = SDL_SCANCODE_A;
    key.key.keysym.sym = SDLK_a;
    window.push(key);
    dispatch_pending();
    CHECK_EQ(received.size(), 2u);
    if (received.size() != 2) return;
    check_cursor_request(received[0], window.handle);
    CHECK_EQ(received[1].message, kKeyUp);
    CHECK_EQ(received[1].wparam, static_cast<WPARAM>('a'));
    CHECK_EQ(received[1].lparam, 0x1e << 16);
    CHECK_EQ(received[1].cursor_visible, SDL_DISABLE);
}

NOCTURNE_TEST(focus_regain_requests_cursor_before_reactivation) {
    Window window;
    window.after_initial_messages();
    SDL_ShowCursor(SDL_ENABLE);
    window.window_event(SDL_WINDOWEVENT_FOCUS_GAINED);
    dispatch_pending();
    CHECK_EQ(focus_requests, 1);
    CHECK_EQ(received.size(), 2u);
    if (received.size() != 2) return;
    check_cursor_request(received[0], window.handle);
    CHECK_EQ(received[1].message, kActivateApp);
    CHECK_EQ(received[1].wparam, 1u);
    CHECK_EQ(received[1].cursor_visible, SDL_DISABLE);
}

NOCTURNE_TEST(focus_loss_keeps_the_existing_deactivation_message) {
    Window window;
    window.after_initial_messages();
    window.window_event(SDL_WINDOWEVENT_FOCUS_LOST);
    dispatch_pending();
    CHECK_EQ(focus_requests, 0);
    CHECK_EQ(received.size(), 1u);
    if (received.size() != 1) return;
    CHECK_EQ(received[0].message, kActivateApp);
    CHECK_EQ(received[0].wparam, 0u);
}

NOCTURNE_TEST(mouse_button_messages_keep_their_transformed_coordinates) {
    Window window;
    window.after_initial_messages();
    for (Uint32 type : {SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP}) {
        SDL_Event event{};
        event.type = type;
        event.button.windowID = SDL_GetWindowID(g_sdlWindow);
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = 250;
        event.button.y = 150;
        window.push(event);
    }
    dispatch_pending();
    CHECK_EQ(mapping_calls, 2);
    CHECK_EQ(received.size(), 2u);
    if (received.size() != 2) return;
    CHECK_EQ(received[0].message, 0x0201u); // WM_LBUTTONDOWN
    CHECK_EQ(received[1].message, 0x0202u); // WM_LBUTTONUP
    for (const auto& event : received) {
        CHECK_EQ(event.window, window.handle);
        CHECK_EQ(event.lparam, (86 << 16) | 132);
    }
}

NOCTURNE_TEST_MAIN()
