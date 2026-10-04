/* Compile the actual PS4 system.c; only firmware reads/sleeps are controlled. */
#include <SDL.h>
#include <orbis/libkernel.h>
#include <stdio.h>

void SDL_TicksInit(void);
void SDL_TicksQuit(void);
static uint64_t clock_us = 9000000;
static uint64_t slept_us;
static unsigned sleeps;
uint64_t sceKernelGetProcessTime(void) { return clock_us; }
uint64_t sceKernelGetProcessTimeCounter(void) { return 1234567890123ULL; }
uint64_t sceKernelGetProcessTimeCounterFrequency(void) { return 1000000000; }
int32_t sceKernelUsleep(uint32_t us) { slept_us += us; ++sleeps; return 0; }
uint32_t sceKernelLoadStartModule(const char *p, size_t n, const void *a, uint32_t f, void *o, void *r)
{ (void)p; (void)n; (void)a; (void)f; (void)o; (void)r; return (uint32_t)-1; }
int32_t sceKernelStopUnloadModule(int32_t h, size_t n, const void *a, uint32_t f, void *o, int32_t *r)
{ (void)h; (void)n; (void)a; (void)f; (void)o; (void)r; return -1; }
int32_t sceKernelDlsym(int32_t h, const char *n, void **p)
{ (void)h; (void)n; (void)p; return -1; }

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main(void)
{
    CHECK(SDL_GetTicks64() == 0); /* Query before any subsystem is initialized. */
    clock_us += 750000;
    CHECK(SDL_GetTicks64() == 750);
    /* SDL calls TicksInit for each initialized subsystem, even after queries. */
    for (int i = 0; i < 3; ++i) {
        SDL_TicksInit();
        CHECK(SDL_GetTicks64() == 750 + (uint64_t)i * 125);
        clock_us += 125000;
    }
    clock_us += (UINT64_C(1) << 32) * 1000;
    CHECK(SDL_GetTicks64() == (UINT64_C(1) << 32) + 1125);
    SDL_TicksQuit();
    clock_us += 123000;
    CHECK(SDL_GetTicks64() == 0);
    clock_us += 999;
    CHECK(SDL_GetTicks64() == 0);
    ++clock_us;
    CHECK(SDL_GetTicks64() == 1);
    /* An epoch of zero is valid and must not be mistaken for uninitialized. */
    SDL_TicksQuit();
    clock_us = 0;
    SDL_TicksInit();
    clock_us = 100000;
    SDL_TicksInit();
    CHECK(SDL_GetTicks64() == 100);
    CHECK(SDL_GetPerformanceCounter() == 1234567890123ULL);
    CHECK(SDL_GetPerformanceFrequency() == 1000000000);
    SDL_Delay(2501);
    CHECK(slept_us == 2501000 && sleeps == 3);
    puts("PS4 SDL timer lifecycle and units passed");
    return 0;
}
