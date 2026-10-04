#ifndef SDL_config_h_
#define SDL_config_h_
/* PS4 configuration for the SDL release pinned by deps/manifest.json. */
#include "SDL_platform.h"
#define HAVE_LIBC 1
#define SDL_BYTEORDER 1234
#define HAVE_STDARG_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STDIO_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_CTYPE_H 1
#define HAVE_MATH_H 1
#define HAVE_FLOAT_H 1
#define HAVE_LIMITS_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_SETJMP_H 1
#define HAVE_MALLOC 1
#define HAVE_CALLOC 1
#define HAVE_REALLOC 1
#define HAVE_FREE 1
#define HAVE_GETENV 1
#define HAVE_SETENV 1
#define HAVE_PUTENV 1
#define HAVE_UNSETENV 1
#define HAVE_QSORT 1
#define HAVE_ABS 1
#define HAVE_BCOPY 1
#define HAVE_MEMSET 1
#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMCMP 1
#define HAVE_STRLEN 1
#define HAVE_STRDUP 1
#define HAVE_STRCHR 1
#define HAVE_STRRCHR 1
#define HAVE_STRSTR 1
#define HAVE_STRTOL 1
#define HAVE_STRTOUL 1
#define HAVE_STRTOLL 1
#define HAVE_STRTOULL 1
#define HAVE_STRTOD 1
#define HAVE_ATOI 1
#define HAVE_ATOF 1
#define HAVE_STRCMP 1
#define HAVE_STRNCMP 1
#define HAVE_STRCASECMP 1
#define HAVE_STRNCASECMP 1
#define HAVE_VSSCANF 1
#define HAVE_VSNPRINTF 1
#define HAVE_FOPEN64 1
#define HAVE_FSEEKO64 1
#define HAVE_FTELLO64 1
#define HAVE_SIGACTION 1
/* OpenOrbis's sigaction layout exposes sa_handler but not sa_sigaction. */
#define HAVE_SETJMP 1
#define HAVE_GCC_ATOMICS 1
#define HAVE_GCC_SYNC_LOCK_TEST_AND_SET 1
#define SDL_AUDIO_DRIVER_PS4 1
#define SDL_VIDEO_DRIVER_PS4 1
#define SDL_VIDEO_RENDER_SW 1
#define SDL_JOYSTICK_PS4 1
/* Thread handles use pthread_t storage; operations use the Sce ABI directly. */
#define SDL_THREAD_PTHREAD 1
#define SDL_HAPTIC_DISABLED 1
#define SDL_HIDAPI_DISABLED 1
#define SDL_SENSOR_DUMMY 1
#define SDL_LOCALE_DUMMY 1
#define SDL_MISC_DUMMY 1
#define SDL_POWER_DISABLED 1
#endif
