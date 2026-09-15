#include "AdaptiveLayout.h"
#include "WorldViewport.h"
#include <cstdlib>

int nLoginFrameX = -400, nLoginFrameY = -300;
namespace {
bool configured = false, rendererReady = false;
int renderWidth = 800, renderHeight = 600;
void** stageSlot = reinterpret_cast<void**>(0x00BEDED4);
void UpdateFrameOrigin() {
    nLoginFrameX = -(AdaptiveLayout::UseWideLoginFrame() ? 1280 : renderWidth) / 2;
    nLoginFrameY = -(AdaptiveLayout::UseWideLoginFrame() ? 720 : renderHeight) / 2;
}
}
namespace AdaptiveLayout {
#ifdef ADAPTIVE_LAYOUT_TESTING
void SetStageSlotForTesting(void** slot) { stageSlot = slot; }
#endif
void ConfigureLogin(bool enabled) { configured = enabled; UpdateFrameOrigin(); }
void SetRenderSize(int width, int height) {
    renderWidth = width; renderHeight = height; UpdateFrameOrigin();
}
bool UseWideLoginFrame() {
    return configured && rendererReady && renderWidth > 1280 && renderHeight >= 720 &&
        // Avoid stretching a 16:9 login into a non-widescreen render mode.
        abs(renderWidth * 9 - renderHeight * 16) <= 16;
}
bool LoginInputRect(HWND window, RECT& rect) {
    return GetPropW(window, kLoginProperty) &&
        BeiDouQueryLoginViewportV1(renderWidth, renderHeight, &rect);
}
void __cdecl AdjustBackground(int* frame, int front) {
    // CMapLoadable::LoadBack, after all WZ properties were read. These locals
    // feed both static/animated layers and the native MakeGrid implementation.
    void* map = reinterpret_cast<void*>(frame[-0x48 / 4]);
    // The active renderer scales whole decorative backdrop groups. Moving
    // only their negative-Y pieces opens gaps in vertically joined artwork.
    if (WorldViewport::RegisterBackground(map,frame[2],front,frame[-0x70/4])) return;
    frame[-0x74 / 4] = BackgroundY(renderHeight, IsLogin(map), front,
        frame[-0x74 / 4], frame[-0x70 / 4], frame[-0x68 / 4]);
}
}
extern "C" void __cdecl BeiDouEnableLoginViewportV1() {
    rendererReady = true;
    UpdateFrameOrigin();
}
extern "C" BOOL __cdecl BeiDouQueryLoginViewportV1(UINT width, UINT height, RECT* rect) {
    if (!rect || !AdaptiveLayout::UseWideLoginFrame() ||
        width != renderWidth || height != renderHeight) return FALSE;
    // get_stage(): the ZRef object starts at BEDED0; its pointer is at +4.
    // Presentation and stage replacement execute on the game's main thread.
    if (!AdaptiveLayout::IsLogin(*stageSlot)) return FALSE;
    const LONG left = (renderWidth - 1280) / 2, top = (renderHeight - 720) / 2;
    *rect = {left, top, left + 1280, top + 720};
    return TRUE;
}
#if defined(_M_IX86)
#pragma comment(linker, "/EXPORT:BeiDouEnableLoginViewportV1=_BeiDouEnableLoginViewportV1")
#pragma comment(linker, "/EXPORT:BeiDouQueryLoginViewportV1=_BeiDouQueryLoginViewportV1")
#endif
