#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <sys/stat.h>

namespace nocturne::ps4 {
inline constexpr std::size_t path_capacity = PATH_MAX;
using InspectPath = int (*)(const char*, struct stat*);
enum class ResolveMode { Existing, MissingFinal, PlannedDirectory };

// Console packages and profiles do not support symbolic links. Walk each
// component before removing '.' or '..', so missing/file ancestors cannot be
// hidden by lexical normalization. InspectPath must use native no-follow stat.
inline int resolve_path(const char* path, const char* cwd, char* output,
                        std::size_t capacity, InspectPath inspect,
                        ResolveMode mode = ResolveMode::Existing,
                        struct stat* final_status = nullptr) {
    if (!path || !cwd || !output || !inspect) { errno = EINVAL; return -1; }
    if (!*path) { errno = ENOENT; return -1; }
    if (path[0] != '/' && cwd[0] != '/') { errno = EINVAL; return -1; }
    const auto input_length = std::strlen(path);
    const auto cwd_length = path[0] == '/' ? 0 : std::strlen(cwd);
    if (input_length >= path_capacity || cwd_length >= path_capacity ||
        input_length + cwd_length + (cwd_length ? 1 : 0) >= path_capacity) {
        errno = ENAMETOOLONG;
        return -1;
    }
    char pending[path_capacity];
    if (cwd_length) {
        if (cwd[0] != '/') { errno = EINVAL; return -1; }
        std::memcpy(pending, cwd, cwd_length);
        pending[cwd_length] = '/';
        std::memcpy(pending + cwd_length + 1, path, input_length + 1);
    } else {
        std::memcpy(pending, path, input_length + 1);
    }
    char resolved[path_capacity] = "/";
    std::size_t length = 1;
    struct stat status{};
    bool missing = false;
    if (inspect(resolved, &status) != 0) return -1;
    if (S_ISLNK(status.st_mode)) { errno = ELOOP; return -1; }
    const char* cursor = pending;
    while (*cursor) {
        while (*cursor == '/') ++cursor;
        if (!*cursor) break;
        if (!missing && !S_ISDIR(status.st_mode)) { errno = ENOTDIR; return -1; }
        const char* start = cursor;
        while (*cursor && *cursor != '/') ++cursor;
        const auto count = static_cast<std::size_t>(cursor - start);
        if (count == 1 && start[0] == '.') continue;
        if (count == 2 && start[0] == '.' && start[1] == '.') {
            // Planning may retain missing directory names, but may not erase
            // an unvalidated ancestor to make a different path appear valid.
            if (missing) { errno = ENOENT; return -1; }
            while (length > 1 && resolved[length - 1] != '/') --length;
            if (length > 1) --length;
            resolved[length] = '\0';
        } else {
            const auto separator = length > 1 ? 1 : 0;
            if (length + separator + count >= sizeof(resolved)) {
                errno = ENAMETOOLONG;
                return -1;
            }
            if (separator) resolved[length++] = '/';
            std::memcpy(resolved + length, start, count);
            length += count;
            resolved[length] = '\0';
        }
        if (!missing && inspect(resolved, &status) != 0) {
            const char* remaining = cursor;
            while (*remaining == '/') ++remaining;
            if (errno != ENOENT ||
                (mode != ResolveMode::PlannedDirectory &&
                 (mode != ResolveMode::MissingFinal || *remaining))) return -1;
            status = {};
            missing = true;
        }
        if (S_ISLNK(status.st_mode)) { errno = ELOOP; return -1; }
    }
    const auto pending_length = std::strlen(pending);
    if (!missing && (pending[pending_length - 1] == '/' ||
                     mode == ResolveMode::PlannedDirectory) && !S_ISDIR(status.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    if (length >= capacity) { errno = ENAMETOOLONG; return -1; }
    std::memcpy(output, resolved, length + 1);
    if (final_status) *final_status = status;
    return 0;
}

inline char* copy_cwd(const char* cwd, char* output, std::size_t capacity) {
    const auto required = std::strlen(cwd) + 1;
    if (output && !capacity) { errno = EINVAL; return nullptr; }
    if (capacity && capacity < required) { errno = ERANGE; return nullptr; }
    if (!output) {
        output = static_cast<char*>(std::malloc(capacity ? capacity : required));
        if (!output) { errno = ENOMEM; return nullptr; }
    }
    std::memcpy(output, cwd, required);
    return output;
}
}
