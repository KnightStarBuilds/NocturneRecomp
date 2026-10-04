// Run on console before interpreting a successful cross-link as runtime support.
// Each completed stage is flushed so a crash identifies the unverified facility.
#include "file_ops.h"
#include "recursive_mutex.h"
#include "ps4_runtime.h"
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <orbis/libkernel.h>

namespace fs = std::filesystem;
static FILE* report;
static void stage(const char* message) {
    std::fprintf(nocturne_ps4_log_stream(stderr), "PS4 SDK probe: %s\n", message);
    if (report) { std::fprintf(report, "%s\n", message); std::fflush(report); }
}
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    nocturne_ps4_startup_log();
    // Direct libc mkdir first: diagnostics remain available if filesystem fails.
    mkdir(nocturne::ps4::user_data, 0777);
    report = std::fopen("/data/NocturneRecompKnightStar/sdk-probe.log", "w");
    stage("begin: console execution required; host linking is insufficient");
    try {
        stage("monotonic clock: sampling across native kernel sleep");
        const auto clock_start = std::chrono::steady_clock::now();
        sceKernelUsleep(20000);
        require(std::chrono::steady_clock::now() - clock_start >= std::chrono::milliseconds(10),
                "steady clock advances in nanoseconds");
        const auto root = fs::path(nocturne::ps4::user_data) / "sdk-probe";
        stage("filesystem: create, stat, iterate, canonicalize");
        fs::create_directories(root);
        require(fs::is_directory(root), "filesystem directory stat");
        { std::ofstream first(root / "first", std::ios::binary); first << "PS4\n";
          require(bool(first), "write fixture"); }
        require(fs::file_size(root / "first") == 4, "filesystem file size/stat ABI");
        require(fs::canonical(root / "first").is_absolute(), "filesystem canonical path");
        stage("filesystem: no-follow status, canonical paths and virtual working directory");
        struct stat native_status{};
        require(lstat((root / "first").c_str(), &native_status) == 0 &&
                    S_ISREG(native_status.st_mode) && native_status.st_size == 4,
                "no-follow status layout");
        require(fs::weakly_canonical(root / "missing" / "tail") == root / "missing" / "tail",
                "weak canonicalization retains a missing tail");
        char* allocated_path = realpath((root / "first").c_str(), nullptr);
        require(allocated_path && fs::path(allocated_path) == root / "first",
                "realpath allocation");
        std::free(allocated_path);
        const auto initial_cwd = fs::current_path();
        fs::current_path(root);
        require(fs::current_path() == root && fs::canonical("./first") == root / "first",
                "chdir/getcwd and relative canonicalization");
        { std::ifstream first("./first"); std::string contents; std::getline(first, contents);
          require(contents == "PS4", "relative C++ file reads"); }
        fs::create_directory("path with spaces");
        FILE* relative = std::fopen("path with spaces/relative", "w");
        require(relative != nullptr, "relative stdio creation");
        require(std::fputs("relative\n", relative) >= 0 && std::fclose(relative) == 0,
                "relative stdio write");
        require(fs::file_size("path with spaces/relative") == 9, "relative file status");
        bool relative_found = false;
        for (const auto& entry : fs::directory_iterator("path with spaces"))
            if (entry.path().filename() == "relative" && entry.is_regular_file())
                relative_found = true;
        require(relative_found, "relative directory iteration");
        require(nocturne_ps4_rename("path with spaces/relative", "path with spaces/moved") == 0,
                "relative rename");
        require(nocturne_ps4_remove("path with spaces/moved") == 0 &&
                    nocturne_ps4_remove("path with spaces") == 0, "relative cleanup");
        char too_small[2];
        require(getcwd(too_small, sizeof(too_small)) == nullptr && errno == ERANGE,
                "getcwd buffer bound");
        fs::current_path(initial_cwd);
        bool found = false;
        for (const auto& entry : fs::directory_iterator(root))
            if (entry.path().filename() == "first" && entry.is_regular_file()) found = true;
        require(found, "filesystem directory iteration/stat ABI");
        stage("filesystem passed; native rename/remove");
        require(nocturne_ps4_rename((root / "first").c_str(), (root / "second").c_str()) == 0,
                "native rename");
        std::string line;
        { std::ifstream second(root / "second"); std::getline(second, line); }
        require(line == "PS4", "renamed fixture content");
        require(nocturne_ps4_remove((root / "second").c_str()) == 0, "native remove");
        stage("native file operations passed; stdio log redirection");
        FILE* redirected = std::fopen((root / "stdio-first").c_str(), "w");
        require(redirected != nullptr, "open stdio fixture");
        redirected = nocturne_ps4_reopen_log((root / "stdio-second").c_str(), "w", redirected);
        require(redirected != nullptr, "owned FILE log replacement");
        std::fputs("redirected\n", redirected);
        require(std::fclose(redirected) == 0, "flush redirected log");
        { std::ifstream log(root / "stdio-second"); std::getline(log, line); }
        require(line == "redirected", "redirected log contents");
        require(nocturne_ps4_remove((root / "stdio-first").c_str()) == 0 &&
                nocturne_ps4_remove((root / "stdio-second").c_str()) == 0, "stdio fixture cleanup");
        stage("stdio log redirection passed; recursive mutex");
        nocturne::ps4::RecursiveMutex recursive;
        recursive.lock();
        recursive.lock();
        recursive.unlock();
        recursive.unlock();
        require(recursive.try_lock_for(std::chrono::milliseconds(0)), "recursive try-lock");
        recursive.unlock();
        stage("recursive mutex passed; C++ thread, mutex and condition variable");
        std::mutex mutex;
        std::condition_variable ready;
        bool done = false;
        std::thread worker([&] { std::lock_guard lock(mutex); done = true; ready.notify_one(); });
        bool woke;
        { std::unique_lock lock(mutex);
          woke = ready.wait_for(lock, std::chrono::seconds(5), [&] { return done; }); }
        worker.join();
        require(woke, "condition variable timed wait");
        stage("C++ synchronization passed; exceptions and strings");
        bool caught = false;
        try { throw std::runtime_error("probe"); }
        catch (const std::runtime_error&) { caught = true; }
        require(caught && std::string("PS4").starts_with("PS"), "C++ exception/string facilities");
        // This standalone probe does not link the game's remove interposer;
        // finish through the same native adapter instead of SDK unlinkat.
        require(nocturne_ps4_remove(root.c_str()) == 0, "native probe directory cleanup");
        stage("all runtime probes passed; rendering/audio/input remain separate checks");
        if (report) std::fclose(report);
        return 0;
    } catch (const std::exception& error) {
        stage(error.what());
        if (report) std::fclose(report);
        return 3;
    }
}
