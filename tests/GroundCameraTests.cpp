#include "WorldViewport.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using WorldViewport::GroundTile;
namespace {
RECT kerning{-2500,-1443,2645,800},henesys{-1018,-587,6328,770};
std::vector<GroundTile> Band(int first,int last,int top,int rows) {
    std::vector<GroundTile> out;
    for(int x=first;x<=last;x+=90)out.push_back({1,x,top,x+90,top+rows});
    return out;
}
bool Apply(RECT bounds,const std::vector<GroundTile>& tiles,int* bottom=nullptr) {
    auto v=WorldViewport::Fit(bounds,1920,1080),before=v;
    bool changed=WorldViewport::ConstrainGround(v,bounds,tiles);
    assert(v.scale==before.scale && v.width==before.width && v.height==before.height);
    assert(!memcmp(&v.clip,&before.clip,sizeof(RECT)));
    assert(v.camera.left==before.camera.left && v.camera.top==before.camera.top && v.camera.right==before.camera.right);
    assert(v.camera.bottom>=v.camera.top && v.camera.bottom<=before.camera.bottom);
    if(bottom)*bottom=v.camera.bottom;
    return changed;
}
int __fastcall IsField(void*,void*,const void*) {return 1;}
void* canvasMethods[40]{},*propertyMethods[8]{};
struct Canvas {void** vtable=canvasMethods;int reads=0;bool fail=false;};
struct Property {void** vtable=propertyMethods;const wchar_t* type=L"enH1";bool fail=false;};
HRESULT __stdcall Item(Property* p,BSTR name,VARIANT* v) {
    assert(!wcscmp(name,L"u"));if(p->fail)return E_FAIL;
    v->vt=VT_BSTR;v->bstrVal=SysAllocString(p->type);return S_OK;
}
HRESULT __stdcall Width(Canvas*,unsigned* n) {*n=90;return S_OK;}
HRESULT __stdcall Height(Canvas*,unsigned* n) {*n=30;return S_OK;}
HRESULT __stdcall Origin(Canvas*,int* n) {*n=0;return S_OK;}
HRESULT __stdcall Pixel(Canvas* c,int x,int y,unsigned* color) {
    ++c->reads;assert(x>=0 && x<90 && y>=0 && y<30);
    if(c->fail)return E_FAIL;
    *color=y<14 ? 0xffffffff:0;return S_OK;
}
}
int main(int argc,char** argv) {
    auto bricks=Band(-2475,2565,690,14),stones=Band(-990,6210,660,9);
    int bottom=0;
    assert(Apply(kerning,bricks,&bottom) && bottom==163);
    assert(Apply(henesys,stones,&bottom) && bottom==128);
    auto broken=bricks;broken.erase(broken.begin()+20);assert(!Apply(kerning,broken));
    auto floating=Band(-100,620,690,14);assert(!Apply(kerning,floating));
    auto distantBand=Band(-2475,2565,200,14);assert(!Apply(kerning,distantBand));
    auto above=Band(-2475,2565,690,0);assert(!Apply(kerning,above));
    assert(!Apply({-809,-633,2765,179},Band(-810,2700,180,14)));
    assert(!Apply({-400,-398,400,600},{}));
    // Do not trade away the upper boundary or magnification on short scenes.
    assert(!Apply({-2500,-300,2645,800},bricks));
    // A lower continuous band wins over a higher platform crossing the map.
    auto lower=bricks;auto platform=Band(-2475,2565,680,14);lower.insert(lower.end(),platform.begin(),platform.end());
    assert(Apply(kerning,lower,&bottom) && bottom==163);

    propertyMethods[0x14/4]=reinterpret_cast<void*>(&Item);
    canvasMethods[0x40/4]=reinterpret_cast<void*>(&Width);canvasMethods[0x48/4]=reinterpret_cast<void*>(&Height);
    canvasMethods[0x6C/4]=canvasMethods[0x74/4]=reinterpret_cast<void*>(&Origin);
    canvasMethods[0x88/4]=reinterpret_cast<void*>(&Pixel);
    alignas(4) unsigned char map[0x110]{};void* stageMethods[19]{};stageMethods[18]=reinterpret_cast<void*>(&IsField);
    *reinterpret_cast<void***>(map+4)=stageMethods;void* stage=map;
    WorldViewport::SetContextForTesting(&stage,map,kerning,1920,1080);
    Canvas canvas;Property property;
    WorldViewport::BeginTerrain(map);
    int storage[32]{};int* frame=storage+20;
    frame[-0x24/4]=reinterpret_cast<int>(map);frame[2]=6;
    frame[4]=reinterpret_cast<int>(&property);frame[-0x14/4]=reinterpret_cast<int>(&canvas);
    for(int x=-2475;x<=2565;x+=90){frame[-0x20/4]=x;WorldViewport::RecordTerrainTile(frame,690);}
    assert(canvas.reads==90*14+1); // Cache opacity once per native canvas/load.
    auto camera=reinterpret_cast<RECT*>(map+0xF0);
    *camera={kerning.left+960,kerning.top+540,kerning.right-960,kerning.bottom-540};
    WorldViewport::AdjustCamera(map);assert(camera->bottom==163);
    // A new load clears old coverage. Failure must not create a partial cap.
    WorldViewport::BeginTerrain(map);canvas.fail=true;
    WorldViewport::CollectTerrainTile(map,6,&property,&canvas,-2475,690);
    *camera={kerning.left+960,kerning.top+540,kerning.right-960,kerning.bottom-540};
    WorldViewport::AdjustCamera(map);assert(camera->bottom==251);
    WorldViewport::BeginTerrain(map);canvas.fail=false;property.type=L"enH0";canvas.reads=0;
    WorldViewport::CollectTerrainTile(map,6,&property,&canvas,-2475,690);assert(canvas.reads==0);

    int maps=0,adjusted=0,targets=0;
    if(argc==2) {
        std::ifstream f(argv[1]);assert(f);std::string line,id;RECT bounds{};std::vector<GroundTile> tiles;
        while(std::getline(f,line)) {
            std::istringstream row(line);std::string kind;row>>kind;
            if(kind=="map"){row>>id>>bounds.left>>bounds.top>>bounds.right>>bounds.bottom;tiles.clear();}
            else if(kind=="tile"){GroundTile t{};row>>t.layer>>t.left>>t.top>>t.right>>t.solidBottom;tiles.push_back(t);}
            else if(kind=="end") {
                const bool changed=Apply(bounds,tiles,&bottom);++maps;if(changed)++adjusted;
                if(id=="100000000" || id=="103000000") {
                    assert(changed && bottom==(id=="100000000" ? 128:163));++targets;
                    printf("actual map %s: camera.bottom=%d, scale unchanged\n",id.c_str(),bottom);
                }
                if(id=="230000001" || id=="200090500" || id=="200090510")assert(!changed);
            }
        }
        assert(targets==2 && maps>1000);
    }
    printf("PASS ground camera: opacity/cache, native frame locals, continuous bands, gaps, floating platforms, load failure, unchanged scale/HUD/side margins; %d map fixtures, %d adjusted.\n",maps,adjusted);
}
