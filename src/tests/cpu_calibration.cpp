// Run the recovered calibration with controlled timer samples. Release formerly
// removed its benchmark loop and divided by zero when successive ticks matched.
#include "nocturne.h"
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

BOOL g_UseMultimediaTimer{};
HWND g_MainWindowHandle{};
_struct_19 g_PerformanceCounter{}, g_PerformanceFrequency{};
double g_TimerCalibration{};
int g_InputDisabled{}, g_CalibratedCPUSpeed{};
char* g_ApplicationTimerTitle = const_cast<char*>("calibration test");

namespace {
std::array<std::uint32_t, 10> ticks{};
unsigned reads{}, counter_reads{};

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
BOOL frequency(LARGE_INTEGER* value) {
    value->QuadPart = 1000000000;
    return 1;
}
BOOL counter(LARGE_INTEGER* value) {
    ++counter_reads;
    value->QuadPart = 123456789;
    return 1;
}
MMRESULT unexpected_period(UINT) { std::abort(); }
int unexpected_message(HWND, LPCSTR, LPCSTR, UINT) { std::abort(); }

void run(const std::array<std::uint32_t, 10>& samples, int expected) {
    ticks = samples;
    reads = counter_reads = 0;
    g_CalibratedCPUSpeed = -1;
    wincore_winrun_cpp_calibrateCPUSpeed_FUN_005f2b80();
    check(reads == 10, "calibration retains five start/end measurements");
    check(counter_reads == 1 && g_TimerCalibration == 1000000000.0 &&
          g_PerformanceCounter.LowPart == 123456789,
          "performance timer setup remains intact");
    check(g_CalibratedCPUSpeed == expected, "calibration preserves integer quotient and average");
    check(!g_UseMultimediaTimer && !g_InputDisabled, "successful performance timer needs no fallback");
}
}

QUERY_PERFORMANCE_FREQUENCY_FUNC* g_QueryPerformanceFrequencyFunc = frequency;
QUERY_PERFORMANCE_COUNTER_FUNC* g_QueryPerformanceCounterFunc = counter;
TIME_BEGIN_PERIOD_FUNC* g_timeBeginPeriodFunc = unexpected_period;
MESSAGE_BOX_A_FUNC* g_MessageBoxAFunc = unexpected_message;

int wincore_winrun_cpp_getTime_FUN_005f2dc0() {
    check(reads < ticks.size(), "calibration must not spin waiting for the clock to advance");
    return std::bit_cast<std::int32_t>(ticks[reads++]);
}

int main() {
    // A fully unchanged clock must still terminate with a bounded estimate.
    run({100, 100, 100, 100, 100, 100, 100, 100, 100, 100}, 117964800);
    run({0, 10, 10, 30, 30, 70, 70, 150, 150, 310},
        (117964800 / 10 + 117964800 / 20 + 117964800 / 40 +
         117964800 / 80 + 117964800 / 160) / 5);
    // Both the signed midpoint and full 32-bit wrap have ordinary elapsed ticks.
    run({0x7ffffffeU, 0x80000002U, 0xfffffffeU, 2, 20, 24, 30, 34, 40, 44},
        117964800 / 4);
    run({7, 7, 10, 12, 20, 24, 30, 38, 40, 56},
        (117964800 + 117964800 / 2 + 117964800 / 4 +
         117964800 / 8 + 117964800 / 16) / 5);
    std::puts("CPU calibration zero, measured and wrapping intervals passed");
}
