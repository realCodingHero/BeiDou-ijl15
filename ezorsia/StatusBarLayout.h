#pragma once
#include <windows.h>
#include <unknwn.h>
#include <oaidl.h>
#include <cstdint>

namespace StatusBarLayout {
// The current assets and 26-key layout occupy 1280 logical pixels.
constexpr int kContentWidth = 1280;
inline int Left(int renderWidth) { return renderWidth > kContentWidth ? (renderWidth-kContentWidth)/2 : 0; }
inline int Width(int renderWidth) { return renderWidth > kContentWidth ? kContentWidth : renderWidth; }

using CreateObject = HRESULT (__cdecl*)(const wchar_t*, const GUID*, IUnknown**, IUnknown*);
bool UsesStatusBarOrigin(uintptr_t caller);
// Replaces one owned native smart-pointer result. The layer will own the new
// vector; no COM object is cached globally or released during DLL teardown.
bool CenterOrigin(IUnknown** result, uintptr_t caller, int left, CreateObject create);
void __cdecl CenterNativeOrigin(IUnknown** result, uintptr_t caller);
}
