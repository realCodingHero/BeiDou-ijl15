#include "WorldViewport.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {
void* canvasMethods[40]{},*propertyMethods[9]{};
struct Canvas {void** vtable=canvasMethods;unsigned refs=1,reads=0;int w=200,h=758,x=100,y=422;bool fail=false;};
struct Property {void** vtable=propertyMethods;std::vector<Canvas*> frames;unsigned counts=0,items=0;bool fail=false;};
ULONG __stdcall AddRef(Canvas* c){return ++c->refs;}
ULONG __stdcall Release(Canvas* c){assert(c->refs>1);return --c->refs;}
HRESULT __stdcall Query(Canvas* c,REFIID iid,void** out){assert(iid.Data1==0x7600dc6c);*out=c;AddRef(c);return S_OK;}
HRESULT __stdcall Width(Canvas* c,unsigned* n){++c->reads;if(c->fail)return E_FAIL;*n=c->w;return S_OK;}
HRESULT __stdcall Height(Canvas* c,unsigned* n){++c->reads;*n=c->h;return S_OK;}
HRESULT __stdcall X(Canvas* c,int* n){++c->reads;*n=c->x;return S_OK;}
HRESULT __stdcall Y(Canvas* c,int* n){++c->reads;*n=c->y;return S_OK;}
HRESULT __stdcall Count(Property* p,unsigned* n){++p->counts;*n=unsigned(p->frames.size())+1;return S_OK;}
HRESULT __stdcall Item(Property* p,BSTR key,VARIANT* value){
    ++p->items;if(p->fail)return E_FAIL;
    const unsigned index=unsigned(_wtoi(key));
    if(index>=p->frames.size()){value->vt=VT_EMPTY;return S_OK;}
    auto c=p->frames[index];AddRef(c);value->vt=VT_UNKNOWN;value->punkVal=reinterpret_cast<IUnknown*>(c);return S_OK;
}
int __fastcall IsField(void*,void*,const void*){return 1;}
bool Same(const WorldViewport::View& a,const WorldViewport::View& b){
    return a.scale==b.scale && a.width==b.width && a.height==b.height &&
        !memcmp(&a.clip,&b.clip,sizeof(RECT)) && !memcmp(&a.camera,&b.camera,sizeof(RECT));
}
}
int main(int argc,char** argv){
    using namespace WorldViewport;
    const RECT doors{-3045,-183,1326,287};
    std::vector<RECT> pillars{{-100,-422,100,336}};
    assert(Classify(doors)==MapShape::ShortWide);
    auto bare=Fit(doors,1920,1080),expanded=FitScenery(doors,1920,1080,pillars);
    assert(bare.scale==1 && bare.height==454);
    assert(std::abs(expanded.scale-1080.0/742)<1e-9 && expanded.height==742);
    assert(expanded.camera.top==-43 && expanded.camera.bottom==-43);
    assert(expanded.clip.top==0 && expanded.clip.bottom==1080);
    auto enough=FitScenery(doors,1920,1080,{{-100,-800,100,600}});
    assert(enough.scale==1 && enough.height==1080 && enough.clip.top==0);
    assert(Same(bare,FitScenery(doors,1920,1080,{})));
    assert(Same(bare,FitScenery(doors,1920,1080,{{5000,-900,6000,800},{0,2,0,9}})));
    // Other maps must remain on the accepted normal-map rule, even when
    // scenery extends well beyond VR. Do not expand their camera by accident.
    for(RECT r:{RECT{-809,-633,2765,179},RECT{-2740,-748,809,179},
        RECT{-400,-300,400,300},RECT{-1018,-587,6328,770},RECT{-1169,-1273,1230,333}}){
        assert(Classify(r)==MapShape::Normal);
        auto v=Fit(r,1920,1080);
        assert(Same(v,FitScenery(r,1920,1080,{{-10000,-10000,10000,10000}})));
        const double expected=(std::max)(1.0,1080.0/(2*((r.bottom-r.top-16)/2)));
        assert(std::abs(v.scale-expected)<1e-9);
    }
    for(RECT r:{RECT{-400,-398,400,600},RECT{-400,-6000,400,595}}){
        assert(Classify(r)==MapShape::TallNarrow);
        for(POINT output:{POINT{1920,1080},POINT{2560,1440},POINT{3840,2160},POINT{1919,1079}}){
            auto v=Fit(r,output.x,output.y);
            assert((v.clip.right-v.clip.left)*3==(v.clip.bottom-v.clip.top)*4);
            assert(std::abs(v.width/v.height-4.0/3)<1e-9);
            assert(Same(v,FitScenery(r,output.x,output.y,{{-10000,-10000,10000,10000}})));
        }
    }
    canvasMethods[0]=reinterpret_cast<void*>(&Query);canvasMethods[1]=reinterpret_cast<void*>(&AddRef);
    canvasMethods[2]=reinterpret_cast<void*>(&Release);canvasMethods[0x40/4]=reinterpret_cast<void*>(&Width);
    canvasMethods[0x48/4]=reinterpret_cast<void*>(&Height);canvasMethods[0x6C/4]=reinterpret_cast<void*>(&X);
    canvasMethods[0x74/4]=reinterpret_cast<void*>(&Y);
    propertyMethods[0x14/4]=reinterpret_cast<void*>(&Item);propertyMethods[0x20/4]=reinterpret_cast<void*>(&Count);
    alignas(4) unsigned char map[0x110]{};void* methods[19]{};methods[18]=reinterpret_cast<void*>(&IsField);
    *reinterpret_cast<void***>(map+4)=methods;void* current=map;
    SetContextForTesting(&current,map,doors,1920,1080);BeginTerrain(map);BeginObjects(map);
    Canvas canvas;Property animation;animation.frames.push_back(&canvas);
    int stack[14]{};stack[2]=reinterpret_cast<int>(&animation);stack[3]=3;
    RecordSceneObject(stack,map);stack[4]=100;RecordSceneObject(stack,map);
    assert(canvas.refs==1 && canvas.reads==4 && animation.counts==1 && animation.items==2);
    auto camera=reinterpret_cast<RECT*>(map+0xF0);
    auto restore=[&](){*camera={doors.left+960,doors.top+540,doors.right-960,doors.bottom-540};AdjustCamera(map);};
    restore();assert(!memcmp(camera,&expanded.camera,sizeof(RECT)));
    // All animation frames count, and metadata references are balanced.
    BeginTerrain(map);BeginObjects(map);Canvas taller;taller.h=900;taller.y=450;
    animation.frames.push_back(&taller);stack[4]=0;RecordSceneObject(stack,map);restore();
    auto animated=FitScenery(doors,1920,1080,{{-100,-450,100,450}});
    assert(!memcmp(camera,&animated.camera,sizeof(RECT)) && canvas.refs==1 && taller.refs==1);
    // Failed metadata invalidates the whole partial result; moving objects
    // and a different map cannot enlarge this static scenery envelope.
    BeginTerrain(map);BeginObjects(map);animation.fail=true;RecordSceneObject(stack,map);restore();
    assert(!memcmp(camera,&bare.camera,sizeof(RECT)) && canvas.refs==1);
    animation.fail=false;BeginTerrain(map);BeginObjects(map);stack[10]=1;RecordSceneObject(stack,map);restore();
    assert(!memcmp(camera,&bare.camera,sizeof(RECT)));
    BeginTerrain(map);BeginObjects(map);stack[10]=0;RecordSceneObject(stack,map+1);restore();
    assert(!memcmp(camera,&bare.camera,sizeof(RECT)));
    // Object-only reload removes the previous envelope, without stale pointers.
    BeginTerrain(map);BeginObjects(map);RecordSceneObject(stack,map);BeginObjects(map);restore();
    assert(!memcmp(camera,&bare.camera,sizeof(RECT)) && canvas.refs==1 && taller.refs==1);
    int samples=0;
    if(argc==2){
        std::ifstream f(argv[1]);assert(f);std::string line,id;RECT bounds{};std::vector<RECT> boxes;double expected=0;
        while(std::getline(f,line)){
            std::istringstream row(line);std::string kind;row>>kind;
            if(kind=="map"){row>>id>>bounds.left>>bounds.top>>bounds.right>>bounds.bottom>>expected;assert(row);boxes.clear();}
            else if(kind=="rect"){RECT b;row>>b.left>>b.top>>b.right>>b.bottom;assert(row);boxes.push_back(b);}
            else if(kind=="end"){
                auto before=Fit(bounds,1920,1080),after=FitScenery(bounds,1920,1080,boxes);
                if(Classify(bounds)!=MapShape::ShortWide)assert(Same(before,after));
                assert(std::abs(after.scale-expected)<1e-8);
                assert(after.scale>=1 && after.camera.top<=after.camera.bottom);
                printf("map %s shape=%d art=%u zoom=%.6f view=%.2fx%.2f\n",id.c_str(),int(Classify(bounds)),unsigned(boxes.size()),after.scale,after.width,after.height);++samples;
            }
        }
        assert(samples>=18);
    }
    printf("PASS scoped scenery: bounds first then fill, unchanged normal maps, tall-map 4:3, frames/cache/refcounts, failure and reload; %d asset samples.\n",samples);
}
