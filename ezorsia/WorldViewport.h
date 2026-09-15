#pragma once
#include <windows.h>

namespace WorldViewport {
struct View {
    double scale = 1.0;
    double width = 0, height = 0;
    RECT camera{};
};
View Fit(RECT bounds, int width, int height);
POINT Inverse(POINT screen, int width, int height, double scale);
void Configure(int width, int height);
bool InstallGraphics(HMODULE module);
void __cdecl AdjustCamera(void* map);
void __cdecl AdjustWorldCursor(POINT* screen);
int BackgroundHeight(const void* map, int fallback);
void __fastcall MouseButton(void*, void*, unsigned message, unsigned flags, int x, int y);
int __fastcall MouseMove(void*, void*, int x, int y);
int __fastcall MouseWheel(void*, void*, int x, int y, int wheel);
void __fastcall DragMove(void*, void*, int state, void* object, int x, int y);
#ifdef WORLD_VIEWPORT_TESTING
void SetContextForTesting(void** stage, void* map, RECT bounds, int width, int height);
void DrawLayerForTesting(void* layer, void* context, void (__thiscall* original)(void*, void*));
void SetObserverForTesting(void (*observer)(void*, void*, double));
#endif
}
