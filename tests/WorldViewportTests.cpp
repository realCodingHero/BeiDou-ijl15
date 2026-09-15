#include "WorldViewport.h"
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
void* methods[39]{};
struct Device { void** vtable=methods; Matrix projection{}; int writes=0, failure=0; };
HRESULT __stdcall Get(Device* d,unsigned type,Matrix* m) {
    assert(type==3); if(d->failure==1) return E_FAIL; *m=d->projection; return S_OK;
}
HRESULT __stdcall Set(Device* d,unsigned type,const Matrix* m) {
    assert(type==3); if(d->failure==2) return E_FAIL; d->projection=*m; ++d->writes; return S_OK;
}
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
        assert(std::abs(d->projection.m[r*4+c]-originalProjection.m[r*4+c]*(c<2 ? expectedScale:1))<0.0001);
    assert(*reinterpret_cast<int*>(static_cast<unsigned char*>(layer)+0x4C)==(expectedScale>1 ? 2:1));
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
    assert(std::abs(v.width/v.height-double(width)/height)<1e-10);
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
        RECT{-3600,-1700,2700,255},RECT{-400,-6000,400,595}})
        for(auto size:{POINT{1920,1080},POINT{2560,1440},POINT{3840,2160}}) CheckBounds(r,size.x,size.y);
    assert(WorldViewport::Fit({-3600,-1700,2700,255},1920,1080).scale==1);
    auto flight=WorldViewport::Fit({-809,-633,2765,179},1920,1080);
    assert(std::abs(flight.height-796)<1e-8 && flight.camera.top==-227 && flight.camera.bottom==-227);
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
    Device d; for(int i=0;i<16;++i) d.projection.m[i]=float(i+1)/16; originalProjection=d.projection;
    Device* context=&d;
    Layer world(-1000000000), equipment(5,&world), ui(10), uiChild(-1,&ui), cursorLayer(0x7ffffffd);
    for(Layer* layer:{&world,&equipment,&ui,&uiChild,&cursorLayer}) for(int fail=0;fail<3;++fail) {
        d.failure=fail;d.writes=0;calls=0;
        expectedScale=(!fail && (layer==&world || layer==&equipment)) ? flight.scale:1;
        WorldViewport::DrawLayerForTesting(layer,&context,reinterpret_cast<void (__thiscall*)(void*,void*)>(&Draw));
        assert(calls==1 && !memcmp(&d.projection,&originalProjection,sizeof(Matrix)));
        assert(*reinterpret_cast<int*>(layer->bytes+0x4C)==1);
        assert(d.writes==(expectedScale>1 ? 2:0));
    }
    d.failure=0; expectedScale=flight.scale; throwDraw=true;
    try { WorldViewport::DrawLayerForTesting(&world,&context,reinterpret_cast<void (__thiscall*)(void*,void*)>(&Draw)); assert(false); }
    catch(const std::runtime_error&) {}
    assert(!memcmp(&d.projection,&originalProjection,sizeof(Matrix)) && *reinterpret_cast<int*>(world.bytes+0x4C)==1);
    throwDraw=false;expectedScale=1; current=nullptr;
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
