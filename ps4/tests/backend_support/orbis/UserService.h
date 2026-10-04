#pragma once
#include <stdint.h>
#define ORBIS_KERNEL_PRIO_FIFO_NORMAL 700
#define ORBIS_USER_SERVICE_ERROR_ALREADY_INITIALIZED 0x80960001u
typedef struct { int32_t priority; } OrbisUserServiceInitializeParams;
int32_t sceUserServiceInitialize(void *);
int32_t sceUserServiceGetInitialUser(int32_t *);
