#include "../ezorsia/QuestHelperUI.cpp"
#include "WorldViewport.h"
#include "UpscaleLoader.h"
#include "d3d8.hpp"
#include "WzLib/IWzGr2D.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
int Client::m_nGameWidth=800;
int Client::m_nGameHeight=600;
bool Client::SwitchChinese=true;
namespace {
std::vector<DWORD> frame;
IWzGr2DLayer* marker=nullptr;
void Check(HRESULT hr,const char* name){if(FAILED(hr)){printf("FAIL %s %08lX\n",name,hr);exit(1);}}
void Observe(void* layer,void* context,double){
    if(layer!=marker)return;
    auto device=*static_cast<IDirect3DDevice8**>(context);
    IDirect3DSurface8* target=nullptr;Check(device->GetRenderTarget(&target),"target");
    D3DSURFACE_DESC8 desc{};Check(target->GetDesc(&desc),"description");
    IDirect3DSurface8* image=nullptr;Check(device->CreateImageSurface(desc.Width,desc.Height,desc.Format,&image),"readback");
    Check(device->CopyRects(target,nullptr,0,image,nullptr),"copy");D3DLOCKED_RECT lock{};
    Check(image->LockRect(&lock,nullptr,D3DLOCK_READONLY),"lock");frame.resize(desc.Width*desc.Height);
    for(unsigned y=0;y<desc.Height;++y)memcpy(frame.data()+y*desc.Width,static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch,desc.Width*4);
    image->UnlockRect();image->Release();target->Release();
}
}
int main(int argc,char** argv){
    assert(argc==3);setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    const auto out=std::filesystem::absolute(argv[2]);assert(SetCurrentDirectoryW(out.c_str()));assert(SetDllDirectoryA(argv[1]));
    std::ofstream(out/L"config.ini")<<"[upscaling]\nenabled=false\n";
    auto pcom=LoadLibraryA("PCOM.dll");assert(pcom);
    auto init=reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(pcom,"PcInitModule"));
    auto pcCreate=reinterpret_cast<HRESULT(__cdecl*)(const wchar_t*,const GUID*,void**,IUnknown*)>(GetProcAddress(pcom,"PcCreateObject"));
    Check(init(),"PCOM");assert(UpscaleLoader::Install(GetModuleHandle(nullptr),true));
    assert(SetCurrentDirectoryA(argv[1]));IWzGr2D* gr=nullptr;
    Check(pcCreate(L"Gr2D_DX8",&__uuidof(IWzGr2D),reinterpret_cast<void**>(&gr),nullptr),"Gr2D");assert(SetCurrentDirectoryW(out.c_str()));
    WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandle(nullptr);cls.lpszClassName="QuestGraphicsFixture";RegisterClassA(&cls);
    auto hwnd=CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,"Quest graphics fixture",WS_POPUP,0,0,800,600,nullptr,nullptr,cls.hInstance,nullptr);assert(hwnd);
    VARIANT window{},bpp{},rate{},empty{};window.vt=VT_BYREF;window.byref=hwnd;bpp.vt=VT_I4;bpp.lVal=32;rate.vt=VT_I4;rate.lVal=60;
    Check(gr->put_fullScreen(0),"windowed");Check(gr->raw_Initialize(800,600,window,bpp,rate),"initialize");Check(gr->put_backColor(0xff000000),"clear");
    assert(WorldViewport::InstallGraphics(GetModuleHandleA("Gr2D_DX8.dll")));WorldViewport::SetObserverForTesting(Observe);
    VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
    IWzGr2DLayer* layer=nullptr;Check(gr->raw_CreateLayer(-300,-225,600,450,int(0xC0061564),missing,missing,&layer),"CWnd layer");
    Check(layer->put_color(0xffffffff),"opacity");IWzCanvas* canvas=nullptr;Check(layer->get_canvas(empty,&canvas),"CWnd canvas");
    unsigned tw=0,th=0,cw=0,ch=0;canvas->get_tileWidth(&tw);canvas->get_tileHeight(&th);canvas->get_width(&cw);canvas->get_height(&ch);
    printf("GPU canvas %ux%u tiles %ux%u\n",cw,ch,tw,th);
    assert(tw==256 && th==256); // Exercise the actual GPU allocator, not a software canvas.
    Bitmap image(600,450);for(int y=0;y<450;++y)for(int x=0;x<600;++x)
        image.pixels[y*600+x]=0xff000000|((0x33+0x44*(x/256))<<16)|((y<256?0x55:0xaa)<<8)|0x77;
    Check(canvas->raw_DrawRectangle(0,0,600,450,0xffffffff),"native clear");
    const bool uploaded=Upload(canvas,image);printf("Upload result %d\n",uploaded);assert(uploaded);
    for(int y=0;y<450;++y)for(int x=0;x<600;++x){unsigned color=0;Check(canvas->get_pixel(x,y,&color),"canvas readback");assert(color==image.pixels[y*600+x]);}
    IWzCanvas* dot=nullptr;Check(pcCreate(L"Canvas",&CanvasGuid,reinterpret_cast<void**>(&dot),nullptr),"marker");Check(dot->raw_Create(1,1,empty,empty),"marker allocate");
    VARIANT source{};source.vt=VT_UNKNOWN;source.punkVal=dot;Check(gr->raw_CreateLayer(390,290,0,0,0x7ffffffd,source,empty,&marker),"marker layer");Check(marker->put_color(0xffffffff),"marker opacity");
    for(int n=0;n<3;++n){Check(gr->raw_UpdateCurrentTime(100+n*20),"clock");Check(gr->raw_RenderFrame(),"frame");}
    assert(frame.size()==800*600);
    BITMAPFILEHEADER f{};f.bfType=0x4D42;f.bfOffBits=sizeof(f)+sizeof(BITMAPINFOHEADER);f.bfSize=f.bfOffBits+800*600*4;
    BITMAPINFOHEADER h{};h.biSize=sizeof(h);h.biWidth=800;h.biHeight=-600;h.biPlanes=1;h.biBitCount=32;
    std::ofstream bmp(out/L"quest-gpu.bmp",std::ios::binary);bmp.write(reinterpret_cast<char*>(&f),sizeof(f));bmp.write(reinterpret_cast<char*>(&h),sizeof(h));bmp.write(reinterpret_cast<char*>(frame.data()),frame.size()*4);
    for(int y=2;y<448;++y)for(int x=2;x<598;++x){
        // Bilinear filtering can blend the single-pixel boundary between tiles.
        if(x%256<2 || x%256>253 || y%256<2 || y%256>253)continue;
        assert((frame[(75+y)*800+100+x]&0xffffff)==(image.pixels[y*600+x]&0xffffff));
    }
    puts("PASS actual DirectX canvas: all six GPU tiles, narrow edge tiles, canvas readback and rendered screen pixels");
}
