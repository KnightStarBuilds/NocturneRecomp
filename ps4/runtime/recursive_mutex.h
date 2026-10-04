#pragma once
#include <chrono>

namespace nocturne::ps4 {
// SDK pthread attributes reserve four bytes but libkernel writes a pointer.
// Keep native handle storage separate from the SDK's C++ synchronization ABI.
class RecursiveMutex {
public:
    RecursiveMutex();
    ~RecursiveMutex();
    RecursiveMutex(const RecursiveMutex&) = delete;
    RecursiveMutex& operator=(const RecursiveMutex&) = delete;
    void lock();
    void unlock();
    bool try_lock_for(std::chrono::milliseconds timeout);
private:
    alignas(void*) unsigned char native_[64]{};
};
}
