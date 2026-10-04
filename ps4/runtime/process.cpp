// Firmware 9.00 rejects libkernel's _exit with SIGSYS. Keep musl's exit cleanup
// and replace only its final process-termination step with the console service.
#include <orbis/SystemService.h>
#include <orbis/Sysmodule.h>
#include <orbis/libkernel.h>
#include <cstdint>
#include <cstdio>
#include <unistd.h>

extern "C" __attribute__((used, visibility("default"))) void _exit(int status) {
    const auto load_result = static_cast<int32_t>(
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE));
    const int32_t exit_result = sceSystemServiceLoadExec("exit", nullptr);

    // A successful request does not return. Preserve an unexpected failure for
    // KLog and leave the title closable from the dashboard without a crash loop.
    char message[192];
    std::snprintf(message, sizeof(message),
                  "Nocturne PS4: exit(%d) returned (load=%#x exit=%#x); close the application\n",
                  status, static_cast<unsigned>(load_result),
                  static_cast<unsigned>(exit_result));
    sceKernelDebugOutText(0, message);
    for (;;) sceKernelUsleep(1000000);
}

// Some C runtimes call _Exit (ISO C11) instead of _exit (POSIX).
extern "C" __attribute__((used, visibility("default"))) void _Exit(int status) {
    _exit(status);
}
