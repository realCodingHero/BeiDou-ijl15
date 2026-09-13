#include "UpscaleRenderer.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace NeuralUpscale;
void Check(HRESULT hr,const char* what){if(FAILED(hr)){printf("FAIL %s %08lX\n",what,hr);exit(1);}}
void Wait(IDirect3DQuery9* query,void* data,DWORD size){
    const auto end=GetTickCount64()+2000;
    HRESULT hr;
    while((hr=query->GetData(data,size,D3DGETDATA_FLUSH))==S_FALSE && GetTickCount64()<end)SwitchToThread();
    if(hr!=S_OK){printf("FAIL GPU timestamp wait %08lX\n",hr);exit(1);}
}
int main(){
    WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandle(nullptr);cls.lpszClassName="NeuralBenchmark";RegisterClassA(&cls);
    HWND window=CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,"GPU benchmark",WS_POPUP,0,0,64,64,nullptr,nullptr,cls.hInstance,nullptr);
    ComPtr<IDirect3D9> d3d;d3d.Attach(Direct3DCreate9(D3D_SDK_VERSION));if(!d3d)return 1;
    D3DADAPTER_IDENTIFIER9 adapter{};d3d->GetAdapterIdentifier(0,0,&adapter);printf("GPU,%s\n",adapter.Description);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=64;pp.BackBufferHeight=64;
    ComPtr<IDirect3DDevice9> device;Check(d3d->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device),"device");
    ComPtr<IDirect3DQuery9> start,end,freq,disjoint;
    Check(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start),"start timestamp");Check(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end),"end timestamp");
    Check(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&freq),"frequency");Check(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint),"disjoint");
    Renderer renderer(device.Get());
    printf("input,output,mode,gpu_median_ms,gpu_p95_ms,cpu_submit_median_ms\n");
    for(auto dimensions : {std::array<UINT,4>{1280,720,1920,1080},{1280,720,2560,1440},{1920,1080,3840,2160},{1920,1080,3365,1893},{1920,1080,2560,1440}}){
        ComPtr<IDirect3DSurface9> source,target;
        Check(device->CreateRenderTarget(dimensions[0],dimensions[1],D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&source,nullptr),"source");
        Check(device->CreateRenderTarget(dimensions[2],dimensions[3],D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr),"target");
        device->ColorFill(source.Get(),nullptr,0xff7f8fa0);
        for(int mode=0;mode<3;++mode){
            Settings settings;settings.enabled=true;settings.algorithm=mode==0?Algorithm::Linear:Algorithm::Cunny;settings.quality=mode==1?Quality::Fast:Quality::Balanced;
            std::vector<double> gpu,cpu;
            for(int frame=0;frame<50;++frame){
                disjoint->Issue(D3DISSUE_BEGIN);start->Issue(D3DISSUE_END);
                const auto before=std::chrono::steady_clock::now();
                Check(renderer.Render(source.Get(),target.Get(),settings),"render");
                const double submit=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();
                end->Issue(D3DISSUE_END);freq->Issue(D3DISSUE_END);disjoint->Issue(D3DISSUE_END);
                UINT64 a=0,b=0,f=0;BOOL changed=FALSE;Wait(end.Get(),&b,sizeof(b));Wait(start.Get(),&a,sizeof(a));Wait(freq.Get(),&f,sizeof(f));Wait(disjoint.Get(),&changed,sizeof(changed));
                if(frame>=10 && !changed && f){gpu.push_back(double(b-a)*1000/f);cpu.push_back(submit);}
            }
            if(gpu.empty())return 1;std::sort(gpu.begin(),gpu.end());std::sort(cpu.begin(),cpu.end());
            printf("%ux%u,%ux%u,%s,%.3f,%.3f,%.3f\n",dimensions[0],dimensions[1],dimensions[2],dimensions[3],mode==0?"linear":mode==1?"fast":"balanced",gpu[gpu.size()/2],gpu[(gpu.size()-1)*95/100],cpu[cpu.size()/2]);fflush(stdout);
        }
    }
    renderer.Reset();start.Reset();end.Reset();freq.Reset();disjoint.Reset();device.Reset();d3d.Reset();DestroyWindow(window);
}
