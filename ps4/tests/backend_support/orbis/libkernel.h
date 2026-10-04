#pragma once
/* Host doubles for the small firmware boundary used by system.c. These are
 * declarations, not an SDK or a claim that host tests validate firmware ABI. */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint64_t sceKernelGetProcessTime(void);
uint64_t sceKernelGetProcessTimeCounter(void);
uint64_t sceKernelGetProcessTimeCounterFrequency(void);
int32_t sceKernelUsleep(uint32_t);
uint32_t sceKernelLoadStartModule(const char *, size_t, const void *, uint32_t, void *, void *);
int32_t sceKernelStopUnloadModule(int32_t, size_t, const void *, uint32_t, void *, int32_t *);
int32_t sceKernelDlsym(int32_t, const char *, void **);
#ifdef __cplusplus
}
#endif
