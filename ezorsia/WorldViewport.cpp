#include "WorldViewport.h"
#include "detours.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>

namespace {
int renderWidth = 800, renderHeight = 600;
void** stageSlot = reinterpret_cast<void**>(0x00BEDED4);
const void* activeMap = nullptr; // Identity only; no retained game/COM objects.
WorldViewport::View activeView;
uintptr_t layerVtable = 0;
bool graphicsReady = false;
using DrawLayer = void (__thiscall*)(void*, void*);
DrawLayer originalDrawLayer = nullptr;
#ifdef WORLD_VIEWPORT_TESTING
void (*drawObserver)(void*,void*,double) = nullptr;
#endif
template<class T> T Method(void* object, unsigned offset) {
    return reinterpret_cast<T>((*static_cast<void***>(object))[offset / 4]);
}
bool IsField(const void* map) {
    if (!map) return false;
    auto handler = static_cast<const unsigned char*>(map) + 4;
    using IsKindOf = int (__thiscall*)(const void*, const void*);
    return Method<IsKindOf>(const_cast<unsigned char*>(handler), 0x48)(handler,
        reinterpret_cast<const void*>(0x00BED758)) != 0;
}
double Scale() {
    return graphicsReady && activeMap && *stageSlot == activeMap ? activeView.scale : 1.0;
}
POINT MapPoint(int x, int y) {
    return WorldViewport::Inverse({x,y}, renderWidth, renderHeight, Scale());
}
void* Handler() {
    auto stage = static_cast<unsigned char*>(*stageSlot);
    return stage ? stage + 4 : nullptr;
}
bool ReadInt(void* info, const wchar_t* key, LONG& value) {
    if (!info) return false;
    BSTR name = SysAllocString(key);
    if (!name) return false;
    VARIANT result{}, converted{};
    using GetItem = HRESULT (__stdcall*)(void*, BSTR, VARIANT*);
    HRESULT hr = Method<GetItem>(info, 0x14)(info, name, &result);
    SysFreeString(name);
    if (SUCCEEDED(hr) && result.vt != VT_EMPTY && result.vt != VT_NULL && result.vt != VT_ERROR)
        hr = VariantChangeType(&converted, &result, 0, VT_I4);
    else hr = E_FAIL;
    if (SUCCEEDED(hr)) value = converted.lVal;
    VariantClear(&converted); VariantClear(&result);
    return SUCCEEDED(hr);
}
bool ReadBounds(const void* map, RECT& bounds) {
    // Same info/link resolution and foothold fallback as RestoreViewRange.
    // Called while loading backgrounds, before the final camera is installed.
    if (!graphicsReady || renderHeight <= 720 || !IsField(map)) return false;
    auto info = *reinterpret_cast<void* const*>(static_cast<const unsigned char*>(map) + 0x2C);
    auto physics = *reinterpret_cast<const unsigned char**>(0x00BEBFA0);
    if (!info || !physics) return false;
    const auto mbr = *reinterpret_cast<const RECT*>(physics + 0x24);
    bounds = {mbr.left-20, mbr.top-60, mbr.right+20, mbr.bottom+100};
    ReadInt(info,L"VRLeft",bounds.left); ReadInt(info,L"VRTop",bounds.top);
    ReadInt(info,L"VRRight",bounds.right); ReadInt(info,L"VRBottom",bounds.bottom);
    return true;
}
bool IsWorldLayer(void* object) {
    // Overlay children have relative Z values. Follow the actual retained
    // overlay chain, not the child's Z (a face or equipment piece may use 0).
    for (unsigned depth = 0; object && depth < 64; ++depth) {
        if (*static_cast<uintptr_t*>(object) != layerVtable) return false;
        auto bytes = static_cast<unsigned char*>(object);
        void* parent = *reinterpret_cast<void**>(bytes + 0x30);
        if (!parent) return *reinterpret_cast<int*>(bytes + 0x44) < 0;
        if (parent == object) return false;
        object = parent;
    }
    return false;
}
struct Matrix { float m[16]; };
using GetTransform = HRESULT (__stdcall*)(void*, unsigned, Matrix*);
using SetTransform = HRESULT (__stdcall*)(void*, unsigned, const Matrix*);
struct ProjectionScope {
    void* device = nullptr;
    Matrix saved{};
    bool applied = false;
    ProjectionScope(void* dev, double scale) : device(dev) {
        if (!dev || FAILED(Method<GetTransform>(dev,0x98)(dev,3,&saved))) return;
        Matrix projection = saved;
        // Scale clip-space X/Y about its center. This also handles camera
        // translations, animation rotations and transformed canvases.
        for (int row=0; row<4; ++row) {
            projection.m[row*4] *= static_cast<float>(scale);
            projection.m[row*4+1] *= static_cast<float>(scale);
        }
        applied = SUCCEEDED(Method<SetTransform>(dev,0x94)(dev,3,&projection));
    }
    ~ProjectionScope() {
        if (applied) Method<SetTransform>(device,0x94)(device,3,&saved);
    }
};
struct FilterScope {
    int* filter;
    int saved;
    explicit FilterScope(void* layer) : filter(reinterpret_cast<int*>(static_cast<unsigned char*>(layer)+0x4C)), saved(*filter) {
        // Let Gr2D update its own sampler cache. Changing D3D state behind that
        // cache would leak linear/point filtering between the world and HUD.
        *filter = 2;
    }
    ~FilterScope() { *filter = saved; }
};
void __fastcall RenderLayer(void* layer, void*, void* context) {
    const double scale = Scale();
    if (scale <= 1.0 || !context || !IsWorldLayer(layer)) {
        originalDrawLayer(layer,context);
#ifdef WORLD_VIEWPORT_TESTING
        if (drawObserver) drawObserver(layer,context,1.0);
#endif
        return;
    }
    ProjectionScope projection(*static_cast<void**>(context),scale);
    if (!projection.applied) { originalDrawLayer(layer,context); return; }
    FilterScope filter(layer);
    originalDrawLayer(layer,context);
#ifdef WORLD_VIEWPORT_TESTING
    if (drawObserver) drawObserver(layer,context,scale);
#endif
}
void Log(const char* message) {
    wchar_t path[MAX_PATH]{};
    const DWORD length=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if (!length || length>=MAX_PATH) return;
    wchar_t* leaf=wcsrchr(path,L'\\');
    if (!leaf || wcscpy_s(leaf+1,MAX_PATH-(leaf+1-path),L"map_viewport.log")) return;
    FILE* file = nullptr;
    if (!_wfopen_s(&file,path,L"a") && file) {
        fprintf(file,"%s\n",message); fclose(file);
    }
}
}

namespace WorldViewport {
View Fit(RECT bounds, int width, int height) {
    View view;
    const double bw = double(bounds.right)-bounds.left, bh = double(bounds.bottom)-bounds.top;
    if (width <= 0 || height <= 0 || bw <= 16 || bh <= 16) return view;
    // The asset audit found cut canvas edges up to eight pixels inside VR.
    // Keep those margins outside the visible rectangle, including at camera
    // limits. Large maps retain their full render-sized viewport.
    bounds.left+=8; bounds.top+=8;
    // Camera coordinates are integers. An even inner extent leaves an integer
    // center even on odd-sized maps, without rounding past an authored edge.
    const double innerWidth=2*std::floor((bw-16)/2), innerHeight=2*std::floor((bh-16)/2);
    if (innerWidth<=0 || innerHeight<=0) return view;
    bounds.right=bounds.left+LONG(innerWidth); bounds.bottom=bounds.top+LONG(innerHeight);
    view.scale = (std::max)({1.0, double(width)/innerWidth, double(height)/innerHeight});
    view.width = width/view.scale; view.height = height/view.scale;
    const double hw=view.width/2, hh=view.height/2;
    constexpr double epsilon=1e-8; // Floating-point roundoff at an integer limit.
    view.camera = {LONG(std::ceil(bounds.left+hw-epsilon)), LONG(std::ceil(bounds.top+hh-epsilon)),
        LONG(std::floor(bounds.right-hw+epsilon)), LONG(std::floor(bounds.bottom-hh+epsilon))};
    return view;
}
POINT Inverse(POINT screen, int width, int height, double scale) {
    if (!std::isfinite(scale) || scale <= 1.0) return screen;
    return {LONG(std::lround(width/2.0+(screen.x-width/2.0)/scale)),
        LONG(std::lround(height/2.0+(screen.y-height/2.0)/scale))};
}
void Configure(int width, int height) {
    renderWidth=width; renderHeight=height; activeMap=nullptr; activeView={};
}
bool InstallGraphics(HMODULE module) {
    if (graphicsReady) return true;
    if (!module) return false;
    auto base = reinterpret_cast<uintptr_t>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.SizeOfImage != 0x40000 || nt->FileHeader.TimeDateStamp != 0x4B7C13FD) return false;
    unsigned char entry[] = {0xB8,0,0,0,0,0xE8,0x6A,0xCB,0x01,0x00};
    *reinterpret_cast<uint32_t*>(entry+1) = static_cast<uint32_t>(base+0x2BA47);
    const unsigned char getZ[] = {0x8B,0x4C,0x24,0x04,0x8B,0x49,0x44};
    const unsigned char getOverlay[] = {0x8B,0x47,0x30,0x89,0x43,0x08};
    const unsigned char filter[] = {0x8B,0x4B,0x4C};
    if (memcmp(reinterpret_cast<void*>(base+0xDA7C),entry,sizeof(entry)) ||
        memcmp(reinterpret_cast<void*>(base+0x9EC8),getZ,sizeof(getZ)) ||
        memcmp(reinterpret_cast<void*>(base+0xA49C),getOverlay,sizeof(getOverlay)) ||
        memcmp(reinterpret_cast<void*>(base+0xE2BF),filter,sizeof(filter))) {
        Log("World viewport disabled: unsupported Gr2D instructions."); return false;
    }
    originalDrawLayer = reinterpret_cast<DrawLayer>(base+0xDA7C);
    if (DetourTransactionBegin() != NO_ERROR) return false;
    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR ||
        DetourAttach(reinterpret_cast<void**>(&originalDrawLayer), RenderLayer) != NO_ERROR) {
        DetourTransactionAbort(); return false;
    }
    if (DetourTransactionCommit() != NO_ERROR) return false;
    layerVtable=base+0x2E3F0; graphicsReady=true;
    Log("World viewport ready: guarded Gr2D layer projection, independent HUD.");
    return true;
}
void __cdecl AdjustCamera(void* map) {
    activeMap=nullptr; activeView={};
    if (!graphicsReady || renderHeight <= 720 || !IsField(map)) return;
    auto camera = reinterpret_cast<RECT*>(static_cast<unsigned char*>(map)+0xF0);
    // Before native short-map clamping, recover the exact bounds the engine
    // resolved, including linked maps and the native foothold fallback.
    RECT bounds = {camera->left-renderWidth/2,camera->top-renderHeight/2,
        camera->right+renderWidth/2,camera->bottom+renderHeight/2};
    auto view=Fit(bounds,renderWidth,renderHeight);
    if (view.width<=0 || view.height<=0 || bounds.right-bounds.left<=16 || bounds.bottom-bounds.top<=16) return;
    *camera=view.camera; activeView=view; activeMap=map;
    char line[240]; sprintf_s(line,"bounds=(%ld,%ld,%ld,%ld) view=%.2fx%.2f zoom=%.4f camera=(%ld,%ld,%ld,%ld)",
        bounds.left,bounds.top,bounds.right,bounds.bottom,view.width,view.height,view.scale,
        camera->left,camera->top,camera->right,camera->bottom); Log(line);
}
void __cdecl AdjustWorldCursor(POINT* screen) { if (screen) *screen=MapPoint(screen->x,screen->y); }
int BackgroundHeight(const void* map, int fallback) {
    RECT bounds{};
    if (!ReadBounds(map,bounds)) return fallback;
    const auto view=Fit(bounds,renderWidth,renderHeight);
    return view.height>0 ? int(std::lround(view.height)) : fallback;
}
void __fastcall MouseButton(void*,void*,unsigned message,unsigned flags,int x,int y) {
    auto handler=Handler(); if (!handler) return;
    POINT p=MapPoint(x,y);
    Method<void (__thiscall*)(void*,unsigned,unsigned,int,int)>(handler,8)(handler,message,flags,p.x,p.y);
}
int __fastcall MouseMove(void*,void*,int x,int y) {
    auto handler=Handler(); if (!handler) return 0;
    POINT p=MapPoint(x,y);
    return Method<int (__thiscall*)(void*,int,int)>(handler,12)(handler,p.x,p.y);
}
int __fastcall MouseWheel(void*,void*,int x,int y,int wheel) {
    auto handler=Handler(); if (!handler) return 0;
    POINT p=MapPoint(x,y);
    return Method<int (__thiscall*)(void*,int,int,int)>(handler,16)(handler,p.x,p.y,wheel);
}
void __fastcall DragMove(void*,void*,int state,void* object,int x,int y) {
    auto handler=Handler(); if (!handler) return;
    POINT p=MapPoint(x,y);
    Method<void (__thiscall*)(void*,int,void*,int,int)>(handler,24)(handler,state,object,p.x,p.y);
}
#ifdef WORLD_VIEWPORT_TESTING
void SetContextForTesting(void** slot,void* map,RECT bounds,int width,int height) {
    Configure(width,height); stageSlot=slot; activeMap=map;
    activeView=Fit(bounds,width,height); graphicsReady=true;
}
void DrawLayerForTesting(void* layer,void* context,DrawLayer original) {
    layerVtable=*static_cast<uintptr_t*>(layer); originalDrawLayer=original;
    RenderLayer(layer,nullptr,context);
}
void SetObserverForTesting(void (*observer)(void*,void*,double)) { drawObserver=observer; }
#endif
}
