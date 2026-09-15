#include "UpscaleLoader.h"
#include "WorldViewport.h"
#include "detours.h"
#include <intrin.h>
#include <cstring>
#include <cwchar>

namespace {
auto originalLoadLibrary = &LoadLibraryA;
wchar_t modulePath[MAX_PATH]{};
bool installed = false;
HMODULE WINAPI LoadGraphicsLibrary(LPCSTR name) {
    if (name) {
        const char* leaf = name;
        for (const char* p = name; *p; ++p) if (*p == '\\' || *p == '/') leaf = p + 1;
        if (_stricmp(leaf, "d3d8.dll") == 0) {
            HMODULE caller = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(_ReturnAddress()), &caller);
            // Only the renderer opts in. Other callers retain native loading.
            if (caller && caller == GetModuleHandleA("Gr2D_DX8.dll")) {
                if (HMODULE custom = LoadLibraryW(modulePath)) {
                    WorldViewport::InstallGraphics(caller);
                    return custom;
                }
            }
        }
    }
    return originalLoadLibrary(name);
}
}
bool UpscaleLoader::Install(HMODULE owner, bool enabled) {
    if (enabled == installed) return true;
    if (enabled) {
        const DWORD length = GetModuleFileNameW(owner, modulePath, MAX_PATH);
        if (!length || length >= MAX_PATH) return false;
        wchar_t* leaf = wcsrchr(modulePath, L'\\');
        if (!leaf || wcscpy_s(leaf + 1, MAX_PATH - (leaf + 1 - modulePath), L"BeiDouUpscale.dll")) return false;
        const DWORD attributes = GetFileAttributesW(modulePath);
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return true;
    }
    if (DetourTransactionBegin() != NO_ERROR) return false;
    const auto operation = enabled ? DetourAttach : DetourDetach;
    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR ||
        operation(reinterpret_cast<void**>(&originalLoadLibrary), LoadGraphicsLibrary) != NO_ERROR) {
        DetourTransactionAbort();
        return false;
    }
    if (DetourTransactionCommit() != NO_ERROR) return false;
    installed = enabled;
    return true;
}
