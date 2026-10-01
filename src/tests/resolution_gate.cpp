#include "game/resolution.h"
#include "shim_config.h"
#include <cstdio>

int main() {
#if defined(__APPLE__) && !NOCTURNE_EXPERIMENTAL_RESOLUTIONS
    if (nocturne_resolution_count() != 1) return 1;
    int width = 0, height = 0;
    if (!nocturne_resolution_at(0, &width, &height) || width != 640 || height != 480)
        return 1;
    if (nocturne_resolution_at(1, &width, &height)) return 1;
    // Neither stepping nor importing an older high-resolution choice escapes
    // the validated mode. Desktop scaling remains owned by the presenter.
    const int steps[] = {-1, 1};
    for (int step : steps) {
        nocturne_resolution_step(640, 480, step, 0, &width, &height);
        if (width != 640 || height != 480) return 1;
        nocturne_resolution_step(1600, 1200, step, 0, &width, &height);
        if (width != 640 || height != 480) return 1;
    }
#else
    if (nocturne_resolution_count() < 2) return 1;
#endif
    std::puts("Platform resolution choices passed");
}
