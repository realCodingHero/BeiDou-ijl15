#include "stdafx.h"
#include <algorithm>
#include <intrin.h>
#include "WindowScaling.h"
#include "WindowScalingGeometry.h"
#include "WindowPlacementConfig.h"
#include "detours.h"

namespace {
bool resizable = true;
bool keepAspectRatio = true;
HWND gameWindow = nullptr;
WNDPROC originalWindowProc = nullptr;
auto originalCreateWindow = &CreateWindowExA;
auto originalSetWindowPos = &SetWindowPos;
auto originalScreenToClient = &ScreenToClient;
auto originalClientToScreen = &ClientToScreen;
auto originalExitProcess = &ExitProcess;
std::string configPath;
WindowPlacementConfig::Placement startupPlacement, lastPlacement;
bool havePlacement = false;
bool restoreMaximizedOnShow = false;
bool returningToWindow = false;
bool destroyingWindow = false;
SIZE returnSize{};
POINT returnPosition{};
UINT restoreMessage = 0;
volatile LONG savingPlacement = 0;
volatile LONG exitStarted = 0;
SRWLOCK placementLock = SRWLOCK_INIT;
unsigned long long placementRevision = 0, savedRevision = 0;
struct PlacementGuard {
    PlacementGuard() { AcquireSRWLockExclusive(&placementLock); }
    ~PlacementGuard() { ReleaseSRWLockExclusive(&placementLock); }
};

bool IsWindowed(HWND window) {
    const auto style = static_cast<DWORD>(GetWindowLongPtrA(window, GWL_STYLE));
    return (style & WS_CAPTION) == WS_CAPTION && !(style & WS_CHILD);
}

bool IsGameClass(LPCSTR name) {
    return reinterpret_cast<ULONG_PTR>(name) > 0xffff && strcmp(name, "MapleStoryClass") == 0;
}

bool IsRendererCaller(void* caller) {
    HMODULE owner = nullptr;
    return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(caller), &owner) &&
        owner == GetModuleHandleA("Gr2D_DX8.dll");
}

UINT WindowDpi(HWND window) {
    using GetDpi = UINT(WINAPI*)(HWND);
    static auto getDpi = reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow"));
    return getDpi ? getDpi(window) : 96;
}

SIZE OuterSize(DWORD style, DWORD exStyle, BOOL menu, int width, int height, UINT dpi) {
    RECT rect{0, 0, width, height};
    using AdjustForDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static auto adjust = reinterpret_cast<AdjustForDpi>(GetProcAddress(GetModuleHandleA("user32.dll"), "AdjustWindowRectExForDpi"));
    if (!adjust || !adjust(&rect, style, menu, exStyle, dpi))
        AdjustWindowRectEx(&rect, style, menu, exStyle);
    return {rect.right - rect.left, rect.bottom - rect.top};
}

SIZE OuterSize(HWND window, int width, int height, UINT dpi = 0) {
    return OuterSize(static_cast<DWORD>(GetWindowLongPtrA(window, GWL_STYLE)),
        static_cast<DWORD>(GetWindowLongPtrA(window, GWL_EXSTYLE)), GetMenu(window) != nullptr,
        width, height, dpi ? dpi : WindowDpi(window));
}

void RememberSize(HWND window) {
    RECT rect{};
    if (returningToWindow || destroyingWindow || !IsWindowed(window) || IsIconic(window) || !GetClientRect(window, &rect) ||
        rect.right <= 0 || rect.bottom <= 0) return;
    Client::m_nWindowWidth = rect.right;
    Client::m_nWindowHeight = rect.bottom;
    Client::m_fScaleX = static_cast<float>(rect.right) / Client::m_nGameWidth;
    Client::m_fScaleY = static_cast<float>(rect.bottom) / Client::m_nGameHeight;
    Client::m_bEnableScaling = rect.right != Client::m_nGameWidth || rect.bottom != Client::m_nGameHeight;
    RECT outer{};
    if (!GetWindowRect(window, &outer)) return;
    const bool maximized = IsZoomed(window) != FALSE;
    PlacementGuard guard;
    lastPlacement.width = rect.right;
    lastPlacement.height = rect.bottom;
    lastPlacement.x = outer.left;
    lastPlacement.y = outer.top;
    lastPlacement.hasPosition = true;
    lastPlacement.maximized = maximized;
    if (!lastPlacement.maximized) {
        lastPlacement.normalWidth = rect.right;
        lastPlacement.normalHeight = rect.bottom;
        lastPlacement.normalX = outer.left;
        lastPlacement.normalY = outer.top;
    }
    havePlacement = true;
    ++placementRevision;
}

void SavePlacement() {
    if (configPath.empty() || InterlockedCompareExchange(&savingPlacement, 1, 0) != 0) return;
    WindowPlacementConfig::Placement snapshot;
    unsigned long long revision;
    {
        PlacementGuard guard;
        if (!havePlacement || placementRevision == savedRevision) {
            InterlockedExchange(&savingPlacement, 0);
            return;
        }
        snapshot = lastPlacement;
        revision = placementRevision;
    }
    // Use the last valid windowed placement even when exiting minimized/fullscreen.
    try {
        if (WindowPlacementConfig::Save(configPath, snapshot, Client::m_nGameWidth, Client::m_nGameHeight)) {
            PlacementGuard guard;
            savedRevision = revision;
        } else OutputDebugStringA("BeiDou: could not save window placement\n");
    } catch (...) {
        OutputDebugStringA("BeiDou: window placement save failed\n");
    }
    InterlockedExchange(&savingPlacement, 0);
}

void KeepPositionVisible(POINT& point, SIZE outer) {
    RECT rect{point.x, point.y, point.x + outer.cx, point.y + outer.cy};
    if (MonitorFromRect(&rect, MONITOR_DEFAULTTONULL)) return;
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (GetMonitorInfoA(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &monitor)) {
        point.x = monitor.rcWork.left;
        point.y = monitor.rcWork.top;
    }
}

BOOL RestoreWindowed(HWND window, HWND after = nullptr,
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED) {
    if (!returningToWindow || !IsWindowed(window) || IsIconic(window)) return FALSE;
    const SIZE outer = OuterSize(window, returnSize.cx, returnSize.cy);
    KeepPositionVisible(returnPosition, outer);
    const BOOL result = originalSetWindowPos(window, after, returnPosition.x, returnPosition.y,
        outer.cx, outer.cy, flags & ~(SWP_NOMOVE | SWP_NOSIZE));
    if (result) {
        returningToWindow = false;
        RememberSize(window);
        if (restoreMaximizedOnShow && IsWindowVisible(window)) PostMessageA(window, restoreMessage, 0, 0);
    }
    return result;
}

LRESULT CALLBACK GameWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_SYSCOMMAND && resizable && IsWindowed(window) &&
        ((wParam & 0xfff0) == SC_MAXIMIZE || (wParam & 0xfff0) == SC_RESTORE)) {
        // BeiDou.exe 0x009FEB57 handles SC_MAXIMIZE by putting Gr2D in fullscreen.
        // Let Windows maximize/restore the framed window without invoking that branch.
        return DefWindowProcA(window, message, wParam, lParam);
    }
    if (message == restoreMessage && restoreMessage) {
        RestoreWindowed(window);
        if (restoreMaximizedOnShow && !returningToWindow && IsWindowed(window) && IsWindowVisible(window)) {
            restoreMaximizedOnShow = false;
            DefWindowProcA(window, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
        }
        return 0;
    }
    if (message == WM_SHOWWINDOW && wParam && restoreMaximizedOnShow)
        PostMessageA(window, restoreMessage, 0, 0);
    if (message == WM_DESTROY || (message == WM_ENDSESSION && wParam)) {
        SavePlacement();
        destroyingWindow = true;
    }
    if (message == WM_WINDOWPOSCHANGING && resizable && keepAspectRatio &&
        IsWindowed(window) && !IsIconic(window)) {
        // Snap, placement restoration and DPI changes can resize without WM_SIZING.
        auto* position = reinterpret_cast<WINDOWPOS*>(lParam);
        if (!(position->flags & SWP_NOSIZE)) {
            const SIZE frame = OuterSize(window, 0, 0);
            LONG width = position->cx - frame.cx;
            LONG height = position->cy - frame.cy;
            const LONGLONG difference = static_cast<LONGLONG>(width) * Client::m_nGameHeight -
                static_cast<LONGLONG>(height) * Client::m_nGameWidth;
            // Allow integer-pixel rounding without shrinking on repeated frame changes.
            if (width > 0 && height > 0 &&
                (difference < 0 ? -difference : difference) >
                    (std::max)(Client::m_nGameWidth, Client::m_nGameHeight) / 2) {
                if (difference > 0) width = (std::max)(1, MulDiv(height, Client::m_nGameWidth, Client::m_nGameHeight));
                else height = (std::max)(1, MulDiv(width, Client::m_nGameHeight, Client::m_nGameWidth));
                position->cx = width + frame.cx;
                position->cy = height + frame.cy;
            }
        }
    } else if (message == WM_SIZE && wParam != SIZE_MINIMIZED) {
        // Save the user's new size before the original procedure can reset Gr2D.
        RememberSize(window);
    } else if (message == WM_STYLECHANGING && wParam == GWL_STYLE) {
        auto* change = reinterpret_cast<STYLESTRUCT*>(lParam);
        const bool wasWindowed = (change->styleOld & WS_CAPTION) == WS_CAPTION;
        const bool willBeWindowed = (change->styleNew & WS_CAPTION) == WS_CAPTION;
        if (wasWindowed && !willBeWindowed) RememberSize(window);
        if (!wasWindowed && willBeWindowed) {
            const auto configured = WindowPlacementConfig::Read(configPath, startupPlacement);
            returnSize = {configured.width, configured.height};
            returnPosition = {lastPlacement.x, lastPlacement.y};
            if (!havePlacement && Client::WindowedMode) {
                // The game creates its HWND without a caption; Gr2D adds it later.
                // This first transition is startup, before any real fullscreen toggle.
                if (restoreMaximizedOnShow) {
                    returnSize = {startupPlacement.normalWidth, startupPlacement.normalHeight};
                    returnPosition = {startupPlacement.normalX, startupPlacement.normalY};
                }
                if (!startupPlacement.hasPosition) {
                    const SIZE outer = OuterSize(change->styleNew | (resizable ? WS_THICKFRAME : 0),
                        GetWindowLongA(window, GWL_EXSTYLE), FALSE, returnSize.cx, returnSize.cy, WindowDpi(window));
                    returnPosition = {(std::max)(0L, (GetSystemMetrics(SM_CXSCREEN) - outer.cx) / 2),
                        (std::max)(0L, (GetSystemMetrics(SM_CYSCREEN) - outer.cy) / 4)};
                }
            }
            returningToWindow = true;
            change->styleNew &= ~(WS_MAXIMIZE | WS_MINIMIZE);
        }
        if (resizable) {
            if (willBeWindowed) change->styleNew |= WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
            else change->styleNew &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        }
    } else if (message == WM_STYLECHANGED && wParam == GWL_STYLE && returningToWindow) {
        // Gr2D first sends SWP_NOSIZE|SWP_FRAMECHANGED, whose temporary WM_SIZE
        // still reports the fullscreen dimensions. Ignore it until the real resize.
        PostMessageA(window, restoreMessage, 0, 0);
    } else if (message == WM_SIZING && resizable && keepAspectRatio && IsWindowed(window)) {
        RECT previous{};
        GetWindowRect(window, &previous);
        auto* rect = reinterpret_cast<RECT*>(lParam);
        *rect = WindowScalingGeometry::Constrain(*rect, previous, static_cast<UINT>(wParam),
            OuterSize(window, 0, 0), {Client::m_nGameWidth, Client::m_nGameHeight});
        return TRUE;
    } else if (message == WM_DPICHANGED && IsWindowed(window)) {
        const RECT rect = *reinterpret_cast<RECT*>(lParam);
        originalSetWindowPos(window, nullptr, rect.left, rect.top, rect.right - rect.left,
            rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    const LRESULT result = CallWindowProcA(originalWindowProc, window, message, wParam, lParam);
    if (message == WM_WINDOWPOSCHANGED) {
        RememberSize(window);
        // Gr2D shows the HWND with SetWindowPos(SWP_SHOWWINDOW), which need not
        // follow the ShowWindow API's WM_SHOWWINDOW notification path.
        if (restoreMaximizedOnShow && IsWindowVisible(window)) PostMessageA(window, restoreMessage, 0, 0);
    }
    if (message == WM_GETMINMAXINFO && resizable && keepAspectRatio && IsWindowed(window)) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        const SIZE frame = OuterSize(window, 0, 0);
        LONG width = (std::max)(1L, limits->ptMinTrackSize.x - frame.cx);
        LONG height = (std::max)(1L, limits->ptMinTrackSize.y - frame.cy);
        width = (std::max)(width, static_cast<LONG>(MulDiv(height, Client::m_nGameWidth, Client::m_nGameHeight)));
        height = (std::max)(height, static_cast<LONG>(MulDiv(width, Client::m_nGameHeight, Client::m_nGameWidth)));
        limits->ptMinTrackSize = {width + frame.cx, height + frame.cy};

        // Maximize inside the current monitor's work area, with a 16:9 client
        // area rather than stretching the image to the monitor's available shape.
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
            const LONG availableWidth = monitor.rcWork.right - monitor.rcWork.left;
            const LONG availableHeight = monitor.rcWork.bottom - monitor.rcWork.top;
            width = (std::max)(1L, availableWidth - frame.cx);
            height = (std::max)(1L, availableHeight - frame.cy);
            if (static_cast<LONGLONG>(width) * Client::m_nGameHeight >
                static_cast<LONGLONG>(height) * Client::m_nGameWidth)
                width = (std::max)(1, MulDiv(height, Client::m_nGameWidth, Client::m_nGameHeight));
            else height = (std::max)(1, MulDiv(width, Client::m_nGameHeight, Client::m_nGameWidth));
            limits->ptMaxSize = {width + frame.cx, height + frame.cy};
            limits->ptMaxPosition = {
                monitor.rcWork.left - monitor.rcMonitor.left + (availableWidth - limits->ptMaxSize.x) / 2,
                monitor.rcWork.top - monitor.rcMonitor.top + (availableHeight - limits->ptMaxSize.y) / 2};
        }
    } else if (message == WM_NCDESTROY) {
        gameWindow = nullptr;
        originalWindowProc = nullptr;
        returningToWindow = false;
    }
    return result;
}

HWND WINAPI CreateWindowHook(DWORD exStyle, LPCSTR className, LPCSTR title, DWORD style,
    int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
    const bool manage = !gameWindow && IsGameClass(className) &&
        (resizable || Client::m_bEnableScaling) && !(style & WS_CHILD) &&
        Client::m_nGameWidth > 0 && Client::m_nGameHeight > 0 &&
        Client::m_nWindowWidth > 0 && Client::m_nWindowHeight > 0;
    if (!manage) return originalCreateWindow(exStyle, className, title, style, x, y, width, height,
        parent, menu, instance, param);

    if (configPath.empty()) {
        startupPlacement.width = Client::m_nWindowWidth;
        startupPlacement.height = Client::m_nWindowHeight;
    }
    {
        PlacementGuard guard;
        lastPlacement = startupPlacement;
        havePlacement = false;
        placementRevision = savedRevision = 0;
    }
    destroyingWindow = false;
    returningToWindow = false;
    restoreMaximizedOnShow = startupPlacement.maximized && Client::WindowedMode;
    // Windows implicitly adds a caption to ordinary overlapped windows even if
    // CreateWindowEx's input style omits WS_CAPTION (the game's normal startup).
    const bool startsWindowed = (style & WS_CAPTION) == WS_CAPTION || !(style & WS_POPUP);
    if (startsWindowed) {
        style |= WS_CAPTION | WS_MINIMIZEBOX;
        if (resizable) style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    }
    const int initialWidth = restoreMaximizedOnShow ? startupPlacement.normalWidth : startupPlacement.width;
    const int initialHeight = restoreMaximizedOnShow ? startupPlacement.normalHeight : startupPlacement.height;
    const SIZE outer = OuterSize(style, exStyle, menu != nullptr,
        initialWidth, initialHeight, 96);
    if (startsWindowed) {
        width = outer.cx;
        height = outer.cy;
        POINT position{(std::max)(0, (GetSystemMetrics(SM_CXSCREEN) - width) / 2),
            (std::max)(0, (GetSystemMetrics(SM_CYSCREEN) - height) / 4)};
        if (startupPlacement.hasPosition) {
            position = restoreMaximizedOnShow ? POINT{startupPlacement.normalX, startupPlacement.normalY} :
                POINT{startupPlacement.x, startupPlacement.y};
            KeepPositionVisible(position, outer);
        }
        x = position.x;
        y = position.y;
    }
    HWND window = originalCreateWindow(exStyle, className, title, style, x, y, width, height,
        parent, menu, instance, param);
    if (!window) return nullptr;

    SetLastError(0);
    auto previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(GameWindowProc)));
    if (!previous) return window;
    gameWindow = window;
    originalWindowProc = previous;
    // The monitor DPI is known after creation. Do not recenter on later ShowWindow calls.
    if (startsWindowed) {
        const SIZE corrected = OuterSize(window, initialWidth, initialHeight);
        originalSetWindowPos(window, nullptr, 0, 0, corrected.cx, corrected.cy,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    RememberSize(window);
    return window;
}

BOOL WINAPI SetWindowPosHook(HWND window, HWND after, int x, int y, int width, int height, UINT flags) {
    if (window == gameWindow && !(flags & SWP_NOSIZE) && IsWindowed(window) &&
        IsRendererCaller(_ReturnAddress())) {
        // Gr2D's initialization and device recovery use the logical render size.
        // Only override those calls; interactive sizing and Windows placement stay native.
        if (IsIconic(window)) return TRUE;
        if (returningToWindow) return RestoreWindowed(window, after, flags);
        const SIZE outer = OuterSize(window, Client::m_nWindowWidth, Client::m_nWindowHeight);
        width = outer.cx;
        height = outer.cy;
    }
    return originalSetWindowPos(window, after, x, y, width, height, flags);
}

void WINAPI ExitProcessHook(UINT exitCode) {
    // Normal game exit can call ExitProcess without destroying the main HWND.
    // Persist before the original call begins DLL teardown / takes the loader lock.
    if (InterlockedCompareExchange(&exitStarted, 1, 0) == 0) SavePlacement();
    originalExitProcess(exitCode);
}

ULONG_PTR ClientCallerOffset(void* caller) {
    return reinterpret_cast<ULONG_PTR>(caller) - reinterpret_cast<ULONG_PTR>(GetModuleHandleA(nullptr));
}

POINT ScaleInput(HWND window, POINT point, bool toRender) {
    RECT rect{};
    if (window != gameWindow || !IsWindowed(window) || !GetClientRect(window, &rect)) return point;
    const SIZE physical{rect.right, rect.bottom};
    const SIZE logical{Client::m_nGameWidth, Client::m_nGameHeight};
    return WindowScalingGeometry::MapPoint(point, toRender ? physical : logical, toRender ? logical : physical);
}

BOOL WINAPI ScreenToClientHook(HWND window, LPPOINT point) {
    const ULONG_PTR offset = ClientCallerOffset(_ReturnAddress());
    const BOOL result = originalScreenToClient(window, point);
    // CInputSystem::GetCursorPos's Win32 fallback (the DirectInput path is already logical).
    if (result && point && offset == 0x19A3C5) *point = ScaleInput(window, *point, true);
    return result;
}

BOOL WINAPI ClientToScreenHook(HWND window, LPPOINT point) {
    const ULONG_PTR offset = ClientCallerOffset(_ReturnAddress());
    // CInputSystem::SetCursorPos / UpdateMouse warp a logical cursor to screen pixels.
    if (point && (offset == 0x19A8F1 || offset == 0x19AC77)) {
        POINT mapped = ScaleInput(window, *point, false);
        if (!originalClientToScreen(window, &mapped)) return FALSE;
        *point = mapped;
        return TRUE;
    }
    return originalClientToScreen(window, point);
}
}

void WindowScaling::SetNativeCursorPosition(void* input, int x, int y, CursorVectorSetter setter) {
    if (!input || !setter) return;
    // CWndMan feeds the LOWORD/HIWORD of native mouse lParam to the old
    // fixMouseWheelHook. Unlike DirectInput coordinates, those are window pixels.
    // Preserve signed mouse coordinates when capture carries them outside the client.
    const POINT point = ScaleInput(*static_cast<HWND*>(input),
        {static_cast<SHORT>(x), static_cast<SHORT>(y)}, true);
    setter(input, nullptr, point.x, point.y);
}

void __fastcall WindowScaling::DrawNativeCursor(void* input, void*, int x, int y) {
    // Fastcall's ECX and stack arguments match this thiscall routine; EDX is unused.
    SetNativeCursorPosition(input, x, y, reinterpret_cast<CursorVectorSetter>(0x0059A0CB));
}

void WindowScaling::Configure(bool allowResize, bool keepRatio) {
    resizable = allowResize;
    keepAspectRatio = keepRatio;
}

void WindowScaling::LoadPlacement(const char* path) {
    configPath = path ? path : "";
    startupPlacement = {};
    startupPlacement.width = Client::m_nWindowWidth;
    startupPlacement.height = Client::m_nWindowHeight;
    startupPlacement = WindowPlacementConfig::Read(configPath, startupPlacement);
}

bool WindowScaling::Hook(bool enable) {
    if (enable) restoreMessage = RegisterWindowMessageA("BeiDou.WindowScaling.RestorePlacement");
    // Commit all API hooks together; an installation failure leaves native APIs intact.
    if (DetourTransactionBegin() != NO_ERROR) return false;
    const auto operation = enable ? DetourAttach : DetourDetach;
    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR ||
        operation(reinterpret_cast<void**>(&originalSetWindowPos), SetWindowPosHook) != NO_ERROR ||
        operation(reinterpret_cast<void**>(&originalScreenToClient), ScreenToClientHook) != NO_ERROR ||
        operation(reinterpret_cast<void**>(&originalClientToScreen), ClientToScreenHook) != NO_ERROR ||
        operation(reinterpret_cast<void**>(&originalExitProcess), ExitProcessHook) != NO_ERROR ||
        operation(reinterpret_cast<void**>(&originalCreateWindow), CreateWindowHook) != NO_ERROR) {
        DetourTransactionAbort();
        return false;
    }
    if (DetourTransactionCommit() != NO_ERROR) return false;
    if (!enable && gameWindow && originalWindowProc) {
        SetWindowLongPtrA(gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWindowProc));
        gameWindow = nullptr;
        originalWindowProc = nullptr;
    }
    return true;
}
