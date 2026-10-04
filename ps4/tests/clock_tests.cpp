// Exercise the actual C adapter with a fake firmware entry point on the host.
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include "../runtime/clock.h"

static int native_id = -1;
static int calls;
static std::int32_t native_result;
extern "C" std::int32_t nocturne_sce_kernel_clock_gettime(int id, struct timespec* output) {
    ++calls;
    native_id = id;
    if (native_result >= 0) { output->tv_sec = 123; output->tv_nsec = 456; }
    return native_result;
}

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

int main() {
    timespec output{};
    errno = EDOM;
    // Literal SDK numbers matter: the host's CLOCK_MONOTONIC may differ.
    CHECK(nocturne_ps4_clock_gettime(0, &output) == 0 && native_id == 0);
    CHECK(output.tv_sec == 123 && output.tv_nsec == 456 && errno == EDOM);
    CHECK(nocturne_ps4_clock_gettime(1, &output) == 0 && native_id == 4);
    const int prior_calls = calls;
    CHECK(nocturne_ps4_clock_gettime(99, &output) == -1 && errno == EINVAL && calls == prior_calls);
    CHECK(nocturne_ps4_clock_gettime(1, nullptr) == -1 && errno == EFAULT && calls == prior_calls);
    native_result = static_cast<std::int32_t>(0x80020016u);
    CHECK(nocturne_ps4_clock_gettime(1, &output) == -1 && errno == EINVAL);
    native_result = -1;
    CHECK(nocturne_ps4_clock_gettime(0, &output) == -1 && errno == EIO);
    std::puts("PS4 clock domain and error conversion checks passed");
}
