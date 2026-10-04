// OpenOrbis's musl headers and precompiled libc++ use Linux clock IDs. The
// firmware imports use BSD IDs, so correct the ABI at the shared C entry point.
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <cerrno>
#include <cstdint>
#include "clock.h"

// The pinned SDK declares sceKernelClockGettime as void() without parameters.
// A separately named declaration supplies the ABI without modifying the SDK.
extern "C" std::int32_t nocturne_sce_kernel_clock_gettime(int, struct timespec*)
#if defined(NOCTURNE_PS4)
    __asm__("sceKernelClockGettime")
#endif
    ;

int nocturne_ps4_clock_gettime(int clock_id, struct timespec* output) {
    if (!output) { errno = EFAULT; return -1; }
    int native_id;
    // These are the two clock domains used by libc++, FFmpeg and Nocturne.
    // Reject other SDK IDs instead of silently selecting a different domain.
    switch (clock_id) {
    case 0: native_id = 0; break; // SDK CLOCK_REALTIME -> native realtime.
    case 1: native_id = 4; break; // SDK CLOCK_MONOTONIC -> native monotonic.
    default: errno = EINVAL; return -1;
    }
    const std::int32_t result = nocturne_sce_kernel_clock_gettime(native_id, output);
    if (result >= 0) return 0;
    const auto code = static_cast<std::uint32_t>(result);
    errno = (code & 0xffff0000u) == 0x80020000u ? static_cast<int>(code & 0xffffu) : EIO;
    return -1;
}

#if defined(NOCTURNE_PS4)
extern "C" int clock_gettime(clockid_t clock_id, struct timespec* output) {
    return nocturne_ps4_clock_gettime(static_cast<int>(clock_id), output);
}
#endif
