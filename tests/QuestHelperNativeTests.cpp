#include "../ezorsia/QuestHelperUI.cpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
int Client::m_nGameWidth=1280;
int Client::m_nGameHeight=720;
bool Client::SwitchChinese=true;
namespace {
int redraws=0;
void __fastcall InvalidateStub(void*,void*,RECT* rect){assert(!rect);++redraws;}
struct RawTile {
    void** methods;
    int width,height;
    std::vector<unsigned char> pixels;
};
struct TiledCanvas {void** methods;std::vector<RawTile> tiles;};
ULONG __stdcall TileRelease(void*){return 1;}
HRESULT __stdcall TileFormat(void*,unsigned* value){*value=2;return S_OK;}
HRESULT __stdcall TileWidth(void* p,unsigned* value){*value=static_cast<RawTile*>(p)->width;return S_OK;}
HRESULT __stdcall TileHeight(void* p,unsigned* value){*value=static_cast<RawTile*>(p)->height;return S_OK;}
HRESULT __stdcall TileLock(void* p,int* pitch,VARIANT* address){auto& t=*static_cast<RawTile*>(p);*pitch=t.width*4;address->vt=VT_BYREF|VT_UI4;address->byref=t.pixels.data();return S_OK;}
HRESULT __stdcall TileUnlock(void* p,RECT* r){auto& t=*static_cast<RawTile*>(p);assert(r && r->left==0 && r->top==0 && r->right>0 && r->right<=t.width && r->bottom>0 && r->bottom<=t.height);return S_OK;}
HRESULT __stdcall CanvasTileWidth(void*,unsigned* value){*value=512;return S_OK;}
HRESULT __stdcall CanvasTileHeight(void*,unsigned* value){*value=256;return S_OK;}
HRESULT __stdcall CanvasTile(void* p,int x,int y,void** out){auto& c=*static_cast<TiledCanvas*>(p);if(x<0 || x>=600 || y<0 || y>=450)return E_INVALIDARG;*out=&c.tiles[(y/256)*2+x/512];return S_OK;}
void TestNarrowTiles(const Bitmap& source){
    void* rawMethods[9]{};rawMethods[2]=reinterpret_cast<void*>(TileRelease);rawMethods[3]=reinterpret_cast<void*>(TileFormat);
    rawMethods[5]=reinterpret_cast<void*>(TileWidth);rawMethods[6]=reinterpret_cast<void*>(TileHeight);
    rawMethods[7]=reinterpret_cast<void*>(TileLock);rawMethods[8]=reinterpret_cast<void*>(TileUnlock);
    void* canvasMethods[16]{};canvasMethods[13]=reinterpret_cast<void*>(CanvasTile);canvasMethods[14]=reinterpret_cast<void*>(CanvasTileWidth);canvasMethods[15]=reinterpret_cast<void*>(CanvasTileHeight);
    TiledCanvas c{canvasMethods,{}};
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){int w=x?88:512,h=y?194:256;c.tiles.push_back({rawMethods,w,h,std::vector<unsigned char>(w*h*4+32,0xAB)});}
    assert(Upload(&c,source));
    for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x){auto& tile=c.tiles[(y/256)*2+x/512];DWORD color=0;memcpy(&color,tile.pixels.data()+((y%256)*tile.width+x%512)*4,4);assert(color==source.pixels[y*source.width+x]);}
    for(const auto& t:c.tiles)for(size_t n=t.width*t.height*4;n<t.pixels.size();++n)assert(t.pixels[n]==0xAB);
    puts("PASS tiled canvas: narrow edge tile strides, every pixel across four tiles and buffer guards");
}
void Save(const Bitmap& b,const std::filesystem::path& path) {
    BITMAPFILEHEADER f{};f.bfType=0x4D42;f.bfOffBits=sizeof(f)+sizeof(BITMAPINFOHEADER);f.bfSize=f.bfOffBits+b.width*b.height*4;
    BITMAPINFOHEADER h{};h.biSize=sizeof(h);h.biWidth=b.width;h.biHeight=-b.height;h.biPlanes=1;h.biBitCount=32;
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<char*>(&f),sizeof(f));out.write(reinterpret_cast<char*>(&h),sizeof(h));out.write(reinterpret_cast<char*>(b.pixels),b.width*b.height*4);
}
}
int main(int argc,char** argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    assert(argc==4);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    auto directory=std::filesystem::path(argv[2]);labelFile=std::filesystem::path(argv[3]).wstring();language=L"zh-CN";
    completionFeatures=3;
    font=CreateFontW(-12,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,NONANTIALIASED_QUALITY,0,L"SimSun");
    boldFont=CreateFontW(-12,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,NONANTIALIASED_QUALITY,0,L"SimSun");
    const wchar_t* paths[]={L"UI/UIWindow.img/Quest/backgrnd",L"UI/UIWindow.img/Quest/backgrnd2",L"UI/UIWindow.img/QuestAlarm/backgrndmax",L"UI/UIWindow.img/QuestAlarm/backgrndcenter",L"UI/UIWindow.img/QuestAlarm/backgrndbottom",L"UI/Basic.img/BtOK/normal/0",L"UI/Basic.img/BtOK/mouseOver/0",L"UI/Basic.img/BtOK/disabled/0"};
    for(int i=0;i<8;++i) {
        auto file=directory/(std::to_string(i)+".bmp");HBITMAP bitmap=static_cast<HBITMAP>(LoadImageW(nullptr,file.c_str(),IMAGE_BITMAP,0,0,LR_LOADFROMFILE|LR_CREATEDIBSECTION));assert(bitmap);
        BITMAP info{};GetObject(bitmap,sizeof(info),&info);auto b=std::make_unique<Bitmap>(info.bmWidth,info.bmHeight);
        HDC src=CreateCompatibleDC(nullptr);auto old=SelectObject(src,bitmap);BitBlt(b->dc,0,0,b->width,b->height,src,0,0,SRCCOPY);SelectObject(src,old);DeleteDC(src);DeleteObject(bitmap);GdiFlush();
        skins.emplace(paths[i],std::move(b));
    }
    quests={{1001,0,0,20,0,0,L"收集猪的蝴蝶结",L""},{1002,0,1,15,0,0,L"准备出发",L""},{1003,0,4,30,0,0,L"维多利亚岛探险家",L""}};
    selectedQuest=1001;questTotal=3;title=L"收集猪的蝴蝶结";
    detailRows={{4000002,2,2,12,30,1800,L"猪的蝴蝶结",L"缺少 18 个"},{1210100,1,2,26,50,2400,L"猪",L"等级 20，账号累计击杀 146"},{1012000,6,0,0,0,0,L"玛亚",L"交付 NPC"}};
    detailTotal=3;selectedRow=0;Paint(mainWindow);Save(*mainWindow.image,directory/"quest-main.bmp");
    quote.token=123;quote.price=1800;quote.message=L"确认支付金币并补齐选定任务目标？";quoteTime=GetTickCount();meso=200000;
    Paint(mainWindow);Save(*mainWindow.image,directory/"quest-confirm.bmp");quote={};
    trackerWindow.tracker=true;trackTitle=title;trackRows=detailRows;Paint(trackerWindow);Save(*trackerWindow.image,directory/"quest-tracker.bmp");
    assert(mainWindow.buttons.size()>=7 && trackerWindow.buttons.size()==3);
    trackerWindow.collapsed=true;Paint(trackerWindow);assert(trackerWindow.image->height==25 && trackerWindow.buttons.size()==2);
    trackerWindow.collapsed=false;Paint(trackerWindow);assert(trackerWindow.image->height==170);
    const auto originalInvalidate=invalidateRect;
    invalidateRect=reinterpret_cast<NativeInvalidate>(InvalidateStub);
    mainWindow.shown=trackerWindow.shown=true;Dirty();assert(redraws==2);
    mainWindow.shown=trackerWindow.shown=false;
    quests.resize(25);listOffset=5;Wheel(mainWindow.native+4,nullptr,40,80,1);assert(listOffset==8);
    Wheel(mainWindow.native+4,nullptr,40,80,-1);assert(listOffset==5);
    detailRows.resize(20);detailOffset=5;Wheel(mainWindow.native+4,nullptr,300,80,1);assert(detailOffset==7);
    Wheel(mainWindow.native+4,nullptr,300,80,-1);assert(detailOffset==5);
    void* control=reinterpret_cast<void*>(1);
    assert(HitTest(mainWindow.native,nullptr,20,10,&control)==1 && control==nullptr);
    assert(HitTest(mainWindow.native,nullptr,560,10,&control)==2); // Close is a client hit.
    assert(HitTest(mainWindow.native,nullptr,20,50,&control)==2);
    assert(HitTest(mainWindow.native,nullptr,-1,10,&control)==0);
    assert(HitTest(trackerWindow.native,nullptr,20,10,&control)==1);
    assert(HitTest(trackerWindow.native,nullptr,195,10,&control)==2);
    // Dispatch actual thiscall input ABI through a vtable with a +4 subobject.
    using MouseInput=void(__thiscall*)(void*,unsigned,unsigned,int,int);
    void* inputMethods[19]{};inputMethods[2]=reinterpret_cast<void*>(MouseButton);
    *reinterpret_cast<void***>(mainWindow.native+4)=inputMethods;
    auto mouse=Method<MouseInput>(mainWindow.native+4,8);
    quests.resize(17);questTotal=17;listPage=listOffset=0;queue.clear();Paint(mainWindow);
    auto buttonEnabled=[](int id){for(const auto& b:mainWindow.buttons)if(b.id==id)return b.enabled;return false;};
    assert(!buttonEnabled(4) && buttonEnabled(5));
    mouse(mainWindow.native+4,WM_LBUTTONUP,0,100,425);assert(listOffset==10 && listPage==0 && queue.empty());
    Paint(mainWindow);assert(buttonEnabled(4) && !buttonEnabled(5));
    mouse(mainWindow.native+4,WM_LBUTTONUP,0,40,425);assert(listOffset==0 && queue.empty());
    quests.resize(50);questTotal=61;listOffset=40;Paint(mainWindow);
    mouse(mainWindow.native+4,WM_LBUTTONUP,0,100,425);assert(listPage==1 && listOffset==0 && queue.back().map==1);
    quests.resize(11);Paint(mainWindow);queue.clear();
    mouse(mainWindow.native+4,WM_LBUTTONUP,0,100,425);assert(listPage==1 && listOffset==10 && queue.empty());
    Paint(mainWindow);assert(!buttonEnabled(5));
    mouse(mainWindow.native+4,WM_LBUTTONUP,0,40,425);Paint(mainWindow);
    mouse(mainWindow.native+4,WM_LBUTTONUP,0,40,425);assert(listPage==0 && listOffset==40 && queue.back().map==0);
    quests={{1001,0,0,20,0,0,L"quest one",L""},{1002,0,1,15,0,0,L"quest two",L""}};
    questTotal=2;listPage=listOffset=0;view=1;quote={};queue.clear();Paint(mainWindow);
    mouse(mainWindow.native+4,WM_LBUTTONUP,0,40,100);assert(selectedQuest==1002 && queue.back().action==1 && queue.back().quest==1002);
    Paint(mainWindow);queue.clear();mouse(mainWindow.native+4,WM_LBUTTONUP,0,500,42);
    assert(queue.back().action==4 && queue.back().kind==7 && queue.back().quest==1002);
    Paint(mainWindow);queue.clear();mouse(mainWindow.native+4,WM_LBUTTONUP,0,170,42);
    assert(category==1 && selectedQuest==0 && title.empty() && queue.back().target==1);
    Paint(mainWindow);queue.clear();mouse(mainWindow.native+4,WM_LBUTTONUP,0,50,42);
    assert(category==0 && queue.back().target==0);
    puts("PASS input dispatch: draggable title/client buttons, quest selection, quote request, both filters and 10-row pagination across 50-row server batches");
    invalidateRect=originalInvalidate;
    puts("PASS native redraw queue and wheel ABI: both windows repaint and each pane scrolls in the correct direction");
    view=3;detailPage=2;queue.clear();SelectQuest(1001,title);
    assert(view==1 && detailPage==0 && selectedRow==-1 && queue.front().quest==1001);
    // Exercise the actual UI dispatch state without opening a game session.
    enabled=true;view=2;sourceKind=2;sourceTarget=4000002;sourceRows={{100000001,2,8,0,0,500,L"map",L"source"}};
    selectedRow=0;queue.clear();Click(mainWindow,10);
    assert(queue.size()==1 && queue.front().action==3 && queue.front().map==100000001 && queue.front().token==0);
    view=3;routeTotal=101;queue.clear();Click(mainWindow,14);
    assert(routePage==1 && queue.front().action==3 && queue.front().token==1);
    queue.clear();Click(mainWindow,12);assert(queue.front().action==3 && queue.front().token==1);
    queue.clear();Click(mainWindow,13);assert(routePage==0 && queue.front().token==0);
    auto response=[](int seq,int pageId,int features=3) {
        std::vector<unsigned char> b;
        QuestProtocol::append(b,QuestProtocol::Version);QuestProtocol::append(b,uint8_t(1));
        for(int value:{seq,1001,42,100000000})QuestProtocol::append(b,value);
        for(int64_t value:{int64_t(3000),int64_t(0),int64_t(0)})QuestProtocol::append(b,value);
        QuestProtocol::append(b,101);QuestProtocol::append(b,uint8_t(features));QuestProtocol::append(b,uint16_t(0));QuestProtocol::append(b,uint16_t(0));
        QuestProtocol::append(b,uint16_t(1));QuestProtocol::append(b,pageId);
        QuestProtocol::append(b,uint8_t(2));QuestProtocol::append(b,uint8_t(0));
        QuestProtocol::append(b,0);QuestProtocol::append(b,10);QuestProtocol::append(b,int64_t(0));
        QuestProtocol::append(b,uint16_t(0));QuestProtocol::append(b,uint16_t(0));return b;
    };
    view=1;detailPage=1;detailRows.clear();pending.emplace(5,Pending{{1,1001,0,0,0,1},0});
    auto stale=response(5,4000002);QuestHelperUI::Receive(stale.data(),stale.size());assert(detailRows.empty());
    pending.emplace(6,Pending{{1,1001,0,0,1,1},0});auto current=response(6,4000003);message=Text(L"loading");
    QuestHelperUI::Receive(current.data(),current.size());assert(detailRows.size()==1 && detailRows[0].id==4000003 && message.empty());
    auto unsolicited=response(7,4000004);QuestHelperUI::Receive(unsolicited.data(),unsolicited.size());assert(detailRows[0].id==4000003);
    pending.emplace(8,Pending{{1,1001,0,0,1,1},0});message=L"server notice";
    auto refresh=response(8,4000003);QuestHelperUI::Receive(refresh.data(),refresh.size());assert(message==L"server notice");
    pending.emplace(9,Pending{{1,1001,0,0,1,1},100});message=Text(L"loading");
    ExpirePending(10200);assert(pending.empty() && message==Text(L"timeout"));
    pending.emplace(10,Pending{{1,1001,0,0,1,1},10300});auto retry=response(10,4000003);
    QuestHelperUI::Receive(retry.data(),retry.size());assert(message.empty());
    auto priceReply=response(11,4000003);priceReply[1]=4;int64_t token=123,price=100;
    memcpy(priceReply.data()+26,&token,8);memcpy(priceReply.data()+34,&price,8);
    pending.emplace(11,Pending{{4,1001,7,0,0,4},10300});
    QuestHelperUI::Receive(priceReply.data(),priceReply.size());assert(quote.token==123 && quotedRequest.kind==7);
    Paint(mainWindow);queue.clear();mouse(mainWindow.native+4,WM_LBUTTONUP,0,300,239);
    assert(!quote.token && queue.size()==1 && queue.front().action==5 && queue.front().kind==7 && queue.front().token==123);
    puts("PASS complete-all flow: quote response displays confirmation and native button dispatch carries the server token");
    auto hasButton=[](int id){for(const auto& b:mainWindow.buttons)if(b.id==id)return true;return false;};
    detailRows={{4000002,2,2,0,30,1500,L"material",L"missing"},{1210100,1,2,0,20,1000,L"mob",L"missing"}};
    selectedRow=0;completionFeatures=0;Paint(mainWindow);assert(!hasButton(9) && !hasButton(11));
    queue.clear();Click(mainWindow,9);Click(mainWindow,11);assert(queue.empty());
    completionFeatures=1;Paint(mainWindow);assert(hasButton(9) && !hasButton(11));
    selectedRow=1;Paint(mainWindow);assert(hasButton(11));
    completionFeatures=2;Paint(mainWindow);assert(hasButton(9) && !hasButton(11));
    selectedRow=0;Paint(mainWindow);assert(hasButton(11));
    quote.token=456;quotedRequest={4,1001,7,0,0,4};
    pending.emplace(13,Pending{{1,1001,0,0,1,1},0});auto off=response(13,4000003,0);
    QuestHelperUI::Receive(off.data(),off.size());assert(completionFeatures==0 && !quote.token && message==Text(L"featuresChanged"));
    assert(queue.empty());Paint(mainWindow);assert(!hasButton(9) && !hasButton(11));
    pending.emplace(12,Pending{{1,1001,0,0,1,1},0});auto older=response(12,4000003,3);
    QuestHelperUI::Receive(older.data(),older.size());assert(completionFeatures==0);
    puts("PASS live feature switches: disabled buttons disappear, direct actions are blocked and old quotes/responses cannot restore them");
    view=3;routePage=0;detailOffset=0;selectedRow=0;
    routeRows={{103000000,0,0,0,0,0,L"金银岛 - 废弃都市",L"主城出发"},{103010000,0,0,0,0,0,L"金银岛 - 废都南方工地",L"通过地图传送门"},{103020000,0,0,0,0,0,L"金银岛 - 逆恩森林 I",L"通过地图传送门"},{103020100,0,0,0,0,0,L"金银岛 - 逆恩森林 II",L"通过地图传送门"}};
    message=L"人工前往路线（不会自动传送）。按表格顺序前往，可滚动查看全部步骤；亲自到达地图后解锁快捷传送。";textScroll=0;Paint(mainWindow);
    assert(mainWindow.rowHits.size()>=3);
    auto lastHit=mainWindow.rowHits.back();
    mouse(mainWindow.native+4,WM_LBUTTONUP,0,350,lastHit.rect.top+4);assert(selectedRow==lastHit.index);
    Paint(mainWindow);Save(*mainWindow.image,directory/"quest-route-table.bmp");
    message+=std::wstring(200,L'路');Paint(mainWindow);assert(textScrollMax>0);
    int oldTextScroll=textScroll;Wheel(mainWindow.native+4,nullptr,300,350,1);assert(textScroll>oldTextScroll);
    Paint(mainWindow);Save(*mainWindow.image,directory/"quest-route-scroll.bmp");
    routeRows.resize(30,routeRows.back());routeTotal=30;detailOffset=0;Paint(mainWindow);
    for(int i=0;i<20;++i)Wheel(mainWindow.native+4,nullptr,300,100,1);
    Paint(mainWindow);assert(detailOffset==29 && mainWindow.rowHits.back().index==29);
    view=2;sourceRows={{103000000,2,8,0,0,800,L"金银岛 - 废弃都市",L"蜗牛：掉落来源"},{103010000,2,0,0,0,1200,L"金银岛 - 废都南方工地",L"道具掉落来源"},{103020000,2,8,0,0,2147483647,L"金银岛 - 逆恩森林 I",L"完整费用与传送状态"}};
    detailOffset=textScroll=0;selectedRow=2;message.clear();Paint(mainWindow);
    assert(mainWindow.rowHits.size()==3);Save(*mainWindow.image,directory/"quest-map-table.bmp");
    puts("PASS table layout: native hit boxes select wrapped rows, last route step is reachable and full text scrolls independently");
    puts("PASS status updates: detail response clears loading, automatic refresh preserves notices and missing responses time out");
    enabled=false;queue.clear();pending.clear();
    puts("PASS UI dispatch: route pagination/refresh, obsolete page responses and unsolicited packet isolation");
    TestNarrowTiles(*mainWindow.image);
    // Verify writes into the ACTUAL v83 Canvas DLL, including tile boundaries.
    assert(SetCurrentDirectoryA(argv[1]));assert(SetDllDirectoryA(argv[1]));HMODULE pcom=LoadLibraryA("PCOM.dll");assert(pcom);
    auto init=reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(pcom,"PcInitModule"));assert(init && SUCCEEDED(init()));
    auto pcCreate=reinterpret_cast<HRESULT(__cdecl*)(const wchar_t*,const GUID*,void**,IUnknown*)>(GetProcAddress(pcom,"PcCreateObject"));assert(pcCreate);
    void* canvas=nullptr;assert(SUCCEEDED(pcCreate(L"Canvas",&CanvasGuid,&canvas,nullptr)) && canvas);
    using CreateCanvas=HRESULT(__stdcall*)(void*,unsigned,unsigned,VARIANT,VARIANT);
    VARIANT empty{},format{};format.vt=VT_I4;format.lVal=2;
    assert(SUCCEEDED(Method<CreateCanvas>(canvas,0x2C)(canvas,Width,Height,empty,format)));
    using Rect=HRESULT(__stdcall*)(void*,int,int,unsigned,unsigned,unsigned);
    assert(SUCCEEDED(Method<Rect>(canvas,0x8C)(canvas,0,0,Width,Height,0xFFFFFFFF)));
    unsigned tw=0,th=0;Method<Size>(canvas,0x38)(canvas,&tw);Method<Size>(canvas,0x3C)(canvas,&th);printf("tile size %u x %u\n",tw,th);
    void* raw=nullptr;HRESULT hr=Method<GetRaw>(canvas,0x34)(canvas,0,0,&raw);printf("raw canvas %08lx %p\n",hr,raw);
    if(raw){int pitch=0;VARIANT ptr{};hr=Method<Lock>(raw,0x1C)(raw,&pitch,&ptr);printf("lock %08lx pitch %d type %d ptr %08lx\n",hr,pitch,ptr.vt,ptr.lVal);if(SUCCEEDED(hr))Method<Unlock>(raw,0x20)(raw,nullptr);Method<Release>(raw,8)(raw);}
    assert(Upload(canvas,*mainWindow.image));
    for(POINT p:{POINT{1,1},POINT{245,30},POINT{511,255},POINT{512,256},POINT{599,449}}){unsigned color=0;
        assert(SUCCEEDED(Method<Pixel>(canvas,0x88)(canvas,p.x,p.y,&color)));
        assert(color==mainWindow.image->pixels[p.y*Width+p.x]);
    }
    Method<Release>(canvas,8)(canvas);
    puts("PASS actual v83 PCOM/Canvas: UI raster upload and tile boundaries; generated main, payment and tracker previews");
}
