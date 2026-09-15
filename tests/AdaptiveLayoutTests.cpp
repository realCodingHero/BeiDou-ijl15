#include "AdaptiveLayout.h"
#include "WindowScalingGeometry.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

int main() {
    using namespace AdaptiveLayout;
    SetRenderSize(1920, 1080);
    ConfigureLogin(true);
    assert(!UseWideLoginFrame() && nLoginFrameX == -960 && nLoginFrameY == -540);
    BeiDouEnableLoginViewportV1();
    assert(UseWideLoginFrame() && nLoginFrameX == -640 && nLoginFrameY == -360);

    // Exercise the actual stage discriminator and the versioned DLL contract.
    uintptr_t login = kLoginVtable, field = 0x00400000;
    void* stageObject = nullptr;
    void** stage = &stageObject;
    SetStageSlotForTesting(stage);
    RECT rect{};
    *stage = &field;
    assert(!BeiDouQueryLoginViewportV1(1920, 1080, &rect));
    *stage = &login;
    assert(BeiDouQueryLoginViewportV1(1920, 1080, &rect));
    assert(rect.left == 320 && rect.top == 180 && rect.right == 1600 && rect.bottom == 900);
    assert(!BeiDouQueryLoginViewportV1(1280, 720, &rect));
    assert(!BeiDouQueryLoginViewportV1(1920, 1080, nullptr));
    HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(window);
    assert(!LoginInputRect(window, rect));
    assert(SetPropW(window, kLoginProperty, reinterpret_cast<HANDLE>(1)));
    assert(LoginInputRect(window, rect));
    for (SIZE physical : {SIZE{1920,1080}, SIZE{3200,1800}, SIZE{2560,1440}}) {
        for (POINT point : {POINT{0,0}, POINT{physical.cx/2,physical.cy/2},
            POINT{physical.cx,physical.cy}, POINT{-50,-50}}) {
            auto native = WindowScalingGeometry::MapViewport(point, physical, rect, true);
            auto back = WindowScalingGeometry::MapViewport(native, physical, rect, false);
            assert(abs(back.x-point.x) <= 2 && abs(back.y-point.y) <= 2);
        }
        auto center = WindowScalingGeometry::MapViewport({physical.cx/2,physical.cy/2}, physical, rect, true);
        assert(center.x == 960 && center.y == 540);
    }
    *stage = &field; // A stale window flag must not keep login mapping after entry.
    assert(!LoginInputRect(window, rect));
    *stage = nullptr;
    assert(!LoginInputRect(window, rect));
    RemovePropW(window, kLoginProperty); DestroyWindow(window);

    for (SIZE render : {SIZE{800,600}, SIZE{1280,720}, SIZE{1600,1200}}) {
        SetRenderSize(render.cx, render.cy); assert(!UseWideLoginFrame());
    }
    SetRenderSize(2560,1440); assert(UseWideLoginFrame());
    ConfigureLogin(false); assert(!UseWideLoginFrame() && nLoginFrameX == -1280);

    // The reported crescent no longer exposes its cropped top at the bottom
    // camera limit. Foreground, world layers and vertical grids stay authored.
    assert(BackgroundY(1080, false, 0, -226, -13, 0) == -497);
    assert(540 + BackgroundY(1080, false, 0, -226, -13, 0) - 139 + 47 <= 0);
    assert(720 + BackgroundY(1440, false, 0, -226, -13, 0) - 139 + 70 <= 0);
    for (int height : {600,720,1080,1440}) {
        for (int type = 0; type <= 7; ++type) {
            assert(BackgroundY(height, false, 1, -226, -13, type) == -226);
            assert(BackgroundY(height, true, 0, -226, -13, type) == -226);
            assert(BackgroundY(height, false, 0, -226, -100, type) == -226);
            assert(BackgroundY(height, false, 0, 120, -13, type) == 120);
            if (height <= 720 || (type != 0 && type != 1 && type != 4))
                assert(BackgroundY(height, false, 0, -226, -13, type) == -226);
        }
    }
    puts("PASS: login handshake, native stage transitions, input round trips, fallback, background anchoring exclusions");
}
