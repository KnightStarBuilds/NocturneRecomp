#pragma once

// Installed content is read-only; configuration, saves and logs belong to /data.
namespace nocturne::ps4 {
inline constexpr char executable[] = "/app0/eboot.bin";
inline constexpr char resources[] = "/app0";
inline constexpr char original_data[] = "/app0/data/original";
inline constexpr char user_data[] = "/data/NocturneRecompKnightStar";
inline constexpr int framebuffer_width = 640;
inline constexpr int framebuffer_height = 480;
inline constexpr int framebuffer_bpp = 32;

// The CPU lightmap compositor is limited to 480 lines. Apply after reading a
// profile too, so a desktop INI cannot select an unavailable console renderer.
inline void apply_graphics(int& width, int& height, int& bpp,
                           int& acceleration, char* renderer) {
    width = framebuffer_width;
    height = framebuffer_height;
    bpp = framebuffer_bpp;
    acceleration = 0;
    renderer[0] = '\0';
}
}
