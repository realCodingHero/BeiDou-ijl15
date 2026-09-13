// Application-local D3D8 entry points. Initialization happens outside DllMain.
#include "d3d8.hpp"
#include "UpscaleRenderer.h"
#include <string>
extern "C" IDirect3D8* WINAPI CreateTranslatedD3D8(UINT sdkVersion);
namespace {
HMODULE Native() {
    static HMODULE module = [] {
        wchar_t system[MAX_PATH]{};
        if (!GetSystemDirectoryW(system, MAX_PATH)) return static_cast<HMODULE>(nullptr);
        return LoadLibraryW((std::wstring(system) + L"\\d3d8.dll").c_str());
    }();
    return module;
}
}
extern "C" IDirect3D8* WINAPI Direct3DCreate8(UINT sdkVersion) {
    using Factory = IDirect3D8*(WINAPI*)(UINT);
    const auto native = reinterpret_cast<Factory>(GetProcAddress(Native(), "Direct3DCreate8"));
    if (NeuralUpscale::Configuration().enabled) {
        HMODULE d3dx = LoadLibraryExW(L"d3dx9_43.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (d3dx) {
            auto* d3d = CreateTranslatedD3D8(sdkVersion);
            FreeLibrary(d3dx);
            if (d3d) {
                NeuralUpscale::Log("BeiDou built-in upscaling: DX8 -> DX9, quality=%s",
                    NeuralUpscale::Configuration().quality == NeuralUpscale::Quality::Fast ? "fast" : "balanced");
                return d3d;
            }
        }
        NeuralUpscale::Log("DX9 translator unavailable; using system DX8");
    }
    return native ? native(sdkVersion) : nullptr;
}
extern "C" HRESULT WINAPI ValidatePixelShader(const DWORD* shader, const D3DCAPS8* caps, BOOL errors, char** text) {
    using Fn = HRESULT(WINAPI*)(const DWORD*, const D3DCAPS8*, BOOL, char**);
    const auto fn = reinterpret_cast<Fn>(GetProcAddress(Native(), "ValidatePixelShader"));
    return fn ? fn(shader, caps, errors, text) : E_FAIL;
}
extern "C" HRESULT WINAPI ValidateVertexShader(const DWORD* shader, const DWORD* declaration, const D3DCAPS8* caps, BOOL errors, char** text) {
    using Fn = HRESULT(WINAPI*)(const DWORD*, const DWORD*, const D3DCAPS8*, BOOL, char**);
    const auto fn = reinterpret_cast<Fn>(GetProcAddress(Native(), "ValidateVertexShader"));
    return fn ? fn(shader, declaration, caps, errors, text) : E_FAIL;
}
extern "C" void WINAPI DebugSetMute() {
    const auto fn = reinterpret_cast<void(WINAPI*)()>(GetProcAddress(Native(), "DebugSetMute"));
    if (fn) fn();
}
