#include "overlay.h"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        // Install the tiny IAT hook while the injector still owns the startup race.
        // Renderer and ImGui initialization are deferred out of the loader lock.
        InstallBootstrapHook();
        if (HANDLE thread = CreateThread(nullptr, 0, OverlayWorker, module, 0, nullptr))
            CloseHandle(thread);
    }
    return TRUE;
}

