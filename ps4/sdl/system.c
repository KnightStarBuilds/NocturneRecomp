/* SDL 2.30.0 PS4 timer, filesystem and module-loading operations. */
#include "SDL_internal.h"
#include "SDL_timer.h"
#include "SDL_filesystem.h"
#include "SDL_loadso.h"
#include <orbis/libkernel.h>
#include <sys/stat.h>
#include <errno.h>

static Uint64 start_time;
static SDL_bool ticks_started;
void SDL_TicksInit(void)
{
    /* SDL_InitSubSystem calls this again when audio/controllers start. Keep
     * one epoch until SDL quits, including queries made before SDL_Init. */
    if (ticks_started) return;
    start_time = sceKernelGetProcessTime();
    ticks_started = SDL_TRUE;
}
void SDL_TicksQuit(void) { ticks_started = SDL_FALSE; }
Uint64 SDL_GetTicks64(void)
{
    if (!ticks_started) SDL_TicksInit();
    return (sceKernelGetProcessTime() - start_time) / 1000;
}
Uint64 SDL_GetPerformanceCounter(void) { return sceKernelGetProcessTimeCounter(); }
Uint64 SDL_GetPerformanceFrequency(void) { return sceKernelGetProcessTimeCounterFrequency(); }
void SDL_Delay(Uint32 ms)
{
    /* Chunk the conversion so the public millisecond API cannot overflow the
     * firmware's 32-bit microsecond duration. */
    while (ms > 1000) { sceKernelUsleep(1000000); ms -= 1000; }
    sceKernelUsleep(ms * 1000);
}
char *SDL_GetBasePath(void) { return SDL_strdup("/app0/"); }
char *SDL_GetPrefPath(const char *org, const char *app)
{
    (void)org; (void)app;
    if (mkdir("/data/NocturneRecompKnightStar", 0700) != 0 && errno != EEXIST) {
        SDL_SetError("Cannot create /data/NocturneRecompKnightStar: %s", strerror(errno));
        return NULL;
    }
    return SDL_strdup("/data/NocturneRecompKnightStar/");
}
void *SDL_LoadObject(const char *path)
{
    int *handle = SDL_malloc(sizeof(*handle));
    if (!handle) { SDL_OutOfMemory(); return NULL; }
    *handle = (int)sceKernelLoadStartModule(path, 0, NULL, 0, NULL, NULL);
    if (*handle < 0) {
        SDL_SetError("Cannot load PS4 module %s: 0x%08x", path, (unsigned)*handle);
        SDL_free(handle);
        return NULL;
    }
    return handle;
}
void *SDL_LoadFunction(void *object, const char *name)
{
    void *address = NULL;
    int result;
    if (!object) { SDL_SetError("Invalid PS4 module handle"); return NULL; }
    result = sceKernelDlsym(*(int *)object, name, &address);
    if (result < 0) { SDL_SetError("PS4 symbol %s: 0x%08x", name, (unsigned)result); return NULL; }
    return address;
}
void SDL_UnloadObject(void *object)
{
    if (object) {
        sceKernelStopUnloadModule(*(int *)object, 0, NULL, 0, NULL, NULL);
        SDL_free(object);
    }
}
