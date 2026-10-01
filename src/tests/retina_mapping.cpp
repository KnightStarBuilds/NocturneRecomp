// Window coordinates are Cocoa points; renderer coordinates are logical pixels.
#include "gl/gl_present.h"
#include <cstdio>

SDL_Window *g_sdlWindow = nullptr;

int main() {
    struct Case { int x, y, ww, wh, dw, dh, want_x, want_y; };
    const Case cases[] = {
        {320, 240, 640, 480, 640, 480, 320, 240},
        {320, 240, 640, 480, 1280, 960, 320, 240},
        {960, 540, 1920, 1080, 3840, 2160, 320, 240},
        {240, 0, 1920, 1080, 3840, 2160, 0, 0},
        {0, 540, 1920, 1080, 3840, 2160, 0, 240},
        {1919, 1079, 1920, 1080, 3840, 2160, 639, 479},
        {320, 400, 640, 800, 1280, 1600, 320, 240},
        {320, 0, 640, 800, 1280, 1600, 320, 0},
        {320, 799, 640, 800, 1280, 1600, 320, 479},
        // An odd drawable leaves one extra padding pixel above the GL viewport.
        {640, 2, 1280, 961, 1280, 961, 320, 0},
        {160, 120, 320, 240, 640, 480, 320, 240},
    };
    for (const auto &c : cases) {
        int x = -1, y = -1;
        nocturne_gl_map_window_point(c.x, c.y, c.ww, c.wh, c.dw, c.dh,
                                    640, 480, &x, &y);
        if (x != c.want_x || y != c.want_y) {
            std::fprintf(stderr, "%dx%d drawable %dx%d point (%d,%d): "
                         "got (%d,%d), expected (%d,%d)\n", c.ww, c.wh, c.dw,
                         c.dh, c.x, c.y, x, y, c.want_x, c.want_y);
            return 1;
        }
    }
    // Inverse conversion must put the mouse-look origin at the drawable centre.
    // Test independently of the forward conversion, which could share a bug.
    const Case centers[] = {
        {320, 240, 640, 480, 1280, 960, 320, 240},
        {320, 240, 1920, 1080, 3840, 2160, 960, 540},
        {320, 240, 640, 800, 1280, 1600, 320, 400},
        {0, 0, 1920, 1080, 3840, 2160, 240, 0},
        {0, 0, 640, 800, 1280, 1600, 0, 160},
    };
    for (const auto &c : centers) {
        int x = -1, y = -1;
        nocturne_gl_map_logical_point(c.x, c.y, c.ww, c.wh, c.dw, c.dh,
                                     640, 480, &x, &y);
        if (x != c.want_x || y != c.want_y) {
            std::fprintf(stderr, "Cursor warp: got (%d,%d), expected (%d,%d)\n",
                         x, y, c.want_x, c.want_y);
            return 1;
        }
    }
    std::puts("Retina, fractional scaling, letterbox edges and odd padding passed");
}
