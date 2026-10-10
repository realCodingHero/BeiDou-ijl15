#include "WorldViewport.h"
#include "BossHpNumber.h"
#include "ConfirmedScalingPolicies.h"
#include "UpscaleLoader.h"
#include "d3d8.hpp"
#include "WzLib/IWzGr2D.h"
#include "WzLib/IWzCanvas.h"
#include "WzLib/IWzProperty.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {
int worldDraws=0,hudDraws=0;
alignas(4) unsigned char field[0x110]{},nonField[8]{};
int __fastcall IsField(void* self,void*,const void*) {return self==field+4;}
IWzGr2DLayer* marker=nullptr;
std::vector<unsigned char> pixels;
unsigned captureWidth=0,captureHeight=0;
void Check(HRESULT hr,const char* what) { if(FAILED(hr)) {printf("FAIL %s %08lX\n",what,hr);exit(1);} }
void Observe(void* layer,void* context,double scale) {
    if(scale>1) ++worldDraws; else ++hudDraws;
    if(layer!=marker) return;
    auto device=*static_cast<IDirect3DDevice8**>(context);
    IDirect3DSurface8* target=nullptr;Check(device->GetRenderTarget(&target),"render target");
    D3DSURFACE_DESC8 desc{};Check(target->GetDesc(&desc),"target description");
    IDirect3DSurface8* image=nullptr;Check(device->CreateImageSurface(desc.Width,desc.Height,desc.Format,&image),"readback surface");
    Check(device->CopyRects(target,nullptr,0,image,nullptr),"readback copy");
    D3DLOCKED_RECT lock{};Check(image->LockRect(&lock,nullptr,D3DLOCK_READONLY),"readback lock");
    assert(desc.Format==D3DFMT_X8R8G8B8 || desc.Format==D3DFMT_A8R8G8B8);
    captureWidth=desc.Width;captureHeight=desc.Height;pixels.resize(desc.Width*desc.Height*4);
    for(unsigned y=0;y<desc.Height;++y) memcpy(pixels.data()+y*desc.Width*4,static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch,desc.Width*4);
    image->UnlockRect();image->Release();target->Release();
}
RECT ColorBounds(DWORD rgb) {
    RECT r{LONG(captureWidth),LONG(captureHeight),-1,-1};
    for(unsigned y=0;y<captureHeight;++y) for(unsigned x=0;x<captureWidth;++x)
        if((*reinterpret_cast<DWORD*>(pixels.data()+(y*captureWidth+x)*4)&0xffffff)==rgb) {
            r.left=(std::min)(r.left,LONG(x));r.top=(std::min)(r.top,LONG(y));
            r.right=(std::max)(r.right,LONG(x));r.bottom=(std::max)(r.bottom,LONG(y));
        }
    return r;
}
}
int main(int argc,char** argv) {
    WorldViewport::SetSceneScaleForTesting(1.0); // Retain baseline regression coverage.

    if(argc!=2)return 2;
    assert(BossHpNumber::Format(12332104321ULL)=="123E3210W4321");
    assert(BossHpNumber::Format(0)=="0");
    assert(BossHpNumber::Format(9999)=="9999");
    assert(BossHpNumber::Format(10000)=="1W");
    assert(BossHpNumber::Format(100000000)=="1E");
    assert(BossHpNumber::Format(100010001)=="1E1W1");
    assert(BossHpNumber::Format(2147483647)=="21E4748W3647");
    setvbuf(stdout,nullptr,_IONBF,0);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    wchar_t exe[MAX_PATH];GetModuleFileNameW(nullptr,exe,MAX_PATH);
    const auto directory=std::filesystem::path(exe).parent_path();
    assert(directory.filename()==L"world-native");
    assert(SetCurrentDirectoryW(directory.c_str()));assert(SetDllDirectoryA(argv[1]));
    std::ofstream(directory/L"config.ini")<<"[upscaling]\nenabled=false\n";
    HMODULE pcom=LoadLibraryA("PCOM.dll");assert(pcom);
    auto init=reinterpret_cast<HRESULT (__cdecl*)()>(GetProcAddress(pcom,"PcInitModule"));
    auto term=reinterpret_cast<void (__cdecl*)()>(GetProcAddress(pcom,"PcTermModule"));
    auto create=reinterpret_cast<HRESULT (__cdecl*)(const wchar_t*,const GUID*,void**,IUnknown*)>(GetProcAddress(pcom,"PcCreateObject"));
    assert(init && term && create);Check(init(),"PCOM init");
    assert(UpscaleLoader::Install(GetModuleHandle(nullptr),true));
    assert(SetCurrentDirectoryA(argv[1]));
    IWzGr2D* gr=nullptr;Check(create(L"Gr2D_DX8",&__uuidof(IWzGr2D),reinterpret_cast<void**>(&gr),nullptr),"actual Gr2D create");
    assert(SetCurrentDirectoryW(directory.c_str()));
    WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandle(nullptr);cls.lpszClassName="WorldViewportFixture";RegisterClassA(&cls);
    HWND hwnd=CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,"World viewport fixture",WS_POPUP,0,0,1920,1080,nullptr,nullptr,cls.hInstance,nullptr);assert(hwnd);
    VARIANT window{},bpp{},refresh{};window.vt=VT_BYREF;window.byref=hwnd;bpp.vt=VT_I4;bpp.lVal=32;refresh.vt=VT_I4;refresh.lVal=60;
    Check(gr->put_fullScreen(0),"windowed");Check(gr->raw_Initialize(1920,1080,window,bpp,refresh),"actual Gr2D initialize");
    Check(gr->put_backColor(0xff102030),"opaque frame clear");
    assert(WorldViewport::InstallGraphics(GetModuleHandleA("Gr2D_DX8.dll")));
    void* stageMethods[19]{};stageMethods[18]=reinterpret_cast<void*>(&IsField);
    *reinterpret_cast<void***>(field+4)=stageMethods;*reinterpret_cast<void***>(nonField+4)=stageMethods;
    void* current=field;WorldViewport::SetContextForTesting(&current,field,{-728,-413,728,413},1920,1080);
    WorldViewport::SetObserverForTesting(Observe);
    auto make=[&](int x,int y,int z,DWORD color,IWzGr2DLayer* parent=nullptr,int width=60,int height=40) {
        assert(SetCurrentDirectoryA(argv[1]));
        IWzCanvas* canvas=nullptr;Check(create(L"Canvas",&__uuidof(IWzCanvas),reinterpret_cast<void**>(&canvas),nullptr),"actual Canvas create");
        assert(SetCurrentDirectoryW(directory.c_str()));
        VARIANT empty{},format{};format.vt=VT_I4;format.lVal=2;
        Check(canvas->raw_Create(width,height,empty,format),"canvas allocate");
        Check(canvas->raw_DrawRectangle(0,0,width,height,color),"canvas fill");
        VARIANT source{},filter{};source.vt=VT_UNKNOWN;source.punkVal=canvas;filter.vt=VT_I4;filter.lVal=1;
        IWzGr2DLayer* layer=nullptr;Check(gr->raw_CreateLayer(x,y,width,height,z,source,filter,&layer),"native layer");
        Check(layer->put_color(0xffffffff),"layer opacity");
        if(parent) {VARIANT overlay{};overlay.vt=VT_UNKNOWN;overlay.punkVal=parent;Check(layer->put_overlay(overlay),"equipment overlay");}
        canvas->Release();return layer;
    };
    auto world=make(-400,-200,int(0xC0000000),0xffff0000);
    auto child=make(-200,-100,1,0xff00ff00,world);
    auto hud=make(200,100,10,0xff0000ff);
    marker=make(400,200,0x7ffffffd,0xffffff00);
    for(int i=0;i<3;++i) {Check(gr->raw_UpdateCurrentTime(100+i*100),"clock");Check(gr->raw_RenderFrame(),"actual frame");}
    assert(worldDraws==0 && hudDraws && !pixels.empty());
    auto red=ColorBounds(0xff0000),green=ColorBounds(0x00ff00),blue=ColorBounds(0x0000ff);
    printf("native pixels: world (%ld,%ld)-(%ld,%ld), equipment (%ld,%ld)-(%ld,%ld), HUD (%ld,%ld)-(%ld,%ld); draws magnified=%d 1x=%d\n",
        red.left,red.top,red.right,red.bottom,green.left,green.top,green.right,green.bottom,blue.left,blue.top,blue.right,blue.bottom,worldDraws,hudDraws);
    assert(red.right-red.left==59 && red.bottom-red.top==39);
    assert(green.right-green.left==59 && green.bottom-green.top==39);
    assert(blue.right-blue.left==59 && blue.bottom-blue.top==39);
    // Screen HP bar must survive both vertical and horizontal map margins.
    for(const RECT bounds : {RECT{-570,-695,660,-95},RECT{-300,-2000,300,1000},RECT{-3000,-2000,3000,2000}}) {
        WorldViewport::SetContextForTesting(&current,field,bounds,1920,1080);
        auto boss=make(-700,-530,int(0xC00614A5),0xffaa3377,nullptr,1600,37);
        assert(!BossHpNumber::Draw(boss,0,100));
        assert(!BossHpNumber::Draw(boss,100,0));
        assert(BossHpNumber::Draw(boss,66000000,140000000));
        for(int i=0;i<2;++i) {Check(gr->raw_UpdateCurrentTime(410+i),"boss time");Check(gr->raw_RenderFrame(),"boss frame");}
        const auto hpBar=ColorBounds(0xaa3377),number=ColorBounds(0xffffff);
        printf("boss bar (%ld,%ld)-(%ld,%ld), number (%ld,%ld)-(%ld,%ld)\n",hpBar.left,hpBar.top,hpBar.right,hpBar.bottom,number.left,number.top,number.right,number.bottom);
        assert(hpBar.right-hpBar.left==1599 && hpBar.bottom-hpBar.top==36);
        assert(number.right>=number.left && number.top>=hpBar.top && number.bottom<hpBar.top+20);
        assert(abs((number.left+number.right)-(hpBar.left+35+hpBar.right))<=2);
        boss->Release();
    }
    auto bar=make(-600,490,int(0xC00615D0),0xffcc00cc,nullptr,1200,40);
    // Real camera/Ratio vectors reproduce authored rx=-70, ry=-50. Growing
    // the backdrop must not multiply motion by 1.8 (126px instead of 70px).
    WorldViewport::SetContextForTesting(&current,field,{-2000,-1200,2000,1200},1920,1080);
    WorldViewport::SetGraphicsForTesting(gr);
    IWzVector2D* center=nullptr;Check(gr->get_center(&center),"camera center");
    const int savedCenterX=center->Getx(),savedCenterY=center->Gety();
    IWzVector2D* parallax=nullptr;
    assert(SetCurrentDirectoryA(argv[1]));
    Check(create(L"Shape2D#Vector2D",&__uuidof(IWzVector2D),reinterpret_cast<void**>(&parallax),nullptr),"parallax vector");
    assert(SetCurrentDirectoryW(directory.c_str()));
    VARIANT cameraOrigin{};cameraOrigin.vt=VT_UNKNOWN;cameraOrigin.punkVal=center;
    Check(parallax->put_origin(cameraOrigin),"native vector origin");
    Check(parallax->raw_RelMove(0,0),"native vector offset");
    Check(parallax->raw_Ratio(center,100,100,-70,-50),"native background Ratio");
    WorldViewport::BeginBackgrounds(field);
    assert(WorldViewport::RegisterBackground(field,17,0,-50,928,-70,1));
    auto moving=make(0,0,int(0xBFFE0C00)+17000,0xff20d0e0,nullptr,60,40);
    VARIANT origin{};origin.vt=VT_UNKNOWN;origin.punkVal=parallax;
    Check(moving->put_origin(origin),"native background origin");
    Check(center->raw_RelMove(0,0),"camera zero");
    for(int i=0;i<3;++i){Sleep(20);Check(gr->raw_UpdateCurrentTime(GetTickCount()),"motion clock");Check(gr->raw_RenderFrame(),"motion zero");}
    auto first=ColorBounds(0x20d0e0);
    Check(center->raw_RelMove(100,100),"camera step");
    for(int i=0;i<3;++i){Sleep(20);Check(gr->raw_UpdateCurrentTime(GetTickCount()),"motion clock");Check(gr->raw_RenderFrame(),"motion step");}
    auto second=ColorBounds(0x20d0e0);
    printf("backdrop camera delta=(%ld,%ld), expected=(-70,-50), artwork width=%ld\n",second.left-first.left,second.top-first.top,second.right-second.left+1);
    assert(second.left-first.left==-70 && second.top-first.top==-50);
    assert(second.right-second.left+1==108 && second.bottom-second.top+1==72);
    // The same native vector without compensation reproduces the regression.
    WorldViewport::SetGraphicsForTesting(nullptr);
    auto motionAt=[&](int value) {
        Check(center->raw_RelMove(value,value),"baseline camera");
        for(int i=0;i<3;++i){Sleep(20);Check(gr->raw_UpdateCurrentTime(GetTickCount()),"baseline time");Check(gr->raw_RenderFrame(),"baseline frame");}
        return ColorBounds(0x20d0e0);
    };
    const auto oldFirst=motionAt(0),oldSecond=motionAt(100);
    printf("uncompensated backdrop delta=(%ld,%ld)\n",oldSecond.left-oldFirst.left,oldSecond.top-oldFirst.top);
    assert(oldSecond.left-oldFirst.left==-126 && oldSecond.top-oldFirst.top==-90);
    WorldViewport::SetGraphicsForTesting(gr);
    moving->Release();parallax->Release();
    // Cave left cap at its 1080p left camera limit: native geometry leaves
    // a 32px gap; compensate the additional 56px drift caused by wider view.
    WorldViewport::SetContextForTesting(&current,field,{-1022,-766,2910,-70},1920,1080);
    WorldViewport::BeginBackgrounds(field);
    Check(center->raw_RelMove(0,0),"cave camera origin");
    assert(SetCurrentDirectoryA(argv[1]));
    Check(create(L"Shape2D#Vector2D",&__uuidof(IWzVector2D),reinterpret_cast<void**>(&parallax),nullptr),"foreground vector");
    assert(SetCurrentDirectoryW(directory.c_str()));
    Check(parallax->put_origin(cameraOrigin),"foreground vector origin");
    Check(parallax->raw_RelMove(0,0),"foreground vector offset");
    Check(parallax->raw_Ratio(center,100,100,-90,-90),"native foreground Ratio");
    auto capLayer=make(-977,-100,int(0xC0042360)+21000,0xff11bb77,nullptr,250,194);
    origin.punkVal=parallax;Check(capLayer->put_origin(origin),"foreground origin");
    Check(center->raw_RelMove(-54,0),"cave camera left limit");
    auto capFrame=[&](){for(int i=0;i<3;++i){Sleep(20);Check(gr->raw_UpdateCurrentTime(GetTickCount()),"cap clock");Check(gr->raw_RenderFrame(),"cap frame");}return ColorBounds(0x11bb77);};
    const auto oldCap=capFrame();
    assert(oldCap.left>=31 && oldCap.left<=33);
    assert(WorldViewport::RegisterBackground(field,21,1,-90,-852,-90,0));
    capFrame();
    auto capBounds=ColorBounds(0x11bb77);
    printf("cave foreground gap: %ld -> %ld pixels; right=%ld\n",oldCap.left,capBounds.left,capBounds.right);
    assert(capBounds.left==0 && capBounds.right>200);
    capLayer->Release();parallax->Release();Check(center->raw_RelMove(savedCenterX,savedCenterY),"restore camera");center->Release();
    WorldViewport::SetGraphicsForTesting(nullptr);
    WorldViewport::SetContextForTesting(&current,field,{-728,-413,728,413},1920,1080);
    // Letterboxed caves must size decoration against the 634px effective
    // scene, not the full 1080px buffer. Actors and HUD remain native-sized.
    WorldViewport::SetContextForTesting(&current,field,{-1290,-720,6330,-70},1920,1080);
    WorldViewport::BeginBackgrounds(field);
    WorldViewport::RegisterBackground(field,12,0,-65,4794,-65,1);
    auto shortBackdrop=make(0,0,int(0xBFFE0C00)+12000,0xffaabb33,nullptr,60,40);
    Check(gr->raw_UpdateCurrentTime(GetTickCount()),"short backdrop clock");
    Check(gr->raw_RenderFrame(),"short backdrop frame");
    auto shortPixels=ColorBounds(0xaabb33);
    printf("634px scene backdrop: %ldx%ld (old full-buffer policy 108x72)\n",
        shortPixels.right-shortPixels.left+1,shortPixels.bottom-shortPixels.top+1);
    assert(shortPixels.right-shortPixels.left+1>=63 && shortPixels.right-shortPixels.left+1<=64);
    assert(shortPixels.bottom-shortPixels.top+1>=42 && shortPixels.bottom-shortPixels.top+1<=43);
    auto shortActor=ColorBounds(0xff0000),shortHud=ColorBounds(0xcc00cc);
    assert(shortActor.right-shortActor.left==59 && shortActor.bottom-shortActor.top==39);
    assert(shortHud.right-shortHud.left==1199 && shortHud.bottom-shortHud.top==39);
    shortBackdrop->Release();
    // Full-height decorative backgrounds retain their established 1.8x.
    WorldViewport::SetContextForTesting(&current,field,{-2000,-1200,2000,1200},1920,1080);
    WorldViewport::BeginBackgrounds(field);
    assert(WorldViewport::RegisterBackground(field,1,0,-5));
    *reinterpret_cast<RECT*>(field+0xF0)={-2000+960,-1200+540,2000-960,1200-540};
    WorldViewport::AdjustCamera(field);
    std::vector<IWzGr2DLayer*> chunks;
    for(int i=0;i<400;++i) chunks.push_back(make(-600+(i%20)*60,-400+(i/20)*40,int(0xBFFE0C00)+1000,0xff20d0e0));
    // Adjacent independently allocated textures expose the half-texel overrun
    // missed by a single flat canvas. Prove both uncorrected filters fail.
    WorldViewport::SetTexelAlignmentForTesting(false);
    for(auto tile:chunks)*reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(tile)+0x4C)=2;
    Check(gr->raw_UpdateCurrentTime(410),"legacy tile clock");Check(gr->raw_RenderFrame(),"legacy linear chunks");
    int legacySeams=0;
    for(int x=300;x<1600;++x)if((*reinterpret_cast<DWORD*>(pixels.data()+(540*captureWidth+x)*4)&0xffffff)!=0x20d0e0)++legacySeams;
    for(auto tile:chunks)*reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(tile)+0x4C)=1;
    Check(gr->raw_UpdateCurrentTime(420),"tile clock");Check(gr->raw_RenderFrame(),"tile and native HUD frame");
    int seamPixels=0;
    for(int x=300;x<1600;++x) {
        DWORD color=*reinterpret_cast<DWORD*>(pixels.data()+(540*captureWidth+x)*4)&0xffffff;
        if(color!=0x20d0e0) ++seamPixels;
    }
    auto magenta=ColorBounds(0xcc00cc);
    printf("uncorrected chunk seams: linear=%d, point=%d; negative-Z HUD bounds=(%ld,%ld)-(%ld,%ld)\n",legacySeams,seamPixels,magenta.left,magenta.top,magenta.right,magenta.bottom);
    assert(legacySeams>0 && seamPixels>0 && magenta.right-magenta.left==1199 && magenta.bottom-magenta.top==39);
    WorldViewport::SetTexelAlignmentForTesting(true);
    for(int filter:{1,2}) {
        for(auto tile:chunks)*reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(tile)+0x4C)=filter;
        Check(gr->raw_UpdateCurrentTime(430+filter),"aligned clock");Check(gr->raw_RenderFrame(),"aligned chunks");
        seamPixels=0;
        // Exercise both horizontal and vertical boundaries across the grid.
        for(int x=300;x<1600;++x)if((*reinterpret_cast<DWORD*>(pixels.data()+(540*captureWidth+x)*4)&0xffffff)!=0x20d0e0)++seamPixels;
        for(int y=100;y<950;++y)if((*reinterpret_cast<DWORD*>(pixels.data()+(y*captureWidth+1500)*4)&0xffffff)!=0x20d0e0)++seamPixels;
        printf("corrected chunk seams: filter=%d, pixels=%d\n",filter,seamPixels);
        // At the decorative 1.8x scale, forced native linear filtering has
        // 24 boundary pixels in the accepted 9bb95fd baseline too. Retain a
        // regression ceiling; only the native point path is seam-free here.
        // This is unrelated to the final whole-frame linear window scaler.
        assert(filter==1 ? seamPixels==0:seamPixels<=24);
    }
    for(auto tile:chunks)tile->Release();bar->Release();
    WorldViewport::SetContextForTesting(&current,field,{-3045,-183,1326,287},1920,1080);
    auto fadeFill=make(-1200,-900,int(0xBFFE0000),0xff20d0e0,nullptr,2400,1800);
    current=nullptr;
    Check(gr->raw_UpdateCurrentTime(450),"fade clock");Check(gr->raw_RenderFrame(),"old scene fade");
    red=ColorBounds(0xff0000);assert(red.right-red.left==59);
    const auto fade=ColorBounds(0x20d0e0);
    assert(fade.left==0 && fade.right==1919 && fade.top==313 && fade.bottom==766);
    current=nonField;
    const int before=hudDraws;
    for(int i=0;i<3;++i) {Sleep(20);Check(gr->raw_UpdateCurrentTime(500+i*100),"stage clock");Check(gr->raw_RenderFrame(),"leave field");}
    red=ColorBounds(0xff0000);
    printf("after stage exit: world (%ld,%ld)-(%ld,%ld), new draws=%d\n",red.left,red.top,red.right,red.bottom,hudDraws-before);
    assert(red.right-red.left==59 && red.bottom-red.top==39 && hudDraws>before);
    const auto exited=ColorBounds(0x20d0e0);
    assert(exited.left==0 && exited.right==1919 && exited.top==0 && exited.bottom==1079);
    fadeFill->Release();
    current=field;
    WorldViewport::SetContextForTesting(&current,field,{-400,-398,400,600},1920,1080);
    auto narrowFill=make(-1200,-900,int(0xBFFE0000),0xff20d0e0,nullptr,2400,1800);
    auto narrowBar=make(-600,490,int(0xC00615D0),0xffcc00cc,nullptr,1200,40);
    auto narrowActor=make(-100,-200,int(0xC0000000),0xffff8800);
    Check(gr->raw_UpdateCurrentTime(900),"narrow clock");Check(gr->raw_RenderFrame(),"narrow scene and HUD");
    auto cyan=ColorBounds(0x20d0e0);magenta=ColorBounds(0xcc00cc);
    printf("narrow pixels: world x=%ld..%ld, HUD x=%ld..%ld; native actor size with margins.\n",cyan.left,cyan.right,magenta.left,magenta.right);
    assert(cyan.left==568 && cyan.right==1351 && cyan.top==49 && cyan.bottom==1029 && magenta.left==360 && magenta.right==1559);
    auto orange=ColorBounds(0xff8800);assert(orange.right-orange.left==59 && orange.bottom-orange.top==39);
    // The original actor at x=560 crosses the new left edge by eight pixels.
    red=ColorBounds(0xff0000);assert(red.left==568 && red.right==619 && red.bottom-red.top==39);
    narrowActor->Release();narrowFill->Release();narrowBar->Release();
    // Real GPU integration of the native AquaRoad split (-867..-273..327).
    WorldViewport::SetContextForTesting(&current,field,{-2000,-1200,2000,1200},1920,1080);
    WorldViewport::BeginBackgrounds(field);
    assert(WorldViewport::RegisterBackground(field,1,0,-5));
    assert(WorldViewport::RegisterBackground(field,2,0,-5));
    *reinterpret_cast<RECT*>(field+0xF0)={-2000+960,-1200+540,2000-960,1200-540};
    WorldViewport::AdjustCamera(field);
    auto lower=make(-600,-273,int(0xBFFE0C00)+1000,0xff20d0e0,nullptr,1200,600);
    auto upper=make(-600,-867,int(0xBFFE0C00)+2000,0xff20d0e0,nullptr,1200,594);
    Check(gr->raw_UpdateCurrentTime(950),"backdrop clock");Check(gr->raw_RenderFrame(),"joined backdrop");
    int joinGaps=0;
    for(int y=5;y<250;++y)if((*reinterpret_cast<DWORD*>(pixels.data()+(y*captureWidth+1500)*4)&0xffffff)!=0x20d0e0)++joinGaps;
    printf("AquaRoad backdrop join gaps=%d\n",joinGaps);assert(joinGaps==0);
    lower->Release();upper->Release();
    // Single-map uniform-background trial, with a neighboring forest control.
    for(int mapId:{240000000,240010000}) {
        const WorldViewport::ConfirmedPolicy* p=nullptr;
        for(const auto& candidate:WorldViewport::confirmedPolicies)if(candidate.map==mapId)p=&candidate;
        assert(p);
        WorldViewport::SetContextForTesting(&current,field,p->bounds,1920,1080);
        WorldViewport::BeginTerrain(field);WorldViewport::BeginBackgrounds(field);
        for(size_t i=0;i<p->count;++i) {
            const auto& b=p->layers[i];WorldViewport::RegisterBackground(field,b.index,b.front,b.ry,b.x,b.rx,b.type,b.y);
        }
        const auto b=p->bounds;
        *reinterpret_cast<RECT*>(field+0xF0)={b.left+960,b.top+540,b.right-960,b.bottom-540};
        WorldViewport::AdjustCamera(field);assert(WorldViewport::GetPolicyForTesting()==mapId);
        if(mapId==240000000)assert(WorldViewport::GetViewForTesting().camera.bottom==68);
        auto decorative=make(0,-200,int(0xBFFE0C00u)+1000,0xffaabb33,nullptr,60,40);
        auto attached=make(200,-200,int(0xBFFE0C00u)+11000,0xff33bbaa,nullptr,60,40);
        Check(gr->raw_UpdateCurrentTime(955),"uniform background clock");Check(gr->raw_RenderFrame(),"uniform background frame");
        const auto a=ColorBounds(0xaabb33),c=ColorBounds(0x33bbaa);
        const int w=mapId==240000000 ? 60:108,h=mapId==240000000 ? 40:72;
        assert(a.right-a.left+1==w && a.bottom-a.top+1==h);
        assert(c.right-c.left+1==60 && c.bottom-c.top+1==40);
        printf("PASS uniform trial %d: decorative=%dx%d attached=60x40, camera retained\n",mapId,w,h);
        decorative->Release();attached->Release();
    }
    // Confirmed finite sky planes: actual projection and camera Ratio, at
    // both vertical limits. Unrelated actor and HUD roots retain native size.
    IWzVector2D* skyCenter=nullptr;Check(gr->get_center(&skyCenter),"sky center");
    WorldViewport::SetGraphicsForTesting(gr);
    const int oldSkyX=skyCenter->Getx(),oldSkyY=skyCenter->Gety();
    IWzVector2D* fixedScreen=nullptr;
    assert(SetCurrentDirectoryA(argv[1]));
    Check(create(L"Shape2D#Vector2D",&__uuidof(IWzVector2D),reinterpret_cast<void**>(&fixedScreen),nullptr),"screen vector");
    assert(SetCurrentDirectoryW(directory.c_str()));
    VARIANT fixedOrigin{};fixedOrigin.vt=VT_UNKNOWN;fixedOrigin.punkVal=skyCenter;
    Check(fixedScreen->put_origin(fixedOrigin),"screen origin");
    Check(fixedScreen->raw_Ratio(skyCenter,100,100,0,0),"screen ratio");
    fixedOrigin.punkVal=fixedScreen;Check(marker->put_origin(fixedOrigin),"capture screen origin");Check(hud->put_origin(fixedOrigin),"HUD screen origin");
    for(const auto& p:WorldViewport::confirmedPolicies) if(p.leftIndex>=0) {
        WorldViewport::SetContextForTesting(&current,field,p.bounds,1920,1080);
        WorldViewport::BeginTerrain(field);WorldViewport::BeginBackgrounds(field);
        for(size_t i=0;i<p.count;++i){const auto& b=p.layers[i];WorldViewport::RegisterBackground(field,b.index,b.front,b.ry,b.x,b.rx,b.type,b.y);}
        const auto b=p.bounds;
        *reinterpret_cast<RECT*>(field+0xF0)={b.left+960,b.top+540,b.right-960,b.bottom-540};WorldViewport::AdjustCamera(field);
        const auto view=WorldViewport::GetViewForTesting();
        Check(skyCenter->raw_RelMove(0,0),"wall construction origin");
        assert(SetCurrentDirectoryA(argv[1]));IWzVector2D* ratio=nullptr;
        Check(create(L"Shape2D#Vector2D",&__uuidof(IWzVector2D),reinterpret_cast<void**>(&ratio),nullptr),"wall vector");
        assert(SetCurrentDirectoryW(directory.c_str()));
        VARIANT ori{};ori.vt=VT_UNKNOWN;ori.punkVal=skyCenter;Check(ratio->put_origin(ori),"wall origin");
        Check(ratio->raw_RelMove(0,0),"wall offset");Check(ratio->raw_Ratio(skyCenter,100,100,-20,-20),"wall ratio");
        IWzGr2DLayer* wall[4]{};const DWORD colors[]={0xff88cc11,0xff11cc88,0xffcc8811,0xff8811cc};
        for(int j=0;j<4;++j) {
            wall[j]=make(j<2 ? p.leftEdge:p.rightEdge-180,(j%2)*80-100,
                int(0xBFFE0C00u)+(j<2 ? p.leftIndex+j:p.rightIndex+j-2)*1000,colors[j],nullptr,180,60);
            ori.punkVal=ratio;Check(wall[j]->put_origin(ori),"wall layer origin");
        }
        for(int cx:{int(view.camera.left),int((view.camera.left+view.camera.right)/2),int(view.camera.right)}) {
            Check(skyCenter->raw_RelMove(cx,0),"wall camera");
            for(int i=0;i<3;++i){Sleep(20);Check(gr->raw_UpdateCurrentTime(GetTickCount()),"wall clock");Check(gr->raw_RenderFrame(),"wall frame");}
            RECT painted[4];
            for(int j=0;j<4;++j) {
                painted[j]=ColorBounds(colors[j]&0xffffff);
                if(painted[j].right>=painted[j].left)
                    assert(j<2 ? painted[j].left==view.clip.left:painted[j].right==view.clip.right-1);
            }
            assert(painted[0].left==painted[1].left && painted[0].right==painted[1].right);
            assert(painted[2].left==painted[3].left && painted[2].right==painted[3].right);
        }
        printf("PASS wall groups %d: both hard edges outside at left/middle/right camera, paired alignment preserved\n",p.map);
        for(auto layer:wall)layer->Release();ratio->Release();Check(skyCenter->raw_RelMove(0,0),"restore wall camera");
    }
    for(const auto& p:WorldViewport::confirmedPolicies) if(p.map==240000000) {
        WorldViewport::SetContextForTesting(&current,field,p.bounds,1920,1080);
        WorldViewport::BeginTerrain(field);WorldViewport::BeginBackgrounds(field);
        for(size_t i=0;i<p.count;++i){const auto& b=p.layers[i];WorldViewport::RegisterBackground(field,b.index,b.front,b.ry,b.x,b.rx,b.type,b.y);}
        const auto b=p.bounds;
        *reinterpret_cast<RECT*>(field+0xF0)={b.left+960,b.top+540,b.right-960,b.bottom-540};WorldViewport::AdjustCamera(field);
        Check(skyCenter->raw_RelMove(0,0),"light construction origin");
        assert(SetCurrentDirectoryA(argv[1]));IWzVector2D* lightRatio=nullptr;
        Check(create(L"Shape2D#Vector2D",&__uuidof(IWzVector2D),reinterpret_cast<void**>(&lightRatio),nullptr),"light vector");
        assert(SetCurrentDirectoryW(directory.c_str()));
        VARIANT ori{};ori.vt=VT_UNKNOWN;ori.punkVal=skyCenter;Check(lightRatio->put_origin(ori),"light origin");
        Check(lightRatio->raw_RelMove(0,0),"light offset");Check(lightRatio->raw_Ratio(skyCenter,100,100,-80,-40),"light ratio");
        auto light=make(0,-547,int(0xC0042360u)+17000,0xff55ddaa,nullptr,60,752);
        ori.punkVal=lightRatio;Check(light->put_origin(ori),"light layer origin");
        for(int cy:{-412,68}) {
            Check(skyCenter->raw_RelMove(0,cy),"light camera");
            for(int i=0;i<3;++i){Sleep(20);Check(gr->raw_UpdateCurrentTime(GetTickCount()),"light clock");Check(gr->raw_RenderFrame(),"light frame");}
            const auto lightBounds=ColorBounds(0x55ddaa);
            assert(lightBounds.top==0 && lightBounds.right-lightBounds.left+1==60);
            printf("PASS Leafre foreground light: camera=%d top=%ld width=60 (native)\n",cy,lightBounds.top);
        }
        light->Release();lightRatio->Release();Check(skyCenter->raw_RelMove(0,0),"reset light camera");
    }
    for(const auto& policy:WorldViewport::confirmedPolicies) if(policy.skyIndex>=0) {
        WorldViewport::SetContextForTesting(&current,field,policy.bounds,1920,1080);
        WorldViewport::BeginTerrain(field);WorldViewport::BeginBackgrounds(field);
        int rx=0,ry=0;
        for(size_t i=0;i<policy.count;++i) {
            const auto& b=policy.layers[i];
            WorldViewport::RegisterBackground(field,b.index,b.front,b.ry,b.x,b.rx,b.type,b.y);
            if(b.index==policy.skyIndex){rx=b.rx;ry=b.ry;}
        }
        const auto b=policy.bounds;
        *reinterpret_cast<RECT*>(field+0xF0)={b.left+960,b.top+540,b.right-960,b.bottom-540};
        WorldViewport::AdjustCamera(field);
        assert(WorldViewport::GetPolicyForTesting()==policy.map);
        const auto view=WorldViewport::GetViewForTesting();
        Check(skyCenter->raw_RelMove(0,0),"sky vector construction origin");
        assert(SetCurrentDirectoryA(argv[1]));
        IWzVector2D* ratio=nullptr;
        Check(create(L"Shape2D#Vector2D",&__uuidof(IWzVector2D),reinterpret_cast<void**>(&ratio),nullptr),"sky ratio");
        assert(SetCurrentDirectoryW(directory.c_str()));
        VARIANT ori{};ori.vt=VT_UNKNOWN;ori.punkVal=skyCenter;Check(ratio->put_origin(ori),"sky origin");
        Check(ratio->raw_RelMove(0,0),"sky offset");Check(ratio->raw_Ratio(skyCenter,100,100,rx,ry),"sky native ratio");
        auto sky=make(-600,policy.skyTop,int(0xBFFE0C00u)+policy.skyIndex*1000,0xff20d0e0,nullptr,1200,policy.skyBottom-policy.skyTop);
        ori.punkVal=ratio;Check(sky->put_origin(ori),"sky layer origin");
        for(int cy:{int(view.camera.top),int(view.camera.bottom)}) {
            Check(skyCenter->raw_RelMove(0,cy),"sky camera limit");
            for(int i=0;i<3;++i) {Sleep(20);Check(gr->raw_UpdateCurrentTime(GetTickCount()),"sky clock");Check(gr->raw_RenderFrame(),"sky frame");}
            for(int y:{int(view.clip.top+2),int(view.clip.bottom-3)})
                if((*reinterpret_cast<DWORD*>(pixels.data()+(y*captureWidth+1000)*4)&0xffffff)!=0x20d0e0) {
                    const auto painted=ColorBounds(0x20d0e0);
                    printf("FAIL sky map=%d cy=%d y=%d color=%08lX painted=(%ld,%ld,%ld,%ld) center=%d ratio=%d layer=%d clip=%ld,%ld\n",policy.map,cy,y,*reinterpret_cast<DWORD*>(pixels.data()+(y*captureWidth+1000)*4),painted.left,painted.top,painted.right,painted.bottom,skyCenter->Gety(),ratio->Gety(),sky->Gety(),view.clip.top,view.clip.bottom);exit(1);
                }
            blue=ColorBounds(0x0000ff);
            if(blue.right-blue.left!=59 || blue.bottom-blue.top!=39) {printf("FAIL sky HUD map=%d center=%d bounds=%ld,%ld,%ld,%ld\n",policy.map,cy,blue.left,blue.top,blue.right,blue.bottom);exit(1);}
        }
        printf("PASS native sky %d: both camera limits covered, HUD unchanged\n",policy.map);
        sky->Release();ratio->Release();
    }
    Check(skyCenter->raw_RelMove(oldSkyX,oldSkyY),"restore sky camera");skyCenter->Release();
    VARIANT resetOrigin{};Check(marker->put_origin(resetOrigin),"restore capture origin");Check(hud->put_origin(resetOrigin),"restore HUD origin");fixedScreen->Release();
    WorldViewport::SetGraphicsForTesting(nullptr);
    // Use real PCOM property/canvas methods, including the alpha-channel read.
    // Reproduce the two towns' bottom end caps and apply the resulting camera.
    for (int town=0;town<2;++town) {
        RECT bounds=town ? RECT{-1018,-587,6328,770}:RECT{-2500,-1443,2645,800};
        const int first=town ? -990:-2475,last=town ? 6210:2565;
        const int groundY=town ? 660:690,solidRows=town ? 9:14;
        const int expectedCamera=town ? 128:163;
        WorldViewport::SetContextForTesting(&current,field,bounds,1920,1080);
        auto view=WorldViewport::Fit(bounds,1920,1080);
        assert(view.scale==1.0);
        assert(SetCurrentDirectoryA(argv[1]));
        IWzProperty* property=nullptr;IWzCanvas* cap=nullptr;
        Check(create(L"Property",&__uuidof(IWzProperty),reinterpret_cast<void**>(&property),nullptr),"ground property");
        Check(create(L"Canvas",&__uuidof(IWzCanvas),reinterpret_cast<void**>(&cap),nullptr),"ground canvas");
        assert(SetCurrentDirectoryW(directory.c_str()));
        VARIANT empty{},format{},type{};format.vt=VT_I4;format.lVal=2;
        Check(cap->raw_Create(90,30,empty,format),"end cap allocate");
        Check(cap->raw_DrawRectangle(0,0,90,30,0),"transparent end cap");
        Check(cap->raw_DrawRectangle(0,0,90,solidRows,0xff22cc66),"opaque end cap prefix");
        Check(cap->put_cx(0),"end cap origin X");Check(cap->put_cy(0),"end cap origin Y");
        type.vt=VT_BSTR;type.bstrVal=SysAllocString(L"enH1");BSTR name=SysAllocString(L"u");
        Check(property->put_item(name,type),"end cap type");SysFreeString(name);VariantClear(&type);
        auto drawGround=[&](int cameraBottom,int tick) {
            std::vector<IWzGr2DLayer*> ends;
            VARIANT source{},filter{};source.vt=VT_UNKNOWN;source.punkVal=cap;filter.vt=VT_I4;filter.lVal=1;
            for(int x=first;x<=last;x+=90) {
                IWzGr2DLayer* layer=nullptr;
                // LoadTile (63A881..63A88C) passes zero width/height, so the
                // layer is anchored at x/y minus the canvas origin.
                Check(gr->raw_CreateLayer(x,groundY-cameraBottom,0,0,int(0xC0000000),source,filter,&layer),"end cap layer");
                Check(layer->put_color(0xffffffff),"end cap layer alpha");ends.push_back(layer);
            }
            auto groundHud=make(-600,490,int(0xC00615D0),0xffcc00cc,nullptr,1200,40);
            Check(gr->raw_UpdateCurrentTime(tick),"ground clock");Check(gr->raw_RenderFrame(),"ground frame");
            int gaps=0;for(int x=100;x<300;++x)
                if((*reinterpret_cast<DWORD*>(pixels.data()+(1079*captureWidth+x)*4)&0xffffff)!=0x22cc66)++gaps;
            auto painted=ColorBounds(0x22cc66);
            printf("ground pixels camera=%d: (%ld,%ld)-(%ld,%ld), pixel(200,1079)=%08lX\n",cameraBottom,
                painted.left,painted.top,painted.right,painted.bottom,*reinterpret_cast<DWORD*>(pixels.data()+(1079*captureWidth+200)*4));
            auto actor=ColorBounds(0xff0000),status=ColorBounds(0xcc00cc);
            assert(actor.right-actor.left==59 && actor.bottom-actor.top==39);
            assert(status.left==360 && status.right==1559 && status.bottom-status.top==39);
            groundHud->Release();for(auto layer:ends)layer->Release();return gaps;
        };
        const int oldGaps=drawGround(view.camera.bottom,960+town*10);
        assert(oldGaps==200);
        WorldViewport::BeginTerrain(field);
        for(int x=first;x<=last;x+=90)WorldViewport::CollectTerrainTile(field,1,property,cap,x,groundY);
        auto camera=reinterpret_cast<RECT*>(field+0xF0);
        *camera={bounds.left+960,bounds.top+540,bounds.right-960,bounds.bottom-540};
        WorldViewport::AdjustCamera(field);assert(camera->bottom==expectedCamera);
        const int newGaps=drawGround(camera->bottom,961+town*10);
        printf("ground camera town=%s: bottom %ld -> %ld, bottom-row gaps %d -> %d, actor/HUD unchanged\n",
            town ? "100000000":"103000000",view.camera.bottom,camera->bottom,oldGaps,newGaps);
        assert(newGaps==0);property->Release();cap->Release();
    }
    WorldViewport::SetContextForTesting(&current,field,{-728,-413,728,413},1920,1080);
    WorldViewport::SetSceneScaleForTesting(1.5);
    for(RECT bounds:{RECT{-3000,-2000,3000,2000},RECT{-3045,-183,1326,287},RECT{-400,-2500,400,450}}) {
        WorldViewport::SetContextForTesting(&current,field,bounds,1920,1080);
        auto actor=make(-20,-20,int(0xC0000000),0xff4488ee,nullptr,60,40);
        auto buff=make(850,-480,int(0xC006156C),0xff88ee44,nullptr,32,32);
        auto boss=make(-700,-530,int(0xC00614A5),0xffaa3377,nullptr,1600,37);
        assert(BossHpNumber::Draw(boss,66000000,140000000));
        for(int i=0;i<3;++i){Sleep(20);Check(gr->raw_UpdateCurrentTime(GetTickCount()),"zoom clock");Check(gr->raw_RenderFrame(),"zoom frame");}
        const auto a=ColorBounds(0x4488ee),b=ColorBounds(0x88ee44),h=ColorBounds(0xaa3377);
        printf("1.5 zoom: actor=%ldx%ld buff=%ldx%ld boss=%ldx%ld\n",a.right-a.left+1,a.bottom-a.top+1,b.right-b.left+1,b.bottom-b.top+1,h.right-h.left+1,h.bottom-h.top+1);
        assert(a.right-a.left+1==90 && a.bottom-a.top+1==60);
        assert(b.right-b.left+1==32 && b.bottom-b.top+1==32);
        assert(h.right-h.left+1==1600 && h.bottom-h.top+1==37);
        actor->Release();buff->Release();boss->Release();
    }
    WorldViewport::SetContextForTesting(&current,field,{-3000,-2000,3000,2000},1920,1080);
    WorldViewport::BeginBackgrounds(field);
    WorldViewport::RegisterBackground(field,1,0,-5);
    WorldViewport::RegisterBackground(field,2,0,-5);
    auto zoomLower=make(-600,-273,int(0xBFFE0C00)+1000,0xff20d0e0,nullptr,1200,600);
    auto zoomUpper=make(-600,-867,int(0xBFFE0C00)+2000,0xff20d0e0,nullptr,1200,594);
    for(int i=0;i<3;++i){Sleep(20);Check(gr->raw_UpdateCurrentTime(GetTickCount()),"zoom join clock");Check(gr->raw_RenderFrame(),"zoom join frame");}
    for(int y=5;y<250;++y) assert((*reinterpret_cast<DWORD*>(pixels.data()+(y*captureWidth+1500)*4)&0xffffff)==0x20d0e0);
    zoomLower->Release();zoomUpper->Release();
    WorldViewport::SetGraphicsForTesting(gr);
    IWzVector2D* zoomCenter=nullptr;Check(gr->get_center(&zoomCenter),"zoom center");
    const int zx=zoomCenter->Getx(),zy=zoomCenter->Gety();
    IWzVector2D* zoomRatio=nullptr;
    assert(SetCurrentDirectoryA(argv[1]));
    Check(create(L"Shape2D#Vector2D",&__uuidof(IWzVector2D),reinterpret_cast<void**>(&zoomRatio),nullptr),"zoom ratio");
    assert(SetCurrentDirectoryW(directory.c_str()));
    VARIANT zo{};zo.vt=VT_UNKNOWN;zo.punkVal=zoomCenter;
    Check(zoomRatio->put_origin(zo),"zoom origin");Check(zoomRatio->raw_RelMove(0,0),"zoom offset");
    Check(zoomRatio->raw_Ratio(zoomCenter,100,100,-70,-50),"zoom ratio values");
    WorldViewport::BeginBackgrounds(field);WorldViewport::RegisterBackground(field,17,0,-50,0,-70,1);
    auto zoomBack=make(0,0,int(0xBFFE0C00)+17000,0xff20d0e0,nullptr,60,40);
    zo.punkVal=zoomRatio;Check(zoomBack->put_origin(zo),"zoom backdrop origin");
    auto zoomAt=[&](int value) {
        Check(zoomCenter->raw_RelMove(value,value),"zoom camera");
        for(int i=0;i<3;++i){Sleep(20);Check(gr->raw_UpdateCurrentTime(GetTickCount()),"zoom motion clock");Check(gr->raw_RenderFrame(),"zoom motion frame");}
        return ColorBounds(0x20d0e0);
    };
    const auto za=zoomAt(0),zb=zoomAt(100);
    assert(zb.left-za.left==-105 && zb.top-za.top==-75);
    printf("PASS 1.5 zoom: joined backdrop gap-free; parallax delta -105,-75 for logical -70,-50\n");
    zoomBack->Release();zoomRatio->Release();Check(zoomCenter->raw_RelMove(zx,zy),"zoom restore");zoomCenter->Release();
    WorldViewport::SetGraphicsForTesting(nullptr);
    WorldViewport::SetSceneScaleForTesting(1.0);
    WorldViewport::SetContextForTesting(&current,field,{-728,-413,728,413},1920,1080);
    // Exercise the hook under many layers without diagnostic pixel readback.
    // This is a local overhead measurement, not an in-game FPS guarantee.
    WorldViewport::SetObserverForTesting(nullptr);
    std::vector<IWzGr2DLayer*> dense;
    for(int i=0;i<800;++i) dense.push_back(make((i%40)*45-900,(i/40)*45-450,int(0xC0000000)+i,0xff203040+(i&255)));
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);int tick=1000;
    auto measure=[&](bool scaled) {
        current=scaled ? field:nonField;
        for(int i=0;i<4;++i) {tick+=17;Check(gr->raw_UpdateCurrentTime(tick),"dense warmup clock");Check(gr->raw_RenderFrame(),"dense warmup");}
        LARGE_INTEGER begin{},end{};QueryPerformanceCounter(&begin);
        for(int i=0;i<24;++i) {tick+=17;Check(gr->raw_UpdateCurrentTime(tick),"dense clock");Check(gr->raw_RenderFrame(),"dense frame");}
        QueryPerformanceCounter(&end);
        return (end.QuadPart-begin.QuadPart)*1000.0/frequency.QuadPart/24;
    };
    const double native1=measure(false),scaled1=measure(true),scaled2=measure(true),native2=measure(false);
    printf("800-layer local timing: native %.3f ms/frame, clipped %.3f ms/frame (includes native Present; no pixel readback).\n",(native1+native2)/2,(scaled1+scaled2)/2);
    current=nullptr;for(auto layer:dense)layer->Release();
    marker->Release();hud->Release();child->Release();world->Release();
    Check(gr->raw_Uninitialize(),"uninitialize");gr->Release();term();DestroyWindow(hwnd);
    puts("PASS actual PCOM/Gr2D/Canvas + D3D8-to-9 GPU readback: consistent actor/equipment size, backdrop seams, two-axis clips, HUD isolation, fade and stage exit.");
}
