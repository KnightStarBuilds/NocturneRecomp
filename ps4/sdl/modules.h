/* Firmware modules remain loaded for the lifetime of the application: other
 * runtime components can share them after SDL has shut down a subsystem. */
#ifndef NOCTURNE_PS4_SDL_MODULES_H
#define NOCTURNE_PS4_SDL_MODULES_H
#include <orbis/Sysmodule.h>

static int ps4_load_module(enum OrbisSysModuleInternal module, const char *name)
{
    const int result = (int)sceSysmoduleLoadModuleInternal(module);
    return result < 0 ? SDL_SetError("Cannot load %s: 0x%08x", name, (unsigned)result) : 0;
}
#endif
