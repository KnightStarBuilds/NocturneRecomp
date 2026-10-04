#ifndef NOCTURNE_PS4_PRESENTATION_H
#define NOCTURNE_PS4_PRESENTATION_H
#include <stddef.h>
#include <stdint.h>

typedef struct { int x, y, width, height; } NocturnePS4Viewport;

/* The game surface is top-left based, with the same packed RGBA byte order as
 * VideoOut. Pitch may include padding; black bars belong to the TV buffer. */
static inline NocturnePS4Viewport nocturne_ps4_viewport(int sw, int sh, int dw, int dh)
{
    NocturnePS4Viewport rect = {0, 0, 0, 0};
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return rect;
    rect.width = dw;
    rect.height = (int)((int64_t)sh * dw / sw);
    if (rect.height > dh) {
        rect.height = dh;
        rect.width = (int)((int64_t)sw * dh / sh);
    }
    rect.x = (dw - rect.width) / 2;
    rect.y = (dh - rect.height) / 2;
    return rect;
}
static inline void nocturne_ps4_scale_pixels(const void *pixels, int sw, int sh,
                                            size_t pitch, uint32_t *output, int dw, int dh)
{
    NocturnePS4Viewport rect = nocturne_ps4_viewport(sw, sh, dw, dh);
    int x, y;
    for (y = 0; y < dh; ++y)
        for (x = 0; x < dw; ++x) output[(size_t)y * dw + x] = 0;
    if (!rect.width || !rect.height) return;
    const int x_step = sw / rect.width;
    const int x_remainder = sw % rect.width;
    for (y = 0; y < rect.height; ++y) {
        const uint32_t *source = (const uint32_t *)((const uint8_t *)pixels +
                    (size_t)((int64_t)y * sh / rect.height) * pitch);
        uint32_t *dest = output + (size_t)(y + rect.y) * dw + rect.x;
        int sx = 0, remainder = 0;
        /* Quotient/remainder stepping equals floor(x * sw / width), without
         * a 64-bit divide for each of the TV's 1.5 million image pixels. */
        for (x = 0; x < rect.width; ++x) {
            dest[x] = source[sx];
            sx += x_step;
            remainder += x_remainder;
            if (remainder >= rect.width) { ++sx; remainder -= rect.width; }
        }
    }
}
static inline int16_t nocturne_ps4_stick_axis(uint8_t value)
{
    const int delta = (int)value - 128;
    return delta < 0 ? (int16_t)(delta * 256) : (int16_t)(delta * 32767 / 127);
}
#endif
