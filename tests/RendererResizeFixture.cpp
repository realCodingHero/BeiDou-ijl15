#include <windows.h>

// Loaded as Gr2D_DX8.dll by the test process only. Reproduce the observed native
// renderer's style/size calls without loading a game or graphics device.
extern "C" __declspec(dllexport) BOOL __cdecl RendererResize(HWND window, int width, int height) {
    const DWORD style = (GetWindowLongA(window, GWL_STYLE) & ~(WS_THICKFRAME | WS_MAXIMIZEBOX)) | WS_CAPTION;
    SetWindowLongA(window, GWL_STYLE, style);
    RECT rect{0, 0, width, height};
    AdjustWindowRectEx(&rect, GetWindowLongA(window, GWL_STYLE), FALSE, GetWindowLongA(window, GWL_EXSTYLE));
    volatile BOOL result = SetWindowPos(window, nullptr, 0, 0, rect.right - rect.left,
        rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return result;
}

extern "C" __declspec(dllexport) BOOL __cdecl RendererMode(HWND window, BOOL windowed, int width, int height) {
    DWORD style = GetWindowLongA(window, GWL_STYLE);
    style = windowed ? (style & 0x7f37ffff) | 0x00c80000 : (style & 0xff37ffff) | WS_POPUP;
    SetWindowLongA(window, GWL_STYLE, style);
    // Match Gr2D's first, frame-only placement call. It sends a transient WM_SIZE.
    SetWindowPos(window, windowed ? HWND_NOTOPMOST : HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RECT client{}, outer{};
    GetClientRect(window, &client);
    GetWindowRect(window, &outer);
    volatile BOOL result = SetWindowPos(window, nullptr, 0, 0,
        width + outer.right - outer.left - client.right,
        height + outer.bottom - outer.top - client.bottom,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return result;
}
