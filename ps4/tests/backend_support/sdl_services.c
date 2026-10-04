/* Keep backend fixtures independent of host SDL: its timer and device symbols
 * must never substitute for the production PS4 implementations under test. */
#include "SDL_internal.h"
#include "SDL_stdinc.h"
#include "SDL_error.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

void *SDL_malloc(size_t n) { return malloc(n); }
void SDL_free(void *p) { free(p); }
void *SDL_memset(void *p, int value, size_t n) { return memset(p, value, n); }
char *SDL_strdup(const char *s) { return strdup(s); }
int SDL_SetError(const char *format, ...) { (void)format; return -1; }
int SDL_Error(SDL_errorcode code) { (void)code; return -1; }
float SDL_sqrtf(float value) { return sqrtf(value); }
