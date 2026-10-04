// Exercise the actual console adapter against a temporary host filesystem.
// Firmware doubles provide absolute-path SCE calls, never native chdir/lstat.
#include "filesystem.h"
#include "path_resolver.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
namespace {
std::set<int> descriptors;
unsigned relative_native_calls;
unsigned native_stat_calls;
bool root_stat_regular;
struct Failure {
    std::string operation;
    std::string path;
    std::int32_t result;
};
std::vector<Failure> failures;
std::int32_t injected_result;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::int32_t encoded_error(int error) {
    return static_cast<std::int32_t>(0x80020000u | static_cast<unsigned>(error));
}

std::int32_t native_result(int result) {
    if (result >= 0) return result;
    const auto error = encoded_error(errno);
    // Firmware reports errors in the return value, not the caller's errno.
    errno = EDOM;
    return error;
}

bool fail(const char* operation, const char* path = nullptr) {
    const auto match = std::find_if(failures.begin(), failures.end(), [&](const Failure& failure) {
        return failure.operation == operation &&
               (failure.path.empty() || (path && failure.path == path));
    });
    if (match == failures.end()) return false;
    injected_result = match->result;
    failures.erase(match);
    errno = EDOM;
    return true;
}

void inject(const char* operation, std::int32_t result, const char* path = nullptr) {
    failures.push_back({operation, path ? path : "", result});
}

bool absolute(const char* path) {
    if (path && path[0] == '/') return true;
    ++relative_native_calls;
    return false;
}

void close_owned(int descriptor) {
    check(descriptors.erase(descriptor) == 1, "returned descriptor remains caller-owned");
    check(::close(descriptor) == 0, "caller can close the returned descriptor");
}

std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

std::string virtual_cwd() {
    char* path = nocturne_ps4_getcwd(nullptr, 0);
    check(path != nullptr, "allocated virtual cwd");
    std::string result(path);
    std::free(path);
    return result;
}
}

extern "C" std::int32_t nocturne_ps4_native_open(const char* path, int flags, mode_t mode) {
    if (!absolute(path)) return encoded_error(ENOSYS);
    if (fail("open", path)) return injected_result;
    const int descriptor = ::open(path, flags, mode);
    if (descriptor >= 0) descriptors.insert(descriptor);
    return native_result(descriptor);
}

extern "C" std::int32_t nocturne_ps4_native_close(int descriptor) {
    const int result = ::close(descriptor);
    if (result == 0) descriptors.erase(descriptor);
    return native_result(result);
}

extern "C" std::int32_t nocturne_ps4_native_fstat(int descriptor, struct stat* status) {
    if (fail("fstat")) return injected_result;
    return native_result(::fstat(descriptor, status));
}

extern "C" std::int32_t nocturne_ps4_native_stat(const char* path, struct stat* status) {
    ++native_stat_calls;
    if (!absolute(path)) return encoded_error(ENOSYS);
    if (fail("stat", path)) return injected_result;
    const auto result = native_result(::stat(path, status));
    if (result == 0 && root_stat_regular && std::strcmp(path, "/") == 0)
        status->st_mode = S_IFREG | 0600;
    return result;
}

extern "C" std::int32_t nocturne_ps4_native_mkdir(const char* path, mode_t mode) {
    if (!absolute(path)) return encoded_error(ENOSYS);
    if (fail("mkdir")) return injected_result;
    return native_result(::mkdir(path, mode));
}

extern "C" std::int32_t nocturne_ps4_native_unlink(const char* path) {
    if (!absolute(path)) return encoded_error(ENOSYS);
    if (fail("unlink")) return injected_result;
    return native_result(::unlink(path));
}

extern "C" std::int32_t nocturne_ps4_native_rmdir(const char* path) {
    if (!absolute(path)) return encoded_error(ENOSYS);
    return native_result(::rmdir(path));
}

extern "C" std::int32_t nocturne_ps4_native_rename(const char* source, const char* destination) {
    if (!absolute(source) || !absolute(destination)) return encoded_error(ENOSYS);
    if (fail("rename")) return injected_result;
    return native_result(::rename(source, destination));
}

extern "C" std::int32_t nocturne_ps4_native_ftruncate(int descriptor, off_t length) {
    if (fail("ftruncate")) return injected_result;
    return native_result(::ftruncate(descriptor, length));
}

int main() {
    fs::path temporary;
    const auto host_cwd = fs::current_path();
    try {
        std::string pattern = (fs::temp_directory_path() / "Nocturne PS4 filesystem.XXXXXX").string();
        std::vector<char> name(pattern.begin(), pattern.end());
        name.push_back('\0');
        check(mkdtemp(name.data()) != nullptr, "temporary directory creation");
        // /var and /tmp are symlinks on macOS; the console policy rejects them.
        temporary = fs::canonical(name.data());
        const auto profile = temporary / "user profile";
        fs::create_directory(profile);
        std::ofstream(temporary / "immutable asset") << "keep asset contents";

        {
            struct stat status{};
            auto prior_stats = native_stat_calls;
            inject("open", encoded_error(ENOENT), "/");
            check(nocturne_ps4_stat("/", &status) == 0 && S_ISDIR(status.st_mode) &&
                  native_stat_calls == prior_stats + 1,
                  "emulator root ENOENT permits a successful native stat");
            for (const int error : {EACCES, EPERM, ELOOP, EIO, ENOTDIR, EMFILE, ENFILE, EINVAL}) {
                prior_stats = native_stat_calls;
                inject("open", encoded_error(error), "/");
                check(nocturne_ps4_stat("/", &status) == -1 && errno == error &&
                      native_stat_calls == prior_stats,
                      "root permission, no-follow and I/O errors never fall back to stat");
            }
            for (const char* root : {"/app0", "/data", "/app0x", "/datax"}) {
                prior_stats = native_stat_calls;
                inject("open", encoded_error(ENOENT), root);
                check(nocturne_ps4_stat(root, &status) == -1 && errno == ENOENT &&
                      native_stat_calls == prior_stats,
                      "missing named mounts and similar paths are not fabricated");
            }
            for (const int error : {ENOENT, EACCES, EIO}) {
                inject("open", encoded_error(ENOENT), "/");
                inject("stat", encoded_error(error), "/");
                check(nocturne_ps4_stat("/", &status) == -1 && errno == error,
                      "a failed root stat is never replaced with fabricated metadata");
            }
            root_stat_regular = true;
            inject("open", encoded_error(ENOENT), "/");
            check(nocturne_ps4_stat("/", &status) == -1 && errno == ENOTDIR,
                  "root fallback requires actual directory metadata");
            root_stat_regular = false;
            check(failures.empty() && descriptors.empty(), "root fixtures consume errors and descriptors");
        }
        check(nocturne_ps4_initialize_cwd() == 0 && virtual_cwd() == "/",
              "initialization works without firmware chdir");
        check(nocturne_ps4_chdir(profile.c_str()) == 0 && virtual_cwd() == profile.string(),
              "virtual cwd accepts an absolute path with spaces");
        check(fs::current_path() == host_cwd, "virtual cwd does not alter the host process cwd");
        check(nocturne_ps4_mkdir("save files", 0700) == 0 &&
              nocturne_ps4_chdir("save files") == 0, "relative directory creation and entry");

        const char contents[] = "saved game state";
        int descriptor = nocturne_ps4_open("slot one.sav", O_CREAT | O_EXCL | O_RDWR, 0600);
        check(descriptor >= 0 && descriptors.size() == 1,
              "open releases inspection descriptors and returns one owned file");
        check(::write(descriptor, contents, sizeof(contents) - 1) == sizeof(contents) - 1,
              "write through returned native descriptor");
        close_owned(descriptor);
        check(read_file(profile / "save files/slot one.sav") == contents,
              "relative creation targets virtual cwd");
        descriptor = nocturne_ps4_open("./slot one.sav", O_RDONLY);
        char buffer[64]{};
        check(descriptor >= 0 && ::read(descriptor, buffer, sizeof(buffer)) == sizeof(contents) - 1 &&
              std::strcmp(buffer, contents) == 0, "relative reopen reads the saved bytes");
        close_owned(descriptor);

        struct stat status{};
        check(nocturne_ps4_stat("slot one.sav", &status) == 0 && S_ISREG(status.st_mode) &&
              status.st_size == sizeof(contents) - 1, "relative stat reports the saved file");
        check(nocturne_ps4_lstat(".", &status) == 0 && S_ISDIR(status.st_mode),
              "lstat adapter succeeds without a firmware lstat export");
        check(nocturne_ps4_access("slot one.sav", F_OK) == 0 &&
              nocturne_ps4_access("absent", F_OK) == -1 && errno == ENOENT,
              "relative existence checks");

        descriptor = nocturne_ps4_open(".", O_RDONLY | O_DIRECTORY);
        check(descriptor >= 0 && descriptors.size() == 1,
              "directory open transfers an owned descriptor");
        DIR* directory = ::fdopendir(descriptor);
        check(directory != nullptr, "directory descriptor is usable by libc directory iteration");
        bool found_save = false;
        while (const auto* entry = ::readdir(directory))
            found_save |= std::strcmp(entry->d_name, "slot one.sav") == 0;
        check(::closedir(directory) == 0, "directory iterator closes its owned descriptor");
        descriptors.erase(descriptor);
        check(found_save, "relative directory listing discovers the saved file");

        char* resolved = nocturne_ps4_realpath("../save files/./slot one.sav", nullptr);
        check(resolved && fs::path(resolved) == profile / "save files/slot one.sav",
              "realpath uses virtual cwd and validates dot components");
        std::free(resolved);
        const auto saved_cwd = virtual_cwd();
        char planned[nocturne::ps4::path_capacity];
        check(nocturne_ps4_resolve_directory("../save files/../new profiles/./nested/",
                  planned, sizeof(planned), true) == 0 &&
              fs::path(planned) == profile / "new profiles/nested" &&
              !fs::exists(profile / "new profiles") && virtual_cwd() == saved_cwd,
              "planning validates existing dot-dot and retains missing directory suffixes without writes");
        check(nocturne_ps4_resolve_directory("../new profiles/nested", planned, sizeof(planned)) == -1 &&
              errno == ENOENT, "existing-directory resolution does not permit missing suffixes");
        check(nocturne_ps4_resolve_directory("missing/../profile", planned, sizeof(planned), true) == -1 &&
              errno == ENOENT && !fs::exists(profile / "save files/missing"),
              "planning cannot discard missing ancestors");
        check(nocturne_ps4_resolve_directory("slot one.sav/../profile", planned, sizeof(planned), true) == -1 &&
              errno == ENOTDIR &&
              nocturne_ps4_resolve_directory("slot one.sav", planned, sizeof(planned), true) == -1 &&
              errno == ENOTDIR, "planned directories reject existing files and file ancestors");
        check(nocturne_ps4_chdir("slot one.sav") == -1 && errno == ENOTDIR &&
              virtual_cwd() == saved_cwd, "failed directory changes retain virtual cwd");
        check(nocturne_ps4_open("missing/../new.sav", O_CREAT | O_WRONLY, 0600) == -1 &&
              errno == ENOENT && !fs::exists(profile / "save files/new.sav"),
              "creation cannot hide missing ancestors with dot-dot");
        check(nocturne_ps4_open("slot one.sav/../new.sav", O_CREAT | O_WRONLY, 0600) == -1 &&
              errno == ENOTDIR, "creation cannot hide file ancestors with dot-dot");
        check(nocturne_ps4_stat("slot one.sav/", &status) == -1 && errno == ENOTDIR,
              "a file with a trailing separator is not a directory");
        check(nocturne_ps4_open("missing file/", O_CREAT | O_WRONLY, 0600) == -1 &&
              !fs::exists(profile / "save files/missing file"),
              "a trailing separator cannot create a regular file");
        check(nocturne_ps4_mkdir("new directory/", 0700) == 0 &&
              fs::is_directory(profile / "save files/new directory"),
              "mkdir permits a trailing separator on a new directory");
        check(nocturne_ps4_rmdir("new directory") == 0, "relative directory removal");

        fs::create_symlink(temporary / "immutable asset", profile / "save files/asset link");
        fs::create_directory_symlink(temporary, profile / "save files/parent link");
        const auto before_links = native_stat_calls;
        check(nocturne_ps4_open("asset link", O_WRONLY | O_TRUNC) == -1 && errno == ELOOP,
              "final symbolic links are rejected before truncation");
        check(nocturne_ps4_open("parent link/immutable asset", O_WRONLY | O_TRUNC) == -1 &&
              errno == ELOOP, "parent symbolic links are rejected before truncation");
        check(nocturne_ps4_open("parent link/new file", O_WRONLY | O_CREAT, 0600) == -1 &&
              errno == ELOOP && !fs::exists(temporary / "new file"),
              "creation cannot escape through a symbolic-link parent");
        check(nocturne_ps4_truncate("asset link", 0) == -1 && errno == ELOOP &&
              read_file(temporary / "immutable asset") == "keep asset contents",
              "all rejected write paths preserve the link target");
        check(nocturne_ps4_lstat("asset link", &status) == -1 && errno == ELOOP &&
              nocturne_ps4_chdir("parent link") == -1 && errno == ELOOP &&
              virtual_cwd() == saved_cwd, "console stat and cwd enforce the no-symlink policy");
        check(nocturne_ps4_resolve_directory("parent link/new profile", planned, sizeof(planned), true) == -1 &&
              errno == ELOOP && native_stat_calls == before_links,
              "planning and filesystem operations never follow a rejected symlink with stat");

        check(nocturne_ps4_rename("slot one.sav", "renamed slot.sav") == 0 &&
              !fs::exists(profile / "save files/slot one.sav") &&
              read_file(profile / "save files/renamed slot.sav") == contents,
              "relative rename preserves saved bytes");
        check(nocturne_ps4_truncate("renamed slot.sav", 5) == 0 &&
              read_file(profile / "save files/renamed slot.sav") == "saved",
              "relative truncation uses an owned descriptor");
        check(nocturne_ps4_rename("renamed slot.sav", "parent link/immutable asset") == -1 &&
              errno == ELOOP && read_file(temporary / "immutable asset") == "keep asset contents",
              "rename cannot overwrite a target through a symbolic-link parent");
        check(descriptors.empty(), "normal operations close all inspection descriptors");

        inject("open", encoded_error(EACCES), (profile / "save files/renamed slot.sav").c_str());
        check(nocturne_ps4_stat("renamed slot.sav", &status) == -1 && errno == EACCES,
              "native encoded open errors become POSIX errno");
        inject("fstat", encoded_error(EIO));
        check(nocturne_ps4_stat("renamed slot.sav", &status) == -1 && errno == EIO &&
              descriptors.empty(), "failed native inspection closes its descriptor and retains errno");
        inject("open", -1, (profile / "save files/renamed slot.sav").c_str());
        check(nocturne_ps4_open("renamed slot.sav", O_RDONLY) == -1 && errno == EIO,
              "unexpected negative native results use EIO");
        inject("mkdir", encoded_error(ENOSPC));
        check(nocturne_ps4_mkdir("out of space", 0700) == -1 && errno == ENOSPC &&
              !fs::exists(profile / "save files/out of space"), "mkdir propagates firmware errors");
        inject("rename", encoded_error(EACCES));
        check(nocturne_ps4_rename("renamed slot.sav", "denied.sav") == -1 && errno == EACCES &&
              read_file(profile / "save files/renamed slot.sav") == "saved",
              "failed rename retains the source and propagates errno");
        inject("ftruncate", encoded_error(ENOSPC));
        check(nocturne_ps4_truncate("renamed slot.sav", 1) == -1 && errno == ENOSPC &&
              descriptors.empty() && read_file(profile / "save files/renamed slot.sav") == "saved",
              "failed truncation closes its descriptor without changing file bytes");
        inject("unlink", encoded_error(EACCES));
        check(nocturne_ps4_unlink("renamed slot.sav") == -1 && errno == EACCES,
              "unlink propagates firmware errors");
        check(nocturne_ps4_unlink("renamed slot.sav") == 0 &&
              !fs::exists(profile / "save files/renamed slot.sav"), "relative file removal");

        check(descriptors.empty() && failures.empty() && relative_native_calls == 0,
              "adapter leaks no descriptors and sends only absolute firmware paths");
        check(fs::current_path() == host_cwd, "all console operations preserve host cwd");
        check(nocturne_ps4_initialize_cwd() == 0, "reset virtual cwd before fixture removal");
        fs::remove_all(temporary);
        std::puts("PS4 native filesystem adapter checks passed");
        return 0;
    } catch (const std::exception& error) {
        for (const int descriptor : descriptors) ::close(descriptor);
        if (!temporary.empty()) fs::remove_all(temporary);
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
