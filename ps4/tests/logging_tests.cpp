#include "file_ops.h"
#include "filesystem.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// Host doubles for directory setup/removal; stdio ownership and routing below
// execute the production implementation with real FILE handles and files.
int nocturne_ps4_initialize_cwd() { return 0; }
int nocturne_ps4_mkdir(const char* path, mode_t mode) { return mkdir(path, mode); }
int nocturne_ps4_unlink(const char* path) { return unlink(path); }
int nocturne_ps4_rmdir(const char* path) { return rmdir(path); }

namespace fs = std::filesystem;
static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "PS4 logging test: %s (errno=%d)\n", message, errno); std::exit(1); }
}
static std::string read(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
static void write(FILE* stream, const char* value) {
    require(stream && std::fputs(value, stream) >= 0 && std::fflush(stream) == 0, "write log");
}

int main() {
    const auto base = fs::temp_directory_path() / "nocturne ps4 logs-XXXXXX";
    std::string pattern = base.string();
    const char* created = mkdtemp(pattern.data());
    require(created != nullptr, "create temporary profile");
    const fs::path root(created);
    const auto startup = root / "startup.log";
    require(nocturne_ps4_log_stream(stdout) == stdout && nocturne_ps4_log_stream(stderr) == stderr,
            "standard streams remain usable before initialization");
    require(nocturne_ps4_open_startup_log(startup.c_str()) == 0, "open startup log");
    FILE* initial = nocturne_ps4_log_stream(stderr);
    require(initial != stderr && nocturne_ps4_log_stream(stdout) == initial, "route startup streams");
    write(nocturne_ps4_log_stream(stdout), "early output\n");
    write(nocturne_ps4_log_stream(stderr), "early error\n");
    require(read(startup).find("early output\nearly error\n") != std::string::npos,
            "startup output and errors saved");

    const auto errors = root / "stderr.txt";
    FILE* redirected = nocturne_ps4_reopen_log(errors.c_str(), "w", stderr);
    require(redirected && redirected != initial && nocturne_ps4_log_stream(stderr) == redirected,
            "stderr follows its replacement FILE");
    require(nocturne_ps4_log_stream(stdout) == initial, "stdout retains shared startup FILE");
    write(nocturne_ps4_log_stream(stderr), "game error\n");
    write(nocturne_ps4_log_stream(stdout), "later output\n");
    require(read(errors) == "game error\n" && read(startup).find("later output\n") != std::string::npos,
            "independent destinations survive redirection");
    errno = 0;
    require(!nocturne_ps4_reopen_log((root / "missing" / "error.txt").c_str(), "w", stderr)
                && errno == ENOENT, "failed reopen reports error");
    require(nocturne_ps4_log_stream(stderr) == redirected, "failed reopen preserves stream");
    write(nocturne_ps4_log_stream(stderr), "after failure\n");
    require(read(errors) == "game error\nafter failure\n", "previous destination remains writable");

    FILE* owned = std::fopen((root / "owned-first").c_str(), "w");
    require(owned != nullptr, "open ordinary stream");
    const int old_descriptor = fileno(owned);
    require(nocturne_ps4_log_stream(owned) == owned, "ordinary stream remains unchanged");
    FILE* replacement = nocturne_ps4_reopen_log((root / "owned-second").c_str(), "w", owned);
    require(replacement != nullptr, "replace ordinary stream");
    errno = 0;
    require(fcntl(old_descriptor, F_GETFD) == -1 && errno == EBADF, "release replaced descriptor");
    write(replacement, "ordinary replacement\n");
    require(std::fclose(replacement) == 0 && read(root / "owned-second") == "ordinary replacement\n",
            "caller owns replacement FILE");
    require(nocturne_ps4_log_stream(stderr) == redirected && nocturne_ps4_log_stream(stdout) == initial,
            "ordinary replacement does not reroute standard streams");
    fs::remove_all(root);
}
