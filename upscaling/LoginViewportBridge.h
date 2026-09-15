#pragma once
#include <windows.h>
namespace LoginViewportBridge {
constexpr wchar_t kProperty[] = L"BeiDou.LoginViewport.v1";
inline void Enable() {
    using Fn = void (__cdecl*)();
    const HMODULE client = GetModuleHandleW(L"ijl15.dll");
    const auto enable = client ? reinterpret_cast<Fn>(GetProcAddress(client, "BeiDouEnableLoginViewportV1")) : nullptr;
    if (enable) enable();
}
inline bool Query(UINT width, UINT height, RECT* rect) {
    using Fn = BOOL (__cdecl*)(UINT, UINT, RECT*);
    static const Fn query = [] {
        const HMODULE client = GetModuleHandleW(L"ijl15.dll");
        return client ? reinterpret_cast<Fn>(GetProcAddress(client, "BeiDouQueryLoginViewportV1")) : nullptr;
    }();
    return query && query(width, height, rect);
}
}
