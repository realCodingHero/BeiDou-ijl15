#pragma once
#include <windows.h>
#include <vector>

namespace WorldViewport {
struct View {
    double scale = 1.0;
    double width = 0, height = 0;
    RECT camera{};
    RECT clip{};
};
View Fit(RECT bounds, int width, int height);
struct GroundTile { int layer,left,top,right,solidBottom; };
bool ConstrainGround(View& view, RECT bounds, const std::vector<GroundTile>& tiles);
void __cdecl BeginTerrain(void* map);
void __cdecl RecordTerrainTile(int* frame,int y);
void CollectTerrainTile(void* map,int layer,void* property,void* canvas,int x,int y);
POINT Inverse(POINT screen, int width, int height, double scale);
void Configure(int width, int height);
bool InstallGraphics(HMODULE module);
void __cdecl AdjustCamera(void* map);
void __cdecl AdjustWorldCursor(POINT* screen);
void __cdecl BeginBackgrounds(void* map);
bool RegisterBackground(void* map,int index,int front,int ry);
void __fastcall MouseButton(void*, void*, unsigned message, unsigned flags, int x, int y);
int __fastcall MouseMove(void*, void*, int x, int y);
int __fastcall MouseWheel(void*, void*, int x, int y, int wheel);
void __fastcall DragMove(void*, void*, int state, void* object, int x, int y);
#ifdef WORLD_VIEWPORT_TESTING
void SetContextForTesting(void** stage, void* map, RECT bounds, int width, int height);
void DrawLayerForTesting(void* layer, void* context, void (__thiscall* original)(void*, void*));
void SetObserverForTesting(void (*observer)(void*, void*, double));
void SetTexelAlignmentForTesting(bool enabled);
#endif
}
