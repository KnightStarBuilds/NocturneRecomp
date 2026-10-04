#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/stat.h>
#include <sys/types.h>

// Firmware refuses chdir/lstat. Keep a process-wide virtual working directory
// and validate components through native no-follow opens before filesystem use.
int nocturne_ps4_initialize_cwd();
int nocturne_ps4_open(const char* path, int flags, mode_t mode = 0);
int nocturne_ps4_stat(const char* path, struct stat* result);
int nocturne_ps4_lstat(const char* path, struct stat* result);
int nocturne_ps4_mkdir(const char* path, mode_t mode);
int nocturne_ps4_unlink(const char* path);
int nocturne_ps4_rmdir(const char* path);
int nocturne_ps4_rename(const char* source, const char* destination);
int nocturne_ps4_access(const char* path, int mode);
int nocturne_ps4_truncate(const char* path, off_t length);
int nocturne_ps4_chdir(const char* path);
char* nocturne_ps4_getcwd(char* output, std::size_t capacity);
char* nocturne_ps4_realpath(const char* path, char* output);
// Read-only startup validation; a planned profile may retain a missing suffix,
// but cannot traverse '..' through missing, non-directory or linked ancestors.
int nocturne_ps4_resolve_directory(const char* path, char* output, std::size_t capacity,
                                  bool allow_missing_tail = false);

// Separately named firmware entry points let host tests exercise these same
// adapters without interposing on the host process's C library.
#if defined(NOCTURNE_PS4)
#define NOCTURNE_PS4_NATIVE(symbol) __asm__(symbol)
#else
#define NOCTURNE_PS4_NATIVE(symbol)
#endif
extern "C" {
std::int32_t nocturne_ps4_native_open(const char*, int, mode_t) NOCTURNE_PS4_NATIVE("sceKernelOpen");
std::int32_t nocturne_ps4_native_close(int) NOCTURNE_PS4_NATIVE("sceKernelClose");
std::int32_t nocturne_ps4_native_fstat(int, struct stat*) NOCTURNE_PS4_NATIVE("sceKernelFstat");
std::int32_t nocturne_ps4_native_stat(const char*, struct stat*) NOCTURNE_PS4_NATIVE("sceKernelStat");
std::int32_t nocturne_ps4_native_mkdir(const char*, mode_t) NOCTURNE_PS4_NATIVE("sceKernelMkdir");
std::int32_t nocturne_ps4_native_unlink(const char*) NOCTURNE_PS4_NATIVE("sceKernelUnlink");
std::int32_t nocturne_ps4_native_rmdir(const char*) NOCTURNE_PS4_NATIVE("sceKernelRmdir");
std::int32_t nocturne_ps4_native_rename(const char*, const char*) NOCTURNE_PS4_NATIVE("sceKernelRename");
std::int32_t nocturne_ps4_native_ftruncate(int, off_t) NOCTURNE_PS4_NATIVE("sceKernelFtruncate");
}
#undef NOCTURNE_PS4_NATIVE
