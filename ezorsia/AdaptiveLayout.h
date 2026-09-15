#pragma once
#include <windows.h>
#include <cstdint>

// Shared presentation contract: coordinates remain in the native render buffer.
// A versioned handshake makes either DLL safe to use with an older counterpart.
namespace AdaptiveLayout {
constexpr wchar_t kLoginProperty[] = L"BeiDou.LoginViewport.v1";
constexpr uintptr_t kLoginVtable = 0x00AF6B24;
void ConfigureLogin(bool enabled);
#ifdef ADAPTIVE_LAYOUT_TESTING
void SetStageSlotForTesting(void** slot);
#endif
void SetRenderSize(int width, int height);
bool UseWideLoginFrame();
bool LoginInputRect(HWND window, RECT& rect);
inline bool IsLogin(const void* stage) {
    return stage && *static_cast<const uintptr_t*>(stage) == kLoginVtable;
}
inline int BackgroundY(int height, bool login, int front, int y, int ry, int type) {
    // Upper parallax decorations were authored against the 600px screen.
    // Preserve world-attached scenery (ry=-100), foreground, vertical tiling,
    // vertical scrolling and all lower decorations. No per-map ID exceptions.
    if (height <= 720 || login || front || y >= 0 || ry <= -100 || ry > 0 ||
        (type != 0 && type != 1 && type != 4)) return y;
    // At a bottom-anchored camera, its center moves up by half the added
    // height. Include that parallax contribution so the top stays stable.
    return y - MulDiv(height - 600, 100 - ry, 200);
}
void __cdecl AdjustBackground(int* frame, int front);
}
extern int nLoginFrameX, nLoginFrameY;
extern "C" void __cdecl BeiDouEnableLoginViewportV1();
extern "C" BOOL __cdecl BeiDouQueryLoginViewportV1(UINT width, UINT height, RECT* rect);
