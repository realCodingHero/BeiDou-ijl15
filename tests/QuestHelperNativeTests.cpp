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
void __fastcall CreateStub(void*,void*,int,int,int width,int height,int,int,void*,int){assert(width>0 && height>0);}
void __fastcall DestroyStub(void*,void*){}
int __fastcall LeftStub(void*,void*){return 110;}
int __fastcall TopStub(void*,void*){return 90;}
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
    invalidateRect=reinterpret_cast<NativeInvalidate>(InvalidateStub);
    create=reinterpret_cast<NativeCreate>(CreateStub);
    destroy=reinterpret_cast<NativeVoid>(DestroyStub);
    leftPosition=reinterpret_cast<NativePosition>(LeftStub);
    topPosition=reinterpret_cast<NativePosition>(TopStub);
    mainWindow.initialized=confirmWindow.initialized=true;confirmWindow.modal=true;enabled=true;
    auto hasButton=[](Window& w,int id){for(const auto& b:w.buttons)if(b.id==id)return true;return false;};
    auto buttonEnabled=[](Window& w,int id){for(const auto& b:w.buttons)if(b.id==id)return b.enabled;return false;};
    auto caption=[](Window& w,int id){for(const auto& b:w.buttons)if(b.id==id)return b.text;return std::wstring();};
    // Use the real installed WZ description and transparent NPC frame.
    std::ifstream descriptionFile(directory/"quest-description.txt",std::ios::binary);
    std::string descriptionBytes((std::istreambuf_iterator<char>(descriptionFile)),{});
    int count=MultiByteToWideChar(CP_UTF8,0,descriptionBytes.data(),int(descriptionBytes.size()),nullptr,0);
    std::wstring originalDescription(count,L'\0');MultiByteToWideChar(CP_UTF8,0,descriptionBytes.data(),int(descriptionBytes.size()),originalDescription.data(),count);
    resourceText[L"Quest/QuestInfo.img/2000/1"]=originalDescription;
    resourceText[L"String/Etc.img/4000018/name"]=L"木块";resourceText[L"String/Etc.img/4000003/name"]=L"树枝";
    resourceText[L"Etc/QuestCategory.img/30"]=L"金银岛";resourceText[L"Etc/QuestCategory.img/20"]=L"彩虹岛";
    resourceText[L"Quest/QuestInfo.img/2000/area"]=L"30";resourceText[L"Quest/QuestInfo.img/2001/area"]=L"20";
    std::ifstream portraitFile(directory/"npc-portrait.rgba",std::ios::binary);int portraitWidth=0,portraitHeight=0;
    portraitFile.read(reinterpret_cast<char*>(&portraitWidth),4);portraitFile.read(reinterpret_cast<char*>(&portraitHeight),4);
    assert(portraitWidth>0 && portraitHeight>0);auto portrait=std::make_unique<Bitmap>(portraitWidth,portraitHeight);
    portraitFile.read(reinterpret_cast<char*>(portrait->pixels),portraitWidth*portraitHeight*4);assert(portraitFile.good());
    skins.emplace(L"Npc/1032001.img/stand/0",std::move(portrait));
    quests={{2000,0,0,20,0,0,L"酋长的家修理",L""},{2001,0,1,15,0,0,L"准备出发",L""},{29001,0,4,30,0,0,L"金银岛探险家",L""}};
    questTotal=3;progressCount=2;readyCount=1;mainWindow.shown=true;Paint(mainWindow);
    assert(mainWindow.image->width==250 && caption(mainWindow,2)==L"进行中 2" && caption(mainWindow,3)==L"可交付 1");
    auto entries=ListEntries();assert(entries.size()==5 && entries.back().group==100000 && entries.back().index==-1);
    Click(mainWindow,16);assert(ListEntries().size()==6);Click(mainWindow,17);assert(ListEntries().size()==3);
    Click(mainWindow,16);SelectQuest(2000,L"酋长的家修理");assert(!mainWindow.collapsed && mainWindow.x==110 && mainWindow.y==90);
    detailRows={{4000018,2,2,12,50,1800,L"木块",L"缺少 38 个"},{4000003,2,2,26,30,200,L"树枝",L"缺少 4 个"},{1032001,6,0,0,0,0,L"汉斯",L"交付 NPC"}};
    questNpc=1032001;questDescription=Description(2000);selectedRow=0;detailTotal=3;Paint(mainWindow);
    assert(questDescription.find(L"木块")!=std::wstring::npos && questDescription.find(L"12/50")!=std::wstring::npos && questDescription.find(L"#")==std::wstring::npos);
    assert(!hasButton(mainWindow,8) && hasButton(mainWindow,9) && mainWindow.rowHits.size()==2);
    assert(descriptionScrollMax>0);Wheel(mainWindow.native+4,nullptr,300,150,1);assert(descriptionScroll>0);
    Save(*mainWindow.image,directory/"quest-main.bmp");
    SetExpanded(false);Paint(mainWindow);assert(mainWindow.image->width==250 && selectedQuest==2000);
    SetExpanded(true);Paint(mainWindow);assert(mainWindow.image->width==600 && questNpc==1032001);
    void* control=reinterpret_cast<void*>(1);assert(HitTest(mainWindow.native,nullptr,20,10,&control)==1 && !control);
    assert(HitTest(mainWindow.native,nullptr,560,10,&control)==2);
    // Actual thiscall input dispatch uses the +4 IUIMsgHandler subobject.
    using MouseInput=void(__thiscall*)(void*,unsigned,unsigned,int,int);
    void* inputMethods[19]{};inputMethods[2]=reinterpret_cast<void*>(MouseButton);
    *reinterpret_cast<void***>(mainWindow.native+4)=inputMethods;
    *reinterpret_cast<void***>(confirmWindow.native+4)=inputMethods;
    auto mouse=Method<MouseInput>(mainWindow.native+4,8);
    auto modalMouse=Method<MouseInput>(confirmWindow.native+4,8);
    quests.resize(50);for(int i=0;i<50;++i){quests[i]={2000+i,0,0,10,0,0,L"任务 "+std::to_wstring(i),L""};resourceText[L"Quest/QuestInfo.img/"+std::to_wstring(2000+i)+L"/area"]=L"30";}
    questTotal=61;listOffset=listPage=0;Paint(mainWindow);assert(!mainWindow.scrollbars.empty());
    auto bar=mainWindow.scrollbars.front();assert(bar.value==&listOffset && bar.maximum==42);
    MouseButton(mainWindow.native+4,nullptr,WM_LBUTTONDOWN,0,235,bar.thumb.top+3);
    MouseMove(mainWindow.native+4,nullptr,235,350);assert(listOffset>0);
    Paint(mainWindow);MouseButton(mainWindow.native+4,nullptr,WM_LBUTTONUP,0,235,350);assert(mainWindow.dragScroll==-1);
    listOffset=0;Wheel(mainWindow.native+4,nullptr,40,100,1);assert(listOffset==3);
    Paint(mainWindow);queue.clear();mouse(mainWindow.native+4,WM_LBUTTONUP,0,100,425);
    assert(listPage==1 && listOffset==0 && queue.back().map==1);quests.resize(11);Paint(mainWindow);
    assert(!buttonEnabled(mainWindow,5) && buttonEnabled(mainWindow,4));
    mouse(mainWindow.native+4,WM_LBUTTONUP,0,40,425);assert(listPage==0 && queue.back().map==0);
    puts("PASS categories: medal isolation, expand/collapse all, tab totals, 50-row pages, drag/wheel scrollbar and detail resizing");
    category=1;Paint(mainWindow);for(const auto& hit:mainWindow.listHits)if(hit.entry.index>=0)assert(hit.rect.bottom-hit.rect.top==30);
    category=0;view=desiredView=3;routePage=0;routeMap=103020100;routeTotal=4;detailOffset=0;selectedRow=-1;
    sourceKind=2;sourceTarget=4000018;sourceRows={{103020100,2,8,0,0,1200,L"金银岛 - 逆恩森林 II",L"木块"}};
    routeRows={{103000000,0,8,0,0,800,L"金银岛 - 废弃都市",L"主城出发"},{103010000,0,0,0,0,1200,L"金银岛 - 废都南方工地",L"通过地图传送门"},{103020000,0,8,0,0,1200,L"金银岛 - 逆恩森林 I",L"通过地图传送门"},{103020100,0,0,0,0,1200,L"金银岛 - 逆恩森林 II",L"通过地图传送门"}};
    message=L"按表格顺序前往，绿色地图可传送。";textScroll=0;Paint(mainWindow);
    assert(hasButton(mainWindow,11) && !buttonEnabled(mainWindow,11));
    int green=0;const auto firstHit=mainWindow.rowHits.front();
    for(int y=firstHit.rect.top+30;y<firstHit.rect.bottom;++y)for(int x=307;x<471;++x)if(GetPixel(mainWindow.image->dc,x,y)==RGB(36,125,86))++green;
    assert(green>0);Save(*mainWindow.image,directory/"quest-route-table.bmp");
    queue.clear();selectedRow=1;Click(mainWindow,11);assert(queue.empty());selectedRow=0;Click(mainWindow,11);
    assert(queue.size()==1 && queue.front().action==4 && queue.front().map==103000000 && queue.front().token==routeMap);
    auto wire=[](Snapshot snapshot){
        std::vector<unsigned char> b;QuestProtocol::append(b,QuestProtocol::Version);QuestProtocol::append(b,uint8_t(snapshot.view));
        for(int n:{snapshot.sequence,snapshot.quest,42,100000000})QuestProtocol::append(b,n);
        for(int64_t n:{int64_t(200000),snapshot.token,snapshot.price})QuestProtocol::append(b,n);
        QuestProtocol::append(b,snapshot.total);QuestProtocol::append(b,uint8_t(snapshot.features));
        auto text=[&](const std::wstring& value){int n=WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);std::string utf8(n,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),utf8.data(),n,nullptr,nullptr);QuestProtocol::append(b,uint16_t(n));b.insert(b.end(),utf8.begin(),utf8.end());};
        text(snapshot.title);text(snapshot.message);QuestProtocol::append(b,uint16_t(snapshot.rows.size()));
        for(const auto& row:snapshot.rows){QuestProtocol::append(b,row.id);QuestProtocol::append(b,uint8_t(row.kind));QuestProtocol::append(b,uint8_t(row.flags));QuestProtocol::append(b,row.current);QuestProtocol::append(b,row.required);QuestProtocol::append(b,row.cost);text(row.label);text(row.info);}
        QuestProtocol::append(b,(std::max)(0,snapshot.progress));QuestProtocol::append(b,(std::max)(0,snapshot.ready));return b;
    };
    auto receive=[&](Snapshot snapshot,Request request){pending.emplace(snapshot.sequence,Pending{request,GetTickCount()});auto b=wire(snapshot);assert(QuestHelperUI::Receive(b.data(),b.size()));};
    Snapshot quoted;quoted.sequence=10;quoted.quest=selectedQuest;quoted.view=4;quoted.token=123;quoted.price=800;quoted.features=3;quoted.message=L"确认支付金币并传送至选定目的地？";
    receive(quoted,queue.front());assert(confirmWindow.shown && quote.token==123 && view==3);Paint(mainWindow);Paint(confirmWindow);
    Save(*confirmWindow.image,directory/"quest-confirm.bmp");
    queue.clear();int originalQuest=selectedQuest,originalOffset=listOffset;
    Click(mainWindow,2);Click(mainWindow,7);Wheel(mainWindow.native+4,nullptr,40,100,1);Key(mainWindow.native+4,nullptr,VK_ESCAPE,0);
    assert(selectedQuest==originalQuest && view==3 && listOffset==originalOffset && mainWindow.shown && queue.empty());
    assert(HitTest(mainWindow.native,nullptr,20,10,&control)==2);
    modalMouse(confirmWindow.native+4,WM_LBUTTONUP,0,40,220);assert(confirming && confirmWindow.shown && queue.size()==1 && queue.front().token==123);
    Paint(confirmWindow);modalMouse(confirmWindow.native+4,WM_LBUTTONUP,0,40,220);Key(confirmWindow.native+4,nullptr,VK_ESCAPE,0);assert(queue.size()==1 && confirmWindow.shown);
    Snapshot result;result.sequence=11;result.quest=selectedQuest;result.view=5;result.features=3;result.message=L"传送成功";
    receive(result,queue.front());assert(!confirmWindow.shown && !quote.token && view==3);
    Click(mainWindow,7);assert(view==2 && selectedRow==0);Click(mainWindow,7);assert(view==1 && selectedRow==0);Click(mainWindow,7);assert(mainWindow.collapsed);
    SetExpanded(true);queue.clear();view=desiredView=1;
    Snapshot stale;stale.sequence=12;stale.quest=selectedQuest;stale.view=3;stale.features=3;
    Request outdated{3,selectedQuest,sourceKind,sourceTarget,routeMap,3,0,navigation-1};receive(stale,outdated);assert(view==1 && !confirmWindow.shown);
    puts("PASS modal: parent mouse/keyboard/wheel/drag blocked, duplicate confirm rejected, payment preserves route and Back follows each level");
    completionFeatures=0;selectedRow=0;Paint(mainWindow);assert(!hasButton(mainWindow,9) && !hasButton(mainWindow,11));
    queue.clear();Click(mainWindow,9);Click(mainWindow,11);assert(queue.empty());
    completionFeatures=2;Paint(mainWindow);assert(hasButton(mainWindow,9) && hasButton(mainWindow,11));
    quoted.sequence=13;quotedRequest={4,selectedQuest,7,0,0,4};receive(quoted,quotedRequest);assert(confirmWindow.shown);
    Snapshot off;off.sequence=15;off.quest=0;off.view=0;off.features=0;off.total=3;off.progress=2;off.ready=1;off.rows.assign(quests.begin(),quests.begin()+3);
    receive(off,{0,0,0,category,listPage,0});assert(!confirmWindow.shown && completionFeatures==0 && progressCount==2 && readyCount==1);
    off.sequence=14;off.features=3;receive(off,{0,0,0,category,listPage,0});assert(completionFeatures==0);
    Snapshot detail;detail.sequence=16;detail.quest=selectedQuest;detail.view=1;detail.features=0;detail.rows=detailRows;detail.total=3;
    message=Text(L"loading");receive(detail,{1,selectedQuest,0,0,detailPage,1});assert(message.empty() && questNpc==1032001);
    pending.emplace(17,Pending{{1,selectedQuest,0,0,detailPage,1},100});message=Text(L"loading");ExpirePending(10200);assert(message==Text(L"timeout"));
    puts("PASS packet state: original description/NPC, loading clears, stale navigation/features ignored and live switches dismiss purchase modal");
    view=desiredView=2;sourceRows={{103000000,2,8,0,0,800,L"金银岛 - 废弃都市",L"掉落来源"},{103020000,2,8,0,0,2147483647,L"金银岛 - 逆恩森林 I",L"完整费用"}};
    selectedRow=0;detailOffset=textScroll=0;Paint(mainWindow);Save(*mainWindow.image,directory/"quest-map-table.bmp");
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
    puts("PASS actual v83 PCOM/Canvas: UI raster upload and tile boundaries; generated categories, NPC/description, payment and table previews");
}
