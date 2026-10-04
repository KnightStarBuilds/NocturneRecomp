#include "recursive_mutex.h"
#include <orbis/libkernel.h>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <system_error>

namespace nocturne::ps4 {
namespace {
void checked(int32_t result, const char* operation) {
    if (result != 0)
        throw std::system_error(static_cast<unsigned>(result) & 0xffffu,
                                std::generic_category(), operation);
}
}

RecursiveMutex::RecursiveMutex() {
    alignas(void*) unsigned char storage[64]{};
    auto* attr = reinterpret_cast<OrbisPthreadMutexattr*>(storage);
    auto* mutex = reinterpret_cast<OrbisPthreadMutex*>(native_);
    checked(scePthreadMutexattrInit(attr), "PS4 mutex attributes");
    const int32_t configured = scePthreadMutexattrSettype(attr, PTHREAD_MUTEX_RECURSIVE);
    const int32_t initialized = configured == 0 ?
        scePthreadMutexInit(mutex, attr, "Nocturne recursive mutex") : configured;
    scePthreadMutexattrDestroy(attr);
    checked(initialized, "PS4 recursive mutex initialization");
}

RecursiveMutex::~RecursiveMutex() {
    scePthreadMutexDestroy(reinterpret_cast<OrbisPthreadMutex*>(native_));
}

void RecursiveMutex::lock() {
    checked(scePthreadMutexLock(reinterpret_cast<OrbisPthreadMutex*>(native_)), "PS4 mutex lock");
}

void RecursiveMutex::unlock() {
    checked(scePthreadMutexUnlock(reinterpret_cast<OrbisPthreadMutex*>(native_)), "PS4 mutex unlock");
}

bool RecursiveMutex::try_lock_for(std::chrono::milliseconds timeout) {
    auto* mutex = reinterpret_cast<OrbisPthreadMutex*>(native_);
    if (timeout.count() <= 0) {
        const int32_t result = scePthreadMutexTrylock(mutex);
        if ((static_cast<unsigned>(result) & 0xffffu) == EBUSY) return false;
        checked(result, "PS4 mutex try-lock");
        return true;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::microseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) return false;
        // The SDK's native timeout is 32-bit microseconds; preserve longer
        // Win32 waits by retrying chunks without changing the game's clock.
        const auto chunk = static_cast<OrbisKernelUseconds>(std::min<std::uint64_t>(
            left, std::numeric_limits<OrbisKernelUseconds>::max()));
        const int32_t result = scePthreadMutexTimedlock(mutex, chunk);
        if (result == 0) return true;
        if ((static_cast<unsigned>(result) & 0xffffu) != ETIMEDOUT)
            checked(result, "PS4 mutex timed lock");
    }
}
}
