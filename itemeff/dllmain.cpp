#include "pch.h"
#include "itemeff_patch.h"

extern "C" __declspec(dllexport) void BeiDouItemEffVersion() {
}

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
#ifndef ITEMEFF_LIFECYCLE_TESTING
        AttachItemEffectStorage();
        AttachCapeItemEffect();
#endif
    }
    return TRUE;
}
