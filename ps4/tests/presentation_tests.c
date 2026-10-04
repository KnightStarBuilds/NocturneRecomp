#include "../sdl/presentation.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "line %d: %s\n", __LINE__, #condition); return EXIT_FAILURE; } } while (0)

int main(void)
{
    NocturnePS4Viewport rect = nocturne_ps4_viewport(640, 480, 1920, 1080);
    /* A 4:3 game must remain 4:3 on a widescreen display. */
    CHECK(rect.x == 240 && rect.y == 0 && rect.width == 1440 && rect.height == 1080);
    rect = nocturne_ps4_viewport(640, 480, 1280, 720);
    CHECK(rect.x == 160 && rect.y == 0 && rect.width == 960 && rect.height == 720);
    rect = nocturne_ps4_viewport(1920, 1080, 640, 480);
    CHECK(rect.x == 0 && rect.y == 60 && rect.width == 640 && rect.height == 360);
    rect = nocturne_ps4_viewport(0, 480, 1920, 1080);
    CHECK(rect.width == 0 && rect.height == 0);
    {
        /* Row padding must not enter the picture; channels and orientation
         * must survive the CPU copy without swapping red/blue or flipping Y. */
        const uint32_t source[2][3] = {{0xff0000ffu, 0xff00ff00u, 0xdeadbeefu},
                                     {0xffff0000u, 0xffffffffu, 0xdeadbeefu}};
        uint32_t output[4][8];
        int y, x;
        nocturne_ps4_scale_pixels(source, 2, 2, sizeof(source[0]), &output[0][0], 8, 4);
        for (y = 0; y < 4; ++y) {
            CHECK(output[y][0] == 0 && output[y][1] == 0);
            CHECK(output[y][6] == 0 && output[y][7] == 0);
            for (x = 2; x < 6; ++x) CHECK(output[y][x] == source[y / 2][(x - 2) / 2]);
        }
    }
    {
        /* Exercise nonintegral enlargement and reduction against the exact
         * pixel-center convention used by nearest-neighbor presentation. */
        uint32_t source[13][19], output[17][23];
        int sw, sh, dw, dh, x, y;
        for (y = 0; y < 13; ++y)
            for (x = 0; x < 19; ++x) source[y][x] = 1u + (uint32_t)(y * 19 + x);
        for (sw = 1; sw <= 19; sw += 3) for (sh = 1; sh <= 13; sh += 3)
            for (dw = 1; dw <= 23; dw += 2) for (dh = 1; dh <= 17; dh += 2) {
                rect = nocturne_ps4_viewport(sw, sh, dw, dh);
                nocturne_ps4_scale_pixels(source, sw, sh, sizeof(source[0]), &output[0][0], dw, dh);
                const uint32_t *packed = &output[0][0];
                for (y = 0; y < dh; ++y) for (x = 0; x < dw; ++x) {
                    uint32_t expected = 0;
                    if (x >= rect.x && x < rect.x + rect.width &&
                        y >= rect.y && y < rect.y + rect.height)
                        expected = source[(y - rect.y) * sh / rect.height][(x - rect.x) * sw / rect.width];
                    CHECK(packed[y * dw + x] == expected);
                }
            }
    }
    CHECK(nocturne_ps4_stick_axis(0) == -32768);
    CHECK(nocturne_ps4_stick_axis(128) == 0);
    CHECK(nocturne_ps4_stick_axis(255) == 32767);
    {
        int i;
        for (i = 1; i < 256; ++i)
            CHECK(nocturne_ps4_stick_axis((uint8_t)i) > nocturne_ps4_stick_axis((uint8_t)(i - 1)));
    }
    puts("PS4 presentation and controller conversion checks passed");
    return EXIT_SUCCESS;
}
