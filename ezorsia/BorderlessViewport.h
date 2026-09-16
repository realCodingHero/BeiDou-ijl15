#pragma once
#include <windows.h>

// Shared by the window/input hook and the D3D presentation module.
namespace BorderlessViewport {
constexpr wchar_t Property[] = L"BeiDou.BorderlessWindowV1";
constexpr wchar_t PresentedProperty[] = L"BeiDou.BorderlessPresentedV1";
inline RECT Fit(LONG width, LONG height, LONG renderWidth, LONG renderHeight) {
    if (width <= 0 || height <= 0 || renderWidth <= 0 || renderHeight <= 0) return {};
    LONG w = width, h = height;
    if (LONGLONG(width) * renderHeight > LONGLONG(height) * renderWidth)
        w = MulDiv(height, renderWidth, renderHeight);
    else h = MulDiv(width, renderHeight, renderWidth);
    return {(width-w)/2, (height-h)/2, (width-w)/2+w, (height-h)/2+h};
}
inline bool Query(HWND window, LONG renderWidth, LONG renderHeight, RECT& rect) {
    RECT client{};
    if (!GetPropW(window, Property) || !GetClientRect(window, &client)) return false;
    rect = Fit(client.right, client.bottom, renderWidth, renderHeight);
    return rect.right > rect.left && rect.bottom > rect.top;
}
}
