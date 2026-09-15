#include "WorldViewport.h"
#include "AdaptiveLayout.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {
struct Matrix { float m[16]; };
struct Viewport { unsigned x=0,y=0,width=1920,height=1080;float minZ=0,maxZ=1; };
void* methods[42]{};
struct Device { void** vtable=methods; Matrix projection{}; int writes=0, failure=0, viewportWrites=0; Viewport viewport; };
HRESULT __stdcall Get(Device* d,unsigned type,Matrix* m) {
    assert(type==3); if(d->failure==1) return E_FAIL; *m=d->projection; return S_OK;
}
HRESULT __stdcall Set(Device* d,unsigned type,const Matrix* m) {
    assert(type==3); if(d->failure==2) return E_FAIL; d->projection=*m; ++d->writes; return S_OK;
}
HRESULT __stdcall GetViewport(Device* d,Viewport* v) { if(d->failure==3)return E_FAIL;*v=d->viewport;return S_OK; }
HRESULT __stdcall SetViewport(Device* d,const Viewport* v) { if(d->failure==4)return E_FAIL;d->viewport=*v;++d->viewportWrites;return S_OK; }
struct Layer { alignas(4) unsigned char bytes[0x100]{};
    Layer(int z,Layer* parent=nullptr) {
        *reinterpret_cast<uintptr_t*>(bytes)=0x123456;
        *reinterpret_cast<Layer**>(bytes+0x30)=parent;
        *reinterpret_cast<int*>(bytes+0x44)=z;
        *reinterpret_cast<int*>(bytes+0x4C)=1;
    }
};
double expectedScale=1;
int calls=0; bool throwDraw=false;
Matrix originalProjection;
void __fastcall Draw(void* layer,void*,void* context) {
    auto d=*static_cast<Device**>(context); ++calls;
    for(int r=0;r<4;++r) for(int c=0;c<4;++c)
        assert(std::abs(d->projection.m[r*4+c]-originalProjection.m[r*4+c]*(c==0 ? expectedScale*1920/d->viewport.width : c==1 ? expectedScale*1080/d->viewport.height:1))<0.0001);
    assert(*reinterpret_cast<int*>(static_cast<unsigned char*>(layer)+0x4C)==1);
    if(throwDraw) throw std::runtime_error("draw failure");
}
int inputX=0,inputY=0;
int __fastcall Move(void*,void*,int x,int y) { inputX=x;inputY=y;return 42; }
void __fastcall Button(void*,void*,unsigned m,unsigned f,int x,int y) {
    assert(m==0x203 && f==7);inputX=x;inputY=y;
}
int __fastcall Wheel(void*,void*,int x,int y,int n) { assert(n==120);return Move(nullptr,nullptr,x,y); }
void __fastcall Drag(void*,void*,int s,void* p,int x,int y) {
    assert(s==2 && p==reinterpret_cast<void*>(9));Move(nullptr,nullptr,x,y);
}
int __fastcall IsField(void*,void*,const void* rtti) { assert(rtti==reinterpret_cast<void*>(0xBED758));return 1; }
void CheckBounds(RECT r,int width,int height) {
    auto v=WorldViewport::Fit(r,width,height);
    assert(v.scale>=1 && std::isfinite(v.scale));
    assert(v.height*v.scale<=height+1e-8 && v.width*v.scale<=width+1e-8);
    assert(std::abs(v.clip.right-v.clip.left-v.width*v.scale)<2.001);
    assert(std::abs(v.clip.bottom-v.clip.top-v.height*v.scale)<1e-8);
    assert(v.clip.left>=0 && v.clip.right<=width && v.clip.top>=0 && v.clip.bottom<=height);
    assert(std::abs(v.clip.left-(width-v.clip.right))<=1);
    assert(std::abs(v.clip.top-(height-v.clip.bottom))<=1);
    assert(v.camera.left<=v.camera.right && v.camera.top<=v.camera.bottom);
    for(LONG x:{v.camera.left,v.camera.right}) for(LONG y:{v.camera.top,v.camera.bottom}) {
        assert(x-v.width/2>=r.left+8-1e-8 && x+v.width/2<=r.right-8+1e-8);
        assert(y-v.height/2>=r.top+8-1e-8 && y+v.height/2<=r.bottom-8+1e-8);
    }
    // Forward-transform followed by the exact production input inverse: error
    // is bounded by one authored pixel even at both edges of the viewport.
    for(int x:{0,width/4,width/2,width-1}) for(int y:{0,height/4,height/2,height-1}) {
        POINT p=WorldViewport::Inverse({x,y},width,height,v.scale);
        assert(std::abs((p.x-width/2.0)*v.scale+width/2.0-x)<=v.scale/2+0.001);
        assert(std::abs((p.y-height/2.0)*v.scale+height/2.0-y)<=v.scale/2+0.001);
    }
}
}
int main(int argc,char** argv) {
    static_assert(sizeof(void*)==4,"Native x86 ABI");
    for(RECT r: {RECT{-809,-633,2765,179},RECT{-2740,-748,809,179},RECT{-400,-300,400,300},
        RECT{-3600,-1700,2700,255},RECT{-400,-6000,400,595},RECT{-3045,-183,1326,287},
        RECT{-449,-274,989,114},RECT{-9,-9,9,9}})
        for(auto size:{POINT{1920,1080},POINT{2560,1440},POINT{3840,2160},POINT{1919,1079}}) CheckBounds(r,size.x,size.y);
    assert(WorldViewport::Fit({-3600,-1700,2700,255},1920,1080).scale==1);
    auto flight=WorldViewport::Fit({-809,-633,2765,179},1920,1080);
    assert(std::abs(flight.height-796)<1e-8 && flight.camera.top==-227 && flight.camera.bottom==-227);
    assert(flight.clip.top==0 && flight.clip.bottom==1080 && flight.clip.right==1920);
    assert(std::abs(flight.scale-1080.0/796)<1e-8); // Accepted normal-map rule.
    auto doors=WorldViewport::Fit({-3045,-183,1326,287},1920,1080);
    assert(doors.scale==1 && doors.width==1920 && doors.height==454);
    assert(doors.clip.top==313 && doors.clip.bottom==767);
    assert(WorldViewport::Fit({1,2,1,2},1920,1080).scale==1);
    void* stageMethods[19]{};
    struct { void* base=nullptr; void** handler; } stage{nullptr,stageMethods};
    void* current=&stage;
    WorldViewport::SetContextForTesting(&current,&stage,{-809,-633,2765,179},1920,1080);
    stageMethods[2]=reinterpret_cast<void*>(&Button);stageMethods[3]=reinterpret_cast<void*>(&Move);
    stageMethods[4]=reinterpret_cast<void*>(&Wheel);stageMethods[6]=reinterpret_cast<void*>(&Drag);
    const auto p=WorldViewport::Inverse({100,900},1920,1080,flight.scale);
    assert(WorldViewport::MouseMove(nullptr,nullptr,100,900)==42 && inputX==p.x && inputY==p.y);
    WorldViewport::MouseButton(nullptr,nullptr,0x203,7,100,900); assert(inputX==p.x && inputY==p.y);
    WorldViewport::MouseWheel(nullptr,nullptr,100,900,120); assert(inputX==p.x && inputY==p.y);
    WorldViewport::DragMove(nullptr,nullptr,2,reinterpret_cast<void*>(9),100,900); assert(inputX==p.x && inputY==p.y);
    POINT cursor{100,900}; WorldViewport::AdjustWorldCursor(&cursor); assert(cursor.x==p.x && cursor.y==p.y);
    methods[0x98/4]=reinterpret_cast<void*>(&Get);methods[0x94/4]=reinterpret_cast<void*>(&Set);
    methods[0xA4/4]=reinterpret_cast<void*>(&GetViewport);methods[0xA0/4]=reinterpret_cast<void*>(&SetViewport);
    Device d; for(int i=0;i<16;++i) d.projection.m[i]=float(i+1)/16; originalProjection=d.projection;
    Device* context=&d;
    Layer world(int(0xC0000000)), equipment(5,&world), ui(int(0xC00615D0)), uiChild(-1,&ui), cursorLayer(0x7ffffffd);
    Layer buff(int(0xC006156C)), cooldown(int(0xC006156C)), buffChild(int(0xC0000000),&buff);
    Layer worldChild(int(0xC006156C),&world), fieldEffect(int(0xC0061508)), adjacentWorld(int(0xC006156B));
    for(Layer* layer:{&world,&equipment,&worldChild,&fieldEffect,&adjacentWorld,&ui,&uiChild,&cursorLayer,&buff,&cooldown,&buffChild}) for(int fail=0;fail<3;++fail) {
        d.failure=fail;d.writes=0;calls=0;
        const bool isWorld=layer==&world || layer==&equipment || layer==&worldChild || layer==&fieldEffect || layer==&adjacentWorld;
        expectedScale=(!fail && isWorld) ? flight.scale:1;
        WorldViewport::DrawLayerForTesting(layer,&context,reinterpret_cast<void (__thiscall*)(void*,void*)>(&Draw));
        assert(calls==1 && !memcmp(&d.projection,&originalProjection,sizeof(Matrix)));
        assert(*reinterpret_cast<int*>(layer->bytes+0x4C)==1);
        assert(d.writes==(!fail && isWorld ? 2:0));
        assert(d.viewport.x==0 && d.viewport.y==0 && d.viewport.width==1920 && d.viewport.height==1080);
    }
    d.failure=0; expectedScale=flight.scale; throwDraw=true;
    try { WorldViewport::DrawLayerForTesting(&world,&context,reinterpret_cast<void (__thiscall*)(void*,void*)>(&Draw)); assert(false); }
    catch(const std::runtime_error&) {}
    assert(!memcmp(&d.projection,&originalProjection,sizeof(Matrix)) && *reinterpret_cast<int*>(world.bytes+0x4C)==1);
    throwDraw=false;expectedScale=flight.scale; current=nullptr;
    WorldViewport::DrawLayerForTesting(&world,&context,reinterpret_cast<void (__thiscall*)(void*,void*)>(&Draw));
    cursor={100,900};WorldViewport::AdjustWorldCursor(&cursor);assert(cursor.x==100 && cursor.y==900);
    assert(WorldViewport::MouseMove(nullptr,nullptr,100,900)==0);
    alignas(4) unsigned char map[0x110]{};
    stageMethods[18]=reinterpret_cast<void*>(&IsField);
    *reinterpret_cast<void***>(map+4)=stageMethods;
    current=map;
    WorldViewport::SetContextForTesting(&current,map,{-809,-633,2765,179},1920,1080);
    auto camera=reinterpret_cast<RECT*>(map+0xF0);
    *camera={-809+960,-633+540,2765-960,179-540};
    WorldViewport::AdjustCamera(map);
    assert(!memcmp(camera,&flight.camera,sizeof(RECT)));
    cursor={100,900};WorldViewport::AdjustWorldCursor(&cursor);assert(cursor.x==p.x && cursor.y==p.y);
    // Adjacent AquaRoad pieces end/start at -273. Both Y values must survive
    // LoadBack unchanged; their common backdrop projection preserves the join.
    WorldViewport::BeginBackgrounds(map);
    int storage[80]{};int* frame=storage+40;
    frame[-0x48/4]=reinterpret_cast<int>(map);frame[-0x70/4]=-5;frame[-0x68/4]=1;
    frame[2]=1;frame[-0x74/4]=27;AdaptiveLayout::AdjustBackground(frame,0);assert(frame[-0x74/4]==27);
    frame[2]=2;frame[-0x74/4]=-619;AdaptiveLayout::AdjustBackground(frame,0);assert(frame[-0x74/4]==-619);
    assert(WorldViewport::RegisterBackground(map,3,0,-100));
    assert(WorldViewport::RegisterBackground(map,4,1,-5));
    *camera={-809+960,-633+540,2765-960,179-540};WorldViewport::AdjustCamera(map);
    Layer lower(int(0xBFFE0C00)+1000),upper(int(0xBFFE0C00)+2000),attached(int(0xBFFE0C00)+3000);
    for(auto layer:{&lower,&upper,&attached}) {
        expectedScale=layer==&attached ? flight.scale:1.8;
        WorldViewport::DrawLayerForTesting(layer,&context,reinterpret_cast<void (__thiscall*)(void*,void*)>(&Draw));
    }
    WorldViewport::BeginBackgrounds(map); // Background-only reload, no camera rebuild.
    expectedScale=flight.scale;
    WorldViewport::DrawLayerForTesting(&lower,&context,reinterpret_cast<void (__thiscall*)(void*,void*)>(&Draw));
    WorldViewport::RegisterBackground(map,2,0,-5);expectedScale=1.8;
    WorldViewport::DrawLayerForTesting(&upper,&context,reinterpret_cast<void (__thiscall*)(void*,void*)>(&Draw));
    // Aqua plaza: centered on both axes, original viewport restored for HUD.
    auto narrow=WorldViewport::Fit({-400,-398,400,600},1920,1080);
    assert(narrow.scale==1 && narrow.clip.left==568 && narrow.clip.right==1352);
    assert(narrow.clip.top==49 && narrow.clip.bottom==1031);
    assert(narrow.width==784 && narrow.height==982);
    WorldViewport::SetContextForTesting(&current,map,{-400,-398,400,600},1920,1080);
    inputX=123;assert(WorldViewport::MouseMove(nullptr,nullptr,100,540)==0 && inputX==123);
    for(POINT outside:{POINT{567,540},POINT{1352,540},POINT{960,48},POINT{960,1031}}) {
        inputX=123;inputY=456;
        assert(WorldViewport::MouseMove(nullptr,nullptr,outside.x,outside.y)==0);
        WorldViewport::MouseButton(nullptr,nullptr,0x203,7,outside.x,outside.y);
        assert(WorldViewport::MouseWheel(nullptr,nullptr,outside.x,outside.y,120)==0);
        WorldViewport::DragMove(nullptr,nullptr,2,reinterpret_cast<void*>(9),outside.x,outside.y);
        assert(inputX==123 && inputY==456);
    }
    for(POINT inside:{POINT{568,49},POINT{1351,1030},POINT{960,540}}) {
        assert(WorldViewport::MouseMove(nullptr,nullptr,inside.x,inside.y)==42);
        const auto mapped=WorldViewport::Inverse(inside,1920,1080,narrow.scale);
        assert(inputX==mapped.x && inputY==mapped.y);
    }
    assert(WorldViewport::MouseMove(nullptr,nullptr,960,540)==42 && inputX==960);
    for(int fail=0;fail<5;++fail) {
        d.failure=fail;d.viewportWrites=0;expectedScale=fail ? 1:narrow.scale;
        WorldViewport::DrawLayerForTesting(&world,&context,reinterpret_cast<void (__thiscall*)(void*,void*)>(&Draw));
        assert(d.viewport.x==0 && d.viewport.width==1920 && d.viewport.height==1080);
        assert(!memcmp(&d.projection,&originalProjection,sizeof(Matrix)));
    }
    d.failure=0;expectedScale=1;
    WorldViewport::DrawLayerForTesting(&ui,&context,reinterpret_cast<void (__thiscall*)(void*,void*)>(&Draw));
    expectedScale=narrow.scale;throwDraw=true;
    try {WorldViewport::DrawLayerForTesting(&world,&context,reinterpret_cast<void (__thiscall*)(void*,void*)>(&Draw));assert(false);}
    catch(const std::runtime_error&) {}
    assert(d.viewport.x==0 && d.viewport.width==1920 && !memcmp(&d.projection,&originalProjection,sizeof(Matrix)));
    throwDraw=false;expectedScale=1;
    // Right-edge Buff HUD bypasses both projection and scene clipping, even
    // at scale 1 (Great Tree I) or outside a tall map's scene rectangle.
    for(RECT bounds:{RECT{-1760,370,56,2196},RECT{-360,-2500,440,450},RECT{-809,-633,2765,179}}) {
        WorldViewport::SetContextForTesting(&current,map,bounds,1920,1080);
        for(Layer* layer:{&buff,&cooldown,&buffChild}) for(int fail=0;fail<5;++fail) {
            d.failure=fail;d.writes=0;d.viewportWrites=0;calls=0;
            WorldViewport::DrawLayerForTesting(layer,&context,reinterpret_cast<void (__thiscall*)(void*,void*)>(&Draw));
            assert(calls==1 && d.writes==0 && d.viewportWrites==0);
            assert(d.viewport.x==0 && d.viewport.y==0 && d.viewport.width==1920 && d.viewport.height==1080);
        }
    }
    d.failure=0;
    WorldViewport::Configure(1920,1080); // Explicit new stage setup ends the held fade projection.
    WorldViewport::DrawLayerForTesting(&world,&context,reinterpret_cast<void (__thiscall*)(void*,void*)>(&Draw));
    int maps=0;
    if(argc==2) {
        std::ifstream file(argv[1]); assert(file);
        std::string line;
        while(std::getline(file,line)) {
            std::istringstream row(line);long id;RECT r{};
            if(row>>id>>r.left>>r.top>>r.right>>r.bottom) { CheckBounds(r,1920,1080);++maps; }
        }
        assert(maps>4000);
    }
    printf("PASS: bounded camera, projection and sampler restoration, equipment overlays, HUD isolation, world input, stage transitions; %d actual map bounds.\n",maps);
}
