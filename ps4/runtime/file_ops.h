#pragma once

#include <cstdio>

int nocturne_ps4_remove(const char* path);
int nocturne_ps4_rename(const char* old_path, const char* new_path);

// The console rejects descriptor duplication. Route the game's standard-stream
// wrappers to owned FILEs instead; native stdout/stderr descriptors stay intact.
// Returned routed streams are borrowed and remain owned by this log router.
FILE* nocturne_ps4_log_stream(FILE* stream);
int nocturne_ps4_open_startup_log(const char* path);
void nocturne_ps4_startup_log();

// Standard streams replace a router destination, retaining any FILE still used
// by the other destination. A nonstandard stream is closed only after its new
// FILE opens successfully. On open/flush failure the old stream stays usable.
FILE* nocturne_ps4_reopen_log(const char* path, const char* mode, FILE* stream);
