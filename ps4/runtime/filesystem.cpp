#include "filesystem.h"
#include "path_resolver.h"

#include <cstdarg>
#include <fcntl.h>
#include <mutex>
#include <unistd.h>

namespace {
std::mutex cwd_mutex;
char current_directory[nocturne::ps4::path_capacity] = "/";

int posix_result(std::int32_t result) {
    if (result >= 0) return result;
    const auto code = static_cast<std::uint32_t>(result);
    errno = (code & 0xffff0000u) == 0x80020000u ? static_cast<int>(code & 0xffffu) : EIO;
    return -1;
}

int close_preserving_result(int descriptor, int result, int saved_errno) {
    const int closed = posix_result(nocturne_ps4_native_close(descriptor));
    if (result < 0) { errno = saved_errno; return -1; }
    return closed < 0 ? -1 : result;
}

int inspect_native(const char* path, struct stat* result) {
    // Firmware's lstat is denied. O_NOFOLLOW rejects a final link, and the
    // resolver calls this for every ancestor too. NONBLOCK avoids waiting on
    // device/FIFO paths during inspection; actual opens retain caller flags.
    const int descriptor = posix_result(nocturne_ps4_native_open(
        path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0));
    if (descriptor < 0) {
        // shadPS4 exposes '/' through stat but reports ENOENT when opening it.
        // Only that exact root can safely use following stat. In particular,
        // preserve ELOOP and permission failures for all other components.
        if (errno != ENOENT || std::strcmp(path, "/") != 0) return -1;
        if (posix_result(nocturne_ps4_native_stat(path, result)) != 0) return -1;
        if (!S_ISDIR(result->st_mode)) { errno = ENOTDIR; return -1; }
        return 0;
    }
    const int status = posix_result(nocturne_ps4_native_fstat(descriptor, result));
    const int saved_errno = errno;
    return close_preserving_result(descriptor, status, saved_errno);
}

int resolve_locked(const char* path, char* output,
                   nocturne::ps4::ResolveMode mode = nocturne::ps4::ResolveMode::Existing,
                   struct stat* status = nullptr) {
    return nocturne::ps4::resolve_path(path, current_directory, output,
        nocturne::ps4::path_capacity, inspect_native, mode, status);
}
}

int nocturne_ps4_open(const char* path, int flags, mode_t mode) {
    std::lock_guard lock(cwd_mutex);
    char resolved[nocturne::ps4::path_capacity];
    if (resolve_locked(path, resolved, (flags & O_CREAT) ?
            nocturne::ps4::ResolveMode::MissingFinal : nocturne::ps4::ResolveMode::Existing) != 0)
        return -1;
    // Canonicalization removes a trailing slash; retain its directory demand.
    const auto length = std::strlen(path);
    if (length && path[length - 1] == '/') flags |= O_DIRECTORY;
    return posix_result(nocturne_ps4_native_open(resolved, flags | O_NOFOLLOW, mode));
}

int nocturne_ps4_stat(const char* path, struct stat* result) {
    if (!result) { errno = EFAULT; return -1; }
    std::lock_guard lock(cwd_mutex);
    char resolved[nocturne::ps4::path_capacity];
    struct stat status{};
    if (resolve_locked(path, resolved, nocturne::ps4::ResolveMode::Existing, &status) != 0) return -1;
    *result = status;
    return 0;
}

int nocturne_ps4_lstat(const char* path, struct stat* result) {
    // This title rejects symbolic links instead of exposing followed targets.
    return nocturne_ps4_stat(path, result);
}

int nocturne_ps4_mkdir(const char* path, mode_t mode) {
    std::lock_guard lock(cwd_mutex);
    char resolved[nocturne::ps4::path_capacity];
    if (resolve_locked(path, resolved, nocturne::ps4::ResolveMode::MissingFinal) != 0) return -1;
    return posix_result(nocturne_ps4_native_mkdir(resolved, mode));
}

int nocturne_ps4_unlink(const char* path) {
    std::lock_guard lock(cwd_mutex);
    char resolved[nocturne::ps4::path_capacity];
    if (resolve_locked(path, resolved) != 0) return -1;
    return posix_result(nocturne_ps4_native_unlink(resolved));
}

int nocturne_ps4_rmdir(const char* path) {
    std::lock_guard lock(cwd_mutex);
    char resolved[nocturne::ps4::path_capacity];
    if (resolve_locked(path, resolved) != 0) return -1;
    return posix_result(nocturne_ps4_native_rmdir(resolved));
}

int nocturne_ps4_rename(const char* source, const char* destination) {
    std::lock_guard lock(cwd_mutex);
    char from[nocturne::ps4::path_capacity], to[nocturne::ps4::path_capacity];
    struct stat source_status{};
    if (resolve_locked(source, from, nocturne::ps4::ResolveMode::Existing, &source_status) != 0 ||
        resolve_locked(destination, to, nocturne::ps4::ResolveMode::MissingFinal) != 0) return -1;
    const auto length = std::strlen(destination);
    if (length && destination[length - 1] == '/' && !S_ISDIR(source_status.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    return posix_result(nocturne_ps4_native_rename(from, to));
}

int nocturne_ps4_access(const char* path, int mode) {
    if (mode & ~(R_OK | W_OK | X_OK)) { errno = EINVAL; return -1; }
    // There is no SCE access(mode) entry point. Check requested file I/O using
    // nonmutating opens; execution permission cannot be established this way.
    if (mode & X_OK) { errno = ENOTSUP; return -1; }
    std::lock_guard lock(cwd_mutex);
    char resolved[nocturne::ps4::path_capacity];
    if (resolve_locked(path, resolved) != 0) return -1;
    if (mode == F_OK) return 0;
    const int flags = (mode & W_OK) ? ((mode & R_OK) ? O_RDWR : O_WRONLY) : O_RDONLY;
    const int descriptor = posix_result(nocturne_ps4_native_open(
        resolved, flags | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0));
    if (descriptor < 0) return -1;
    return posix_result(nocturne_ps4_native_close(descriptor));
}

int nocturne_ps4_truncate(const char* path, off_t length) {
    if (length < 0) { errno = EINVAL; return -1; }
    const int descriptor = nocturne_ps4_open(path, O_WRONLY | O_CLOEXEC);
    if (descriptor < 0) return -1;
    const int result = posix_result(nocturne_ps4_native_ftruncate(descriptor, length));
    const int saved_errno = errno;
    return close_preserving_result(descriptor, result, saved_errno);
}

int nocturne_ps4_chdir(const char* path) {
    std::lock_guard lock(cwd_mutex);
    char resolved[nocturne::ps4::path_capacity];
    struct stat status{};
    if (resolve_locked(path, resolved, nocturne::ps4::ResolveMode::Existing, &status) != 0) return -1;
    if (!S_ISDIR(status.st_mode)) { errno = ENOTDIR; return -1; }
    std::strcpy(current_directory, resolved);
    return 0;
}

char* nocturne_ps4_getcwd(char* output, std::size_t capacity) {
    std::lock_guard lock(cwd_mutex);
    return nocturne::ps4::copy_cwd(current_directory, output, capacity);
}

int nocturne_ps4_resolve_directory(const char* path, char* output, std::size_t capacity,
                                  bool allow_missing_tail) {
    std::lock_guard lock(cwd_mutex);
    struct stat status{};
    const auto mode = allow_missing_tail ? nocturne::ps4::ResolveMode::PlannedDirectory :
                                          nocturne::ps4::ResolveMode::Existing;
    if (nocturne::ps4::resolve_path(path, current_directory, output, capacity,
                                  inspect_native, mode, &status) != 0) return -1;
    if (!allow_missing_tail && !S_ISDIR(status.st_mode)) { errno = ENOTDIR; return -1; }
    return 0;
}

char* nocturne_ps4_realpath(const char* path, char* output) {
    char resolved[nocturne::ps4::path_capacity];
    {
        std::lock_guard lock(cwd_mutex);
        if (resolve_locked(path, resolved) != 0) return nullptr;
    }
    if (!output) {
        output = static_cast<char*>(std::malloc(std::strlen(resolved) + 1));
        if (!output) { errno = ENOMEM; return nullptr; }
    }
    std::strcpy(output, resolved);
    return output;
}

int nocturne_ps4_initialize_cwd() {
    return nocturne_ps4_chdir("/");
}

#if defined(NOCTURNE_PS4)
extern "C" __attribute__((used, visibility("default"))) int open(const char* path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list arguments;
        va_start(arguments, flags);
        mode = static_cast<mode_t>(va_arg(arguments, int));
        va_end(arguments);
    }
    return nocturne_ps4_open(path, flags, mode);
}
#undef open64
#if defined(__APPLE__)
extern "C" __attribute__((used, visibility("default"))) int open64(const char* path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }
    return open(path, flags, mode);
}
#else
extern "C" __attribute__((used, visibility("default"))) int open64(const char*, int, ...) __attribute__((alias("open")));
#endif
extern "C" __attribute__((used, visibility("default"))) int stat(const char* path, struct stat* result) { return nocturne_ps4_stat(path, result); }
extern "C" __attribute__((used, visibility("default"))) int lstat(const char* path, struct stat* result) { return nocturne_ps4_lstat(path, result); }
extern "C" __attribute__((used, visibility("default"))) int mkdir(const char* path, mode_t mode) { return nocturne_ps4_mkdir(path, mode); }
extern "C" __attribute__((used, visibility("default"))) int unlink(const char* path) { return nocturne_ps4_unlink(path); }
extern "C" __attribute__((used, visibility("default"))) int rmdir(const char* path) { return nocturne_ps4_rmdir(path); }
extern "C" __attribute__((used, visibility("default"))) int access(const char* path, int mode) { return nocturne_ps4_access(path, mode); }
extern "C" __attribute__((used, visibility("default"))) int truncate(const char* path, off_t length) { return nocturne_ps4_truncate(path, length); }
extern "C" __attribute__((used, visibility("default"))) int chdir(const char* path) { return nocturne_ps4_chdir(path); }
extern "C" __attribute__((used, visibility("default"))) char* getcwd(char* output, std::size_t capacity) { return nocturne_ps4_getcwd(output, capacity); }
extern "C" __attribute__((used, visibility("default"))) char* realpath(const char* path, char* output) { return nocturne_ps4_realpath(path, output); }
#endif
