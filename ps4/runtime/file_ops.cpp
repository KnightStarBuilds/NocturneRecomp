#include "file_ops.h"
#include "filesystem.h"
#include "ps4_runtime.h"
#include <cerrno>

namespace {
FILE* output_log;
FILE* error_log;

void release_unused(FILE* stream) {
    if (stream && stream != stdout && stream != stderr &&
        stream != output_log && stream != error_log)
        std::fclose(stream);
}
}

int nocturne_ps4_remove(const char* path) {
    if (nocturne_ps4_unlink(path) == 0) return 0;
    if (errno != EISDIR && errno != EPERM) return -1;
    return nocturne_ps4_rmdir(path);
}

FILE* nocturne_ps4_log_stream(FILE* stream) {
    if (stream == stdout && output_log) return output_log;
    if (stream == stderr && error_log) return error_log;
    return stream;
}

int nocturne_ps4_open_startup_log(const char* path) {
    FILE* log = std::fopen(path, "w");
    if (!log) return -1;
    std::setvbuf(log, nullptr, _IONBF, 0);
    FILE* previous_output = output_log;
    FILE* previous_error = error_log;
    output_log = error_log = log;
    release_unused(previous_output);
    if (previous_error != previous_output) release_unused(previous_error);
    return 0;
}

void nocturne_ps4_startup_log() {
    // This precedes std::filesystem so failures in its SDK implementation or
    // asset discovery remain visible on the first hardware launch.
    nocturne_ps4_mkdir(nocturne::ps4::user_data, 0777);
    nocturne_ps4_open_startup_log("/data/NocturneRecompKnightStar/startup.log");
    std::fputs("Nocturne PS4: beginning runtime preparation\n",
               nocturne_ps4_log_stream(stderr));
    if (nocturne_ps4_initialize_cwd() != 0)
        std::fprintf(nocturne_ps4_log_stream(stderr),
                     "PS4: initial working directory failed (errno=%d)\n", errno);
}

FILE* nocturne_ps4_reopen_log(const char* path, const char* mode, FILE* stream) {
    if (!path || !mode || !stream) { errno = EINVAL; return nullptr; }
    // The recovered game reopens stderr. If both destinations still share the
    // startup FILE, a routed pointer selects stderr; the stdout sentinel can
    // always select stdout explicitly.
    const bool replace_error = stream == stderr ||
        (stream != stdout && stream == error_log);
    const bool replace_output = stream == stdout ||
        (!replace_error && stream == output_log);
    FILE* previous = nocturne_ps4_log_stream(stream);
    if (std::fflush(previous) != 0) return nullptr;
    FILE* replacement = std::fopen(path, mode);
    if (!replacement) return nullptr;
    std::setvbuf(replacement, nullptr, _IONBF, 0);
    if (replace_error) error_log = replacement;
    if (replace_output) output_log = replacement;
    release_unused(previous);
    return replacement;
}
