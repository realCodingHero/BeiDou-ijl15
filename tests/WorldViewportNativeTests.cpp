#include "WorldViewport.h"
#include "UpscaleLoader.h"
#include "d3d8.hpp"
#include "WzLib/IWzGr2D.h"
#include "WzLib/IWzCanvas.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {
int worldDraws=0,hudDraws=0;
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
    if(argc!=2)return 2;
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
    void* current=&current;WorldViewport::SetContextForTesting(&current,current,{-728,-413,728,413},1920,1080);
    WorldViewport::SetObserverForTesting(Observe);
    auto make=[&](int x,int y,int z,DWORD color,IWzGr2DLayer* parent=nullptr) {
        assert(SetCurrentDirectoryA(argv[1]));
        IWzCanvas* canvas=nullptr;Check(create(L"Canvas",&__uuidof(IWzCanvas),reinterpret_cast<void**>(&canvas),nullptr),"actual Canvas create");
        assert(SetCurrentDirectoryW(directory.c_str()));
        VARIANT empty{},format{};format.vt=VT_I4;format.lVal=2;
        Check(canvas->raw_Create(60,40,empty,format),"canvas allocate");
        Check(canvas->raw_DrawRectangle(0,0,60,40,color),"canvas fill");
        VARIANT source{},filter{};source.vt=VT_UNKNOWN;source.punkVal=canvas;filter.vt=VT_I4;filter.lVal=1;
        IWzGr2DLayer* layer=nullptr;Check(gr->raw_CreateLayer(x,y,60,40,z,source,filter,&layer),"native layer");
        Check(layer->put_color(0xffffffff),"layer opacity");
        if(parent) {VARIANT overlay{};overlay.vt=VT_UNKNOWN;overlay.punkVal=parent;Check(layer->put_overlay(overlay),"equipment overlay");}
        canvas->Release();return layer;
    };
    auto world=make(-400,-200,-1000000000,0xffff0000);
    auto child=make(-200,-100,1,0xff00ff00,world);
    auto hud=make(200,100,10,0xff0000ff);
    marker=make(400,200,0x7ffffffd,0xffffff00);
    for(int i=0;i<3;++i) {Check(gr->raw_UpdateCurrentTime(100+i*100),"clock");Check(gr->raw_RenderFrame(),"actual frame");}
    assert(worldDraws && hudDraws && !pixels.empty());
    auto red=ColorBounds(0xff0000),green=ColorBounds(0x00ff00),blue=ColorBounds(0x0000ff);
    printf("native pixels: world (%ld,%ld)-(%ld,%ld), equipment (%ld,%ld)-(%ld,%ld), HUD (%ld,%ld)-(%ld,%ld); draws world=%d HUD=%d\n",
        red.left,red.top,red.right,red.bottom,green.left,green.top,green.right,green.bottom,blue.left,blue.top,blue.right,blue.bottom,worldDraws,hudDraws);
    assert(red.right-red.left>=77 && red.right-red.left<=81);
    assert(green.right-green.left>=77 && green.right-green.left<=81);
    assert(blue.right-blue.left==59 && blue.bottom-blue.top==39);
    current=nullptr;
    const int before=hudDraws;
    for(int i=0;i<3;++i) {Sleep(20);Check(gr->raw_UpdateCurrentTime(500+i*100),"stage clock");Check(gr->raw_RenderFrame(),"leave field");}
    red=ColorBounds(0xff0000);
    printf("after stage exit: world (%ld,%ld)-(%ld,%ld), new draws=%d\n",red.left,red.top,red.right,red.bottom,hudDraws-before);
    assert(red.right-red.left==59 && red.bottom-red.top==39 && hudDraws>before);
    // Exercise the hook under many layers without diagnostic pixel readback.
    // This is a local overhead measurement, not an in-game FPS guarantee.
    WorldViewport::SetObserverForTesting(nullptr);
    std::vector<IWzGr2DLayer*> dense;
    for(int i=0;i<800;++i) dense.push_back(make((i%40)*45-900,(i/40)*45-450,-1000000000+i,0xff203040+(i&255)));
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);int tick=1000;
    auto measure=[&](bool scaled) {
        current=scaled ? &current:nullptr;
        for(int i=0;i<4;++i) {tick+=17;Check(gr->raw_UpdateCurrentTime(tick),"dense warmup clock");Check(gr->raw_RenderFrame(),"dense warmup");}
        LARGE_INTEGER begin{},end{};QueryPerformanceCounter(&begin);
        for(int i=0;i<24;++i) {tick+=17;Check(gr->raw_UpdateCurrentTime(tick),"dense clock");Check(gr->raw_RenderFrame(),"dense frame");}
        QueryPerformanceCounter(&end);
        return (end.QuadPart-begin.QuadPart)*1000.0/frequency.QuadPart/24;
    };
    const double native1=measure(false),scaled1=measure(true),scaled2=measure(true),native2=measure(false);
    printf("800-layer local timing: native %.3f ms/frame, scaled %.3f ms/frame (includes native Present; no pixel readback).\n",(native1+native2)/2,(scaled1+scaled2)/2);
    current=nullptr;for(auto layer:dense)layer->Release();
    marker->Release();hud->Release();child->Release();world->Release();
    Check(gr->raw_Uninitialize(),"uninitialize");gr->Release();term();DestroyWindow(hwnd);
    puts("PASS actual PCOM/Gr2D/Canvas + D3D8-to-9 GPU readback: world and equipment scale, HUD unchanged, stage exit restores native draw size.");
}
