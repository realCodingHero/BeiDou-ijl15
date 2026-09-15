#include "WorldViewport.h"
#include "UpscaleLoader.h"
#include "d3d8.hpp"
#include "WzLib/IWzGr2D.h"
#include "WzLib/IWzCanvas.h"
#include "WzLib/IWzProperty.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace {
struct Sample {std::string id;RECT bounds{};double expected=1;std::vector<RECT> art;};
std::vector<DWORD> pixels;
IWzGr2DLayer* marker=nullptr;
void Check(HRESULT hr,const char* name){if(FAILED(hr)){printf("FAIL %s %08lX\n",name,hr);exit(1);}}
int __fastcall IsField(void*,void*,const void*){return 1;}
void Observe(void* layer,void* context,double){
 if(layer!=marker)return;
 auto device=*static_cast<IDirect3DDevice8**>(context);
 IDirect3DSurface8* target=nullptr;Check(device->GetRenderTarget(&target),"target");
 D3DSURFACE_DESC8 desc{};Check(target->GetDesc(&desc),"description");assert(desc.Width==1920&&desc.Height==1080);
 IDirect3DSurface8* image=nullptr;Check(device->CreateImageSurface(desc.Width,desc.Height,desc.Format,&image),"readback");
 Check(device->CopyRects(target,nullptr,0,image,nullptr),"copy");D3DLOCKED_RECT lock{};
 Check(image->LockRect(&lock,nullptr,D3DLOCK_READONLY),"lock");pixels.resize(1920*1080);
 for(int y=0;y<1080;++y)memcpy(pixels.data()+y*1920,static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch,1920*4);
 image->UnlockRect();image->Release();target->Release();
}
DWORD Pixel(int x,int y){return pixels[y*1920+x]&0xffffff;}
RECT Bounds(DWORD color){
 RECT r{1920,1080,-1,-1};
 for(int y=0;y<1080;++y)for(int x=0;x<1920;++x)if(Pixel(x,y)==color){r.left=std::min(r.left,LONG(x));r.top=std::min(r.top,LONG(y));r.right=std::max(r.right,LONG(x));r.bottom=std::max(r.bottom,LONG(y));}
 return r;
}
void Save(const std::filesystem::path& path){
 BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+DWORD(pixels.size()*4);
 BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=1920;info.biHeight=-1080;info.biPlanes=1;info.biBitCount=32;
 std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<char*>(&file),sizeof(file));out.write(reinterpret_cast<char*>(&info),sizeof(info));out.write(reinterpret_cast<char*>(pixels.data()),pixels.size()*4);
}
}
int main(int argc,char** argv){
 if(argc!=4)return 2;const std::string mode=argv[3];const bool baseline=mode=="baseline";
 const bool buffTest=mode=="buff"||mode=="buff-before",expectBuffBug=mode=="buff-before";
 setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 wchar_t exe[MAX_PATH];GetModuleFileNameW(nullptr,exe,MAX_PATH);const auto out=std::filesystem::path(exe).parent_path();
 assert(out.filename()==L"world-native");assert(SetCurrentDirectoryW(out.c_str()));assert(SetDllDirectoryA(argv[1]));
 std::ofstream(out/L"config.ini")<<"[upscaling]\nenabled=false\n";
 auto pcom=LoadLibraryA("PCOM.dll");assert(pcom);
 auto init=reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(pcom,"PcInitModule"));
 auto term=reinterpret_cast<void(__cdecl*)()>(GetProcAddress(pcom,"PcTermModule"));
 auto create=reinterpret_cast<HRESULT(__cdecl*)(const wchar_t*,const GUID*,void**,IUnknown*)>(GetProcAddress(pcom,"PcCreateObject"));
 assert(init&&term&&create);Check(init(),"PCOM");assert(UpscaleLoader::Install(GetModuleHandle(nullptr),true));
 assert(SetCurrentDirectoryA(argv[1]));IWzGr2D* gr=nullptr;
 Check(create(L"Gr2D_DX8",&__uuidof(IWzGr2D),reinterpret_cast<void**>(&gr),nullptr),"Gr2D");assert(SetCurrentDirectoryW(out.c_str()));
 WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandle(nullptr);cls.lpszClassName="MapScaleFixture";RegisterClassA(&cls);
 auto hwnd=CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,"Map scale fixture",WS_POPUP,0,0,1920,1080,nullptr,nullptr,cls.hInstance,nullptr);assert(hwnd);
 VARIANT window{},bpp{},rate{};window.vt=VT_BYREF;window.byref=hwnd;bpp.vt=VT_I4;bpp.lVal=32;rate.vt=VT_I4;rate.lVal=60;
 Check(gr->put_fullScreen(0),"windowed");Check(gr->raw_Initialize(1920,1080,window,bpp,rate),"initialize");Check(gr->put_backColor(0xff000000),"clear");
 assert(WorldViewport::InstallGraphics(GetModuleHandleA("Gr2D_DX8.dll")));WorldViewport::SetObserverForTesting(Observe);
 auto canvas=[&](int w,int h,DWORD color){
  assert(SetCurrentDirectoryA(argv[1]));IWzCanvas* c=nullptr;Check(create(L"Canvas",&__uuidof(IWzCanvas),reinterpret_cast<void**>(&c),nullptr),"Canvas");assert(SetCurrentDirectoryW(out.c_str()));
  VARIANT empty{},format{};format.vt=VT_I4;format.lVal=2;Check(c->raw_Create(w,h,empty,format),"allocate");
  Check(c->raw_DrawRectangle(0,0,w,h,color),"fill");Check(c->put_cx(0),"origin X");Check(c->put_cy(0),"origin Y");return c;
 };
 auto layer=[&](IWzCanvas* c,int x,int y,int z){
  VARIANT source{},filter{};source.vt=VT_UNKNOWN;source.punkVal=c;filter.vt=VT_I4;filter.lVal=1;IWzGr2DLayer* l=nullptr;
  Check(gr->raw_CreateLayer(x,y,0,0,z,source,filter,&l),"layer");Check(l->put_color(0xffffffff),"opacity");return l;
 };
 std::ifstream runs(out/L"temple-keeper.runs",std::ios::binary);assert(runs);int header[7];runs.read(reinterpret_cast<char*>(header),sizeof(header));
 auto npc=canvas(header[0],header[1],0);
 for(int n=0;n<header[6];++n){int r[4];runs.read(reinterpret_cast<char*>(r),sizeof(r));assert(runs);Check(npc->raw_DrawRectangle(r[0],r[1],r[2],1,unsigned(r[3])),"NPC pixels");}
 auto bar=canvas(1200,40,0xff0000ff),dot=canvas(1,1,0xffffffff);
 std::vector<IWzGr2DLayer*> tiles;
 for(int i=0;i<400;++i){
  auto tile=canvas(120,80,0xff20d0e0);
  tiles.push_back(layer(tile,-1200+(i%20)*120,-800+(i/20)*80,int(0xBFFE0000)));tile->Release();
 }
 auto actor=layer(npc,-header[0]/2,-header[1]/2,int(0xC0000000));
 auto hud=layer(bar,-600,490,int(0xC00615D0));marker=layer(dot,900,530,0x7ffffffd);
 // Two 32px screen icons and a separate cooldown layer at the real native Z.
 // The middle marker straddles Great Tree I's right scene clip (x=1860).
 const DWORD buffColors[]={0xff00ff,0xff0000,0x00ff00};
 const int buffX[]={1885,1853,1817};std::vector<IWzGr2DLayer*> buffLayers;
 if(buffTest)for(int i=0;i<3;++i){
  auto c=canvas(32,32,0xff000000|buffColors[i]);
  buffLayers.push_back(layer(c,buffX[i]-960,23-540,int(0xC006156C)));c->Release();
 }
 alignas(4) unsigned char field[0x110]{};void* methods[19]{};methods[18]=reinterpret_cast<void*>(&IsField);*reinterpret_cast<void***>(field+4)=methods;void* current=field;
 std::ifstream input(argv[2]);assert(input);std::string line;int count=0,tick=100;
 std::vector<Sample> samples;Sample sample;
 while(std::getline(input,line)){
  if(line.empty()||line[0]=='#')continue;std::istringstream row(line);std::string kind;row>>kind;
  if(baseline){sample={};sample.id=kind;row>>sample.bounds.left>>sample.bounds.top>>sample.bounds.right>>sample.bounds.bottom>>sample.expected;assert(row);samples.push_back(sample);}
  else if(kind=="map"){sample={};row>>sample.id>>sample.bounds.left>>sample.bounds.top>>sample.bounds.right>>sample.bounds.bottom>>sample.expected;assert(row);}
  else if(kind=="rect"){RECT r;row>>r.left>>r.top>>r.right>>r.bottom;assert(row);sample.art.push_back(r);}
  else if(kind=="end")samples.push_back(sample);
 }
 std::ofstream csv(out/(baseline ? L"magnification-before.csv":L"magnification-after.csv"));csv<<"map,scale,npcWidth,npcHeight,clipLeft,clipTop,clipRight,clipBottom\n";
 std::ofstream buffCsv;
 if(buffTest){buffCsv.open(out/(expectBuffBug ? L"buff-before.csv":L"buff-after.csv"));buffCsv<<"map,scale,iconRightWidth,iconNextWidth,cooldownWidth\n";}
 for(const auto& s:samples){
  const auto& id=s.id;const RECT bounds=s.bounds;
  WorldViewport::SetContextForTesting(&current,field,bounds,1920,1080);auto v=WorldViewport::Fit(bounds,1920,1080);
#ifdef VIEWPORT_SCENERY_FIXED
  WorldViewport::BeginTerrain(field);WorldViewport::BeginObjects(field);
  std::vector<IWzProperty*> properties;std::vector<IWzCanvas*> canvases;
  for(const RECT& r:s.art){
   // Real PCOM metadata, non-zero origins and native entry-stack arguments.
   auto c=canvas(r.right-r.left,r.bottom-r.top,0);
   Check(c->put_cx(5),"scenery origin X");Check(c->put_cy(7),"scenery origin Y");
   assert(SetCurrentDirectoryA(argv[1]));IWzProperty* property=nullptr;
   Check(create(L"Property",&__uuidof(IWzProperty),reinterpret_cast<void**>(&property),nullptr),"scenery property");
   assert(SetCurrentDirectoryW(out.c_str()));VARIANT frame{},empty{};frame.vt=VT_UNKNOWN;frame.punkVal=c;
   BSTR key=SysAllocString(L"0");Check(property->raw_Add(key,frame,empty),"scenery frame");SysFreeString(key);
   int stack[14]{};stack[2]=reinterpret_cast<int>(property);stack[3]=3;stack[4]=r.left+5;stack[5]=r.top+7;
   WorldViewport::RecordSceneObject(stack,field);
   properties.push_back(property);canvases.push_back(c);
  }
  *reinterpret_cast<RECT*>(field+0xF0)={bounds.left+960,bounds.top+540,bounds.right-960,bounds.bottom-540};
  WorldViewport::AdjustCamera(field);v=WorldViewport::FitScenery(bounds,1920,1080,s.art);
  assert(!memcmp(field+0xF0,&v.camera,sizeof(RECT)));
  // Only metadata values remain after the native map load completes.
  for(auto property:properties)property->Release();for(auto c:canvases)c->Release();
#endif
  assert(std::abs(v.scale-s.expected)<1e-6);
  tick+=20;Check(gr->raw_UpdateCurrentTime(tick),"clock");Check(gr->raw_RenderFrame(),"frame");
  RECT visible{1920,1080,-1,-1};
  for(int y=std::max(300L,v.clip.top);y<std::min(780L,v.clip.bottom);++y)
   for(int x=std::max(600L,v.clip.left);x<std::min(1320L,v.clip.right);++x)
    if(Pixel(x,y)!=0x20d0e0){visible.left=std::min(visible.left,LONG(x));visible.top=std::min(visible.top,LONG(y));visible.right=std::max(visible.right,LONG(x));visible.bottom=std::max(visible.bottom,LONG(y));}
  int w=visible.right-visible.left+1,h=visible.bottom-visible.top+1;
  assert(std::abs(w-(header[4]-header[2])*v.scale)<=2&&std::abs(h-(header[5]-header[3])*v.scale)<=2);
  const auto hb=Bounds(0x0000ff);assert(hb.left==360&&hb.right==1559&&hb.top==1030&&hb.bottom==1069);
  if(buffTest){
   int widths[3]{};
   for(int i=0;i<3;++i){
    const auto b=Bounds(buffColors[i]);widths[i]=std::max(0L,b.right-b.left+1);
    if(!expectBuffBug)assert(b.left==buffX[i]&&b.right==buffX[i]+31&&b.top==23&&b.bottom==54);
   }
   if(expectBuffBug){
    if(id=="101010100")assert(widths[0]==0&&widths[1]==7&&widths[2]==32);
    else assert(widths[0]==0&&widths[1]==0&&widths[2]==0);
   }
   printf("Buff %s %s: right=%d next=%d cooldown=%d (full width=32)\n",expectBuffBug ? "before":"after",id.c_str(),widths[0],widths[1],widths[2]);
   buffCsv<<id<<','<<v.scale<<','<<widths[0]<<','<<widths[1]<<','<<widths[2]<<'\n';
   Save(out/(id+(expectBuffBug ? "-buff-before.bmp":"-buff-after.bmp")));
  }
  // The full-window HUD may cover the last few scene rows on a narrow map.
  const int worldBottom=v.clip.left>=360&&v.clip.right<=1560&&v.clip.bottom>1030&&v.clip.bottom<=1070 ? 1029:v.clip.bottom-1;
  const auto fb=Bounds(0x20d0e0);assert(fb.left==v.clip.left&&fb.right==v.clip.right-1&&fb.top==v.clip.top&&fb.bottom==worldBottom);
  // Independent native textures must stay joined at each actual map scale.
  for(int x=v.clip.left;x<v.clip.right;++x)assert(Pixel(x,200)==0x20d0e0);
  for(int y=50;y<1000;++y)assert(Pixel(std::min(1400L,v.clip.right-10),y)==0x20d0e0);
  for(POINT p:{POINT{100,v.clip.top-1},POINT{100,v.clip.bottom},POINT{v.clip.left-1,540},POINT{v.clip.right,540}})
   if(p.x>=0&&p.x<1920&&p.y>=0&&p.y<1020)assert(Pixel(p.x,p.y)==0);
  printf("%s %s: NPC=%dx%d, scale=%.4f, world=(%ld,%ld)-(%ld,%ld), HUD=1200x40\n",baseline ? "before":"after",id.c_str(),w,h,v.scale,v.clip.left,v.clip.top,v.clip.right,v.clip.bottom);
  csv<<id<<','<<v.scale<<','<<w<<','<<h<<','<<v.clip.left<<','<<v.clip.top<<','<<v.clip.right<<','<<v.clip.bottom<<'\n';++count;
  if(id=="270000000"||id=="260010402")Save(out/(id+(baseline ? "-before.bmp":"-after.bmp")));
 }
 assert(count==(buffTest ? 3:baseline ? 14:18));for(auto buff:buffLayers)buff->Release();marker->Release();hud->Release();actor->Release();for(auto tile:tiles)tile->Release();npc->Release();bar->Release();dot->Release();
 Check(gr->raw_Uninitialize(),"uninitialize");gr->Release();term();DestroyWindow(hwnd);
 printf("PASS %d sampled map bounds through real Gr2D + NPC 2140000, scene clips and independent HUD.\n",count);
}
