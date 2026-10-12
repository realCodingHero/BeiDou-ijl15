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
    int format=2;
};
struct TiledCanvas {void** methods;std::vector<RawTile> tiles;};
ULONG __stdcall TileRelease(void*){return 1;}
HRESULT __stdcall TileFormat(void* p,unsigned* value){*value=static_cast<RawTile*>(p)->format;return S_OK;}
HRESULT __stdcall TileWidth(void* p,unsigned* value){*value=static_cast<RawTile*>(p)->width;return S_OK;}
HRESULT __stdcall TileHeight(void* p,unsigned* value){*value=static_cast<RawTile*>(p)->height;return S_OK;}
HRESULT __stdcall TileLock(void* p,int* pitch,VARIANT* address){auto& t=*static_cast<RawTile*>(p);*pitch=t.width*(t.format==2?4:2);address->vt=VT_BYREF|VT_UI4;address->byref=t.pixels.data();return S_OK;}
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
    Bitmap transparent(600,450);for(int y=0;y<450;++y)for(int x=0;x<600;++x)transparent.pixels[y*600+x]=x<425?0x88223344:0;
    for(auto& t:c.tiles){t.format=1;t.pixels.assign(t.width*t.height*2+32,0xAB);}
    assert(Upload(&c,transparent));
    for(int y=0;y<450;++y)for(int x=0;x<600;++x){auto& tile=c.tiles[(y/256)*2+x/512];WORD color=0;memcpy(&color,tile.pixels.data()+((y%256)*tile.width+x%512)*2,2);assert(color==(x<425?0x8234:0));}
    for(const auto& t:c.tiles)for(size_t n=t.width*t.height*2;n<t.pixels.size();++n)assert(t.pixels[n]==0xAB);
    puts("PASS 16-bit tiled canvas: drawer alpha, transparent tail, narrow tile strides and buffer guards");
}
void Save(const Bitmap& b,const std::filesystem::path& path) {
    BITMAPFILEHEADER f{};f.bfType=0x4D42;f.bfOffBits=sizeof(f)+sizeof(BITMAPINFOHEADER);f.bfSize=f.bfOffBits+b.width*b.height*4;
    BITMAPINFOHEADER h{};h.biSize=sizeof(h);h.biWidth=b.width;h.biHeight=-b.height;h.biPlanes=1;h.biBitCount=32;
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<char*>(&f),sizeof(f));out.write(reinterpret_cast<char*>(&h),sizeof(h));out.write(reinterpret_cast<char*>(b.pixels),b.width*b.height*4);
}
std::wstring Utf8(const std::string& bytes){if(bytes.empty())return L"";int size=MultiByteToWideChar(CP_UTF8,0,bytes.data(),int(bytes.size()),nullptr,0);std::wstring value(size,L'\0');MultiByteToWideChar(CP_UTF8,0,bytes.data(),int(bytes.size()),value.data(),size);return value;}
std::wstring ReadText(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);assert(file.is_open());return Utf8(std::string((std::istreambuf_iterator<char>(file)),{}));}
}
int main(int argc,char** argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    assert(argc==4);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    auto directory=std::filesystem::path(argv[2]);labelFile=std::filesystem::path(argv[3]).wstring();language=L"zh-CN";
    completionFeatures=3;
    font=CreateFontW(-12,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,NONANTIALIASED_QUALITY,0,L"SimSun");
    boldFont=CreateFontW(-12,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,NONANTIALIASED_QUALITY,0,L"SimSun");
    std::ifstream pathFile(directory/"skin-paths.txt");std::vector<std::wstring> paths;std::string pathLine;
    while(std::getline(pathFile,pathLine)){if(!pathLine.empty() && pathLine.back()=='\r')pathLine.pop_back();std::wstring path(pathLine.begin(),pathLine.end());paths.push_back(L"UI/"+path);}
    assert(paths.size()==32);
    // Additional native skins (gauges, quest icons, 12x12 buttons, sample item/mob art).
    std::vector<std::filesystem::path> files;for(int i=0;i<32;++i)files.push_back(directory/(std::to_string(i)+".bmp"));
    std::ifstream extraFile(directory/"skin-paths-extra.txt");
    for(int i=0;std::getline(extraFile,pathLine);++i){if(!pathLine.empty() && pathLine.back()=='\r')pathLine.pop_back();if(pathLine.empty())continue;
        auto file=directory/("extra-"+std::to_string(i)+".bmp");if(!std::filesystem::exists(file))continue;paths.push_back(std::wstring(pathLine.begin(),pathLine.end()));files.push_back(file);}
    for(int i=0;i<int(paths.size());++i) {
        auto file=files[i];HBITMAP bitmap=static_cast<HBITMAP>(LoadImageW(nullptr,file.c_str(),IMAGE_BITMAP,0,0,LR_LOADFROMFILE|LR_CREATEDIBSECTION));assert(bitmap);
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
    auto caption=[](Window& w,int id){for(const auto& b:w.buttons)if(b.id==id)return b.text;for(const auto& tab:w.tabs)if(tab.id==id)return tab.text;return std::wstring();};
    // Use the real installed WZ description and transparent NPC frame.
    std::ifstream descriptionFile(directory/"quest-description.txt",std::ios::binary);
    std::string descriptionBytes((std::istreambuf_iterator<char>(descriptionFile)),{});
    int count=MultiByteToWideChar(CP_UTF8,0,descriptionBytes.data(),int(descriptionBytes.size()),nullptr,0);
    std::wstring originalDescription(count,L'\0');MultiByteToWideChar(CP_UTF8,0,descriptionBytes.data(),int(descriptionBytes.size()),originalDescription.data(),count);
    resourceText[L"Quest/QuestInfo.img/2000/1"]=originalDescription;
    std::ifstream tokenFile(directory/"token-resources.tsv");std::string tokenLine;
    while(std::getline(tokenFile,tokenLine)){if(!tokenLine.empty() && tokenLine.back()=='\r')tokenLine.pop_back();auto tab=tokenLine.find('\t');assert(tab!=std::string::npos);resourceText[Utf8(tokenLine.substr(0,tab))]=Utf8(tokenLine.substr(tab+1));}
    for(int id:{8048,2089,28269})resourceText[L"Quest/QuestInfo.img/"+std::to_wstring(id)+L"/1"]=ReadText(directory/("quest-"+std::to_string(id)+".txt"));
    selectedQuest=8048;int originalCounterMob=_wtoi(resourceText[L"Quest/Check.img/8048/1/mob/0/id"].c_str());assert(originalCounterMob>0);
    detailRows={{originalCounterMob,1,0,53,100,0,L"绿蘑菇",L""}};auto cleaned=Description(8048);
    assert(cleaned.find(L"53/100")!=std::wstring::npos && cleaned.find(L"80481")==std::wstring::npos && cleaned.find(L"#")==std::wstring::npos);
    selectedQuest=28269;cleaned=Description(28269);assert(cleaned.find(resourceText[L"String/Npc.img/1012109/name"])!=std::wstring::npos && cleaned.find(L"#@") ==std::wstring::npos && cleaned.find(L"1012109")==std::wstring::npos);
    selectedQuest=2089;detailRows={{4000003,2,0,12,30,0,L"树枝",L""},{4000004,2,0,7,30,0,L"绿液球",L""},{4000010,2,0,1,10,0,L"绿水灵珠",L""}};cleaned=Description(2089);
    assert(cleaned.find(L"12/30")!=std::wstring::npos && cleaned.find(L"7/30")!=std::wstring::npos && cleaned.find(L"1/10")!=std::wstring::npos);
    for(int id:{4000003,4000004,4000010}){assert(cleaned.find(resourceText[L"String/Etc.img/Etc/"+std::to_wstring(id)+L"/name"])!=std::wstring::npos);assert(cleaned.find(std::to_wstring(id))==std::wstring::npos);}
    assert(cleaned.find(L"#")==std::wstring::npos);selectedQuest=0;detailRows.clear();
    puts("PASS actual quest markup: 8048 kill counter, 28269 NPC reference, 2089 nested item names and colored progress numbers");
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
    assert(mainWindow.tabs.size()==2 && !hasButton(mainWindow,2) && !hasButton(mainWindow,3));
    assert(mainWindow.scrollbars.size()==1 && mainWindow.scrollbars.front().maximum==0);
    assert(caption(mainWindow,16)==Trimmed(Text(L"expandAll")) && caption(mainWindow,17)==Trimmed(Text(L"collapseAll")));
    assert(buttonEnabled(mainWindow,16) && buttonEnabled(mainWindow,17)); // the medal group starts collapsed
    auto entries=ListEntries();assert(entries.size()==5 && entries.back().group==100000 && entries.back().index==-1);
    assert(hasButton(mainWindow,16) && hasButton(mainWindow,17));
    Click(mainWindow,17);assert(ListEntries().size()==3);Paint(mainWindow);assert(buttonEnabled(mainWindow,16) && !buttonEnabled(mainWindow,17));Click(mainWindow,16);assert(ListEntries().size()==6);
    Click(mainWindow,17);SelectQuest(2000,L"酋长的家修理");assert(!mainWindow.collapsed && mainWindow.x==110 && mainWindow.y==90);
    assert(mainWindow.animating && mainWindow.drawer==0);AdvanceDrawer(mainWindow.drawerStart+100);Paint(mainWindow);
    assert(mainWindow.drawer==175 && mainWindow.image->width==600);
    for(int y=0;y<450;++y)for(int x=425;x<600;++x)assert(mainWindow.image->pixels[y*600+x]==0);
    void* drawerControl=nullptr;assert(HitTest(mainWindow.native,nullptr,450,10,&drawerControl)==0);
    Save(*mainWindow.image,directory/"quest-drawer.bmp");AdvanceDrawer(mainWindow.drawerStart+DrawerDuration);
    detailRows={{4000018,2,2,12,50,1800,L"木块",L"缺少 38 个"},{4000003,2,2,26,30,200,L"树枝",L"缺少 4 个"},{1032001,6,0,0,0,0,L"汉斯",L"交付 NPC"}};
    questNpc=1032001;questDescription=Description(2000,true);selectedRow=0;detailTotal=3;Paint(mainWindow);
    assert(PlainText(questDescription).find(L"木块")!=std::wstring::npos && PlainText(questDescription).find(L"12/50")!=std::wstring::npos && questDescription.find(L"#")==std::wstring::npos);
    // Colour markup survives as renderer markers and disappears from plain text.
    assert(questDescription.find(std::wstring(1,L'\x01')+L"b")!=std::wstring::npos && PlainText(questDescription)==Description(2000));
    assert(ObjectiveCaption(detailRows.front()).find(L"(4000018)")!=std::wstring::npos);
    printf("objective rows visible: %d\n",int(mainWindow.rowHits.size()));
    assert(!hasButton(mainWindow,8) && hasButton(mainWindow,9) && mainWindow.rowHits.size()==3); // compact 36-pixel rows: all three fit above the message pane
    assert(descriptionScrollMax>0);Wheel(mainWindow.native+4,nullptr,300,150,1);assert(descriptionScroll>0);
    Save(*mainWindow.image,directory/"quest-main.bmp");
    {   // Frames are copied 1:1 (tiled, never stretched) with their alpha.
        auto frame=skins[L"UI/UIWindow.img/Quest/backgrnd"].get();auto drawer=skins[L"UI/UIWindow.img/Quest/backgrnd2"].get();
        for(int y:{0,1,2,30,43,100}){assert(mainWindow.image->pixels[y*600+0]==frame->pixels[y*frame->width+0]);assert(mainWindow.image->pixels[y*600+2]==frame->pixels[y*frame->width+2]);}
        for(int y=0;y<450;++y){int sy=y<200?y:y>=314?y-54:200+(y-200)%60;assert(mainWindow.image->pixels[y*600+0]==frame->pixels[sy*frame->width+0]);assert(mainWindow.image->pixels[y*600+599]==drawer->pixels[sy*drawer->width+drawer->width-1]);}
        assert((mainWindow.image->pixels[0]>>24)==(frame->pixels[0]>>24));
        for(const auto& b:mainWindow.buttons)if(b.id>=4 && b.id<100 && b.id!=15 && b.id!=16 && b.id!=17)assert(b.rect.bottom-b.rect.top==17);
        puts("PASS native frame: 9-slice tiling keeps border pixels, shadow alpha and 17-pixel buttons");
    }
    message.clear();selectedRow=2;Paint(mainWindow);assert(mainWindow.rowHits.size()==3);
    assert(textScrollMax==0);Save(*mainWindow.image,directory/"quest-details-expanded.bmp");
    // Enough objectives to overflow the compact rows and show the condition scrollbar.
    for(int i=0;i<4;++i)detailRows.push_back({1110100,1,0,53+i,100,0,L"绿蘑菇",L"等级 15，账号累计击杀 23"});Paint(mainWindow);
    bool descriptionBar=false,conditionBar=false;for(const auto& bar:mainWindow.scrollbars){assert(bar.track.right-bar.track.left==12);if(bar.value==&descriptionScroll){descriptionBar=true;assert(bar.track.left==579);}if(bar.value==&detailOffset){conditionBar=true;assert(bar.track.left==579);}}
    assert(descriptionBar && conditionBar && ObjectiveCaption(detailRows.back()).find(L"(1110100)")!=std::wstring::npos);
    {int npcHits=0;for(const auto& hit:mainWindow.rowHits)if(hit.index==2)++npcHits;assert(npcHits<=1);} // No duplicate NPC row below the conditions.
    Save(*mainWindow.image,directory/"quest-scrollbar-alignment.bmp");
    {   // Mob rows: "Lv. N" plus the account kill badge only while kill completion is enabled.
        Row mob{1110100,1,0,53,100,0,L"绿蘑菇",L"等级 15，账号累计击杀 1,234"};int saved=completionFeatures;
        completionFeatures=3;auto shown=Describe(mob);assert(shown.meta==L"Lv. 15" && shown.badge==Text(L"accountKills")+L" 1,234" && shown.detail.empty());
        completionFeatures=2;assert(Describe(mob).badge.empty());
        // Items: the ambiguous level-gate text is explained; completed items show no lock at all.
        Row single{4032086,2,0,0,1,0,L"疑问小球",Text(L"lockLevelLiteral")};
        assert(Describe(single).detail==Text(L"lockSingle") && Describe(single).badge==Text(L"purchaseLocked"));
        single.flags=1;single.current=1;assert(Describe(single).badge.empty() && Describe(single).detail.empty());
        Row missing{4000003,2,0,26,30,0,L"树枝",Format(Text(L"itemMissing"),L"4")};assert(Describe(missing).badge.empty());
        missing.flags=2;assert(Describe(missing).badge==Text(L"purchasable"));
        completionFeatures=saved;
        selectedRow=3;Paint(mainWindow);for(const auto& hit:mainWindow.rowHits)if(hit.index==3)assert(hit.rect.bottom-hit.rect.top==ObjectiveRow); // nothing to unfold
        puts("PASS objective rows: Lv. N, kill badge follows the kill-completion switch, clearer item locks, no empty unfolding");
    }
    detailRows.resize(3);message=Text(L"loading");
    SetExpanded(false);AdvanceDrawer(mainWindow.drawerStart+100);assert(mainWindow.drawer==175);
    SetExpanded(true);assert(mainWindow.animating);AdvanceDrawer(mainWindow.drawerStart+DrawerDuration);assert(mainWindow.drawer==350);
    SetExpanded(false);AdvanceDrawer(mainWindow.drawerStart+DrawerDuration);Paint(mainWindow);assert(mainWindow.image->width==250 && selectedQuest==2000);
    SetExpanded(true);AdvanceDrawer(mainWindow.drawerStart+DrawerDuration);Paint(mainWindow);assert(mainWindow.image->width==600 && questNpc==1032001);
    void* control=reinterpret_cast<void*>(1);assert(HitTest(mainWindow.native,nullptr,20,10,&control)==1 && !control);
    assert(HitTest(mainWindow.native,nullptr,235,10,&control)==2);
    // Actual thiscall input dispatch uses the +4 IUIMsgHandler subobject.
    using MouseInput=void(__thiscall*)(void*,unsigned,unsigned,int,int);
    void* inputMethods[19]{};inputMethods[2]=reinterpret_cast<void*>(MouseButton);
    *reinterpret_cast<void***>(mainWindow.native+4)=inputMethods;
    *reinterpret_cast<void***>(confirmWindow.native+4)=inputMethods;
    auto nativeMouse=Method<MouseInput>(mainWindow.native+4,8);
    auto mouse=[&](void* input,unsigned msg,unsigned flags,int x,int y){if(msg==WM_LBUTTONUP)nativeMouse(input,WM_LBUTTONDOWN,flags,x,y);nativeMouse(input,msg,flags,x,y);};
    auto modalMouse=mouse;
    quests.resize(50);for(int i=0;i<50;++i){quests[i]={2000+i,0,0,10,0,0,L"任务 "+std::to_wstring(i),L""};resourceText[L"Quest/QuestInfo.img/"+std::to_wstring(2000+i)+L"/area"]=L"30";}
    questTotal=61;listOffset=listPage=0;Paint(mainWindow);assert(!mainWindow.scrollbars.empty());
    auto bar=mainWindow.scrollbars.front();assert(bar.value==&listOffset && bar.maximum==ListMaximum() && bar.maximum==ListHeight()-(ListViewportBottom-ListViewportTop));
    assert(bar.thumb.bottom-bar.thumb.top==12); // v83 thumbs keep their native size.
    queue.clear();Click(mainWindow,101);assert(listOffset==20);Click(mainWindow,100);assert(listOffset==0 && queue.empty());
    MouseButton(mainWindow.native+4,nullptr,WM_LBUTTONDOWN,0,235,bar.thumb.top+3);
    MouseMove(mainWindow.native+4,nullptr,235,350);assert(listOffset>0);
    Paint(mainWindow);MouseButton(mainWindow.native+4,nullptr,WM_LBUTTONUP,0,235,350);assert(mainWindow.dragScroll==-1);
    listOffset=0;Wheel(mainWindow.native+4,nullptr,40,100,1);assert(listOffset==60);
    listOffset=0;Paint(mainWindow);assert(mainWindow.listHits.front().rect.bottom-mainWindow.listHits.front().rect.top==18);
    for(const auto& b:mainWindow.buttons){if(b.id==1)assert(b.rect.right-b.rect.left==12 && b.rect.bottom-b.rect.top==12);else assert(b.rect.bottom-b.rect.top<=19);}
    queue.clear();MouseButton(mainWindow.native+4,nullptr,WM_LBUTTONUP,0,190,430);assert(queue.empty());
    MouseMove(mainWindow.native+4,nullptr,190,430);Paint(mainWindow);std::vector<DWORD> unpressed(mainWindow.image->pixels,mainWindow.image->pixels+600*450);
    MouseButton(mainWindow.native+4,nullptr,WM_LBUTTONDOWN,0,190,430);Paint(mainWindow);
    {int changed=0;for(int y=426;y<443;++y)for(int x=172;x<236;++x)if(mainWindow.image->pixels[y*600+x]!=unpressed[y*600+x])++changed;assert(mainWindow.pressed==6 && changed>0);}Save(*mainWindow.image,directory/"quest-pressed.bmp");
    MouseMove(mainWindow.native+4,nullptr,40,100);MouseButton(mainWindow.native+4,nullptr,WM_LBUTTONUP,0,40,100);assert(mainWindow.pressed==-1 && queue.empty());
    mouse(mainWindow.native+4,WM_LBUTTONUP,0,190,430);assert(!queue.empty());
    MouseButton(mainWindow.native+4,nullptr,WM_LBUTTONDOWN,0,190,430);MouseEnter(mainWindow.native+4,nullptr,0);assert(mainWindow.pressed==-1 && !mainWindow.mouseDown);
    Paint(mainWindow);queue.clear();mouse(mainWindow.native+4,WM_LBUTTONUP,0,100,430);
    assert(listPage==1 && listOffset==0 && queue.back().map==1);quests.resize(11);Paint(mainWindow);
    assert(!buttonEnabled(mainWindow,5) && buttonEnabled(mainWindow,4));
    mouse(mainWindow.native+4,WM_LBUTTONUP,0,40,430);assert(listPage==0 && queue.back().map==0);
    puts("PASS categories: medal isolation, expand/collapse all, tab totals, 50-row pages, drag/wheel scrollbar and detail resizing");
    category=1;Paint(mainWindow);for(const auto& hit:mainWindow.listHits)if(hit.entry.index>=0)assert(hit.rect.bottom-hit.rect.top<=20);
    category=0;view=desiredView=3;routePage=0;routeMap=103020100;routeTotal=4;detailOffset=0;selectedRow=-1;
    sourceKind=2;sourceTarget=4000018;sourceRows={{103020100,2,8,0,0,1200,L"金银岛 - 逆恩森林 II",L"木块"}};
    routeRows={{103000000,0,8,0,0,800,L"金银岛 - 废弃都市",L"主城出发"},{103010000,0,0,0,0,1200,L"金银岛 - 废都南方工地",L"通过地图传送门"},{103020000,0,8,0,0,1200,L"金银岛 - 逆恩森林 I",L"通过地图传送门"},{103020100,0,0,0,0,1200,L"金银岛 - 逆恩森林 II",L"通过地图传送门"}};
    resourceText[L"Map/Map/Map1/103020100.img/info/mapMark"]=L"KerningCity";
    message.clear();textScroll=0;Paint(mainWindow);assert(mainWindow.rowHits.size()==4); // every step of the route map is visible
    assert(hasButton(mainWindow,11) && !buttonEnabled(mainWindow,11));
    int green=0;const auto firstHit=mainWindow.rowHits.front();
    for(int y=firstHit.rect.top;y<firstHit.rect.bottom;++y)for(int x=295;x<571;++x)if(GetPixel(mainWindow.image->dc,x,y)==Palette::Green)++green;
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
    assert(GetPixel(confirmWindow.image->dc,1,120)!=RGB(253,253,253) && GetPixel(confirmWindow.image->dc,348,120)!=RGB(253,253,253));
    Save(*confirmWindow.image,directory/"quest-confirm.bmp");
    queue.clear();int originalQuest=selectedQuest,originalOffset=listOffset;
    Click(mainWindow,2);Click(mainWindow,7);Wheel(mainWindow.native+4,nullptr,40,100,1);Key(mainWindow.native+4,nullptr,VK_ESCAPE,0);
    assert(selectedQuest==originalQuest && view==3 && listOffset==originalOffset && mainWindow.shown && queue.empty());
    assert(HitTest(mainWindow.native,nullptr,20,10,&control)==2);
    using EnabledInput=int(__thiscall*)(void*);inputMethods[8]=reinterpret_cast<void*>(InputEnabled);
    assert(Method<EnabledInput>(mainWindow.native+4,32)(mainWindow.native+4)==0);
    assert(Method<EnabledInput>(confirmWindow.native+4,32)(confirmWindow.native+4)==1);
    modalMouse(confirmWindow.native+4,WM_LBUTTONUP,0,120,229);assert(confirming && confirmWindow.shown && queue.size()==1 && queue.front().token==123);
    Paint(confirmWindow);modalMouse(confirmWindow.native+4,WM_LBUTTONUP,0,120,229);Key(confirmWindow.native+4,nullptr,VK_ESCAPE,0);assert(queue.size()==1 && confirmWindow.shown);
    Snapshot result;result.sequence=11;result.quest=selectedQuest;result.view=5;result.features=3;result.message=L"传送成功";
    receive(result,queue.front());assert(!confirmWindow.shown && !quote.token && view==3);
    assert(Method<EnabledInput>(mainWindow.native+4,32)(mainWindow.native+4)==1);
    Click(mainWindow,7);assert(view==2 && selectedRow==0);Click(mainWindow,7);assert(view==1 && selectedRow==0);Click(mainWindow,7);assert(mainWindow.collapsed);
    SetExpanded(true);AdvanceDrawer(mainWindow.drawerStart+DrawerDuration);queue.clear();view=desiredView=1;
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
    // Reopening restores the last selection in each tab, including a later server page.
    categoryMemory={};category=0;listPage=1;listOffset=0;quests={{2050,0,0,10,0,0,L"第一页之外",L""},{2055,0,0,20,0,0,L"记住这项任务",L""}};
    questTotal=80;SelectQuest(2055,L"记住这项任务");AdvanceDrawer(mainWindow.drawerStart+DrawerDuration);
    Click(mainWindow,1);assert(!mainWindow.shown);queue.clear();OpenMain();assert(restoreSelection && listPage==1 && selectedQuest==0);
    Snapshot remembered;remembered.sequence=20;remembered.view=0;remembered.total=80;remembered.progress=80;remembered.ready=1;remembered.rows=quests;
    // RestoreCategory intentionally clears the visible rows; use the server's new list.
    remembered.rows={{2050,0,0,10,0,0,L"第一页之外",L""},{2055,0,0,20,0,0,L"记住这项任务",L""}};
    receive(remembered,{0,0,0,0,1,0});assert(selectedQuest==2055 && !mainWindow.collapsed && !restoreSelection);
    AdvanceDrawer(mainWindow.drawerStart+DrawerDuration);Click(mainWindow,3);assert(category==1 && restoreSelection && selectedQuest==0 && listPage==0);
    Snapshot readyList=remembered;readyList.sequence=21;readyList.total=1;readyList.rows={{2001,0,1,15,0,0,L"可交付的任务",L""}};
    receive(readyList,{0,0,0,1,0,0});assert(selectedQuest==0);SelectQuest(2001,L"可交付的任务");AdvanceDrawer(mainWindow.drawerStart+DrawerDuration);
    Click(mainWindow,2);remembered.sequence=22;receive(remembered,{0,0,0,0,1,0});assert(selectedQuest==2055 && listPage==1);
    AdvanceDrawer(mainWindow.drawerStart+DrawerDuration);Paint(mainWindow);mouse(mainWindow.native+4,WM_LBUTTONUP,0,170,36);readyList.sequence=23;receive(readyList,{0,0,0,1,0,0});assert(selectedQuest==2001 && !mainWindow.collapsed);
    AdvanceDrawer(mainWindow.drawerStart+DrawerDuration);Key(mainWindow.native+4,nullptr,VK_ESCAPE,0);OpenMain();readyList.sequence=24;
    receive(readyList,{0,0,0,1,0,0});assert(selectedQuest==2001 && !mainWindow.collapsed);
    AdvanceDrawer(mainWindow.drawerStart+DrawerDuration);Click(mainWindow,1);OpenMain();readyList.sequence=25;readyList.rows.clear();readyList.total=0;
    receive(readyList,{0,0,0,1,0,0});assert(selectedQuest==0 && categoryMemory[1].quest==0 && mainWindow.collapsed && !restoreSelection);
    category=0;listPage=1;quests=remembered.rows;SelectQuest(2055,L"记住这项任务");Click(mainWindow,1);OpenMain();
    Snapshot moved=remembered;moved.sequence=26;moved.rows.resize(1);receive(moved,{0,0,0,0,1,0});assert(restoreSelection && listPage==0);
    moved.sequence=27;moved.rows={remembered.rows[1]};receive(moved,{0,0,0,0,0,0});assert(selectedQuest==2055 && listPage==0 && !restoreSelection);
    category=0;listPage=listOffset=0;quests=remembered.rows;resourceText[L"Quest/QuestInfo.img/2050/area"]=L"20";resourceText[L"Quest/QuestInfo.img/2055/area"]=L"30";
    SelectQuest(2055,L"记住这项任务");SelectQuest(2050,L"第一页之外");AdvanceDrawer(mainWindow.drawerStart+DrawerDuration);collapsedGroups.insert(30);Paint(mainWindow);
    for(const auto& hit:mainWindow.listHits)if(hit.entry.index<0 && hit.entry.group==30){int y=(hit.rect.top+hit.rect.bottom)/2;mouse(mainWindow.native+4,WM_LBUTTONUP,0,80,y);break;}
    assert(selectedQuest==2055 && !collapsedGroups.count(30));
    Snapshot anotherCharacter=remembered;anotherCharacter.sequence=28;
    pending.emplace(28,Pending{{0,0,0,category,listPage,0},GetTickCount()});auto otherBytes=wire(anotherCharacter);
    // Snapshot wire character is at byte offset 10. A different character clears all selections.
    int otherCharacter=99;memcpy(otherBytes.data()+10,&otherCharacter,4);QuestHelperUI::Receive(otherBytes.data(),otherBytes.size());
    assert(selectedQuest==0 && categoryMemory[0].quest==0 && categoryMemory[1].quest==0 && categoryMemory[0].groupQuests.empty());
    SetExpanded(true);AdvanceDrawer(mainWindow.drawerStart+DrawerDuration);
    puts("PASS compact UI: 18-pixel category rows, one banner, 12-pixel close icon, pressed/drag-out feedback and drawer clipping/reversal");
    puts("PASS remembered selections: both tabs, close/ESC reopening, page 2 and reordered pages, regional group, removed quest and character isolation");
    view=desiredView=2;sourceRows={{103000000,2,8,0,0,800,L"金银岛 - 废弃都市",L"掉落来源"},{103020000,2,8,0,0,2147483647,L"金银岛 - 逆恩森林 I",L"完整费用"}};
    selectedRow=0;detailOffset=textScroll=0;Paint(mainWindow);Save(*mainWindow.image,directory/"quest-map-table.bmp");
        TestNarrowTiles(*mainWindow.image);
    {   // ---- Native Maple Helper (NPC 9900001) ----
        assert(QuestHelperUI::InstallHelper(true) && QuestHelperUI::HelperEnabled() && helperWindow.kind==2);
        helperWindow.initialized=true;Show(helperWindow);assert(helperWindow.shown && WindowWidth(helperWindow)==HelperWidth && WindowHeight(helperWindow)==HelperHeight);
        auto utf8=[](std::vector<unsigned char>& b,const std::wstring& value){int n=WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);std::string bytes(n,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),bytes.data(),n,nullptr,nullptr);QuestProtocol::append(b,uint16_t(n));b.insert(b.end(),bytes.begin(),bytes.end());};
        auto mwire=[&](const MapleProtocol::Snapshot& s){
            std::vector<unsigned char> b;using QuestProtocol::append;
            append(b,MapleProtocol::Version);append(b,uint8_t(s.view));append(b,s.sequence);append(b,s.character);append(b,s.meso);
            append(b,s.cash);append(b,s.maplePoint);append(b,s.credit);append(b,uint8_t(s.flags));append(b,s.value);append(b,s.extra);
            utf8(b,s.title);utf8(b,s.message);append(b,uint16_t(s.rows.size()));
            for(const auto& r:s.rows){append(b,r.id);append(b,uint8_t(r.kind));append(b,uint8_t(r.flags));append(b,r.cost);append(b,int16_t(r.min));append(b,int16_t(r.max));append(b,r.icon);utf8(b,r.label);utf8(b,r.info);}
            return b;
        };
        auto sent=[&](int action){assert(!helper.queue.empty());auto& b=helper.queue.back();assert(b.size()==24 && b[0]==0x04 && b[1]==0x10 && b[2]==MapleProtocol::Version && b[3]==action);int arg=0;memcpy(&arg,b.data()+8,4);return arg;};
        auto respond=[&](MapleProtocol::Snapshot s){s.sequence=helper.sequence;s.character=42;s.meso=445100938;s.cash=11099068;auto b=mwire(s);assert(QuestHelperUI::ReceiveHelper(b.data(),b.size()));};
        // Requests match the server's 22-byte payload behind the 0x1004 opcode.
        auto warpBytes=MapleProtocol::Request(MapleProtocol::Warp,9,100000000,2,800);int64_t cost=0;memcpy(&cost,warpBytes.data()+16,8);assert(warpBytes.size()==24 && cost==800);
        MapleProtocol::Snapshot open;open.view=MapleProtocol::Open;open.character=42;auto openBytes=mwire(open);MapleProtocol::Snapshot parsed;
        assert(MapleProtocol::Parse(openBytes.data(),openBytes.size(),parsed) && parsed.view==MapleProtocol::Open);
        open.view=MapleProtocol::HubView;openBytes=mwire(open);assert(!MapleProtocol::Parse(openBytes.data(),openBytes.size(),parsed)); // only Open may be unsolicited
        helper.page=HubPage;HelperRequest(MapleProtocol::Hub);sent(MapleProtocol::Hub);
        MapleProtocol::Snapshot hub;hub.view=MapleProtocol::HubView;hub.flags=1;hub.title=L"月兒枫叶助手";hub.message=L"欢迎来到月兒枫叶助手";
        hub.rows={{71,0,1,0,0,5040000,0,L"超级传送",L"前往已探索的城镇与练级地图"},{81,0,0,0,0,1302000,0,L"装备商店",L"购买基础装备"},{82,0,1,0,0,2120000,0,L"宠物杂物设置",L"宠物拾取杂物的处理方式"},
            {3,1,0,0,0,5030000,0,L"传送自由",L"前往自由市场"},{69,1,0,0,0,4031012,0,L"快速转职",L"完成职业转职"},{70,1,0,0,0,2290000,0,L"学习技能",L"学习职业技能"},
            {72,1,0,0,0,4031344,0,L"转世重生",L"转世重生"},{4,1,0,0,0,5360000,0,L"爆率一览",L"查看当前地图掉落"},{2,1,0,0,0,4280000,0,L"在线奖励",L"领取在线时长奖励"},
            {0,1,0,0,0,2022336,0,L"新人福利",L"领取新人礼包"},{1,1,0,0,0,4001126,0,L"每日签到",L"每日签到奖励"},{83,1,0,0,0,2370000,0,L"技能书 / 能手册",L"查看技能书与能手册"},
            {62,2,0,0,0,5450000,0,L"超级商店",L"GM 商店"}};
        respond(hub);assert(helper.hub.size()==13 && helper.cash==11099068 && helper.pending.empty() && helper.title==L"月兒枫叶助手");
        helper.queue.clear();PaintHelper(helperWindow);Save(*helperWindow.image,directory/"maple-hub.bmp");
        auto tile=[&](int feature){for(int i=0;i<int(helper.hub.size());++i)if(helper.hub[i].id==feature)return HelperTile+i;return -1;};
        assert(hasButton(helperWindow,tile(81)) && helperWindow.tabs.size()==2 && !hasButton(helperWindow,tile(62))); // GM tiles live on their own tab
        Click(helperWindow,HelperHubTab+1);PaintHelper(helperWindow);assert(hasButton(helperWindow,tile(62)) && !hasButton(helperWindow,tile(81)));Click(helperWindow,HelperHubTab);PaintHelper(helperWindow);
        // Server features: one request each; the window steps aside when the server answers.
        Click(helperWindow,tile(81));assert(sent(MapleProtocol::Feature)==81);
        MapleProtocol::Snapshot done;done.view=MapleProtocol::Done;done.value=81;respond(done);assert(!helperWindow.shown && helper.queue.empty());
        // Super teleport page.
        Show(helperWindow);helper.page=HubPage;PaintHelper(helperWindow);Click(helperWindow,tile(71));
        assert(helper.page==TeleportPage && sent(MapleProtocol::Teleports)==2);
        MapleProtocol::Snapshot towns;towns.view=MapleProtocol::TeleportView;towns.value=2;towns.extra=40;
        towns.rows={{104000000,2,1,0,0,0,800,L"维多利亚岛 - 明珠港",L""},{100000000,2,1,0,0,0,1200,L"维多利亚岛 - 射手村",L"维多利亚岛 - 射手村"},
            {101050000,2,1,0,0,0,600000000,L"妖精森林 - 埃欧雷",L"妖精森林 - 埃欧雷"}};
        respond(towns);assert(helper.teleports.size()==3 && helper.locked==40);
        HelperRow(1);PaintHelper(helperWindow);Save(*helperWindow.image,directory/"maple-teleport.bmp");
        assert(buttonEnabled(helperWindow,HelperWarp));
        Click(helperWindow,HelperWarp);assert(sent(MapleProtocol::Warp)==100000000);
        {auto& b=helper.queue.back();int category=0;int64_t price=0;memcpy(&category,b.data()+12,4);memcpy(&price,b.data()+16,8);assert(category==2 && price==1200);}
        HelperRow(2);PaintHelper(helperWindow);assert(!buttonEnabled(helperWindow,HelperWarp)); // 600,000,000 mesos is not affordable
        MapleProtocol::Snapshot failed;failed.view=MapleProtocol::Error;failed.value=MapleProtocol::Warp;failed.message=L"该地图当前无法直接传送。";
        respond(failed);assert(helper.status==failed.message && sent(MapleProtocol::Teleports)==2); // a failed warp refreshes prices
        helper.queue.clear();helper.pending.clear();helper.loading=false;
        // Level maps carry their level range; switching tabs drops the old list.
        Click(helperWindow,HelperTab+1);assert(helper.category==1 && helper.teleports.empty() && sent(MapleProtocol::Teleports)==1);
        MapleProtocol::Snapshot levels;levels.view=MapleProtocol::TeleportView;levels.value=1;levels.extra=17;
        levels.rows={{104040000,1,1,1,15,0,500,L"维多利亚岛 - 射手训练场",L"维多利亚岛 - 射手训练场"},{103000101,1,1,20,25,0,680,L"废弃都市 - 地铁<第1地区>",L"废弃都市 - 地铁<第1地区>"}};
        respond(levels);PaintHelper(helperWindow);Save(*helperWindow.image,directory/"maple-levels.bmp");
        // Pet loot page: choosing another mode sends it; the current one cannot be chosen again.
        helper.queue.clear();Click(helperWindow,HelperBack);assert(helper.page==HubPage);PaintHelper(helperWindow);Click(helperWindow,tile(82));
        assert(helper.page==PetPage);sent(MapleProtocol::Pet);
        MapleProtocol::Snapshot pet;pet.view=MapleProtocol::PetView;pet.value=0;
        pet.message=L"任务专属道具始终正常拾取。进行中任务所需的普通怪物杂物会保留拾取入包，直到满足任务数量。矿石、母矿、宝石、魔法石、召唤石等珍贵材料始终正常拾取。本设置仅针对满足任务后多余的普通怪物杂物生效。";
        pet.rows={{0,0,1,0,0,4000000,0,L"正常拾取",L"默认行为，所有杂物正常放入背包。"},{1,0,0,0,0,4000000,0,L"忽略杂物",L"宠物跳过普通怪物杂物不捡，不再占用背包。"},
            {2,0,0,0,0,4031138,0,L"自动出售",L"宠物拾取普通怪物杂物时按店价折算为金币，无需背包空间。"}};
        respond(pet);helper.queue.clear();PaintHelper(helperWindow);
        assert(!buttonEnabled(helperWindow,HelperTile+500) && buttonEnabled(helperWindow,HelperTile+502));
        Click(helperWindow,HelperTile+502);assert(sent(MapleProtocol::PetSet)==2);
        pet.value=2;pet.title=L"设置成功！当前宠物策略：自动出售";for(auto& r:pet.rows)r.flags=r.id==2;respond(pet);
        assert(helper.petMode==2 && helper.status==pet.title);PaintHelper(helperWindow);Save(*helperWindow.image,directory/"maple-pet.bmp");
        // Late or unknown sequences never change the view.
        auto stale=mwire(pet);assert(QuestHelperUI::ReceiveHelper(stale.data(),stale.size()) && helper.petMode==2);
        HideHelper();assert(!helperWindow.shown && helper.queue.empty() && helper.pending.empty());
        puts("PASS maple helper: hub tiles and GM tab, server features, teleport tabs/prices/affordability/refresh, pet modes and stale replies");

        // ---- Lone Adventurer's Talisman (item 2430034), opened by the server inside a solo boss room ----
        open.view=MapleProtocol::Open;open.value=MapleProtocol::OpenTalisman;openBytes=mwire(open);
        assert(MapleProtocol::Parse(openBytes.data(),openBytes.size(),parsed) && parsed.value==MapleProtocol::OpenTalisman);
        open.view=MapleProtocol::TalismanView;openBytes=mwire(open);assert(!MapleProtocol::Parse(openBytes.data(),openBytes.size(),parsed)); // never unsolicited
        open.view=MapleProtocol::TalismanView+1;openBytes=mwire(open);assert(!MapleProtocol::Parse(openBytes.data(),openBytes.size(),parsed));
        helper.page=TalismanPage;helper.talisman.clear();helper.talismanSkill=0;helper.talismanTab=0;helper.status.clear();
        Show(helperWindow);RefreshHelperPage();assert(sent(MapleProtocol::TalismanList)==0 && helper.queue.size()==1); // no hub request
        // A third-job Ranger: own bow skills are refused, fourth-job Buffs are locked, Meditation is already borrowed.
        constexpr int A=MapleProtocol::TalismanAvailable,On=MapleProtocol::TalismanActive,Own=MapleProtocol::TalismanOwnJob,Lock=MapleProtocol::TalismanLocked;
        MapleProtocol::Snapshot list;list.view=MapleProtocol::TalismanView;list.title=L"扎昆的祭台";list.value=3;list.extra=1;
        list.rows={
            {3121008,0,Own|Lock,4,0,3121008,0,L"集中精力",L"物攻 +26\n弓箭手 · 四转（神射手）\n物理攻击 +26，并降低技能的 MP 消耗。"},
            {1101006,0,A,2,0,1101006,0,L"怒气",L"物攻 +12\n战士 · 二转（剑客）\n物理攻击 +12，物理防御 -12。"},
            {2101001,0,A|On,2,0,2101001,0,L"精神力",L"魔攻 +20\n法师 · 二转（火毒法师）\n魔法攻击 +20。"},
            {3121002,0,Own|Lock,4,0,3121002,0,L"火眼晶晶",L"暴击率 +15%\n弓箭手 · 四转（神射手）\n暴击率提高 15%，暴击伤害随之提高。"},
            {2301004,1,A,2,0,2301004,0,L"祝福",L"防御回避 +20\n法师 · 二转（牧师）\n物理防御、魔法防御、命中、回避各 +20。"},
            {1301007,1,A,2,0,1301007,0,L"神圣之火",L"HP/MP +60%\n战士 · 二转（枪战士）\n最大 HP 与最大 MP 各提高 60%。"},
            {1121002,1,Lock,4,0,1121002,0,L"稳如泰山",L"防击退 90%\n战士 · 四转（英雄）\n受到攻击时有 90% 的概率不被击退。"},
            {1101007,1,A,2,0,1101007,0,L"伤害反击",L"反弹伤害\n战士 · 二转（剑客）\n将受到的部分伤害反弹给敌人，对首领同样有效。"},
            {2001002,1,A,1,0,2001002,0,L"魔法盾",L"MP 抵伤 80%\n法师 · 一转\n受到伤害的 80% 改由 MP 承担。"},
            {2001003,1,A,1,0,2001003,0,L"魔法铠甲",L"物防 +40\n法师 · 一转\n物理防御 +40。"},
            {2311003,2,A,3,0,2311003,0,L"神圣之光",L"经验提升\n法师 · 三转（祭司）\n提高获得的经验值。"},
            {4101004,2,A,2,0,4101004,0,L"极速",L"移速 +40\n飞侠 · 二转（刺客）\n移动速度 +40，跳跃力 +20。"},
            {5121009,2,Lock,4,0,5121009,0,L"速度激发",L"攻速提升\n海盗 · 四转（冲锋队长）\n提高攻击速度。"},
            {1101004,2,A,2,0,1101004,0,L"快速武器",L"攻速提升\n战士 · 二转（剑客）\n提高攻击速度。"},
            {2321005,1,Lock,4,0,2321005,0,L"圣灵之盾",L"免疫异常\n法师 · 四转（主教）\n效果期间不会陷入异常状态。"},
            {2121004,2,Lock,4,0,2121004,0,L"无限",L"不耗 MP\n法师 · 四转（火毒魔导师）\n效果期间使用技能不消耗 MP。"},
            {3101004,2,Own,2,0,3101004,0,L"无形箭",L"不耗箭矢\n弓箭手 · 二转（猎人）\n使用弓、弩攻击时不消耗箭矢。"},
            {1311008,0,A,3,0,1311008,0,L"龙之魂",L"物攻 +12\n战士 · 三转（龙骑士）\n物理攻击 +12，但效果期间会持续消耗 HP。"},
            {1121010,0,Lock,4,0,1121010,0,L"激怒",L"物攻提升\n战士 · 四转（英雄）\n提高物理攻击力，效果规则与原技能相同。"}};
        respond(list);helper.queue.clear();
        assert(helper.talisman.size()==19 && helper.room==L"扎昆的祭台" && helper.talismanCount==3 && helper.talismanActive==1 && !helper.loading);
        assert(TalismanTabCount(0)==6 && TalismanTabCount(1)==7 && TalismanTabCount(2)==6); // Maple Warrior is never lent
        assert(TalismanPart(helper.talisman[1],0)==L"物攻 +12" && TalismanPart(helper.talisman[1],1)==L"战士 · 二转（剑客）" && TalismanPart(helper.talisman[1],2)==L"物理攻击 +12，物理防御 -12。");
        PaintHelper(helperWindow);
        auto cards=[&]{int n=0;for(const auto& b:helperWindow.buttons)if(b.id>=TalismanTile && b.id<TalismanTile+100)++n;return n;};
        assert(cards()==6 && helperWindow.tabs.size()==3 && !buttonEnabled(helperWindow,HelperBorrow) && buttonEnabled(helperWindow,HelperClose));
        assert(!hasButton(helperWindow,HelperBack) && !hasButton(helperWindow,HelperRefresh)); // opened by the item, not the hub
        // Selecting an available Buff enables borrowing; the request carries the skill id.
        Click(helperWindow,TalismanTile+1);assert(helper.talismanSkill==1101006);PaintHelper(helperWindow);assert(buttonEnabled(helperWindow,HelperBorrow));
        Click(helperWindow,HelperBorrow);assert(sent(MapleProtocol::TalismanBorrow)==1101006 && helper.loading);
        PaintHelper(helperWindow);assert(!buttonEnabled(helperWindow,HelperBorrow)); // no double borrow while waiting
        list.message=L"已借用「精神力」，离开首领房前持续有效。";list.value=2;
        respond(list);helper.queue.clear();
        assert(helper.status==list.message && helper.statusColor==Palette::Green && helper.talismanCount==2 && helper.talismanSkill==1101006);
        PaintHelper(helperWindow);Save(*helperWindow.image,directory/"maple-talisman.bmp");
        // The status line stays inside the white body: nothing is drawn between y=365 and the bar.
        std::vector<DWORD> withStatus(helperWindow.image->pixels,helperWindow.image->pixels+HelperWidth*HelperHeight);
        helper.status=std::wstring(80,L'长');PaintHelper(helperWindow);
        for(int y=365;y<HelperBar;++y)for(int x=0;x<HelperWidth;++x)assert(helperWindow.image->pixels[y*HelperWidth+x]==withStatus[y*HelperWidth+x]);
        helper.status=list.message;
        // Own-job and locked Buffs can be inspected but not borrowed.
        Click(helperWindow,TalismanTile+0);PaintHelper(helperWindow);assert(helper.talismanSkill==3121008 && !buttonEnabled(helperWindow,HelperBorrow));
        Click(helperWindow,TalismanTab+2);PaintHelper(helperWindow);assert(helper.talismanTab==2 && cards()==6);
        Click(helperWindow,TalismanTile+16);PaintHelper(helperWindow);assert(helper.talismanSkill==3101004 && !buttonEnabled(helperWindow,HelperBorrow));
        Save(*helperWindow.image,directory/"maple-talisman-own.bmp");
        // A refused borrow shows the reason in red and reloads the list.
        Click(helperWindow,TalismanTile+11);Click(helperWindow,HelperBorrow);assert(sent(MapleProtocol::TalismanBorrow)==4101004);
        MapleProtocol::Snapshot refused;refused.view=MapleProtocol::Error;refused.value=MapleProtocol::TalismanBorrow;refused.message=L"背包中没有孤胆冒险家的护符。";
        respond(refused);assert(helper.status==refused.message && helper.statusColor==Palette::Red && sent(MapleProtocol::TalismanList)==0);
        helper.queue.clear();helper.pending.clear();helper.loading=false;
        // Without talismans nothing can be borrowed.
        helper.talismanCount=0;PaintHelper(helperWindow);assert(!buttonEnabled(helperWindow,HelperBorrow));
        // Leaving the room: the list request is refused and the cards disappear.
        HelperRequest(MapleProtocol::TalismanList);
        MapleProtocol::Snapshot outside;outside.view=MapleProtocol::Error;outside.value=MapleProtocol::TalismanList;outside.message=L"只有单人挑战专属首领房时，才能使用孤胆冒险家的护符。";
        respond(outside);assert(helper.talisman.empty() && helper.talismanSkill==0);PaintHelper(helperWindow);assert(cards()==0);
        Click(helperWindow,HelperClose);assert(!helperWindow.shown && helper.queue.empty() && helper.pending.empty());
        puts("PASS talisman: item-opened page, 6/7/6 tabs, card states, borrow request, green/red status inside the body, own-job/locked/empty guards");

        // ---- Universal Warp, version 3: Level / Town / Custom tabs ----
        Show(helperWindow);helper.page=TeleportPage;helper.category=MapleProtocol::TownMaps;helper.status.clear();helper.queue.clear();helper.pending.clear();helper.loading=false;
        PaintHelper(helperWindow);
        assert(helperWindow.tabs.size()==3);
        for(const auto& tab:helperWindow.tabs)assert(tab.id==HelperTab+MapleProtocol::LevelMaps || tab.id==HelperTab+MapleProtocol::TownMaps || tab.id==HelperTab+MapleProtocol::CustomMaps); // no Boss or Event tab
        Click(helperWindow,HelperTab+MapleProtocol::CustomMaps);assert(helper.category==MapleProtocol::CustomMaps && sent(MapleProtocol::Teleports)==MapleProtocol::CustomMaps);
        MapleProtocol::Snapshot custom;custom.view=MapleProtocol::TeleportView;custom.value=MapleProtocol::CustomMaps;custom.extra=30; // extra = the server's limit
        custom.rows={{100000000,4,1,0,0,0,1200,L"维多利亚岛 - 射手村",L""},{104040000,4,1,0,0,0,500,L"维多利亚岛 - 射手训练场",L""},
            {101030000,4,0,0,0,0,0,L"维多利亚岛 - 林中之城",L"尚未到访，到访后才能传送"}};
        respond(custom);helper.queue.clear();
        assert(helper.customLimit==30 && helper.locked==1 && helper.teleports.size()==3);
        PaintHelper(helperWindow);
        assert(buttonEnabled(helperWindow,HelperAddHere) && !buttonEnabled(helperWindow,HelperAddId) && hasButton(helperWindow,HelperInput));
        // Typing a map id: focus the box, digits (no leading zero) and Backspace, then Enter adds it.
        Click(helperWindow,HelperInput);assert(helper.inputFocus);
        for(unsigned key:{unsigned('0'),unsigned('1'),unsigned('0'),unsigned('1'),unsigned('0'),unsigned('3'),unsigned('9'),unsigned(VK_BACK),unsigned(VK_NUMPAD0),unsigned('0'),unsigned('0'),unsigned('0')})assert(HelperKey(key));
        assert(helper.customInput==L"101030000");
        PaintHelper(helperWindow);assert(buttonEnabled(helperWindow,HelperAddId));
        Save(*helperWindow.image,directory/"maple-custom.bmp");
        assert(HelperKey(VK_RETURN) && sent(MapleProtocol::CustomAdd)==101030000);
        MapleProtocol::Snapshot added;added.view=MapleProtocol::Done;added.value=MapleProtocol::CustomAdd;added.message=L"已将「林中之城」加入自定义地区。";
        respond(added);assert(helper.status==added.message && helper.statusColor==Palette::Green && helper.customInput.empty() && sent(MapleProtocol::Teleports)==MapleProtocol::CustomMaps);
        helper.queue.clear();helper.pending.clear();helper.loading=false;
        // Keys only reach the box while it has focus; clicking a row releases it.
        HelperRow(2);assert(!helper.inputFocus && !HelperKey('5'));
        // An unvisited map can be removed but not warped to.
        PaintHelper(helperWindow);assert(!buttonEnabled(helperWindow,HelperWarp) && buttonEnabled(helperWindow,HelperRemove));
        Click(helperWindow,HelperRemove);assert(sent(MapleProtocol::CustomRemove)==101030000);
        helper.queue.clear();helper.pending.clear();helper.loading=false;
        HelperRow(0);PaintHelper(helperWindow);assert(buttonEnabled(helperWindow,HelperWarp));
        Click(helperWindow,HelperWarp);{auto& b=helper.queue.back();int category=0;memcpy(&category,b.data()+12,4);assert(category==MapleProtocol::CustomMaps);}
        helper.queue.clear();helper.pending.clear();helper.loading=false;
        Click(helperWindow,HelperAddHere);assert(sent(MapleProtocol::CustomAdd)==0); // 0 = the current map
        helper.queue.clear();helper.pending.clear();helper.loading=false;
        // At the limit both add buttons are disabled.
        custom.extra=3;HelperRequest(MapleProtocol::Teleports,MapleProtocol::CustomMaps);respond(custom);helper.queue.clear();
        helper.customInput=L"100000000";PaintHelper(helperWindow);assert(!buttonEnabled(helperWindow,HelperAddHere) && !buttonEnabled(helperWindow,HelperAddId));
        helper.customInput.clear();
        puts("PASS universal warp v3: Level/Town/Custom tabs, server limit, map id box (focus, digits, Backspace, Enter), add/remove, unvisited rows cannot warp");

        // ---- Boss-room boost page (exit NPC) ----
        helper.page=BoostPage;helper.boostPrices.clear();helper.boostNpc=2030010;helper.status.clear();helper.queue.clear();helper.pending.clear();helper.loading=false;
        RefreshHelperPage();assert(sent(MapleProtocol::Boost)==0 && helper.queue.size()==1);
        auto price=[](int t)->int64_t{return t<=1?0:1000000+250000LL*(t-2)+10000LL*(t-2)*(t-2);};
        MapleProtocol::Snapshot boost;boost.view=MapleProtocol::BoostView;boost.value=10;boost.extra=0;boost.title=L"扎昆的祭台";
        for(int t=0;t<=30;++t)boost.rows.push_back({t,MapleProtocol::BoostPrice,0,0,0,0,price(t),L"",L""});
        boost.rows.push_back({10,MapleProtocol::BoostPaid,0,0,0,0,price(10),L"",L""});
        respond(boost);helper.queue.clear();
        assert(helper.boostApplied[0]==10 && helper.boostDraft[0]==10 && helper.boostDraft[1]==0 && helper.boostPaid==3640000 && helper.boostPaidTotal==10 && helper.room==L"扎昆的祭台");
        PaintHelper(helperWindow);assert(!buttonEnabled(helperWindow,BoostConfirm) && hasButton(helperWindow,BoostExit) && !hasButton(helperWindow,HelperBack));
        // A click steps once; holding repeats after 400 ms and every 60 ms, and the release adds nothing.
        Click(helperWindow,BoostStep+1);assert(helper.boostDraft[0]==11);
        helperWindow.mouseDown=true;helperWindow.pressed=helperWindow.hover=BoostStep+1;helper.repeatStart=0;helper.lastRepeat=0;
        HelperRepeat(1000);assert(helper.boostDraft[0]==11);HelperRepeat(1300);assert(helper.boostDraft[0]==11);
        HelperRepeat(1400);assert(helper.boostDraft[0]==12);HelperRepeat(1430);assert(helper.boostDraft[0]==12);HelperRepeat(1460);assert(helper.boostDraft[0]==13);
        helperWindow.mouseDown=false;HelperRepeat(1500);Click(helperWindow,BoostStep+1);assert(helper.boostDraft[0]==13 && !helper.repeated);
        helperWindow.pressed=helperWindow.hover=-1;
        // The slider jumps to the clicked value, drags, and never lets the total pass 30.
        PaintHelper(helperWindow);
        int span=BoostTrackRight-BoostTrackLeft-BoostThumb;auto thumbCentre=[&](int v){return BoostTrackLeft+v*span/30+BoostThumb/2;};
        RECT physical=helper.sliderTracks[0];
        assert(HelperPress({thumbCentre(20),physical.top+8}) && HelperDragging() && helper.boostDraft[0]==20);
        HelperDrag(thumbCentre(5));assert(helper.boostDraft[0]==5);HelperDrag(BoostTrackRight+50);assert(helper.boostDraft[0]==30);HelperDrag(-40);assert(helper.boostDraft[0]==0);
        HelperDrag(thumbCentre(25));HelperRelease();assert(!HelperDragging() && helper.boostDraft[0]==25);
        Click(helperWindow,BoostStep+3);Click(helperWindow,BoostStep+3);Click(helperWindow,BoostStep+3);Click(helperWindow,BoostStep+3);Click(helperWindow,BoostStep+3);Click(helperWindow,BoostStep+3);
        assert(helper.boostDraft[1]==5); // 25 + 5 = 30: the sixth step is refused
        // Presets.
        Click(helperWindow,BoostPreset+0);assert(helper.boostDraft[0]==30 && helper.boostDraft[1]==0);
        Click(helperWindow,BoostPreset+1);assert(helper.boostDraft[0]==0 && helper.boostDraft[1]==30);
        Click(helperWindow,BoostPreset+2);assert(helper.boostDraft[0]==15 && helper.boostDraft[1]==15);
        Click(helperWindow,BoostPreset+3);assert(helper.boostDraft[0]==0 && helper.boostDraft[1]==0);
        // Physical 12 + magic 3 after paying for 10: only the difference is due.
        helper.boostDraft[0]=12;helper.boostDraft[1]=3;assert(BoostCharge()==2300000);
        PaintHelper(helperWindow);assert(buttonEnabled(helperWindow,BoostConfirm));
        Save(*helperWindow.image,directory/"maple-boost.bmp");
        // Re-splitting inside the paid tier is free.
        helper.boostDraft[0]=4;helper.boostDraft[1]=6;assert(BoostCharge()==0);
        int64_t savedMeso=helper.meso;helper.meso=1000;helper.boostDraft[0]=12;helper.boostDraft[1]=3;PaintHelper(helperWindow);assert(!buttonEnabled(helperWindow,BoostConfirm));helper.meso=savedMeso;
        Click(helperWindow,BoostConfirm);
        {auto& b=helper.queue.back();int p=0,m=0;int64_t charge=0;memcpy(&p,b.data()+8,4);memcpy(&m,b.data()+12,4);memcpy(&charge,b.data()+16,8);assert(b[3]==MapleProtocol::BoostSet && p==12 && m==3 && charge==2300000);}
        PaintHelper(helperWindow);assert(!buttonEnabled(helperWindow,BoostConfirm)); // nothing twice while waiting
        MapleProtocol::Snapshot boostRefused;boostRefused.view=MapleProtocol::Error;boostRefused.value=MapleProtocol::BoostSet;boostRefused.message=L"价格已变化，请确认新的补交金额后再试。";
        respond(boostRefused);assert(helper.statusColor==Palette::Red && sent(MapleProtocol::Boost)==0 && helper.boostDraft[0]==12); // the draft survives the reload
        boost.rows.back().cost=price(10);respond(boost);assert(helper.boostDraft[0]==12 && helper.boostDraft[1]==3);
        helper.queue.clear();Click(helperWindow,BoostConfirm);
        boost.value=12;boost.extra=3;boost.message=L"强化完成：物理 ×12，魔法 ×3，补交 2,300,000 金币。";boost.rows.back()={15,MapleProtocol::BoostPaid,0,0,0,0,price(15),L"",L""};
        respond(boost);helper.queue.clear();
        assert(helper.statusColor==Palette::Green && helper.boostApplied[0]==12 && helper.boostApplied[1]==3 && helper.boostPaidTotal==15 && BoostCharge()==0);
        // The status line stays inside the white body here too.
        std::vector<DWORD> boostShot(helperWindow.image->pixels,helperWindow.image->pixels+HelperWidth*HelperHeight);PaintHelper(helperWindow);boostShot.assign(helperWindow.image->pixels,helperWindow.image->pixels+HelperWidth*HelperHeight);
        helper.status=std::wstring(80,L'长');PaintHelper(helperWindow);
        for(int y=365;y<HelperBar;++y)for(int x=0;x<HelperWidth;++x)assert(helperWindow.image->pixels[y*HelperWidth+x]==boostShot[y*HelperWidth+x]);
        helper.status=boost.message;
        // The exit dialogue belongs to the NPC script: the window steps aside.
        Click(helperWindow,BoostExit);assert(sent(MapleProtocol::BoostExit)==0);
        MapleProtocol::Snapshot exited;exited.view=MapleProtocol::Done;exited.value=MapleProtocol::BoostExit;respond(exited);assert(!helperWindow.shown);
        puts("PASS boost page: server price table, steps and press-and-hold, slider jump/drag/clamp, presets, difference-only charge, quoted confirm, refused reload, exit dialogue");
    }
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
    // Opaque points across tile boundaries (the frame corners are intentionally translucent).
    for(POINT p:{POINT{5,30},POINT{245,30},POINT{511,255},POINT{512,256},POINT{590,440}}){unsigned color=0;
        assert(SUCCEEDED(Method<Pixel>(canvas,0x88)(canvas,p.x,p.y,&color)));
        assert(color==mainWindow.image->pixels[p.y*Width+p.x]);
    }
    Method<Release>(canvas,8)(canvas);
    puts("PASS actual v83 PCOM/Canvas: UI raster upload and tile boundaries; generated categories, NPC/description, payment and table previews");
    auto term=reinterpret_cast<void(__cdecl*)()>(GetProcAddress(pcom,"PcTermModule"));assert(term);term();
}
