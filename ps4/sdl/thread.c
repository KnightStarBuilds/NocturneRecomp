/* SDL 2.30.0 threading through libkernel's public Sce pthread ABI.
 * Generic SDL semaphore/TLS storage uses these mutex and condition operations.
 */
#include "SDL_internal.h"
#include "thread/SDL_systhread.h"
#include <orbis/libkernel.h>
#include <errno.h>

struct SDL_mutex { OrbisPthreadMutex native; };
struct SDL_cond { OrbisPthreadCond native; };
/* The shipped musl mutexattr declaration is four bytes, while the firmware
 * stores a pointer. Pad local attributes without changing libc++'s ABI. */
typedef union { Uint64 padding[8]; OrbisPthreadAttr thread; OrbisPthreadMutexattr mutex; } PS4_Attr;

static int ps4_thread_result(const char *name, int result)
{
    return result ? SDL_SetError("%s failed: 0x%08x", name, (unsigned)result) : 0;
}
static void *ps4_run_thread(void *arg) { SDL_RunThread(arg); return NULL; }
int SDL_SYS_CreateThread(SDL_Thread *thread)
{
    PS4_Attr attr;
    int result;
    SDL_zero(attr);
    result = scePthreadAttrInit(&attr.thread);
    if (result) return ps4_thread_result("scePthreadAttrInit", result);
    /* FFmpeg/audio callbacks require more than the firmware's small default. */
    result = scePthreadAttrSetstacksize(&attr.thread, SDL_max(thread->stacksize, (size_t)1024 * 1024));
    if (!result) result = scePthreadCreate(&thread->handle, &attr.thread, ps4_run_thread, thread,
                                         thread->name ? thread->name : "SDL thread");
    scePthreadAttrDestroy(&attr.thread);
    return ps4_thread_result("scePthreadCreate", result);
}
void SDL_SYS_SetupThread(const char *name) { (void)name; }
SDL_threadID SDL_ThreadID(void) { return (SDL_threadID)scePthreadSelf(); }
int SDL_SYS_SetThreadPriority(SDL_ThreadPriority priority)
{
    (void)priority;
    /* Keep the scheduler priority inherited from the parent thread. */
    return 0;
}
void SDL_SYS_WaitThread(SDL_Thread *thread) { scePthreadJoin(thread->handle, NULL); }
void SDL_SYS_DetachThread(SDL_Thread *thread) { scePthreadDetach(thread->handle); }
SDL_mutex *SDL_CreateMutex(void)
{
    PS4_Attr attr;
    SDL_mutex *mutex = SDL_calloc(1, sizeof(*mutex));
    int result;
    if (!mutex) { SDL_OutOfMemory(); return NULL; }
    SDL_zero(attr);
    result = scePthreadMutexattrInit(&attr.mutex);
    if (result) goto fail;
    result = scePthreadMutexattrSettype(&attr.mutex, PTHREAD_MUTEX_RECURSIVE);
    if (!result) result = scePthreadMutexInit(&mutex->native, &attr.mutex, "SDL mutex");
    scePthreadMutexattrDestroy(&attr.mutex);
    if (!result) return mutex;
fail:
    ps4_thread_result("scePthreadMutexInit", result);
    SDL_free(mutex);
    return NULL;
}
void SDL_DestroyMutex(SDL_mutex *mutex)
{
    if (mutex) { scePthreadMutexDestroy(&mutex->native); SDL_free(mutex); }
}
int SDL_LockMutex(SDL_mutex *mutex)
{ return mutex ? ps4_thread_result("scePthreadMutexLock", scePthreadMutexLock(&mutex->native)) : 0; }
int SDL_UnlockMutex(SDL_mutex *mutex)
{ return mutex ? ps4_thread_result("scePthreadMutexUnlock", scePthreadMutexUnlock(&mutex->native)) : 0; }
int SDL_TryLockMutex(SDL_mutex *mutex)
{
    int result;
    if (!mutex) return 0;
    result = scePthreadMutexTrylock(&mutex->native);
    if (result == EBUSY || (Uint32)result == ORBIS_KERNEL_ERROR_EBUSY) return SDL_MUTEX_TIMEDOUT;
    return ps4_thread_result("scePthreadMutexTrylock", result);
}
SDL_cond *SDL_CreateCond(void)
{
    SDL_cond *cond = SDL_calloc(1, sizeof(*cond));
    if (!cond) { SDL_OutOfMemory(); return NULL; }
    if (ps4_thread_result("scePthreadCondInit", scePthreadCondInit(&cond->native, NULL, "SDL condition"))) {
        SDL_free(cond); return NULL;
    }
    return cond;
}
void SDL_DestroyCond(SDL_cond *cond)
{
    if (cond) { scePthreadCondDestroy(&cond->native); SDL_free(cond); }
}
int SDL_CondSignal(SDL_cond *cond)
{ return cond ? ps4_thread_result("scePthreadCondSignal", scePthreadCondSignal(&cond->native)) : SDL_InvalidParamError("cond"); }
int SDL_CondBroadcast(SDL_cond *cond)
{ return cond ? ps4_thread_result("scePthreadCondBroadcast", scePthreadCondBroadcast(&cond->native)) : SDL_InvalidParamError("cond"); }
int SDL_CondWait(SDL_cond *cond, SDL_mutex *mutex)
{
    if (!cond || !mutex) return SDL_SetError("Invalid condition or mutex");
    return ps4_thread_result("scePthreadCondWait", scePthreadCondWait(&cond->native, &mutex->native));
}
int SDL_CondWaitTimeout(SDL_cond *cond, SDL_mutex *mutex, Uint32 ms)
{
    Uint64 deadline, remaining;
    int result;
    if (!cond || !mutex) return SDL_SetError("Invalid condition or mutex");
    if (ms == SDL_MUTEX_MAXWAIT) return SDL_CondWait(cond, mutex);
    if (!ms) return SDL_MUTEX_TIMEDOUT;
    remaining = (Uint64)ms * 1000;
    deadline = sceKernelGetProcessTime() + remaining;
    /* The firmware duration is 32 bits in microseconds; SDL's is 32 bits in
     * milliseconds. Repeat a long wait without truncating its deadline. */
    for (;;) {
        result = scePthreadCondTimedwait(&cond->native, &mutex->native,
                                         (OrbisKernelUseconds)SDL_min(remaining, (Uint64)SDL_MAX_UINT32));
        if (result != ETIMEDOUT && (Uint32)result != ORBIS_KERNEL_ERROR_ETIMEDOUT)
            return ps4_thread_result("scePthreadCondTimedwait", result);
        const Uint64 now = sceKernelGetProcessTime();
        if (now >= deadline) return SDL_MUTEX_TIMEDOUT;
        remaining = deadline - now;
    }
}
