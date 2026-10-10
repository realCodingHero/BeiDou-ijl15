#include "WorldViewport.h"
#include "ConfirmedScalingPolicies.h"
#include "detours.h"
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <map>

namespace {
int renderWidth = 800, renderHeight = 600;
double sceneScale = 1.5; // Uniform actor size; never fit magnification to map bounds.
void** stageSlot = reinterpret_cast<void**>(0x00BEDED4);
const void* activeMap = nullptr; // Identity only; no retained game/COM objects.
WorldViewport::View activeView;
const void* loadingMap=nullptr;
struct Backdrop { int z,x,rx,ry,type; bool front; int y; };
std::vector<Backdrop> loadingBackdrops, activeBackdrops;
std::vector<WorldViewport::SceneBackdrop> loadingScene;
const WorldViewport::ConfirmedPolicy* activePolicy=nullptr;
const WorldViewport::ConfirmedPolicy* MatchPolicy(RECT bounds,const std::vector<WorldViewport::GroundTile>& tiles) {
    for(const auto& p:WorldViewport::confirmedPolicies) {
        if(!EqualRect(&bounds,&p.bounds) || loadingScene.size()!=p.count) continue;
        if(p.ground) {
            if(tiles.size()!=p.capCount) continue;
            bool capsMatch=true;
            for(size_t i=0;i<p.capCount && capsMatch;++i) {
                const auto& a=p.caps[i];
                capsMatch=std::any_of(tiles.begin(),tiles.end(),[&](const WorldViewport::GroundTile& b){return a.layer==b.layer && a.left==b.left && a.top==b.top && a.right==b.right && a.solidBottom==b.solidBottom;});
            }
            if(!capsMatch) continue;
        }
        bool match=true;
        for(size_t i=0;i<p.count && match;++i) {
            const auto& a=p.layers[i];
            auto b=std::find_if(loadingScene.begin(),loadingScene.end(),[&](const WorldViewport::SceneBackdrop& v){return v.index==a.index;});
            match=b!=loadingScene.end() && a.front==b->front && a.x==b->x && a.y==b->y && a.rx==b->rx && a.ry==b->ry && a.type==b->type;
        }
        if(match) return &p;
    }
    return nullptr;
}
RECT activeBounds{};
#ifdef WORLD_VIEWPORT_TESTING
void* testGraphics=nullptr;
#endif
const void* terrainMap=nullptr;
bool terrainValid=false;
std::vector<WorldViewport::GroundTile> groundTiles;
struct GroundCanvas { int width=0,originX=0,originY=0,solidRows=0; };
// Borrowed identity keys only during LoadTiles; native layers own the canvases.
std::map<void*,GroundCanvas> groundCanvases;
const void* sceneryMap=nullptr;
bool sceneryValid=false;
std::vector<RECT> sceneryTiles,sceneryObjects;
// Only value metadata survives a map load; COM identities are borrowed keys.
std::map<void*,RECT> sceneryCanvases,sceneryAnimations;
uintptr_t layerVtable = 0;
bool graphicsReady = false;
using DrawLayer = void (__thiscall*)(void*, void*);
DrawLayer originalDrawLayer = nullptr;
void* originalSubmitQuad = nullptr;
thread_local bool alignWorldTexels=false;
#ifdef WORLD_VIEWPORT_TESTING
bool testTexelAlignment=true;
int testBuffCount=0;
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
bool CameraCenter(POINT& center) {
#ifdef WORLD_VIEWPORT_TESTING
    void* graphics=testGraphics;
#else
    void* graphics=*reinterpret_cast<void**>(0x00BF14EC);
#endif
    if(!graphics) return false;
    using GetCenter=HRESULT (__stdcall*)(void*,void**);
    using GetCoordinate=HRESULT (__stdcall*)(void*,int*);
    using Release=ULONG (__stdcall*)(void*);
    void* vector=nullptr;
    if(FAILED(Method<GetCenter>(graphics,0x5C)(graphics,&vector)) || !vector) return false;
    int x=0,y=0;
    const bool ok=SUCCEEDED(Method<GetCoordinate>(vector,0x20)(vector,&x)) &&
        SUCCEEDED(Method<GetCoordinate>(vector,0x28)(vector,&y));
    Method<Release>(vector,8)(vector);
    if(ok) center={x,y};
    return ok;
}
bool CanvasBounds(void* canvas,RECT& bounds) {
    const auto cached=sceneryCanvases.find(canvas);
    if(cached!=sceneryCanvases.end()){bounds=cached->second;return true;}
    using Size=HRESULT (__stdcall*)(void*,unsigned*);
    using Origin=HRESULT (__stdcall*)(void*,int*);
    unsigned w=0,h=0;int x=0,y=0;
    if(FAILED(Method<Size>(canvas,0x40)(canvas,&w)) || FAILED(Method<Size>(canvas,0x48)(canvas,&h)) ||
        FAILED(Method<Origin>(canvas,0x6C)(canvas,&x)) || FAILED(Method<Origin>(canvas,0x74)(canvas,&y)) ||
        !w || !h || w>8192 || h>8192 || x<-1000000 || x>1000000 || y<-1000000 || y>1000000) return false;
    bounds={-x,-y,int(w)-x,int(h)-y};sceneryCanvases.emplace(canvas,bounds);return true;
}
void AddScenery(std::vector<RECT>& destination,RECT local,int x,int y,bool flip) {
    if(x<-1000000 || x>1000000 || y<-1000000 || y>1000000 || destination.size()>=32768) {
        sceneryValid=false;return;
    }
    const LONG left=flip ? x-local.right:x+local.left;
    const LONG right=flip ? x-local.left:x+local.right;
    destination.push_back({left,y+local.top,right,y+local.bottom});
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
    ProjectionScope(void* dev, double scale, const RECT& clip, double dx=0, double dy=0,double verticalScale=0) : device(dev) {
        if (!dev || FAILED(Method<GetTransform>(dev,0x98)(dev,3,&saved))) return;
        double sx=scale,sy=verticalScale>0 ? verticalScale:scale;
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
            // A smaller viewport clips both axes without squeezing the world.
            sx*=double(savedViewport.width)/cropped.width;
            sy*=double(savedViewport.height)/cropped.height;
        }
        Matrix projection = saved;
        for (int row=0; row<4; ++row) {
            projection.m[row*4] *= static_cast<float>(sx);
            projection.m[row*4+1] *= static_cast<float>(sy);
        }
        // Row-vector projection: translate by physical render pixels after
        // scaling. Include the homogeneous W column for non-identity matrices.
        const double tx=2*dx/(clip.right-clip.left),ty=-2*dy/(clip.bottom-clip.top);
        for(int row=0;row<4;++row) {
            projection.m[row*4]+=float(tx*saved.m[row*4+3]);
            projection.m[row*4+1]+=float(ty*saved.m[row*4+3]);
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
    // Screen roots are negative too. TemporaryStatView creates both its icon
    // (7B406D) and cooldown (7B4310) at C006156C, 100 below the status bar.
    // Boss HP is assigned C00614A5 at 00533AF8. Native C00614A4 is
    // shared with teleport (00958083) and must remain world-scaled.
    // Exclude only these roots; adjacent field-effect bands remain world.
    const int z=root ? *reinterpret_cast<int*>(static_cast<unsigned char*>(root)+0x44):0;
    const bool world=root && z<int(0xC00615D0u) && z!=int(0xC006156Cu) && z!=int(0xC00614A5u) && context && HasScene();
    const double zoom=world ? DrawScale():1.0;
    double scale=1.0,dx=0,dy=0,verticalScale=0;
    if(world) {
        const auto entry=std::find_if(activeBackdrops.begin(),activeBackdrops.end(),
            [z](const Backdrop& b){return b.z==z;});
        if(entry!=activeBackdrops.end()) {
            if(!entry->front) {
                // Leafre-only trial: retain native dimensions on every backdrop.
                // Exact scene matching keeps the surrounding forest unchanged.
                if(!activePolicy || !activePolicy->uniformBackground)
                    scale=(std::max)(scale,activeView.height/600.0);
                // LoadBack's static/tiled vectors use Ratio(center,100,100,rx,ry).
                // Grow artwork and tile spacing, not camera-driven motion.
                // Auto-scroll types 4..7 have a different time-based vector path.
                POINT center{};
                if(entry->type>=0 && entry->type<=3 && CameraCenter(center)) {
                    dx=(1-scale)*center.x*entry->rx/100.0;
                    dy=(1-scale)*center.y*entry->ry/100.0;
                    if(activePolicy) {
                        const int index=(z-int(0xBFFE0C00u))/1000;
                        const bool left=activePolicy->leftIndex>=0 && index>=activePolicy->leftIndex && index<=activePolicy->leftIndex+1;
                        const bool right=activePolicy->rightIndex>=0 && index>=activePolicy->rightIndex && index<=activePolicy->rightIndex+1;
                        // Move the complete upper/lower wall pair by one offset.
                        // Both authored cut edges must remain outside the view.
                        if(left || right) dx+=WorldViewport::BackdropSideOffset(activeView.width,scale,
                            right ? activePolicy->rightEdge:activePolicy->leftEdge,entry->rx,center.x,right);
                    }
                    if(activePolicy && activePolicy->skyIndex>=0 &&
                        z==int(0xBFFE0C00u)+activePolicy->skyIndex*1000) {
                        verticalScale=scale;
                        WorldViewport::CoverBackdrop(activeView.height,activePolicy->skyTop,
                            activePolicy->skyBottom,entry->ry,center.y,verticalScale,dy);
                    }
                }
            } else {
                dx=WorldViewport::ForegroundEdgeOffset(activeBounds,activeView.width,
                    entry->x,entry->rx,entry->type);
                // Audited Leafre minar/ani/0..4 have hard top edges, but fade
                // naturally at the bottom. Translate only when the top enters
                // the visible area; retain native dimensions and horizontal motion.
                if(activePolicy && activePolicy->map==240000000) {
                    const int index=(z-int(0xC0042360u))/1000-17;
                    static const int originTop[]={-376,-304,-365,-363,-310};
                    POINT center{};
                    if(index>=0 && index<5 && CameraCenter(center))
                        dy=WorldViewport::ForegroundTopOffset(activeView.height,
                            entry->y+originTop[index],entry->ry,center.y);
                }
            }
        }
    }
    // All corrections above operate in logical world pixels. Apply the common
    // zoom last, including translations, so parallax and joined art agree.
    scale*=zoom; dx*=zoom; dy*=zoom;
    if(verticalScale>0) verticalScale*=zoom;
    const bool clipped=world && (activeView.clip.left || activeView.clip.top ||
        activeView.clip.right!=renderWidth || activeView.clip.bottom!=renderHeight);
    if (!world || (scale<=1.0 && verticalScale<=1.0 && !clipped && dx==0 && dy==0)) {
        originalDrawLayer(layer,context);
#ifdef WORLD_VIEWPORT_TESTING
        if (drawObserver) drawObserver(layer,context,1.0);
#endif
        return;
    }
    ProjectionScope projection(*static_cast<void**>(context),scale,activeView.clip,dx,dy,verticalScale);
    if (!projection.applied) { originalDrawLayer(layer,context); return; }
    // Preserve native point/automatic sampling and correct the native 1:1 UV
    // bias only inside this draw. No cached vertices/textures are modified.
    TexelScope texels(scale>1.0 || verticalScale>1.0);
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
#ifdef WORLD_VIEWPORT_TESTING
void SetBuffCountForTesting(int count) {testBuffCount=count;}
void SetSceneScaleForTesting(double scale) { sceneScale=scale; }
#endif
MapShape Classify(RECT bounds) {
    const int64_t w=int64_t(bounds.right)-bounds.left,h=int64_t(bounds.bottom)-bounds.top;
    if(w<=16 || h<=16) return MapShape::Normal;
    const int64_t iw=(w-16)/2*2,ih=(h-16)/2*2;
    // Classification controls scenery-envelope expansion only; every map
    // keeps native actor size regardless of its shape or render resolution.
    if(ih<=540 && iw*9>=ih*16) return MapShape::ShortWide;
    // Narrower than the established 720p world width, and actually taller
    // than wide. A small 800x600 room is not a tall map.
    if(w<=1280 && h>w) return MapShape::TallNarrow;
    return MapShape::Normal;
}
namespace {
View FitRegion(RECT bounds,int width,int height) {
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
    // Map dimensions must never change actor size. Center finite scenes
    // with margins; the final window presentation scales all maps equally.
    view.scale=sceneScale;
    view.width=(std::min)(double(width)/view.scale,innerWidth);
    view.height=(std::min)(double(height)/view.scale,innerHeight);
    const int visibleWidth=int(std::floor(view.width*view.scale+1e-8)),visibleHeight=int(std::floor(view.height*view.scale+1e-8));
    const int left=(width-visibleWidth)/2,top=(height-visibleHeight)/2;
    view.clip={left,top,left+visibleWidth,top+visibleHeight};
    const double hw=view.width/2, hh=view.height/2;
    constexpr double epsilon=1e-8; // Floating-point roundoff at an integer limit.
    view.camera = {LONG(std::ceil(bounds.left+hw-epsilon)), LONG(std::ceil(bounds.top+hh-epsilon)),
        LONG(std::floor(bounds.right-hw+epsilon)), LONG(std::floor(bounds.bottom-hh+epsilon))};
    return view;
}
}
View Fit(RECT bounds,int width,int height) {return FitRegion(bounds,width,height);}
View FitScenery(RECT bounds,int width,int height,const std::vector<RECT>& scenery) {
    const auto shape=Classify(bounds);
    if(shape!=MapShape::ShortWide) return Fit(bounds,width,height);
    for(const auto& r:scenery) {
        if(r.left>=r.right || r.top>=r.bottom || r.right<=bounds.left || r.left>=bounds.right) continue;
        bounds.top=(std::min)(bounds.top,r.top);bounds.bottom=(std::max)(bounds.bottom,r.bottom);
    }
    return FitRegion(bounds,width,height);
}
bool ConstrainBottom(View& view,int bottom) {
    if(view.height<=0 || view.width<=0 || bottom>=view.camera.bottom+view.height/2) return false;
    const LONG limit=LONG(std::floor(bottom-view.height/2));
    if(limit>=view.camera.top) {view.camera.bottom=limit;return true;}
    // Preserve the authored top edge and native actor size on finite scenes.
    const double top=view.camera.top-view.height/2;
    const int height=2*int(std::floor((bottom-top)/2));
    if(height<=0 || height>=view.height) return false;
    const int screenHeight=view.clip.top+view.clip.bottom;
    const int pixels=int(std::floor(height*view.scale+1e-8));
    view.height=height;view.clip.top=(screenHeight-pixels)/2;view.clip.bottom=view.clip.top+pixels;
    view.camera.top=view.camera.bottom=LONG(top+height/2);
    return true;
}
void CoverBackdrop(double height,int top,int bottom,int ry,int cameraY,double& sy,double& dy) {
    if(height<=0 || bottom<=top) return;
    // Only allowlisted near-opaque sky gradients use this vertical extension.
    // Horizontal tiling and camera parallax remain unchanged; never scale actors.
    sy=(std::max)(sy,(height+2)/(bottom-top));
    const double parallax=cameraY*ry/100.0;
    const double low=height/2+1-sy*bottom-parallax;
    const double high=-height/2-1-sy*top-parallax;
    const double shift=(std::min)(high,(std::max)(low,0.0));
    dy=(1-sy)*parallax+shift;
}
bool ConstrainGround(View& view, RECT bounds, const std::vector<GroundTile>& tiles,bool allowFinite) try {
    // Henesys Park has a deliberate 180px ravine in its bottom tile row.
    // Its native fallback also leaves no vertical camera travel at 1080p.
    // Restrict this exception to the audited bounds; arbitrary platform gaps
    // elsewhere are not evidence of a safe map-wide bottom.
    const bool park=bounds.left==-118 && bounds.top==-56 && bounds.right==4708 && bounds.bottom==1010;
    if (view.width<=0 || view.height<=0 || (!park && !allowFinite && view.camera.top>=view.camera.bottom)) return false;
    std::map<std::pair<int,int>,std::vector<GroundTile>> rows;
    for (const auto& tile:tiles) {
        if (tile.right>tile.left && tile.solidBottom>tile.top)
            rows[{tile.layer,tile.top}].push_back(tile);
    }
    int bestBottom=INT_MIN;
    const double oldBottom=view.camera.bottom+view.height/2;
    for (auto& entry:rows) {
        auto& row=entry.second;
        std::sort(row.begin(),row.end(),[](const GroundTile& a,const GroundTile& b){return a.left<b.left;});
        int left=row[0].left,right=row[0].right,solidBottom=row[0].solidBottom;
        int edgeWidth=row[0].right-row[0].left, count=1,gaps=0;
        auto consider=[&]() {
            // The native fallback adds 100 below the foothold MBR. Correct
            // only that near-boundary band, not intentional deep empty space.
            const int bottom=solidBottom-1; // Keep the first transparent row outside.
            if (count<2 || right-left<view.width || gaps*20>right-left ||
                left>bounds.left+edgeWidth/2 || right<bounds.right-edgeWidth/2 ||
                bottom>=oldBottom || oldBottom-bottom>100) return;
            bestBottom=(std::max)(bestBottom,bottom);
        };
        for (size_t i=1;i<row.size();++i) {
            if (row[i].left>right && !(park && row[i].left-right<=180)) {consider();left=row[i].left;right=row[i].right;solidBottom=row[i].solidBottom;edgeWidth=row[i].right-row[i].left;count=1;gaps=0;}
            else {gaps+=(std::max)(0,row[i].left-right);right=(std::max)(right,row[i].right);solidBottom=(std::min)(solidBottom,row[i].solidBottom);edgeWidth=(std::min)(edgeWidth,row[i].right-row[i].left);++count;}
        }
        consider();
    }
    if (bestBottom==INT_MIN) return false;
    if(allowFinite) return ConstrainBottom(view,bestBottom);
    if(park && bestBottom-view.height/2<view.camera.top) {
        const double top=view.camera.top-view.height/2;
        const int height=2*int(std::floor((bestBottom-top)/2));
        if(height<=0 || height>=view.height) return false;
        const int screenHeight=view.clip.top+view.clip.bottom;
        view.height=height;
        view.clip.top=(screenHeight-height)/2;view.clip.bottom=view.clip.top+height;
        view.camera.top=view.camera.bottom=LONG(top+height/2);
        return true;
    }
    const int limit=int(std::floor(bestBottom-view.height/2));
    // Preserve both magnification and the existing upper limit. A scene with
    // insufficient vertical camera travel cannot be fixed by moving it alone.
    if (limit<view.camera.top || limit>=view.camera.bottom) return false;
    view.camera.bottom=limit;
    return true;
} catch (...) {
    // Allocation failure during a map load must keep the existing camera.
    return false;
}
void __cdecl BeginTerrain(void* map) {
    terrainMap=map;terrainValid=graphicsReady && renderHeight>720;
    groundTiles.clear();groundCanvases.clear();
    sceneryMap=map;sceneryValid=terrainValid;
    sceneryTiles.clear();sceneryObjects.clear();sceneryCanvases.clear();sceneryAnimations.clear();
}
void __cdecl BeginObjects(void* map) {
    if(sceneryMap!=map) {
        sceneryMap=map;sceneryTiles.clear();sceneryCanvases.clear();
        sceneryValid=graphicsReady && renderHeight>720;
    }
    sceneryObjects.clear();sceneryAnimations.clear();
}
void CollectSceneObject(void* map,void* property,int x,int y,bool flip,int moveType) try {
    if(!sceneryValid || sceneryMap!=map || !property || moveType) return;
    auto found=sceneryAnimations.find(property);
    if(found==sceneryAnimations.end()) {
        using Count=HRESULT (__stdcall*)(void*,unsigned*);
        using Item=HRESULT (__stdcall*)(void*,BSTR,VARIANT*);
        unsigned count=0;
        if(FAILED(Method<Count>(property,0x20)(property,&count)) || !count || count>1024){sceneryValid=false;return;}
        const GUID canvasId={0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};
        RECT total{};bool any=false;
        for(unsigned i=0;i<count;++i) {
            wchar_t number[16];swprintf_s(number,L"%u",i);BSTR key=SysAllocString(number);
            if(!key){sceneryValid=false;return;}
            VARIANT frame{};const HRESULT hr=Method<Item>(property,0x14)(property,key,&frame);SysFreeString(key);
            if(FAILED(hr)){VariantClear(&frame);sceneryValid=false;return;}
            IUnknown* value=frame.vt==VT_UNKNOWN ? frame.punkVal:frame.vt==VT_DISPATCH ? frame.pdispVal:nullptr;
            if(!value){VariantClear(&frame);break;}
            IUnknown* canvas=nullptr;RECT box{};
            const bool ok=SUCCEEDED(value->QueryInterface(canvasId,reinterpret_cast<void**>(&canvas)));
            bool readable=false;
            // Release both owned references even if a metadata allocation fails.
            try {if(ok)readable=CanvasBounds(canvas,box);}
            catch (...) {if(canvas)canvas->Release();VariantClear(&frame);throw;}
            if(canvas)canvas->Release();VariantClear(&frame);
            if(!readable){sceneryValid=false;return;}
            if(!any)total=box;
            else {total.left=(std::min)(total.left,box.left);total.top=(std::min)(total.top,box.top);
                total.right=(std::max)(total.right,box.right);total.bottom=(std::max)(total.bottom,box.bottom);}
            any=true;
        }
        if(!any){sceneryValid=false;return;}
        found=sceneryAnimations.emplace(property,total).first;
    }
    AddScenery(sceneryObjects,found->second,x,y,flip);
} catch (...) {sceneryValid=false;}
void __cdecl RecordSceneObject(int* stack,void* map) {
    // CMapLoadable::MakeObjLayer entry 63C212: return address, sret,
    // resolved animation property, layer, x, y, z, flip, rx, ry, moveType...
    CollectSceneObject(map,reinterpret_cast<void*>(stack[2]),stack[4],stack[5],stack[7]!=0,stack[10]);
}
void CollectTerrainTile(void* map,int layer,void* property,void* canvas,int x,int y) {
    if(sceneryValid && sceneryMap==map && canvas && layer>=0 && layer<=7) {
        try {RECT box{};if(CanvasBounds(canvas,box))AddScenery(sceneryTiles,box,x,y,false);else sceneryValid=false;}
        catch (...) {sceneryValid=false;}
    }
    if (!terrainValid || terrainMap!=map || !property || !canvas || layer<0 || layer>7) return;
    VARIANT type{};BSTR name=SysAllocString(L"u");
    if (!name) {terrainValid=false;return;}
    using Item=HRESULT (__stdcall*)(void*,BSTR,VARIANT*);
    const HRESULT hr=Method<Item>(property,0x14)(property,name,&type);
    SysFreeString(name);
    const bool end=SUCCEEDED(hr) && type.vt==VT_BSTR && type.bstrVal && wcscmp(type.bstrVal,L"enH1")==0;
    VariantClear(&type);
    if (FAILED(hr)) {terrainValid=false;return;}
    if (!end) return;
    try {
        auto found=groundCanvases.find(canvas);
        if (found==groundCanvases.end()) {
            GroundCanvas meta;unsigned width=0,height=0;
            using Size=HRESULT (__stdcall*)(void*,unsigned*);
            using Origin=HRESULT (__stdcall*)(void*,int*);
            using Pixel=HRESULT (__stdcall*)(void*,int,int,unsigned*);
            if (FAILED(Method<Size>(canvas,0x40)(canvas,&width)) ||
                FAILED(Method<Size>(canvas,0x48)(canvas,&height)) ||
                FAILED(Method<Origin>(canvas,0x6C)(canvas,&meta.originX)) ||
                FAILED(Method<Origin>(canvas,0x74)(canvas,&meta.originY))) {terrainValid=false;return;}
            // Bound load-time inspection. Unknown/oversized end caps are not
            // evidence that a map has a safe continuous bottom edge.
            if (!width || width>512 || !height || height>512) {terrainValid=false;return;}
            meta.width=int(width);
            for (unsigned row=0;row<height;++row) {
                bool solid=true;
                for (unsigned col=0;col<width;++col) {
                    unsigned color=0;
                    if (FAILED(Method<Pixel>(canvas,0x88)(canvas,int(col),int(row),&color))) {terrainValid=false;return;}
                    if ((color>>24)!=255) {solid=false;break;}
                }
                if (!solid) break;
                ++meta.solidRows;
            }
            found=groundCanvases.emplace(canvas,meta).first;
        }
        const auto& m=found->second;
        if (m.solidRows) groundTiles.push_back({layer,x-m.originX,y-m.originY,x-m.originX+m.width,y-m.originY+m.solidRows});
    } catch (...) {terrainValid=false;groundTiles.clear();groundCanvases.clear();}
}
void __cdecl RecordTerrainTile(int* frame,int y) {
    // Guarded CMapLoadable::LoadTile at 63A843, before CreateLayer.
    CollectTerrainTile(reinterpret_cast<void*>(frame[-0x24/4]),frame[2],
        reinterpret_cast<void*>(frame[4]),reinterpret_cast<void*>(frame[-0x14/4]),frame[-0x20/4],y);
}
POINT Inverse(POINT screen, int width, int height, double scale) {
    if (!std::isfinite(scale) || scale <= 1.0) return screen;
    return {LONG(std::lround(width/2.0+(screen.x-width/2.0)/scale)),
        LONG(std::lround(height/2.0+(screen.y-height/2.0)/scale))};
}
void Configure(int width, int height) {
    renderWidth=width; renderHeight=height; activeMap=nullptr; activeView={};
    loadingMap=nullptr; loadingBackdrops.clear(); activeBackdrops.clear();
    terrainMap=nullptr;terrainValid=false;groundTiles.clear();groundCanvases.clear();
    sceneryMap=nullptr;sceneryValid=false;sceneryTiles.clear();sceneryObjects.clear();
    sceneryCanvases.clear();sceneryAnimations.clear();
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
    Log("World viewport ready: uniform 1.5x world, scoped scenery, independent HUD and backdrop motion.");
    return true;
}
void __cdecl AdjustCamera(void* map) {
    activeMap=nullptr; activeView={}; activeBackdrops.clear();activePolicy=nullptr;
    if (!graphicsReady || renderHeight <= 720 || !IsField(map)) return;
    auto camera = reinterpret_cast<RECT*>(static_cast<unsigned char*>(map)+0xF0);
    // Before native short-map clamping, recover the exact bounds the engine
    // resolved, including linked maps and the native foothold fallback.
    RECT bounds = {camera->left-renderWidth/2,camera->top-renderHeight/2,
        camera->right+renderWidth/2,camera->bottom+renderHeight/2};
    auto view=Fit(bounds,renderWidth,renderHeight);
    size_t artCount=0;
    if(sceneryValid && sceneryMap==map && Classify(bounds)==MapShape::ShortWide) {
        try {
            auto rectangles=sceneryTiles;rectangles.insert(rectangles.end(),sceneryObjects.begin(),sceneryObjects.end());
            view=FitScenery(bounds,renderWidth,renderHeight,rectangles);artCount=rectangles.size();
        } catch (...) {sceneryValid=false;}
    }
    if (view.width<=0 || view.height<=0 || bounds.right-bounds.left<=16 || bounds.bottom-bounds.top<=16) return;
    activePolicy=loadingMap==map ? MatchPolicy(bounds,groundTiles):nullptr;
    const bool groundAdjusted=terrainMap==map && terrainValid && ConstrainGround(view,bounds,groundTiles,activePolicy && activePolicy->ground);
    if(activePolicy && activePolicy->bottom) ConstrainBottom(view,activePolicy->bottom);
    // Audited Leafre minar/back/27: y=577, originY=31, height=63.
    // This horizontal scrolling foreground terminates at world Y=609.
    // Keep its bottom outside the viewport instead of exposing terrain below
    // the river. Bounds plus the full placement signature limit the policy
    // to this asset arrangement; do not anchor other moving effects.
    if(loadingMap==map && bounds.left==-2385 && bounds.top==-960 &&
        bounds.right==2834 && bounds.bottom==666) {
        for(const auto& b:loadingBackdrops) if(b.front && b.z==int(0xC0042360u)+22000 &&
            b.x==-45 && b.y==577 && b.rx==5 && b.ry==-100 && b.type==4) {
            const LONG limit=LONG(std::floor(608-view.height/2));
            if(limit>=view.camera.top && limit<view.camera.bottom)view.camera.bottom=limit;
        }
    }
    groundCanvases.clear(); // Never retain canvas identities beyond the load.
    sceneryCanvases.clear();sceneryAnimations.clear();
    *camera=view.camera; activeView=view; activeMap=map; activeBounds=bounds;
    if (loadingMap==map) activeBackdrops=loadingBackdrops;
    if(activePolicy) {
        char policyLine[100];sprintf_s(policyLine,"confirmedPolicy=%d layers=%u caps=%u",
            activePolicy->map,unsigned(loadingScene.size()),unsigned(groundTiles.size()));Log(policyLine);
    }
    char line[400]; sprintf_s(line,"bounds=(%ld,%ld,%ld,%ld) view=%.2fx%.2f zoom=%.4f camera=(%ld,%ld,%ld,%ld) clip=(%ld,%ld,%ld,%ld) backdrops=%u groundCamera=%d shape=%d scenery=%u",
        bounds.left,bounds.top,bounds.right,bounds.bottom,view.width,view.height,view.scale,
        camera->left,camera->top,camera->right,camera->bottom,
        view.clip.left,view.clip.top,view.clip.right,view.clip.bottom,unsigned(activeBackdrops.size()),groundAdjusted,int(Classify(bounds)),unsigned(artCount)); Log(line);
}
void __cdecl AdjustWorldCursor(POINT* screen) { if (screen) *screen=MapPoint(screen->x,screen->y); }
void __cdecl BeginBackgrounds(void* map) {
    loadingMap=map; loadingBackdrops.clear();loadingScene.clear();
    if (map==activeMap) activeBackdrops.clear();
}
double ForegroundEdgeOffset(RECT bounds,double visibleWidth,int x,int rx,int type) {
    // Only non-repeating, parallax foreground pieces near a map side. Move
    // vertically joined pieces together; never move scenery attached at -100.
    if(type!=0 || rx<=-100 || rx>=0 || visibleWidth<=800 ||
        int64_t(bounds.right)-bounds.left<=800) return 0;
    const bool left=std::abs(double(x)-bounds.left)<400;
    const bool right=std::abs(double(x)-bounds.right)<400;
    if(left==right) return 0;
    const double amount=(visibleWidth-800)*(100+rx)/200.0;
    return left ? -amount:amount;
}
double ForegroundTopOffset(double visibleHeight,int top,int ry,int cameraY) {
    if(visibleHeight<=0) return 0;
    return (std::min)(0.0,-visibleHeight/2-1-(top+cameraY*ry/100.0));
}
double BackdropSideOffset(double visibleWidth,double scale,int edge,int rx,int cameraX,bool right) {
    if(visibleWidth<=0 || scale<=0) return 0;
    const double position=scale*edge+cameraX*rx/100.0;
    return right ? (std::max)(0.0,visibleWidth/2+2-position):
        (std::min)(0.0,-visibleWidth/2-2-position);
}
bool RegisterBackground(void* map,int index,int front,int ry,int x,int rx,int type,int y) {
    if (!graphicsReady || renderHeight<=720 || !IsField(map)) return false;
    if (loadingMap!=map) BeginBackgrounds(map);
    if(index>=0 && index<128 && loadingScene.size()<128)
        loadingScene.push_back({index,front!=0,x,y,rx,ry,type});
    if(index>=0 && index<128 && ((!front && ry>-100) || (front && type==0 && rx>-100 && rx<0) ||
        (front && type==4 && ry==-100))) {
        const int z=int(front ? 0xC0042360u:0xBFFE0C00u)+index*1000;
        const Backdrop entry{z,x,rx,ry,type,front!=0,y};
        loadingBackdrops.push_back(entry);
        if(map==activeMap) activeBackdrops.push_back(entry);
    }
    return true;
}
void __fastcall MouseButton(void*,void*,unsigned message,unsigned flags,int x,int y) {
    auto handler=Handler(); if (!handler || !AcceptPoint(x,y)) return;
    POINT p=MapPoint(x,y);
    Method<void (__thiscall*)(void*,unsigned,unsigned,int,int)>(handler,8)(handler,message,flags,p.x,p.y);
}
bool BuffHit(POINT p,int count,int width) {
    // CTemporaryStatView::ShowToolTip: [23,55), 32 pixels per icon.
    return count>0 && count<=128 && p.y>=23 && p.y<55 &&
        p.x>=width-6-count*32 && p.x<width-6;
}
static bool OverBuff(int x,int y) {
#ifdef WORLD_VIEWPORT_TESTING
    const int count=testBuffCount;
#else
    auto ctx=*reinterpret_cast<unsigned char**>(0x00BE7918);
    const int count=ctx ? *reinterpret_cast<int*>(ctx+0x2EA8+0xC):0;
#endif
    return BuffHit({x,y},count,renderWidth);
}
int __fastcall MouseMove(void*,void*,int x,int y) {
    auto handler=Handler(); if (!handler || (!AcceptPoint(x,y) && !OverBuff(x,y))) return 0;
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
View GetViewForTesting() {return activeView;}
int GetPolicyForTesting() {return activePolicy ? activePolicy->map:0;}
void SetGroundForTesting(void* map,const std::vector<GroundTile>& tiles) {terrainMap=map;terrainValid=true;groundTiles=tiles;}
void SetContextForTesting(void** slot,void* map,RECT bounds,int width,int height) {
    Configure(width,height); stageSlot=slot; activeMap=map;activePolicy=nullptr;
    activeView=Fit(bounds,width,height); activeBounds=bounds; graphicsReady=true;
}
void DrawLayerForTesting(void* layer,void* context,DrawLayer original) {
    layerVtable=*static_cast<uintptr_t*>(layer); originalDrawLayer=original;
    RenderLayer(layer,nullptr,context);
}
void SetGraphicsForTesting(void* graphics) { testGraphics=graphics; }
void SetObserverForTesting(void (*observer)(void*,void*,double)) { drawObserver=observer; }
void SetTexelAlignmentForTesting(bool enabled) { testTexelAlignment=enabled; }
#endif
}
