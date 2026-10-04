#pragma once
#include <stdint.h>
enum OrbisSysModuleInternal {
    ORBIS_SYSMODULE_INTERNAL_PAD,
    ORBIS_SYSMODULE_INTERNAL_USER_SERVICE
};
uint32_t sceSysmoduleLoadModuleInternal(enum OrbisSysModuleInternal);
