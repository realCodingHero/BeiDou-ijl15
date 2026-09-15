#include "WorldViewport.h"
#include "detours.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>

namespace {
int renderWidth = 800, renderHeight = 600;
void** stageSlot = reinterpret_cast<void**>(0x00BEDED4);
const void* activeMap = nullptr; // Identity only; no retained game/COM objects.
WorldViewport::View activeView;
const void* loadingMap=nullptr;
std::vector<int> loadingBackdrops, activeBackdrops;
uintptr_t layerVtable = 0;
bool graphicsReady = false;
using DrawLayer = void (__thiscall*)(void*, void*);
DrawLayer originalDrawLayer = nullptr;
void* originalSubmitQuad = nullptr;
thread_local bool alignWorldTexels=false;
#ifdef WORLD_VIEWPORT_TESTING
bool testTexelAlignment=true;
#endif
struct TexelScope {
    bool saved=alignWorldTexels;
    explicit TexelScope(bool enabled) {alignWorldTexels=enabled;}
    ~TexelScope() {alignWorldTexels=saved;}
};
void __cdecl AlignQuad(float* vertices,const unsigned char* rawCanvas) {
    if (!alignWorldTexels) return;
#ifdef WORLD_VIEWPORT_TESTING
    if (!testTexelAlignment) return;
#endif
    // Gr2D adds half a source texel to every UV (including clipped raw chunks).
    // This compensates native 1:1 pixel centers, but after extra projection
    // scaling the last output pixels sample beyond the allocated canvas.
    const int logWidth=*reinterpret_cast<const int*>(rawCanvas+0x4C);
    const int logHeight=*reinterpret_cast<const int*>(rawCanvas+0x50);
    if (logWidth<0 || logWidth>16 || logHeight<0 || logHeight>16) return;
    const float du=std::ldexp(0.5f,-logWidth),dv=std::ldexp(0.5f,-logHeight);
    for (int i=0;i<4;++i) {vertices[i*6+4]-=du; vertices[i*6+5]-=dv;}
}
void __declspec(naked) SubmitQuad() {
    __asm {
        pushfd
        pushad
        push esi
        lea eax,[ebp-80h]
        push eax
        call AlignQuad
        add esp,8
        popad
        popfd
        jmp dword ptr [originalSubmitQuad]
    }
}
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
double InputScale() {
    return graphicsReady && activeMap && *stageSlot == activeMap ? activeView.scale : 1.0;
}
POINT MapPoint(int x, int y) {
    return WorldViewport::Inverse({x,y}, renderWidth, renderHeight, InputScale());
}
void* Handler() {
    auto stage = static_cast<unsigned char*>(*stageSlot);
    return stage ? stage + 4 : nullptr;
}
double DrawScale() {
    if (!graphicsReady || !activeMap) return 1.0;
    // Native transitions temporarily clear get_stage while the old layers
    // still fade out. Keep their projection; input stops immediately.
    auto stage=*stageSlot;
    return !stage || stage==activeMap || IsField(stage) ? activeView.scale : 1.0;
}
bool HasScene() {
    if (!graphicsReady || !activeMap) return false;
    auto stage=*stageSlot;
    return !stage || stage==activeMap || IsField(stage);
}
bool AcceptPoint(int x,int y) {
    if (!graphicsReady || !activeMap || *stageSlot!=activeMap) return true;
    const RECT& clip=activeView.clip;
    return x>=clip.left && x<clip.right && y>=clip.top && y<clip.bottom;
}
void* RootLayer(void* object) {
    // Overlay children have relative Z values. Follow the actual retained
    // overlay chain, not the child's Z (a face or equipment piece may use 0).
    for (unsigned depth = 0; object && depth < 64; ++depth) {
        if (*static_cast<uintptr_t*>(object) != layerVtable) return nullptr;
        auto bytes = static_cast<unsigned char*>(object);
        void* parent = *reinterpret_cast<void**>(bytes + 0x30);
        if (!parent) return object;
        if (parent == object) return nullptr;
        object = parent;
    }
    return nullptr;
}
struct Matrix { float m[16]; };
using GetTransform = HRESULT (__stdcall*)(void*, unsigned, Matrix*);
using SetTransform = HRESULT (__stdcall*)(void*, unsigned, const Matrix*);
struct Viewport { unsigned x,y,width,height; float minZ,maxZ; };
using GetViewport = HRESULT (__stdcall*)(void*, Viewport*);
using SetViewport = HRESULT (__stdcall*)(void*, const Viewport*);
struct ProjectionScope {
    void* device = nullptr;
    Matrix saved{};
    Viewport savedViewport{};
    bool applied = false, viewportApplied=false;
    ProjectionScope(void* dev, double scale, const RECT& clip) : device(dev) {
        if (!dev || FAILED(Method<GetTransform>(dev,0x98)(dev,3,&saved))) return;
        double sx=scale,sy=scale;
        if (clip.left || clip.top || clip.right!=renderWidth || clip.bottom!=renderHeight) {
            if (FAILED(Method<GetViewport>(dev,0xA4)(dev,&savedViewport))) return;
            Viewport cropped=savedViewport;
            cropped.width=unsigned((clip.right-clip.left)*double(savedViewport.width)/renderWidth);
            cropped.height=unsigned((clip.bottom-clip.top)*double(savedViewport.height)/renderHeight);
            if (!cropped.width || !cropped.height) return;
            cropped.x+=(savedViewport.width-cropped.width)/2;
            cropped.y+=(savedViewport.height-cropped.height)/2;
            if (FAILED(Method<SetViewport>(dev,0xA0)(dev,&cropped))) return;
            viewportApplied=true;
            // A narrower viewport must clip, not squeeze the world in X.
            sx*=double(savedViewport.width)/cropped.width;
            sy*=double(savedViewport.height)/cropped.height;
        }
        Matrix projection = saved;
        for (int row=0; row<4; ++row) {
            projection.m[row*4] *= static_cast<float>(sx);
            projection.m[row*4+1] *= static_cast<float>(sy);
        }
        applied = SUCCEEDED(Method<SetTransform>(dev,0x94)(dev,3,&projection));
        if (!applied && viewportApplied) {
            Method<SetViewport>(device,0xA0)(device,&savedViewport); viewportApplied=false;
        }
    }
    ~ProjectionScope() {
        if (applied) Method<SetTransform>(device,0x94)(device,3,&saved);
        if (viewportApplied) Method<SetViewport>(device,0xA0)(device,&savedViewport);
    }
};
void __fastcall RenderLayer(void* layer, void*, void* context) {
    auto root=RootLayer(layer);
    // Native status bar (8CFD46) is C00615D0, screen messages C0061634,
    // windows C00616FC. They are negative too. World effects stop below HUD.
    const int z=root ? *reinterpret_cast<int*>(static_cast<unsigned char*>(root)+0x44):0;
    const bool world=root && z<int(0xC00615D0u) && context && HasScene();
    double scale=world ? DrawScale():1.0;
    if (world && std::find(activeBackdrops.begin(),activeBackdrops.end(),z)!=activeBackdrops.end())
        scale=(std::max)(scale,renderHeight/600.0);
    const bool clipped=world && (activeView.clip.left || activeView.clip.top ||
        activeView.clip.right!=renderWidth || activeView.clip.bottom!=renderHeight);
    if (!world || (scale<=1.0 && !clipped)) {
        originalDrawLayer(layer,context);
#ifdef WORLD_VIEWPORT_TESTING
        if (drawObserver) drawObserver(layer,context,1.0);
#endif
        return;
    }
    ProjectionScope projection(*static_cast<void**>(context),scale,activeView.clip);
    if (!projection.applied) { originalDrawLayer(layer,context); return; }
    // Preserve native point/automatic sampling and correct the native 1:1 UV
    // bias only inside this draw. No cached vertices/textures are modified.
    TexelScope texels(scale>1.0);
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
    // Fit vertically. A narrow map uses a centered, narrower drawing region
    // instead of forcing the entire window width to be filled by zooming in.
    view.scale = (std::max)(1.0, double(height)/innerHeight);
    view.width = (std::min)(width/view.scale,innerWidth); view.height = height/view.scale;
    const int visibleWidth=(std::min)(width,2*int(std::floor((view.width*view.scale+1e-8)/2)));
    view.clip={(width-visibleWidth)/2,0,(width-visibleWidth)/2+visibleWidth,height};
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
    loadingMap=nullptr; loadingBackdrops.clear(); activeBackdrops.clear();
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
    const unsigned char submit[] = {0x8B,0x07,0x8B,0x08,0x6A,0x18,0x8D,0x55,0x80,
        0x52,0x6A,0x02,0x6A,0x05,0x50,0xFF,0x91,0x20,0x01,0x00,0x00};
    const unsigned char uv[] = {0xD9,0x46,0x54,0xD9,0x55,0xC0,0xD9,0x5D,0x90,
        0xD9,0x46,0x58};
    if (memcmp(reinterpret_cast<void*>(base+0xDA7C),entry,sizeof(entry)) ||
        memcmp(reinterpret_cast<void*>(base+0x9EC8),getZ,sizeof(getZ)) ||
        memcmp(reinterpret_cast<void*>(base+0xA49C),getOverlay,sizeof(getOverlay)) ||
        memcmp(reinterpret_cast<void*>(base+0xE2BF),filter,sizeof(filter)) ||
        memcmp(reinterpret_cast<void*>(base+0x8877),submit,sizeof(submit)) ||
        memcmp(reinterpret_cast<void*>(base+0x8703),uv,sizeof(uv))) {
        Log("World viewport disabled: unsupported Gr2D instructions."); return false;
    }
    originalDrawLayer = reinterpret_cast<DrawLayer>(base+0xDA7C);
    originalSubmitQuad = reinterpret_cast<void*>(base+0x8877);
    if (DetourTransactionBegin() != NO_ERROR) return false;
    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR ||
        DetourAttach(reinterpret_cast<void**>(&originalDrawLayer), RenderLayer) != NO_ERROR ||
        DetourAttach(&originalSubmitQuad,SubmitQuad) != NO_ERROR) {
        DetourTransactionAbort(); return false;
    }
    if (DetourTransactionCommit() != NO_ERROR) return false;
    layerVtable=base+0x2E3F0; graphicsReady=true;
    Log("World viewport ready: vertical fit, side margins, independent HUD, aligned texels.");
    return true;
}
void __cdecl AdjustCamera(void* map) {
    activeMap=nullptr; activeView={}; activeBackdrops.clear();
    if (!graphicsReady || renderHeight <= 720 || !IsField(map)) return;
    auto camera = reinterpret_cast<RECT*>(static_cast<unsigned char*>(map)+0xF0);
    // Before native short-map clamping, recover the exact bounds the engine
    // resolved, including linked maps and the native foothold fallback.
    RECT bounds = {camera->left-renderWidth/2,camera->top-renderHeight/2,
        camera->right+renderWidth/2,camera->bottom+renderHeight/2};
    auto view=Fit(bounds,renderWidth,renderHeight);
    if (view.width<=0 || view.height<=0 || bounds.right-bounds.left<=16 || bounds.bottom-bounds.top<=16) return;
    *camera=view.camera; activeView=view; activeMap=map;
    if (loadingMap==map) activeBackdrops=loadingBackdrops;
    char line[320]; sprintf_s(line,"bounds=(%ld,%ld,%ld,%ld) view=%.2fx%.2f zoom=%.4f camera=(%ld,%ld,%ld,%ld) clip=(%ld,%ld,%ld,%ld) backdrops=%u",
        bounds.left,bounds.top,bounds.right,bounds.bottom,view.width,view.height,view.scale,
        camera->left,camera->top,camera->right,camera->bottom,
        view.clip.left,view.clip.top,view.clip.right,view.clip.bottom,unsigned(activeBackdrops.size())); Log(line);
}
void __cdecl AdjustWorldCursor(POINT* screen) { if (screen) *screen=MapPoint(screen->x,screen->y); }
void __cdecl BeginBackgrounds(void* map) {
    loadingMap=map; loadingBackdrops.clear();
    if (map==activeMap) activeBackdrops.clear();
}
bool RegisterBackground(void* map,int index,int front,int ry) {
    if (!graphicsReady || renderHeight<=720 || !IsField(map)) return false;
    if (loadingMap!=map) BeginBackgrounds(map);
    // Register the exact root Z assigned by LoadBack to this entry. All tiled
    // copies and overlay animation children use the same root classification.
    // World-attached scenery and foreground keep the world projection.
    if (!front && ry>-100 && index>=0 && index<128) {
        const int z=int(0xBFFE0C00u)+index*1000;
        loadingBackdrops.push_back(z);
        // Native background-only reloads do not always restore the camera.
        if (map==activeMap) activeBackdrops.push_back(z);
    }
    return true;
}
void __fastcall MouseButton(void*,void*,unsigned message,unsigned flags,int x,int y) {
    auto handler=Handler(); if (!handler || !AcceptPoint(x,y)) return;
    POINT p=MapPoint(x,y);
    Method<void (__thiscall*)(void*,unsigned,unsigned,int,int)>(handler,8)(handler,message,flags,p.x,p.y);
}
int __fastcall MouseMove(void*,void*,int x,int y) {
    auto handler=Handler(); if (!handler || !AcceptPoint(x,y)) return 0;
    POINT p=MapPoint(x,y);
    return Method<int (__thiscall*)(void*,int,int)>(handler,12)(handler,p.x,p.y);
}
int __fastcall MouseWheel(void*,void*,int x,int y,int wheel) {
    auto handler=Handler(); if (!handler || !AcceptPoint(x,y)) return 0;
    POINT p=MapPoint(x,y);
    return Method<int (__thiscall*)(void*,int,int,int)>(handler,16)(handler,p.x,p.y,wheel);
}
void __fastcall DragMove(void*,void*,int state,void* object,int x,int y) {
    auto handler=Handler(); if (!handler || !AcceptPoint(x,y)) return;
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
void SetTexelAlignmentForTesting(bool enabled) { testTexelAlignment=enabled; }
#endif
}
