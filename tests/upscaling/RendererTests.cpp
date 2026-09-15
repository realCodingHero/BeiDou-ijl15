#include "UpscaleRenderer.h"
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <vector>
#include <cstring>
#include <string>
using Microsoft::WRL::ComPtr;
using namespace NeuralUpscale;
void Check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) { printf("FAIL %s: %08lX\n", operation, hr); exit(1); }
}
void Require(bool condition, const char* what) { if (!condition) { printf("FAIL %s\n", what); exit(1); } }
ComPtr<IDirect3DSurface9> Target(IDirect3DDevice9* device, UINT w, UINT h, bool memory = false) {
    ComPtr<IDirect3DSurface9> result;
    if (memory) Check(device->CreateOffscreenPlainSurface(w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&result,nullptr), "staging");
    else Check(device->CreateRenderTarget(w,h,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&result,nullptr), "target");
    return result;
}
void Save(IDirect3DDevice9* device, IDirect3DSurface9* source, const std::filesystem::path& path) {
    D3DSURFACE_DESC desc{}; source->GetDesc(&desc);
    auto staging = Target(device, desc.Width, desc.Height, true);
    Check(device->GetRenderTargetData(source, staging.Get()), "readback");
    D3DLOCKED_RECT locked{}; Check(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY), "read lock");
    std::ofstream output(path, std::ios::binary);
    for (UINT y = 0; y < desc.Height; ++y) output.write(static_cast<char*>(locked.pBits)+y*locked.Pitch, desc.Width*4);
    staging->UnlockRect();
}
void TestLoginCrop(IDirect3DDevice9* device) {
    Renderer renderer(device);
    Settings linear; linear.enabled = true; linear.algorithm = Algorithm::Linear;
    const RECT crop{8,6,24,18};
    auto source = Target(device,32,24);
    Check(device->ColorFill(source.Get(),nullptr,0xFFFF0000),"crop border");
    Check(device->ColorFill(source.Get(),&crop,0xFF2080C0),"crop interior");
    // Covers window scaling and in-place fullscreen backbuffer composition.
    for (bool inPlace : {false,true}) {
        auto output = inPlace ? source : Target(device,48,27);
        Check(renderer.Render(source.Get(),output.Get(),linear,&crop),"login viewport render");
        D3DSURFACE_DESC desc{}; output->GetDesc(&desc);
        auto read = Target(device,desc.Width,desc.Height,true);
        Check(device->GetRenderTargetData(output.Get(),read.Get()),"crop readback");
        D3DLOCKED_RECT lock{}; Check(read->LockRect(&lock,nullptr,D3DLOCK_READONLY),"crop lock");
        for (UINT y=0;y<desc.Height;++y) for (UINT x=0;x<desc.Width;++x) {
            const DWORD pixel = reinterpret_cast<const DWORD*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch)[x];
            Require((pixel&0xFFFFFF)==0x2080C0,"no black border or outside pixels in crop");
        }
        read->UnlockRect();
        Require(renderer.InternalReferences() > 0 && renderer.InternalReferences() <= 4,"crop stays in linear resource budget");
    }
    RECT invalid{-1,0,16,16};
    Require(renderer.Render(source.Get(),source.Get(),linear,&invalid)==E_INVALIDARG,"reject out-of-range viewport");
    invalid={0,0,0,16};
    Require(renderer.Render(source.Get(),source.Get(),linear,&invalid)==E_INVALIDARG,"reject empty viewport");
    puts("PASS GPU: window/fullscreen crop pixels, invalid bounds and linear resource budget");
}
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::filesystem::path folder(argv[1]);
    const UINT w = argc > 2 ? static_cast<UINT>(atoi(argv[2])) : 64;
    const UINT h = argc > 3 ? static_cast<UINT>(atoi(argv[3])) : 48;
    WNDCLASSA cls{}; cls.lpfnWndProc=DefWindowProcA; cls.hInstance=GetModuleHandle(nullptr); cls.lpszClassName="NeuralFixture";
    RegisterClassA(&cls);
    HWND window=CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,"Neural GPU fixture",WS_POPUP,0,0,w,h,nullptr,nullptr,cls.hInstance,nullptr);
    Require(window != nullptr, "fixture window");
    ComPtr<IDirect3D9> d3d; d3d.Attach(Direct3DCreate9(D3D_SDK_VERSION)); Require(d3d != nullptr, "D3D9");
    D3DADAPTER_IDENTIFIER9 adapter{}; d3d->GetAdapterIdentifier(0,0,&adapter);
    printf("GPU: %s\n",adapter.Description);
    D3DPRESENT_PARAMETERS pp{}; pp.Windowed=TRUE; pp.hDeviceWindow=window; pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth=w; pp.BackBufferHeight=h; pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    ComPtr<IDirect3DDevice9> device;
    Check(d3d->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device), "create device");
    TestLoginCrop(device.Get());
    auto source=Target(device.Get(),w,h), upload=Target(device.Get(),w,h,true);
    std::ifstream input(folder/"input.bgra",std::ios::binary);
    Require(input.good(), "input fixture");
    D3DLOCKED_RECT locked{}; Check(upload->LockRect(&locked,nullptr,0),"upload lock");
    for (UINT y=0;y<h;++y) input.read(static_cast<char*>(locked.pBits)+y*locked.Pitch,w*4);
    Require(input.good(),"complete input"); upload->UnlockRect();
    Check(device->UpdateSurface(upload.Get(),nullptr,source.Get(),nullptr),"upload");
    Renderer renderer(device.Get());
    Settings settings; settings.enabled=true;
    D3DVIEWPORT9 sentinel{3,5,w-6,h-10,.2f,.8f};
    device->SetViewport(&sentinel); device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
    device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);
    {
        Renderer direct(device.Get());
        Settings linearSettings; linearSettings.enabled=true; linearSettings.algorithm=Algorithm::Linear;
        ULONG linearReferences=0;
        for (auto size : {std::pair<UINT,UINT>{w,h},{w*3/2,h*3/2},{w/2,h/2},{w*2,h*2},{w*5/3,h*5/3}}) {
            auto target=Target(device.Get(),size.first,size.second);
            Check(direct.Render(source.Get(),target.Get(),linearSettings),"single-pass linear");
            if (size.first==w && size.second==h) {
                Require(direct.InternalReferences()==0,"identity allocates no renderer resources");
            } else {
                const ULONG references=direct.InternalReferences();
                Require(references>0 && references<=4,"linear retains only one shader and input texture");
                if (linearReferences) Require(references==linearReferences,"linear resources stable across output sizes");
                linearReferences=references;
            }
            D3DVIEWPORT9 after{}; device->GetViewport(&after);
            Require(memcmp(&sentinel,&after,sizeof(after))==0,"linear preserves viewport");
            DWORD state=0; device->GetRenderState(D3DRS_ALPHABLENDENABLE,&state);
            Require(state==TRUE,"linear preserves blend state");
            device->GetSamplerState(0,D3DSAMP_ADDRESSU,&state);
            Require(state==D3DTADDRESS_WRAP,"linear preserves sampler state");
            Save(device.Get(),target.Get(),folder/("linear-"+std::to_string(size.first)+"x"+std::to_string(size.second)+".bgra"));
        }
    }
    for (auto quality : {Quality::Fast,Quality::Balanced}) {
        settings.quality=quality;
        auto target=Target(device.Get(),w*2,h*2);
        Check(renderer.Render(source.Get(),target.Get(),settings),"neural render");
        D3DVIEWPORT9 after{}; device->GetViewport(&after);
        Require(memcmp(&sentinel,&after,sizeof(after))==0,"restore viewport");
        DWORD state=0;device->GetRenderState(D3DRS_ALPHABLENDENABLE,&state);Require(state==TRUE,"restore blend");
        device->GetSamplerState(0,D3DSAMP_ADDRESSU,&state);Require(state==D3DTADDRESS_WRAP,"restore sampler");
        Save(device.Get(),target.Get(),folder/(quality==Quality::Fast ? "fast.bgra" : "balanced.bgra"));
    }
    auto identity=Target(device.Get(),w,h);Check(renderer.Render(source.Get(),identity.Get(),settings),"identity");
    Save(device.Get(),identity.Get(),folder/"identity.bgra");
    for (auto size : {std::pair<UINT,UINT>{w*3/2,h*3/2},{w/2,h/2},{w*2,h*2},{w*3/2,h*3/2}}) {
        auto target=Target(device.Get(),size.first,size.second);
        Check(renderer.Render(source.Get(),target.Get(),settings),"resize");
        Save(device.Get(),target.Get(),folder/(std::to_string(size.first)+"x"+std::to_string(size.second)+".bgra"));
    }
    settings.algorithm=Algorithm::Linear;
    auto linear=Target(device.Get(),w*2,h*2);Check(renderer.Render(source.Get(),linear.Get(),settings),"linear");
    Save(device.Get(),linear.Get(),folder/"linear.bgra");
    identity.Reset();linear.Reset();source.Reset();upload.Reset();renderer.Reset();
    Check(device->Reset(&pp),"device reset after neural resources released");
    device->AddRef(); const ULONG refs=device->Release();
    Require(refs==1,"no retained device references");
    printf("PASS GPU render: lightweight linear resource budget/state, two networks, identity, shrink, fractional resize, reset and resource release\n");
    device.Reset();d3d.Reset();DestroyWindow(window);
}
