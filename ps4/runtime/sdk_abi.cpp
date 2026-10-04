// libkernel writes this native layout; the SDK's bundled libc++ reads it too.
// Keep these checks in both the game and standalone console SDK probe.
#include <cstddef>
#include <sys/stat.h>

static_assert(sizeof(mode_t) == 2, "Use the PS4 SDK header overlay: mode_t is 16 bits");
static_assert(sizeof(struct stat) == 120);
static_assert(offsetof(struct stat, st_mode) == 8);
static_assert(offsetof(struct stat, st_nlink) == 10);
static_assert(offsetof(struct stat, st_uid) == 12);
static_assert(offsetof(struct stat, st_gid) == 16);
static_assert(offsetof(struct stat, st_rdev) == 20);
static_assert(offsetof(struct stat, st_atim) == 24);
static_assert(offsetof(struct stat, st_mtim) == 40);
static_assert(offsetof(struct stat, st_ctim) == 56);
static_assert(offsetof(struct stat, st_size) == 72);
static_assert(offsetof(struct stat, st_blocks) == 80);
static_assert(offsetof(struct stat, st_blksize) == 88);
static_assert(offsetof(struct stat, st_birthtim) == 104);
