#pragma once
#include <algorithm>
#include <windows.h>

namespace WindowScalingGeometry {
inline POINT MapPoint(POINT point, SIZE from, SIZE to) {
    if (from.cx > 0 && from.cy > 0 && to.cx > 0 && to.cy > 0) {
        point.x = MulDiv(point.x, to.cx, from.cx);
        point.y = MulDiv(point.y, to.cy, from.cy);
    }
    return point;
}

// Keep the opposite corner/edge anchored. Borders are not part of the image.
inline RECT Constrain(RECT proposed, RECT previous, UINT edge, SIZE frame, SIZE render) {
    if (render.cx <= 0 || render.cy <= 0) return proposed;
    LONG width = (std::max)(1L, proposed.right - proposed.left - frame.cx);
    LONG height = (std::max)(1L, proposed.bottom - proposed.top - frame.cy);
    const LONGLONG dx = static_cast<LONGLONG>(width) - (previous.right - previous.left - frame.cx);
    const LONGLONG dy = static_cast<LONGLONG>(height) - (previous.bottom - previous.top - frame.cy);
    const bool corner = edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT ||
        edge == WMSZ_BOTTOMLEFT || edge == WMSZ_BOTTOMRIGHT;
    const bool useWidth = edge == WMSZ_LEFT || edge == WMSZ_RIGHT ||
        (corner && (dx < 0 ? -dx : dx) * render.cy >= (dy < 0 ? -dy : dy) * render.cx);
    if (useWidth) height = (std::max)(1, MulDiv(width, render.cy, render.cx));
    else width = (std::max)(1, MulDiv(height, render.cx, render.cy));
    width += frame.cx;
    height += frame.cy;
    if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT)
        proposed.left = proposed.right - width;
    else proposed.right = proposed.left + width;
    if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT)
        proposed.top = proposed.bottom - height;
    else proposed.bottom = proposed.top + height;
    return proposed;
}
}
