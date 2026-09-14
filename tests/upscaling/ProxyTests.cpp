#include "d3d8.hpp"
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <string>

void Check(HRESULT hr, const char* what) { if (FAILED(hr)) { printf("FAIL %s %08lX\n",what,hr);exit(1); } }
void Require(bool ok,const char* what) { if (!ok) { printf("FAIL %s\n",what);exit(1); } }
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    const std::string mode=argv[2];
    const bool enabled=mode=="enabled" || mode=="linear";
    const bool linear=mode=="linear";
    char exe[MAX_PATH]{};GetModuleFileNameA(nullptr,exe,MAX_PATH);
    const auto directory=std::filesystem::path(exe).parent_path();
    // This executable lives in out/neural/fixture; never touch a game's config.
    Require(directory.filename()=="fixture","isolated config directory");
    {std::ofstream config(directory/"config.ini");config<<"[upscaling]\nenabled="<<(enabled?"true":"false")<<"\nalgorithm="<<(linear?"linear":"cunny")<<"\nquality=balanced\n";}
    HMODULE module=LoadLibraryA(argv[1]);Require(module!=nullptr,"load built DLL");
    auto factory=reinterpret_cast<IDirect3D8*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate8"));
    Require(factory!=nullptr,"factory export");
    WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandle(nullptr);cls.lpszClassName="MapleStoryClass";
    RegisterClassA(&cls);
    HWND window=CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,"Isolated proxy fixture",WS_POPUP,0,0,128,96,nullptr,nullptr,cls.hInstance,nullptr);
    Require(window!=nullptr,"window");
    IDirect3D8* d3d=factory(220);Require(d3d!=nullptr,"create D3D8");
    D3DPRESENT_PARAMETERS8 pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth=64;pp.BackBufferHeight=48;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    IDirect3DDevice8* device=nullptr;
    Check(d3d->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device),"D3D8 device");
    auto frame=[&] {
        Check(device->BeginScene(),"begin scene");
        Check(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff739ac5,1,0),"clear");
        Check(device->EndScene(),"end scene");
        Check(device->Present(nullptr,nullptr,nullptr,nullptr),"present");
        RECT fullSource{0,0,static_cast<LONG>(pp.BackBufferWidth),static_cast<LONG>(pp.BackBufferHeight)}, fullTarget{};
        GetClientRect(window,&fullTarget);
        Check(device->Present(&fullSource,&fullTarget,nullptr,nullptr),"explicit full rectangles");
        IDirect3DSurface8* back=nullptr;Check(device->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&back),"backbuffer");
        D3DSURFACE_DESC8 desc{};Check(back->GetDesc(&desc),"description");back->Release();
        Require(desc.Width==pp.BackBufferWidth && desc.Height==pp.BackBufferHeight,"logical backbuffer unchanged");
    };
    // Managed texture semantics must survive adding the upscaler.
    IDirect3DTexture8* managed=nullptr;
    Check(device->CreateTexture(16,16,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&managed),"managed texture");
    D3DLOCKED_RECT lock{};Check(managed->LockRect(0,&lock,nullptr,0),"managed lock");
    *static_cast<DWORD*>(lock.pBits)=0xff123456;managed->UnlockRect(0);
    DWORD token=0;Check(device->CreateStateBlock(D3DSBT_ALL,&token),"game state block");
    for(int cycle=0;cycle<12;++cycle){
        const int scale=cycle%3+1;
        SetWindowPos(window,nullptr,0,0,64*scale,48*scale,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        frame();
    }
    Check(device->DeleteStateBlock(token),"release game state block");
    for(int cycle=0;cycle<5;++cycle){Check(device->Reset(&pp),"reset");frame();}
    Check(managed->LockRect(0,&lock,nullptr,D3DLOCK_READONLY),"managed after reset");
    Require(*static_cast<DWORD*>(lock.pBits)==0xff123456,"managed contents survive reset");managed->UnlockRect(0);
    managed->Release();
    Require(device->Release()==0,"device released including upscaler resources");
    d3d->Release();DestroyWindow(window);FreeLibrary(module);
    if(enabled){
        std::ifstream input(directory/"upscaling.log");
        const std::string log((std::istreambuf_iterator<char>(input)),{});
        Require(log.find("active:")!=std::string::npos,"neural presentation actually active");
        Require(log.find("failed")==std::string::npos,"no silent fallback");
        if(linear) {
            Require(log.find("backend=single-pass-linear")!=std::string::npos,"single-pass linear presentation active");
            Require(log.find("CuNNy=yes")==std::string::npos,"linear mode never executes CuNNy");
        }
    }
    printf("PASS proxy %s: real D3D8 calls, resize, logical dimensions, managed textures, state blocks, reset, final Release\n",argv[2]);
}
