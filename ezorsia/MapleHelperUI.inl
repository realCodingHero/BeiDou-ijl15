// Native Maple Helper (NPC 9900001): hub, super teleport and pet loot pages, plus the
// Lone Adventurer's Talisman page that item 2430034 opens inside a solo boss room.
// Included by QuestHelperUI.cpp inside its anonymous namespace; it reuses that file's
// native window, skin and text helpers. The server (MapleHelperUiHandler) owns every
// rule, price and script; this view only renders snapshots and sends choices.

// Quest/backgrnd3 (296x289): title, blue summary box, white body and button bar.
constexpr Axis HelperX{150,150,175,175},HelperY{150,150,200,200};
constexpr int HelperBodyBottom=366,HelperBar=376,HelperNpc=9900001,TalismanItem=2430034;
enum HelperPage {HubPage=0,TeleportPage=1,PetPage=2,TalismanPage=3,BoostPage=4,BooksPage=5,TransmogPage=6};
enum HelperButton {HelperBack=3001,HelperWarp=3002,HelperRefresh=3003,HelperBorrow=3004,HelperClose=3005,
    HelperRemove=3006,HelperAddHere=3007,HelperAddId=3008,HelperInput=3009,BoostConfirm=3010,BoostExit=3011,
    BookExchangeButton=3012,BookDecomposeButton=3013,BookDecomposeAllButton=3014,BookTab=2300,
    BookMapsButton=3015,BookRouteButton=3016,BookWarpButton=3017,
    TgTryButton=3018,TgApplyButton=3019,TgRestoreButton=3020,TgTab=2400,TgBaseRow=4000,TgSourceRow=4500,
    HelperExpandAll=3021,HelperCollapseAll=3022,HelperAskYes=3023,HelperAskNo=3024,
    BoostStep=3100,BoostPreset=3110, // steps: 3100+2*slider (down) / +1 (up); presets 3110..3113
    HelperTile=1000,TalismanTile=1600,HelperTab=2000,HelperHubTab=2100,TalismanTab=2200};
constexpr int BoostTrackLeft=100,BoostTrackRight=282,BoostThumb=22;
struct HelperState {
    bool enabled=false,loading=false,serverReady=false;
    int page=HubPage,hubTab=0,category=2,selected=-1,offset=0,locked=0,petMode=0,sequence=0;
    int cash=0,maplePoint=0,credit=0,flags=0;int64_t meso=0;
    std::vector<MapleProtocol::Row> hub,teleports,pets,talisman;
    // Talisman page: tab, selected skill, talismans left and Buffs already borrowed.
    int talismanTab=0,talismanSkill=0,talismanCount=0,talismanActive=0;
    // Custom teleport tab: the typed map id and whether its box has keyboard focus.
    // The limit comes from the server (game_config custom_warp_limit) with every custom list.
    std::wstring customInput;bool inputFocus=false;int customLimit=0;
    // Teleport lists group by region (the grey part of "Region - Name"); folded regions by name.
    std::set<std::wstring> foldedRegions;
    // Teleport and remove wait for a yes in a small dialog over the list: 0 none, 1 warp, 2 remove.
    int ask=0;MapleProtocol::Row askRow{};int askCategory=0;
    // Boost page: applied and drafted allocations (physical, magic), the price of each total,
    // what this run has paid, the exit NPC, slider drag and press-and-hold repeat.
    int boostApplied[2]={0,0},boostDraft[2]={0,0},boostPaidTotal=0,boostNpc=0,dragSlider=-1;int64_t boostPaid=0;
    std::vector<int64_t> boostPrices;RECT sliderTracks[2]{};
    DWORD repeatStart=0,lastRepeat=0;bool repeated=false;
    // Book exchange: rows of both tabs, the tab, the selected book, the page balance, the refund
    // percent and the jobs line; locked books and "decompose all" need a second click.
    std::vector<MapleProtocol::Row> books;int bookTab=0,bookSelected=0,bookPages=0,bookRefund=0,bookConfirm=0;
    bool bookAllArmed=false;std::wstring bookLine;
    // Drop maps and routes of one book: view 0 = book list, 1 = drop maps, 2 = route to one map.
    std::vector<MapleProtocol::Row> bookMaps,bookRoute;int bookView=0,bookItem=0,bookMapPick=-1,bookStepPick=-1,bookRouteMap=0;
    std::wstring bookMapsTitle,bookRouteTitle,bookNotice;
    // Transmog page: equips, looks for the chosen equip, the ore, the craftsman, the chosen codes,
    // the right column's scroll, a pending second click and whether a try-on is showing.
    std::vector<MapleProtocol::Row> tgBases,tgSources;std::wstring tgOreName,tgSourcesNote;
    int tgTab=0,tgBase=0,tgSource=0,tgRight=0,tgOre=0,tgOreHeld=0,tgOreNeed=0,tgNpc=0;
    bool tgArmed=false,tgTrying=false,tgTryPending=false;
    std::wstring title,welcome,notice,notes,status,room;COLORREF statusColor=Palette::Muted;
    std::deque<std::vector<unsigned char>> queue;
    std::map<int,std::pair<int,DWORD>> pending; // sequence -> (action, sent tick; 0 while queued)
    DWORD lastSend=0;
};
HelperState helper;
bool InField();
// The salon window (SalonUI.inl) shares this request queue and these replies.
void OpenSalon();
void SalonOnCatalog(const MapleProtocol::Snapshot& s);
void SalonOnState(const MapleProtocol::Snapshot& s);
void SalonOnError(const std::wstring& message);
void HelperDirty(){if(helperWindow.shown)Invalidate(helperWindow);}
void HelperStatus(const std::wstring& text,COLORREF color){helper.status=text;helper.statusColor=color;}
void SendBytes(std::vector<unsigned char>& bytes) {
    void* socket=*reinterpret_cast<void**>(0x00BE7914);if(!socket)return;
    struct Packet{int loopback;unsigned char* data;unsigned long size;unsigned offset;int shanda;};
    Packet packet{0,bytes.data(),static_cast<unsigned long>(bytes.size()),0,0};
    using Send=void(__thiscall*)(void*,Packet*);reinterpret_cast<Send>(0x0049637B)(socket,&packet);
}
void HelperRequest(int action,int arg=0,int arg2=0,int64_t arg3=0) {
    // Queue at most a handful; an unanswered flood helps nobody.
    if(helper.queue.size()>=8)return;
    int seq=++helper.sequence;helper.queue.push_back(MapleProtocol::Request(action,seq,arg,arg2,arg3));
    helper.pending[seq]={action,0};helper.loading=true;
}
void HelperFlush(DWORD now) {
    if(helper.queue.empty() || now-helper.lastSend<150)return;
    if(!*reinterpret_cast<void**>(0x00BE7914))return;
    auto bytes=std::move(helper.queue.front());helper.queue.pop_front();
    int seq=0;memcpy(&seq,bytes.data()+4,4);auto it=helper.pending.find(seq);if(it!=helper.pending.end())it->second.second=now;
    SendBytes(bytes);helper.lastSend=now;
}
// The shared "loading"/"timeout" labels talk about quests and a Refresh button the talisman lacks.
const std::wstring& HelperLoadingText(){return Text(helper.page==TalismanPage || helper.page==BoostPage || helper.page==BooksPage?L"tmLoading":L"loading");}
void HelperExpire(DWORD now) {
    bool expired=false;
    for(auto it=helper.pending.begin();it!=helper.pending.end();)if(it->second.second && now-it->second.second>10000){it=helper.pending.erase(it);expired=true;}else ++it;
    if(expired){helper.loading=!helper.pending.empty();HelperStatus(Text(helper.page==TalismanPage?L"tmTimeout":L"timeout"),Palette::Red);HelperDirty();}
}
void RefreshHelperPage() {
    // The talisman page is opened by the item, not the helper: it needs no hub snapshot.
    if(helper.page==TalismanPage){HelperRequest(MapleProtocol::TalismanList);return;}
    if(helper.page==BoostPage){HelperRequest(MapleProtocol::Boost);return;}
    if(helper.page==TransmogPage){HelperRequest(MapleProtocol::TransmogList);return;}
    if(helper.page==BooksPage) {
        if(helper.bookView==1)HelperRequest(MapleProtocol::BookMaps,helper.bookItem);
        else if(helper.bookView==2)HelperRequest(MapleProtocol::BookRoute,helper.bookItem,helper.bookRouteMap);
        else HelperRequest(MapleProtocol::Books);
        return;
    }
    HelperRequest(MapleProtocol::Hub);
    if(helper.page==TeleportPage)HelperRequest(MapleProtocol::Teleports,helper.category);
    else if(helper.page==PetPage)HelperRequest(MapleProtocol::Pet);
}
void OpenHelperPage(int page) {
    if(!helper.enabled || !InField())return;
    helper.page=page;helper.selected=-1;helper.offset=0;helper.status.clear();helper.ask=0;
    if(page==TalismanPage){helper.talisman.clear();helper.talismanTab=0;helper.talismanSkill=0;helper.room.clear();}
    if(page==BoostPage){helper.boostPrices.clear();helper.room.clear();helper.dragSlider=-1;}
    if(page==TransmogPage){helper.tgBases.clear();helper.tgSources.clear();helper.tgTab=0;helper.tgBase=helper.tgSource=helper.tgRight=0;helper.tgArmed=helper.tgTrying=false;}
    helper.inputFocus=false;
    Show(helperWindow);RefreshHelperPage();HelperDirty();
}
// Sends one request now, outside the 150 ms queue; tests swap it out.
void (*HelperSendNow)(std::vector<unsigned char>&)=SendBytes;
void HideHelper() {
    // A try-on only lives on this client. The hidden window gets no more updates to flush the
    // queue, so the real look is asked for straight away.
    if(helper.page==TransmogPage && helper.tgTrying){auto bytes=MapleProtocol::Request(MapleProtocol::TransmogTry,++helper.sequence,helper.tgBase,0);HelperSendNow(bytes);}
    helper.tgTrying=false;helper.ask=0;
    Hide(helperWindow);helper.queue.clear();helper.pending.clear();helper.loading=false;
}

// ---- Drawing -----------------------------------------------------------------
std::wstring HelperTitle() {
    if(helper.page==TeleportPage)return Text(L"mhTeleport");
    if(helper.page==PetPage)return Text(L"mhPet");
    if(helper.page==TalismanPage)return Text(L"tmTitle");
    if(helper.page==BoostPage)return Text(L"bsTitle");
    if(helper.page==BooksPage)return Text(L"bkTitle");
    if(helper.page==TransmogPage)return Text(L"tgTitle");
    return helper.title.empty()?Text(L"mhTitle"):helper.title;
}
// A label/value pair inside the blue box: muted label, white figure.
void Figure(HDC dc,int left,int right,int y,const std::wstring& label,const std::wstring& value) {
    int width=TextWidth(dc,label);
    Write(dc,{left,y,left+width+2,y+14},label,Palette::OnBlueMuted);
    Write(dc,{left+width+6,y,right,y+14},value,Palette::OnBlue);
}
void RedRule(HDC dc,int top) {
    // The same three rows that sit under the native quest window tabs.
    Fill(dc,{4,top,HelperWidth-4,top+1},RGB(170,0,0));Fill(dc,{4,top+1,HelperWidth-4,top+2},RGB(187,0,0));Fill(dc,{4,top+2,HelperWidth-4,top+3},RGB(204,0,0));
}
void SectionHeading(HDC dc,int y,const std::wstring& text) {
    int width=TextWidth(dc,text);
    Write(dc,{12,y,12+width+2,y+16},text,Palette::Heading);
    Dotted(dc,20+width,HelperWidth-12,y+8,Palette::StripEdge);
}
// Feature tile: native item icon and the name, vertically centred (no hint line, so 13+ tiles fit).
constexpr int HubTileHeight=34,HubTilePitch=36;
void Tile(Window& w,HDC dc,RECT r,int id,const MapleProtocol::Row& row) {
    bool hover=w.hover==id,pressed=hover && w.pressed==id;
    if(hover)SelectionBox(dc,r);
    else{Fill(dc,r,RGB(246,248,251));HBRUSH edge=CreateSolidBrush(RGB(221,229,238));FrameRect(dc,&r,edge);DeleteObject(edge);}
    int shift=pressed?1:0;
    {PixelClip clip(r);Scaled(*w.image,{r.left+4+shift,r.top+1+shift,r.left+36+shift,r.top+33+shift},ItemIcon(row.icon),true);}
    Write(dc,{r.left+40+shift,r.top+shift,r.right-3,r.bottom+shift},row.label,Palette::Ink);
    w.buttons.push_back({r,id,row.label,true});
}
int TileGrid(Window& w,HDC dc,int y,int group) {
    int column=0;bool any=false;
    for(int i=0;i<int(helper.hub.size());++i) {
        if(helper.hub[i].kind!=group)continue;any=true;
        RECT r{10+column*121,y,10+column*121+118,y+HubTileHeight};Tile(w,dc,r,HelperTile+i,helper.hub[i]);
        if(++column==3){column=0;y+=HubTilePitch;}
    }
    return any && column?y+HubTilePitch:y;
}
void PaintHub(Window& w,HDC dc) {
    bool gm=(helper.flags&1)!=0;if(!gm)helper.hubTab=0;
    int y=110;
    if(gm) {
        DrawTab(w,dc,HelperHubTab,{7,108,92,127},Text(L"mhFeatures"),helper.hubTab==0);
        DrawTab(w,dc,HelperHubTab+1,{93,108,178,127},Text(L"mhGm"),helper.hubTab==1);
        RedRule(dc,127);y=134;
    }
    if(helper.hub.empty()){TextBlock(dc,{14,y+6,HelperWidth-14,y+40},Text(L"loading"),Palette::Muted);return;}
    if(helper.hubTab==0) {
        SectionHeading(dc,y,Text(L"mhCommon"));y=TileGrid(w,dc,y+18,0);
        SectionHeading(dc,y+2,Text(L"mhMore"));TileGrid(w,dc,y+20,1);
    } else {SectionHeading(dc,y,Text(L"mhGm"));TileGrid(w,dc,y+18,2);}
}
// One teleport list line: a region header (index<0, the region's position) or a destination.
struct TeleportEntry {int index;std::wstring region;int count;};
std::wstring TeleportRegion(const MapleProtocol::Row& r){std::wstring map,street;SplitMapName(r.label,map,street);return street;}
// Regions keep the server's order of their first destination; rows without one stay ungrouped.
std::vector<TeleportEntry> TeleportEntries() {
    std::vector<std::wstring> order;std::map<std::wstring,std::vector<int>> groups;
    for(int i=0;i<int(helper.teleports.size());++i){auto region=TeleportRegion(helper.teleports[i]);if(!groups.count(region))order.push_back(region);groups[region].push_back(i);}
    std::vector<TeleportEntry> result;
    for(int g=0;g<int(order.size());++g) {
        const auto& region=order[g];
        if(!region.empty())result.push_back({-1-g,region,int(groups[region].size())});
        if(region.empty() || !helper.foldedRegions.count(region))for(int i:groups[region])result.push_back({i,region,0});
    }
    return result;
}
bool AnyRegion(bool folded) {
    for(const auto& r:helper.teleports){auto region=TeleportRegion(r);if(!region.empty() && helper.foldedRegions.count(region)!=0==folded)return true;}
    return false;
}
void ToggleRegion(int index) {
    for(const auto& e:TeleportEntries())if(e.index==index) {
        if(!helper.foldedRegions.erase(e.region))helper.foldedRegions.insert(e.region);
        HelperDirty();return;
    }
}
void PaintTeleports(Window& w,HDC dc,int bottom) {
    auto& img=*w.image;
    // Version 3: boss and event shortcuts are gone; the third tab lists the character's own maps.
    const int categories[]={MapleProtocol::LevelMaps,MapleProtocol::TownMaps,MapleProtocol::CustomMaps};
    const wchar_t* names[]={L"mhCat1",L"mhCat2",L"mhCatCustom"};
    for(int c=0;c<3;++c)DrawTab(w,dc,HelperTab+categories[c],{7+c*121,108,7+c*121+120,127},Text(names[c]),helper.category==categories[c]);
    RedRule(dc,127);
    int top=133;
    if(helper.category==MapleProtocol::CustomMaps) {
        // Toolbar: add the current map, or type a map id and add it.
        int saved=int(helper.teleports.size());bool room=saved<helper.customLimit && !helper.loading;
        ButtonAt(w,dc,HelperAddHere,{10,top,104,top+17},L"mhAddHere",room);
        RECT box{110,top,196,top+17};
        Fill(dc,box,RGB(255,255,255));{HBRUSH edge=CreateSolidBrush(helper.inputFocus?Palette::SelectEdge:Palette::StripEdge);FrameRect(dc,&box,edge);DeleteObject(edge);}
        if(helper.customInput.empty() && !helper.inputFocus)Write(dc,{box.left+4,box.top,box.right-3,box.bottom},Text(L"mhMapId"),Palette::Faint);
        else {
            std::wstring shown=helper.customInput;if(helper.inputFocus && (GetTickCount()/500)%2==0)shown+=L"|";
            Write(dc,{box.left+4,box.top,box.right-3,box.bottom},shown,Palette::Ink);
        }
        w.buttons.push_back({box,HelperInput,Text(L"mhMapId"),true});
        ButtonAt(w,dc,HelperAddId,{200,top,246,top+17},L"mhAdd",room && !helper.customInput.empty());
        Write(dc,{250,top,HelperWidth-12,top+17},Text(L"mhSaved")+L" "+std::to_wstring(saved)+L"/"+std::to_wstring(helper.customLimit),
            Palette::Muted,false,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
        top+=23;
    }
    if(!helper.notice.empty()) {
        top+=TextBlock(dc,{12,top,HelperWidth-14,top+42},helper.notice,helper.teleports.empty()?Palette::Muted:Palette::Amber)+4;
        if(helper.teleports.empty())return;
    }
    // Count and expand all (down arrow) / collapse all (up arrow), as on the quest list.
    Write(dc,{12,top,HelperWidth-50,top+14},Text(L"mhPlaces")+L" "+std::to_wstring(helper.teleports.size()),Palette::Muted);
    IconButton(w,HelperExpandAll,HelperWidth-40,top+1,L"BtMin2",Trimmed(Text(L"expandAll")),AnyRegion(true));
    IconButton(w,HelperCollapseAll,HelperWidth-25,top+1,L"BtMax2",Trimmed(Text(L"collapseAll")),AnyRegion(false));
    top+=17;
    // The offset counts lines; the last offset is the first one whose tail fits.
    constexpr int CardHeight=34,HeaderHeight=20;auto entries=TeleportEntries();
    auto height=[&](const TeleportEntry& e){return e.index<0?HeaderHeight:CardHeight;};
    int maximum=0;for(int i=int(entries.size())-1,h=0;i>=0;--i){h+=height(entries[i]);if(h>bottom-top){maximum=i+1;break;}}
    helper.offset=(std::min)(helper.offset,maximum);
    int y=top,visible=0;
    for(int n=helper.offset;n<int(entries.size()) && y+height(entries[n])<=bottom;++n,++visible) {
        const auto& e=entries[n];
        if(e.index<0) {
            RECT strip{8,y,HelperWidth-22,y+HeaderHeight-2};HeaderStrip(dc,strip);bool folded=helper.foldedRegions.count(e.region)!=0;
            Sprite(img,11,y+3,Skin(folded?L"UI/Basic.img/BtMax/normal/0":L"UI/Basic.img/BtMin/normal/0"));
            int nameWidth=(std::min)(200,TextWidth(dc,e.region));
            Write(dc,{27,y,27+nameWidth,y+HeaderHeight-2},e.region,Palette::Heading);
            Write(dc,{27+nameWidth,y,HelperWidth-26,y+HeaderHeight-2},L" ("+std::to_wstring(e.count)+L")",Palette::Muted);
            w.rowHits.push_back({strip,e.index});y+=HeaderHeight;continue;
        }
        int i=e.index;const auto& r=helper.teleports[i];bool selected=i==helper.selected,unlocked=(r.flags&1)!=0,affordable=unlocked && helper.meso>=r.cost;
        RECT card{8,y,HelperWidth-22,y+CardHeight-1};
        int saved=SaveDC(dc);IntersectClipRect(dc,card.left,card.top,card.right,card.bottom);
        if(selected)SelectionBox(dc,card);
        {PixelClip clip(card);SpotAt(img,20,y+10,3);}
        // Saved custom maps that were never visited are listed without a price and cannot be used.
        auto cost=unlocked?Amount(r.cost)+L" "+Text(L"costColumn"):Text(L"mhNotVisited");int costWidth=TextWidth(dc,cost);
        // Name and region only: the price has its own column and the server sends no map id.
        MapNameText(dc,{32,y+3,HelperWidth-26-costWidth-8,y+17},r.label,r.id,unlocked?Palette::Ink:Palette::Faint,false);
        Write(dc,{HelperWidth-26-costWidth,y+3,HelperWidth-26,y+17},cost,!unlocked?Palette::Faint:affordable?Palette::Green:Palette::Red,false,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
        std::wstring second;
        if(r.min>0)second=L"Lv. "+std::to_wstring(r.min)+L" ~ "+std::to_wstring(r.max);
        if(!r.info.empty() && r.info!=r.label)second+=(second.empty()?L"":L"   ")+r.info;
        Write(dc,{32,y+18,HelperWidth-26,y+32},second,Palette::Muted);
        RestoreDC(dc,saved);
        if(!selected)Dotted(dc,10,HelperWidth-24,y+CardHeight-1);
        w.rowHits.push_back({card,i});y+=CardHeight;
    }
    if(maximum>0)ScrollbarAt(w,dc,{HelperWidth-18,top,HelperWidth-6,bottom},helper.offset,maximum,(std::max)(1,visible),true,1);
}
// The confirm dialog: the page behind is dimmed and stops taking input; Enter says yes, Esc no.
bool AskWarpReady(){return helper.ask==1 && (helper.askRow.flags&1) && helper.meso>=helper.askRow.cost && !helper.loading;}
void PaintAsk(Window& w,HDC dc) {
    auto& img=*w.image;GdiFlush();
    for(int i=0;i<img.width*img.height;++i){DWORD c=img.pixels[i];img.pixels[i]=(c&0xFF000000)|((c>>1)&0x7F7F7F);}
    w.buttons.clear();w.tabs.clear();w.rowHits.clear();w.scrollbars.clear();
    constexpr int AskWidth=300,AskHeight=150;int x=(HelperWidth-AskWidth)/2,y=(HelperHeight-AskHeight)/2;
    {Bitmap frame(AskWidth,AskHeight);if(frame.pixels){std::fill(frame.pixels,frame.pixels+AskWidth*AskHeight,0);
        Nine(frame,{0,0,AskWidth,AskHeight},Skin(L"UI/UIWindow.img/Quest/backgrnd"),MainX,ModalY,true);Sprite(img,x,y,&frame);}}
    Fill(dc,{x+5,y+5,x+AskWidth-5,y+19},RGB(255,255,255));
    Write(dc,{x+10,y+5,x+AskWidth-20,y+19},Text(L"mhAskTitle"));
    std::wstring map,street;SplitMapName(helper.askRow.label,map,street);
    int line=y+32;
    line+=TextBlock(dc,{x+16,line,x+AskWidth-16,line+30},Format(Text(helper.ask==1?L"mhAskWarp":L"mhAskRemove"),map),Palette::Ink)+2;
    if(!street.empty()){Write(dc,{x+16,line,x+AskWidth-16,line+14},street,Palette::Faint);line+=18;}
    if(helper.ask==1) {
        Dotted(dc,x+16,x+AskWidth-16,line+2,Palette::StripEdge);line+=8;
        Write(dc,{x+16,line,x+AskWidth-16,line+14},Text(L"cost")+L": "+Amount(helper.askRow.cost),Palette::Red);
        Write(dc,{x+16,line+16,x+AskWidth-16,line+30},Text(L"meso")+L": "+Amount(helper.meso),helper.meso>=helper.askRow.cost?Palette::Muted:Palette::Red);
    }
    int bar=y+AskHeight-26;
    ButtonAt(w,dc,HelperAskYes,{x+AskWidth/2-84,bar,x+AskWidth/2-4,bar+17},L"mhAskYes",helper.ask==1?AskWarpReady():!helper.loading);
    ButtonAt(w,dc,HelperAskNo,{x+AskWidth/2+4,bar,x+AskWidth/2+84,bar+17},L"cancel");
}
// Opens the dialog for the selected destination; nothing is sent until the player says yes.
void AskSelected(int kind) {
    if(helper.page!=TeleportPage || helper.selected<0 || helper.selected>=int(helper.teleports.size()) || helper.loading)return;
    const auto& r=helper.teleports[helper.selected];
    if(kind==1 && (!(r.flags&1) || helper.meso<r.cost))return;
    helper.ask=kind;helper.askRow=r;helper.askCategory=helper.category;helper.inputFocus=false;HelperDirty();
}
void AnswerAsk(bool yes) {
    int kind=helper.ask;helper.ask=0;
    if(yes && kind==1 && helper.askCategory==helper.category){helper.status.clear();HelperRequest(MapleProtocol::Warp,helper.askRow.id,helper.category,helper.askRow.cost);}
    else if(yes && kind==2){helper.status.clear();HelperRequest(MapleProtocol::CustomRemove,helper.askRow.id);}
    HelperDirty();
}
void PaintPet(Window& w,HDC dc) {
    auto& img=*w.image;int y=110;
    for(int i=0;i<int(helper.pets.size());++i,y+=52) {
        const auto& r=helper.pets[i];int id=HelperTile+500+i;bool current=r.id==helper.petMode,hover=w.hover==id;
        RECT card{8,y,HelperWidth-8,y+50};
        if(current || hover)SelectionBox(dc,card);else Dotted(dc,10,HelperWidth-10,y+50);
        Sprite(img,14,y+19,Skin(current?L"UI/Basic.img/CheckBox/1":L"UI/Basic.img/CheckBox/0"));
        {PixelClip clip(card);Scaled(img,{34,y+9,66,y+41},ItemIcon(r.icon),true,r.id==1?90:255);}
        Write(dc,{72,y+5,HelperWidth-14,y+19},r.label,current?Palette::Blue:Palette::Ink,current);
        TextBlock(dc,{72,y+21,HelperWidth-14,y+49},r.info,Palette::Muted);
        w.buttons.push_back({card,id,r.label,!current && !helper.loading});
    }
    if(!helper.notes.empty()) {
        RECT panel{8,268,HelperWidth-8,346};
        if(auto beige=Skin(L"UI/UIWindow.img/Quest/backgrnd5"))Nine(img,panel,beige,{8,8,250,258},{8,8,100,109});
        TextBlock(dc,{16,274,HelperWidth-16,344},helper.notes,Palette::Muted);
    }
}
// ---- Lone Adventurer's Talisman --------------------------------------------------
// Row info is "tile line \n source job \n detail"; the server keeps each part on one line.
std::wstring TalismanPart(const MapleProtocol::Row& row,int part) {
    size_t start=0;
    for(int i=0;i<part;++i){start=row.info.find(L'\n',start);if(start==std::wstring::npos)return L"";++start;}
    size_t end=row.info.find(L'\n',start);
    return row.info.substr(start,end==std::wstring::npos?std::wstring::npos:end-start);
}
const MapleProtocol::Row* TalismanSelected() {
    for(const auto& r:helper.talisman)if(r.id==helper.talismanSkill)return &r;
    return nullptr;
}
int TalismanTabCount(int tab){int n=0;for(const auto& r:helper.talisman)if(r.kind==tab)++n;return n;}
Bitmap* SkillIcon(int skill) {
    if(skill<=0)return nullptr;
    return Skin(L"Skill/"+Padded(skill/10000,3)+L".img/skill/"+Padded(skill,7)+L"/icon");
}
// Own-job and higher-advancement Buffs: a faded grey icon, like disabled native skills.
void Faded(Bitmap& d,int x,int y,Bitmap* s) {
    if(!s || !s->pixels)return;GdiFlush();bool alpha=HasAlpha(*s);
    for(int j=0;j<s->height;++j)for(int i=0;i<s->width;++i) {
        DWORD p=s->pixels[j*s->width+i];unsigned a=alpha?p>>24:255;if(!a)continue;
        unsigned grey=(((p>>16)&255)*30+((p>>8)&255)*59+(p&255)*11)/100;grey=(grey+255)/2;
        Put(d,x+i,y+j,((a*90/255)<<24)|(grey<<16)|(grey<<8)|grey,false);
    }
}
// Nearest-neighbour enlargement for the item icon in the summary box.
void Enlarged(Bitmap& d,int cx,int cy,Bitmap* s,int factor) {
    if(!s || !s->pixels)return;GdiFlush();bool alpha=HasAlpha(*s);
    int left=cx-s->width*factor/2,top=cy-s->height*factor/2;
    for(int y=0;y<s->height*factor;++y)for(int x=0;x<s->width*factor;++x){DWORD p=s->pixels[(y/factor)*s->width+x/factor];if(!alpha)p|=0xFF000000;Put(d,left+x,top+y,p,false);}
}
std::wstring TalismanState(const MapleProtocol::Row& r,COLORREF& color) {
    if(r.flags&MapleProtocol::TalismanActive){color=Palette::Green;return Text(L"tmActive");}
    if(r.flags&MapleProtocol::TalismanOwnJob){color=Palette::Faint;return Text(L"tmOwn");}
    if(r.flags&MapleProtocol::TalismanLocked){color=Palette::Amber;const wchar_t* need[]={L"tmNeed1",L"tmNeed2",L"tmNeed3",L"tmNeed4"};return Text(need[(std::max)(0,(std::min)(3,r.min-1))]);}
    color=Palette::Muted;return TalismanPart(r,0);
}
void TalismanCard(Window& w,HDC dc,RECT r,int index,const MapleProtocol::Row& row) {
    int id=TalismanTile+index;bool selected=row.id==helper.talismanSkill,hover=w.hover==id,pressed=hover && w.pressed==id;
    bool usable=(row.flags&MapleProtocol::TalismanAvailable)!=0,active=(row.flags&MapleProtocol::TalismanActive)!=0;
    if(selected || hover)SelectionBox(dc,r);
    else{Fill(dc,r,active?RGB(236,248,236):RGB(246,248,251));HBRUSH edge=CreateSolidBrush(active?RGB(120,190,120):RGB(221,229,238));FrameRect(dc,&r,edge);DeleteObject(edge);}
    int shift=pressed?1:0;
    {PixelClip clip(r);if(usable)Sprite(*w.image,r.left+4+shift,r.top+4+shift,SkillIcon(row.id));else Faded(*w.image,r.left+4+shift,r.top+4+shift,SkillIcon(row.id));}
    COLORREF stateColor;auto state=TalismanState(row,stateColor);
    Write(dc,{r.left+40+shift,r.top+5+shift,r.right-3,r.top+19+shift},row.label,usable || active?Palette::Ink:Palette::Faint);
    Write(dc,{r.left+40+shift,r.top+21+shift,r.right-3,r.top+35+shift},state,stateColor);
    w.buttons.push_back({r,id,row.label,true}); // every card can be inspected; borrowing is checked separately
}
bool TalismanBorrowable() {
    auto r=TalismanSelected();
    return r && (r->flags&MapleProtocol::TalismanAvailable) && helper.talismanCount>0 && !helper.loading;
}
void PaintTalisman(Window& w,HDC dc) {
    auto& img=*w.image;
    const wchar_t* names[]={L"tmTab0",L"tmTab1",L"tmTab2"};
    for(int c=0;c<3;++c)DrawTab(w,dc,TalismanTab+c,{7+c*92,108,7+c*92+91,127},Text(names[c])+L" "+std::to_wstring(TalismanTabCount(c)),helper.talismanTab==c);
    RedRule(dc,127);
    if(helper.talisman.empty())return; // the status line shows loading or the server's reason
    // Three columns of hub-sized cards; the largest tab has seven Buffs (three rows).
    int column=0,y=136;
    for(int i=0;i<int(helper.talisman.size());++i) {
        const auto& row=helper.talisman[i];if(row.kind!=helper.talismanTab)continue;
        TalismanCard(w,dc,{10+column*121,y,10+column*121+117,y+39},i,row);
        if(++column==3){column=0;y+=43;}
    }
    // Detail panel: the beige native note box, above the status line.
    RECT panel{8,266,HelperWidth-8,346};
    if(auto beige=Skin(L"UI/UIWindow.img/Quest/backgrnd5"))Nine(img,panel,beige,{8,8,250,258},{8,8,100,109});
    auto selected=TalismanSelected();
    if(!selected){TextBlock(dc,{16,274,HelperWidth-16,344},Text(L"tmPick"),Palette::Muted);return;}
    bool usable=(selected->flags&MapleProtocol::TalismanAvailable)!=0;
    {PixelClip clip(panel);if(usable || (selected->flags&MapleProtocol::TalismanActive))Sprite(img,16,274,SkillIcon(selected->id));else Faded(img,16,274,SkillIcon(selected->id));}
    int nameWidth=TextWidth(dc,selected->label,true);
    Write(dc,{54,274,54+nameWidth+2,288},selected->label,Palette::Blue,true);
    Write(dc,{54+nameWidth+10,274,HelperWidth-16,288},TalismanPart(*selected,1),Palette::Muted);
    Write(dc,{54,290,HelperWidth-16,304},TalismanPart(*selected,2),Palette::Ink);
    Write(dc,{16,311,HelperWidth-16,325},Text(L"tmDuration"),Palette::Muted);
    std::wstring hint;COLORREF color=Palette::Amber;
    if(selected->flags&MapleProtocol::TalismanOwnJob){hint=Text(L"tmOwnHint");color=Palette::Red;}
    else if(selected->flags&MapleProtocol::TalismanLocked){hint=Text(L"tmLockedHint");color=Palette::Red;}
    else if(helper.talismanCount<=0){hint=Text(L"tmNoItem");color=Palette::Red;}
    else hint=Text((selected->flags&MapleProtocol::TalismanActive)?L"tmRefreshHint":L"tmCost");
    Write(dc,{16,327,HelperWidth-16,341},hint,color);
}
// ---- Book exchange --------------------------------------------------------------------
const MapleProtocol::Row* BookSelected() {
    for(const auto& r:helper.books)if(r.kind==helper.bookTab && r.id==helper.bookSelected)return &r;
    return nullptr;
}
int BookCount(int kind){int n=0;for(const auto& r:helper.books)if(r.kind==kind)++n;return n;}
// Books "decompose all" takes: every stack the character cannot use any more.
int64_t BookAllPages(int& count) {
    int64_t pages=0;count=0;
    for(const auto& r:helper.books)if(r.kind==MapleProtocol::BookHeld && !(r.flags&MapleProtocol::BookLocked)){pages+=r.cost*r.max;count+=r.max;}
    return pages;
}
std::wstring BookPages(int64_t value){return Amount(value)+L" "+Text(L"bkPageUnit");}
void BookTier(HDC dc,int& x,int y,const MapleProtocol::Row& r) {
    const wchar_t* keys[]={L"bkTier0",L"bkTier1",L"bkTier2"};const COLORREF colors[]={Palette::Green,Palette::Blue,Palette::Purple};
    int tier=(std::max)(0,(std::min)(2,r.min));auto text=Text(keys[tier]);int width=TextWidth(dc,text);
    Write(dc,{x,y,x+width+2,y+14},text,colors[tier]);x+=width+8;
    if(r.flags&(MapleProtocol::BookHot|MapleProtocol::BookCold)) {
        bool hot=(r.flags&MapleProtocol::BookHot)!=0;auto heat=Text(hot?L"bkHot":L"bkCold");width=TextWidth(dc,heat);
        Write(dc,{x,y,x+width+2,y+14},heat,hot?Palette::Red:Palette::Faint);x+=width+8;
    }
}
bool BookExchangeable() {
    auto r=BookSelected();
    return r && helper.bookTab==MapleProtocol::BookOffer && !(r->flags&MapleProtocol::BookMaxed) && helper.bookPages>=r->cost && !helper.loading;
}
constexpr int BookListTop=133,BookRowHeight=30,BookPanelTop=285;
void PaintBooks(Window& w,HDC dc) {
    auto& img=*w.image;
    DrawTab(w,dc,BookTab,{7,108,127,127},Text(L"bkTabExchange")+L" "+std::to_wstring(BookCount(MapleProtocol::BookOffer)),helper.bookTab==0);
    DrawTab(w,dc,BookTab+1,{128,108,248,127},Text(L"bkTabDecompose")+L" "+std::to_wstring(BookCount(MapleProtocol::BookHeld)),helper.bookTab==1);
    RedRule(dc,127);
    std::vector<int> shown;for(int i=0;i<int(helper.books.size());++i)if(helper.books[i].kind==helper.bookTab)shown.push_back(i);
    int bottom=BookPanelTop-2,visible=(bottom-BookListTop)/BookRowHeight;
    int maximum=(std::max)(0,int(shown.size())-visible);helper.offset=(std::min)(helper.offset,maximum);
    if(shown.empty() && !helper.loading)
        TextBlock(dc,{14,BookListTop+6,HelperWidth-14,bottom},Text(helper.bookTab==0?L"bkNoOffer":L"bkNoHeld"),Palette::Muted);
    int y=BookListTop;
    for(int n=helper.offset;n<int(shown.size()) && y+BookRowHeight<=bottom;++n,y+=BookRowHeight) {
        const auto& r=helper.books[shown[n]];bool selected=r.id==helper.bookSelected,offer=r.kind==MapleProtocol::BookOffer;
        bool maxed=(r.flags&MapleProtocol::BookMaxed)!=0,locked=(r.flags&MapleProtocol::BookLocked)!=0;
        RECT card{8,y,HelperWidth-22,y+BookRowHeight-1};
        int saved=SaveDC(dc);IntersectClipRect(dc,card.left,card.top,card.right,card.bottom);
        if(selected)SelectionBox(dc,card);
        {PixelClip clip(card);Scaled(img,{12,y+1,40,y+29},ItemIcon(r.icon),true,offer && maxed?110:255);}
        // Right column: the price (green when affordable) or the refund per copy.
        std::wstring right;COLORREF rightColor=Palette::Ink;
        if(offer){if(maxed){right=Text(L"bkMaxed");rightColor=Palette::Faint;}else{right=BookPages(r.cost);rightColor=helper.bookPages>=r.cost?Palette::Green:Palette::Red;}}
        else{right=L"+"+BookPages(r.cost);rightColor=Palette::Green;}
        int rightWidth=TextWidth(dc,right);
        auto label=offer?r.label:r.label+L" \x00D7"+std::to_wstring(r.max);
        Write(dc,{46,y+1,HelperWidth-30-rightWidth,y+15},label,offer && maxed?Palette::Faint:Palette::Ink);
        Write(dc,{HelperWidth-26-rightWidth,y+1,HelperWidth-26,y+15},right,rightColor,false,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
        int x=46;BookTier(dc,x,y+15,r);
        if(offer)Write(dc,{x,y+15,HelperWidth-26,y+29},TalismanPart(r,0),Palette::Muted);
        else if(locked){auto own=Text(L"bkOwnJob");Write(dc,{x,y+15,HelperWidth-26,y+29},own,Palette::Amber);}
        else Write(dc,{x,y+15,HelperWidth-26,y+29},r.info,Palette::Muted);
        RestoreDC(dc,saved);
        if(!selected)Dotted(dc,10,HelperWidth-24,y+BookRowHeight-1);
        w.rowHits.push_back({card,shown[n]});
    }
    if(maximum>0)ScrollbarAt(w,dc,{HelperWidth-18,BookListTop,HelperWidth-6,bottom},helper.offset,maximum,visible,true,1);
    // Detail panel: sources of the selected book, or what decomposing it gives.
    RECT panel{8,BookPanelTop,HelperWidth-8,346};
    if(auto beige=Skin(L"UI/UIWindow.img/Quest/backgrnd5"))Nine(img,panel,beige,{8,8,250,258},{8,8,100,109});
    auto selected=BookSelected();int left=16,line=BookPanelTop+4;
    if(!selected) {
        if(helper.bookTab==MapleProtocol::BookHeld){int count;auto pages=BookAllPages(count);
            TextBlock(dc,{left,line,HelperWidth-16,344},count?Format(Format(Text(L"bkAllSummary"),std::to_wstring(count)),Amount(pages)):Text(L"bkNothingToDecompose"),Palette::Muted);}
        else TextBlock(dc,{left,line,HelperWidth-16,344},Format(Text(L"bkPick"),std::to_wstring(helper.bookRefund)),Palette::Muted);
        return;
    }
    int nameWidth=TextWidth(dc,selected->label,true);
    Write(dc,{left,line,left+nameWidth+2,line+14},selected->label,Palette::Blue,true);
    int x=left+nameWidth+10;BookTier(dc,x,line,*selected);
    if(selected->kind==MapleProtocol::BookOffer) {
        Write(dc,{left,line+14,HelperWidth-16,line+28},TalismanPart(*selected,0),Palette::Ink);
        Write(dc,{left,line+28,HelperWidth-16,line+42},TalismanPart(*selected,1),Palette::Muted);
        Write(dc,{left,line+42,HelperWidth-16,line+56},TalismanPart(*selected,2),Palette::Muted);
    } else {
        Write(dc,{left,line+14,HelperWidth-16,line+28},Text(L"bkJobs")+selected->info,Palette::Ink);
        Write(dc,{left,line+28,HelperWidth-16,line+42},Format(Format(Text(L"bkRefundLine"),BookPages(selected->cost)),BookPages(selected->cost*selected->max)),Palette::Muted);
        if(selected->flags&MapleProtocol::BookLocked)
            Write(dc,{left,line+42,HelperWidth-16,line+56},Text(helper.bookConfirm==selected->id?L"bkConfirmLocked":L"bkLockedHint"),Palette::Amber);
    }
}
// ---- Book drop maps and routes ------------------------------------------------------------
const MapleProtocol::Row* BookPicked(){auto& rows=helper.bookView==1?helper.bookMaps:helper.bookRoute;int i=helper.bookView==1?helper.bookMapPick:helper.bookStepPick;return i>=0 && i<int(rows.size())?&rows[i]:nullptr;}
bool BookWarpReady(){auto r=BookPicked();return r && (r->flags&MapleProtocol::BookWarpable) && helper.meso>=r->cost && !helper.loading;}
// One map row: name with the price (or why there is none) on the right, sources or the step below.
void BookMapRow(Window& w,HDC dc,int y,int index,const MapleProtocol::Row& r,bool selected,bool route) {
    constexpr int Height=34;
    RECT card{8,y,HelperWidth-22,y+Height-1};
    int saved=SaveDC(dc);IntersectClipRect(dc,card.left,card.top,card.right,card.bottom);
    if(selected)SelectionBox(dc,card);
    {PixelClip clip(card);SpotAt(*w.image,20,y+10,3);}
    bool warpable=(r.flags&MapleProtocol::BookWarpable)!=0,boss=!route && r.kind==MapleProtocol::BookMapBoss;
    std::wstring right;COLORREF color;
    // Boss maps, and a visited map the route may not warp to, offer the route alone.
    if(boss || (route && !warpable && (r.flags&MapleProtocol::BookVisited))){right=Text(L"bkRouteOnly");color=Palette::Amber;}
    else if(warpable){right=Amount(r.cost)+L" "+Text(L"costColumn");color=helper.meso>=r.cost?Palette::Green:Palette::Red;}
    else{right=Text(L"mhNotVisited");color=Palette::Faint;}
    int rightWidth=TextWidth(dc,right);
    MapNameText(dc,{32,y+3,HelperWidth-34-rightWidth,y+17},r.label,r.id,warpable || boss?Palette::Ink:Palette::Faint,false);
    Write(dc,{HelperWidth-26-rightWidth,y+3,HelperWidth-26,y+17},right,color,false,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
    int x=32;
    if(boss){auto tag=Text(L"bkTier2");int width=TextWidth(dc,tag);Write(dc,{x,y+18,x+width+2,y+32},tag,Palette::Purple);x+=width+8;}
    Write(dc,{x,y+18,HelperWidth-26,y+32},r.info,Palette::Muted);
    RestoreDC(dc,saved);
    if(!selected)Dotted(dc,10,HelperWidth-24,y+Height-1);
    w.rowHits.push_back({card,index});
}
void PaintBookPlaces(Window& w,HDC dc,int bottom) {
    bool route=helper.bookView==2;auto& rows=route?helper.bookRoute:helper.bookMaps;
    SectionHeading(dc,110,Format(Text(route?L"bkRouteHeading":L"bkMapsHeading"),route?helper.bookRouteTitle:helper.bookMapsTitle));
    int top=130;
    if(!helper.bookNotice.empty())top+=TextBlock(dc,{12,top,HelperWidth-14,top+42},helper.bookNotice,route && !rows.empty()?Palette::Amber:Palette::Muted)+4;
    if(rows.empty())return;
    constexpr int Height=34;int visible=(std::max)(1,(bottom-top)/Height);
    int maximum=(std::max)(0,int(rows.size())-visible);helper.offset=(std::min)(helper.offset,maximum);
    int y=top,pick=route?helper.bookStepPick:helper.bookMapPick;
    for(int i=helper.offset;i<int(rows.size()) && y+Height<=bottom;++i,y+=Height)BookMapRow(w,dc,y,i,rows[i],i==pick,route);
    if(maximum>0)ScrollbarAt(w,dc,{HelperWidth-18,top,HelperWidth-6,bottom},helper.offset,maximum,visible,true,1);
}
void OpenBookMaps(int item) {
    helper.bookView=1;helper.bookItem=item;helper.bookMaps.clear();helper.bookMapPick=-1;helper.bookNotice.clear();helper.bookMapsTitle.clear();
    helper.offset=0;helper.status.clear();HelperRequest(MapleProtocol::BookMaps,item);HelperDirty();
}
void OpenBookRoute(int map) {
    helper.bookView=2;helper.bookRouteMap=map;helper.bookRoute.clear();helper.bookStepPick=-1;helper.bookNotice.clear();helper.bookRouteTitle.clear();
    helper.offset=0;helper.status.clear();HelperRequest(MapleProtocol::BookRoute,helper.bookItem,map);HelperDirty();
}
void OpenBooks() {
    helper.page=BooksPage;helper.selected=-1;helper.offset=0;helper.bookSelected=0;helper.bookConfirm=0;helper.bookAllArmed=false;helper.bookView=0;
    helper.books.clear();helper.status.clear();HelperRequest(MapleProtocol::Books);HelperDirty();
}
void BookChoose(int tab,int id){helper.bookTab=tab;helper.bookSelected=id;helper.bookConfirm=0;helper.bookAllArmed=false;HelperDirty();}
// ---- Transmog looks on this client's own avatar -----------------------------------------------
// The server sends the real equips; only the sprites drawn for the local avatar swap to the look.
// CAvatar::PrepareActionLayer (0x453AD1) hands an equip-id array to CActionMan::LoadCharacterAction
// at 0x45464B (eax = the array, edi = the avatar); the cave there passes a swapped copy instead, so
// the avatar's own AvatarLook (and everything that reads it) keeps the real ids.
struct TransmogLook {int slot=0,real=0,look=0;};
std::vector<TransmogLook> transmogLooks;
constexpr int TransmogEquipSlots=60;
int transmogEquips[TransmogEquipSlots];
DWORD transmogActionMan=0x00BE78D4,transmogLoadActionReturn=0x00454651;
constexpr DWORD LocalUserAddress=0x00BEBF98,LocalAvatarOffset=0x88,ClearActionLayerAddress=0x00453A29;
// Copies the array with each look swapped in where the slot still holds that real equip.
int* TransmogSwap(const int* equips) {
    memcpy(transmogEquips,equips,sizeof(transmogEquips));
    for(const auto& l:transmogLooks)if(l.slot>=0 && l.slot<TransmogEquipSlots && transmogEquips[l.slot]==l.real)transmogEquips[l.slot]=l.look;
    return transmogEquips;
}
void* LocalAvatar(){void* user=*reinterpret_cast<void**>(LocalUserAddress);return user?static_cast<char*>(user)+LocalAvatarOffset:nullptr;}
int* __cdecl TransmogLoadAction(void* avatar,int* equips) {
    if(transmogLooks.empty() || !equips || avatar!=LocalAvatar())return equips;
    return TransmogSwap(equips);
}
__declspec(naked) void TransmogLoadActionCave() {
    __asm {
        push eax
        push edi
        call TransmogLoadAction
        add esp,8
        mov ecx,transmogActionMan
        mov ecx,[ecx]
        jmp dword ptr[transmogLoadActionReturn]
    }
}
// Rebuilds the local avatar's layers the way a mount change does: clear, then PrepareActionLayer.
void RefreshLocalAvatar() {
    void* avatar=LocalAvatar();if(!avatar)return;
    reinterpret_cast<void(__thiscall*)(void*,int)>(ClearActionLayerAddress)(avatar,0);
    auto prepare=reinterpret_cast<void(__thiscall*)(void*,int,int,int)>((*reinterpret_cast<void***>(avatar))[5]);
    prepare(avatar,6,100,0);
}
// Tests swap these out: the live ones read client memory.
void (*TransmogRefreshAvatar)()=RefreshLocalAvatar;
void TransmogOnLooks(const MapleProtocol::Snapshot& s) {
    std::vector<TransmogLook> looks;
    for(const auto& r:s.rows)if(r.id>0 && r.id<TransmogEquipSlots && r.cost>0 && r.icon>0)looks.push_back({r.id,int(r.cost),r.icon});
    bool changed=looks.size()!=transmogLooks.size();
    for(size_t i=0;!changed && i<looks.size();++i)changed=looks[i].slot!=transmogLooks[i].slot || looks[i].real!=transmogLooks[i].real || looks[i].look!=transmogLooks[i].look;
    transmogLooks=std::move(looks);
    if(changed)TransmogRefreshAvatar();
}
// ---- Transmog ----------------------------------------------------------------------------
const MapleProtocol::Row* TgRow(const std::vector<MapleProtocol::Row>& rows,int code){for(const auto& r:rows)if(r.id==code)return &r;return nullptr;}
const MapleProtocol::Row* TgBase(){return TgRow(helper.tgBases,helper.tgBase);}
const MapleProtocol::Row* TgSource(){return TgRow(helper.tgSources,helper.tgSource);}
bool TgFits(){auto s=TgSource();return TgBase() && s && (s->flags&MapleProtocol::TransmogFits);}
bool TgApplicable(){return TgFits() && helper.tgOreHeld>=helper.tgOreNeed && !helper.loading;}
bool TgTryable(){auto b=TgBase();return helper.tgTrying || (TgFits() && (b->flags&MapleProtocol::TransmogWorn) && !helper.loading);}
std::vector<int> TgShown(){std::vector<int> v;for(int i=0;i<int(helper.tgBases.size());++i)if(helper.tgTab==0 || (helper.tgBases[i].flags&MapleProtocol::TransmogChanged))v.push_back(i);return v;}
void TgStopTry(){if(helper.tgTrying){HelperRequest(MapleProtocol::TransmogTry,helper.tgBase,0);helper.tgTryPending=false;helper.tgTrying=false;}}
// One equip row: icon, name and a second line (worn, the original equip, or why it can't be the look).
void TgRowAt(Window& w,HDC dc,RECT card,int id,const MapleProtocol::Row& r,bool selected,bool source) {
    bool usable=!source || (r.flags&MapleProtocol::TransmogFits),hover=w.hover==id;
    int saved=SaveDC(dc);IntersectClipRect(dc,card.left,card.top,card.right,card.bottom);
    if(selected || hover)SelectionBox(dc,card);
    {PixelClip clip(card);Scaled(*w.image,{card.left+2,card.top+1,card.left+28,card.top+27},ItemIcon(r.icon),true,usable?255:110);}
    Write(dc,{card.left+32,card.top+1,card.right-2,card.top+14},r.label,usable?Palette::Ink:Palette::Faint);
    std::wstring second;COLORREF color=Palette::Muted;
    if(source){if(usable)second=L"Lv. "+std::to_wstring(r.min);else{second=r.info;color=Palette::Red;}}
    else if(r.flags&MapleProtocol::TransmogChanged){second=r.info;color=Palette::Amber;}
    else{second=(r.flags&MapleProtocol::TransmogWorn)?Text(L"tgWorn"):L"Lv. "+std::to_wstring(r.min);color=(r.flags&MapleProtocol::TransmogWorn)?Palette::Blue:Palette::Muted;}
    Write(dc,{card.left+32,card.top+14,card.right-2,card.top+27},second,color);
    RestoreDC(dc,saved);
    if(!selected)Dotted(dc,card.left+2,card.right-2,card.bottom);
    w.buttons.push_back({card,id,r.label,true});
}
constexpr int TgListTop=146,TgRowHeight=28,TgPanelTop=290;
void TgColumn(Window& w,HDC dc,int left,int right,int idBase,const std::vector<MapleProtocol::Row>& rows,const std::vector<int>& shown,int& offset,int selected,bool source,int scrollId) {
    int bottom=TgPanelTop-4,visible=(bottom-TgListTop)/TgRowHeight;
    int maximum=(std::max)(0,int(shown.size())-visible);offset=(std::max)(0,(std::min)(offset,maximum));
    for(int n=offset,y=TgListTop;n<int(shown.size()) && y+TgRowHeight<=bottom;++n,y+=TgRowHeight) {
        const auto& r=rows[shown[n]];
        TgRowAt(w,dc,{left,y,right-14,y+TgRowHeight-1},idBase+shown[n],r,r.id==selected,source);
    }
    if(maximum>0)ScrollbarAt(w,dc,{right-12,TgListTop,right,bottom},offset,maximum,visible,true,1,scrollId);
}
void PaintTransmog(Window& w,HDC dc) {
    auto& img=*w.image;
    int changed=0;for(const auto& r:helper.tgBases)if(r.flags&MapleProtocol::TransmogChanged)++changed;
    DrawTab(w,dc,TgTab,{7,108,127,127},Text(L"tgTabApply"),helper.tgTab==0);
    DrawTab(w,dc,TgTab+1,{128,108,248,127},Text(L"tgTabRestore")+L" "+std::to_wstring(changed),helper.tgTab==1);
    RedRule(dc,127);
    auto shown=TgShown();
    if(helper.tgTab==0) {
        Write(dc,{12,130,180,144},Text(L"tgPickBase"),Palette::Heading,true);
        Write(dc,{196,130,HelperWidth-14,144},Text(L"tgPickSource"),Palette::Heading,true);
        if(shown.empty() && !helper.loading)TextBlock(dc,{12,TgListTop+4,180,TgPanelTop-6},Text(L"tgNoBase"),Palette::Muted);
        TgColumn(w,dc,8,182,TgBaseRow,helper.tgBases,shown,helper.offset,helper.tgBase,false,106);
        std::vector<int> sources;for(int i=0;i<int(helper.tgSources.size());++i)sources.push_back(i);
        if(helper.tgBase && sources.empty() && !helper.tgSourcesNote.empty())TextBlock(dc,{196,TgListTop+4,HelperWidth-14,TgPanelTop-6},helper.tgSourcesNote,Palette::Muted);
        TgColumn(w,dc,192,HelperWidth-14,TgSourceRow,helper.tgSources,sources,helper.tgRight,helper.tgSource,true,108);
    } else {
        if(shown.empty() && !helper.loading)TextBlock(dc,{12,TgListTop-12,HelperWidth-14,TgPanelTop-6},Text(L"tgNoChanged"),Palette::Muted);
        TgColumn(w,dc,8,HelperWidth-14,TgBaseRow,helper.tgBases,shown,helper.offset,helper.tgBase,false,106);
    }
    RECT panel{8,TgPanelTop,HelperWidth-8,346};
    if(auto beige=Skin(L"UI/UIWindow.img/Quest/backgrnd5"))Nine(img,panel,beige,{8,8,250,258},{8,8,100,109});
    int left=16,line=TgPanelTop+5;auto base=TgBase();
    if(helper.tgTab==1) {
        if(!base || !(base->flags&MapleProtocol::TransmogChanged)){TextBlock(dc,{left,line,HelperWidth-16,344},Text(L"tgPickRestore"),Palette::Muted);return;}
        Write(dc,{left,line,HelperWidth-16,line+14},base->label,Palette::Blue,true);
        Write(dc,{left,line+16,HelperWidth-16,line+30},base->info,Palette::Ink);
        Write(dc,{left,line+32,HelperWidth-16,line+46},Text(helper.tgArmed?L"tgArmedRestore":L"tgRestoreHint"),Palette::Amber);
        return;
    }
    if(!base){TextBlock(dc,{left,line,HelperWidth-16,344},Text(L"tgHintBase"),Palette::Muted);return;}
    auto source=TgSource();
    if(!source){TextBlock(dc,{left,line,HelperWidth-16,344},Text(L"tgHintSource"),Palette::Muted);return;}
    auto arrow=base->label+L"  \x2192  "+source->label;
    Write(dc,{left,line,HelperWidth-16,line+14},arrow,Palette::Blue,true);
    if(!(source->flags&MapleProtocol::TransmogFits)){Write(dc,{left,line+16,HelperWidth-16,line+30},source->info,Palette::Red);return;}
    auto cost=Text(L"tgCost")+helper.tgOreName+L" \x00D7"+std::to_wstring(helper.tgOreNeed)+L"  ("+Text(L"tgOreHeld")+L" "+std::to_wstring(helper.tgOreHeld)+L")";
    Write(dc,{left,line+16,HelperWidth-16,line+30},helper.tgOreNeed?cost:Text(L"tgFree"),helper.tgOreHeld>=helper.tgOreNeed?Palette::Ink:Palette::Red);
    Write(dc,{left,line+32,HelperWidth-16,line+46},Format(Text(helper.tgArmed?L"tgArmedApply":L"tgConsume"),source->label),Palette::Amber);
}
void TgPickBase(int code) {
    TgStopTry();helper.tgBase=code;helper.tgSource=0;helper.tgArmed=false;helper.tgSources.clear();helper.tgSourcesNote.clear();helper.tgRight=0;
    if(helper.tgTab==0 && code)HelperRequest(MapleProtocol::TransmogSources,code);
    HelperDirty();
}
// ---- Boss-room boost ------------------------------------------------------------------
int64_t BoostPrice(int total){return total>=0 && total<int(helper.boostPrices.size())?helper.boostPrices[total]:0;}
int BoostTotal(){return helper.boostDraft[0]+helper.boostDraft[1];}
// The server charges only the difference to the highest price this run has paid.
int64_t BoostCharge(){return (std::max)(int64_t(0),BoostPrice(BoostTotal())-helper.boostPaid);}
bool BoostChanged(){return helper.boostDraft[0]!=helper.boostApplied[0] || helper.boostDraft[1]!=helper.boostApplied[1];}
bool BoostConfirmable(){return !helper.boostPrices.empty() && BoostChanged() && !helper.loading && helper.meso>=BoostCharge();}
// Keeps physical + magic within the budget: raising one never pushes the other down.
void SetBoost(int slider,int value) {
    if(slider<0 || slider>1 || helper.boostPrices.empty())return;
    value=(std::max)(0,(std::min)(value,MapleProtocol::BoostBudget-helper.boostDraft[1-slider]));
    if(value!=helper.boostDraft[slider]){helper.boostDraft[slider]=value;HelperDirty();}
}
void BoostStepBy(int step){SetBoost(step/2,helper.boostDraft[step/2]+(step%2?1:-1));}
int SliderValue(int x) {
    int span=BoostTrackRight-BoostTrackLeft-BoostThumb;
    return (x-BoostTrackLeft-BoostThumb/2)*MapleProtocol::BoostBudget*2/span+1>>1; // rounded
}
std::wstring Times(int value){return L"\x00D7"+std::to_wstring((std::max)(1,value));}
void BoostSlider(Window& w,HDC dc,int slider,int y,const wchar_t* label) {
    auto& img=*w.image;int value=helper.boostDraft[slider];bool live=!helper.boostPrices.empty();
    Write(dc,{14,y,84,y+14},Text(label),live?Palette::Ink:Palette::Faint);
    IconButton(w,BoostStep+2*slider,84,y+1,L"BtMin2",Text(L"bsLess"),live && value>0);
    // Basic.img/Slider: 3-pixel caps, a tiled middle and a 22x14 thumb.
    RECT track{BoostTrackLeft,y+3,BoostTrackRight,y+11};
    if(auto mid=Skin(L"UI/Basic.img/Slider/mid"))Nine(img,{track.left+3,track.top,track.right-3,track.bottom},mid,{0,0,1,1},Whole(mid->height));
    Sprite(img,track.left,track.top,Skin(L"UI/Basic.img/Slider/left"));Sprite(img,track.right-3,track.top,Skin(L"UI/Basic.img/Slider/right"));
    int span=BoostTrackRight-BoostTrackLeft-BoostThumb,thumbX=BoostTrackLeft+value*span/MapleProtocol::BoostBudget;
    bool hot=helper.dragSlider==slider;
    Sprite(img,thumbX,y,Skin(std::wstring(L"UI/Basic.img/Slider/")+(!live?L"thumbDisabled":hot?L"thumbPressed":L"thumbNormal")));
    helper.sliderTracks[slider]={BoostTrackLeft,y-1,BoostTrackRight,y+15};
    IconButton(w,BoostStep+2*slider+1,BoostTrackRight+4,y+1,L"BtMax2",Text(L"bsMore"),live && BoostTotal()<MapleProtocol::BoostBudget);
    Write(dc,{BoostTrackRight+20,y,HelperWidth-16,y+14},value?Times(value):Text(L"bsNormal"),value?Palette::Blue:Palette::Muted,value>0,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
}
void PaintBoost(Window& w,HDC dc) {
    auto& img=*w.image;
    auto current=Text(L"bsCurrent")+L"  "+Text(L"bsPhysicalShort")+L" "+Times(helper.boostApplied[0])+L" \x00B7 "+Text(L"bsMagicShort")+L" "+Times(helper.boostApplied[1]);
    int currentWidth=TextWidth(dc,current),headingWidth=TextWidth(dc,Text(L"bsAllocation"),true);
    Write(dc,{12,112,14+headingWidth,126},Text(L"bsAllocation"),Palette::Heading,true);
    Dotted(dc,20+headingWidth,HelperWidth-24-currentWidth,119,Palette::StripEdge);
    Write(dc,{HelperWidth-16-currentWidth,112,HelperWidth-16,126},current,Palette::Muted);
    BoostSlider(w,dc,0,134,L"bsPhysical");
    BoostSlider(w,dc,1,156,L"bsMagic");
    Write(dc,{14,178,84,192},Text(L"bsTotal"),Palette::Muted);
    Gauge(img,BoostTrackLeft,181,BoostTrackRight,double(BoostTotal())/MapleProtocol::BoostBudget);
    Write(dc,{BoostTrackRight+8,178,HelperWidth-16,192},std::to_wstring(BoostTotal())+L" / "+std::to_wstring(MapleProtocol::BoostBudget),
        Palette::Muted,false,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
    const wchar_t* presets[]={L"bsAllPhysical",L"bsAllMagic",L"bsSplit",L"bsClear"};
    for(int i=0;i<4;++i)ButtonAt(w,dc,BoostPreset+i,{14+i*88,202,14+i*88+82,219},presets[i],!helper.boostPrices.empty());
    RECT panel{8,228,HelperWidth-8,346};
    if(auto beige=Skin(L"UI/UIWindow.img/Quest/backgrnd5"))Nine(img,panel,beige,{8,8,250,258},{8,8,100,109});
    if(helper.boostPrices.empty())return;
    auto right=[&](int y,const std::wstring& text,COLORREF color,bool bold){Write(dc,{150,y,HelperWidth-18,y+14},text,color,bold,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);};
    auto tier=[&](const wchar_t* key,int total){return Text(key)+L"\xFF08"+Text(L"bsTier")+L" "+std::to_wstring(total)+L"\xFF09";};
    auto mesos=[&](int64_t value){return Amount(value)+L" "+Text(L"costColumn");};
    Write(dc,{18,238,200,252},tier(L"bsNewPrice",BoostTotal()),Palette::Muted);right(238,mesos(BoostPrice(BoostTotal())),Palette::Ink,false);
    Write(dc,{18,256,200,270},tier(L"bsPaid",helper.boostPaidTotal),Palette::Muted);right(256,mesos(helper.boostPaid),Palette::Ink,false);
    Dotted(dc,18,HelperWidth-18,274,RGB(200,190,160));
    int64_t charge=BoostCharge();
    Write(dc,{18,280,200,294},Text(L"bsNeed"),Palette::Ink,true);
    right(280,mesos(charge),helper.meso<charge?Palette::Red:charge?Palette::Amber:Palette::Green,true);
    TextBlock(dc,{18,298,HelperWidth-18,344},Text(L"bsNote"),Palette::Muted);
}
void PaintHelper(Window& w) {
    int width=HelperWidth,height=HelperHeight;
    if(!w.image || w.image->width!=width || w.image->height!=height)w.image=std::make_unique<Bitmap>(width,height);
    if(!w.image->pixels)return;
    auto& img=*w.image;HDC dc=img.dc;w.buttons.clear();w.tabs.clear();w.rowHits.clear();w.listHits.clear();w.scrollbars.clear();
    GdiFlush();std::fill(img.pixels,img.pixels+width*height,0);
    auto frame=Skin(L"UI/UIWindow.img/Quest/backgrnd3");
    Nine(img,{0,0,width,height},frame,HelperX,HelperY,true);
    std::vector<unsigned char> shape(size_t(width)*height,255);
    if(frame){GdiFlush();for(int i=0;i<width*height;++i)shape[i]=static_cast<unsigned char>(img.pixels[i]>>24);}
    else Fill(dc,{0,0,width,height},RGB(240,240,240));
    Write(dc,{10,5,HelperWidth-40,19},HelperTitle());
    CloseButton(w,dc,width-20);
    // Blue summary box: left part text, right part (after the divider) picture.
    if(helper.page==HubPage) {
        Write(dc,{22,39,262,53},helper.welcome.empty()?Text(L"mhTitle"):helper.welcome,Palette::OnBlue,true);
        Figure(dc,14,136,60,Text(L"mhCash"),Amount(helper.cash));Figure(dc,140,262,60,Text(L"mhMaplePoint"),Amount(helper.maplePoint));
        Figure(dc,14,136,78,Text(L"mhCredit"),Amount(helper.credit));Figure(dc,140,262,78,Text(L"mhMeso"),Amount(helper.meso));
    } else if(helper.page==TeleportPage) {
        Write(dc,{22,39,262,53},Text(L"mhTeleport"),Palette::OnBlue,true);
        Figure(dc,14,262,60,Text(L"mhMeso"),Amount(helper.meso));
        int unlocked=0;for(const auto& r:helper.teleports)if(r.flags&1)++unlocked;
        Figure(dc,14,262,78,Text(L"unlocked"),std::to_wstring(unlocked)+L"   "+Text(L"locked")+L" "+std::to_wstring(helper.locked));
    } else if(helper.page==PetPage) {
        Write(dc,{22,39,262,53},Text(L"mhPet"),Palette::OnBlue,true);
        std::wstring mode;for(const auto& r:helper.pets)if(r.id==helper.petMode)mode=r.label;
        Figure(dc,14,262,60,Text(L"mhCurrentMode"),mode);
    } else if(helper.page==TransmogPage) {
        Write(dc,{22,39,262,53},Text(L"tgHeadline"),Palette::OnBlue,true);
        Figure(dc,14,262,60,Text(L"tgOre"),helper.tgOreName);
        Figure(dc,14,262,78,Text(L"tgOreHeld"),std::to_wstring(helper.tgOreHeld)+L" / "+Text(L"tgOreNeed")+L" "+std::to_wstring(helper.tgOreNeed));
    } else if(helper.page==BooksPage) {
        Write(dc,{22,39,262,53},Text(L"bkHeadline"),Palette::OnBlue,true);
        Figure(dc,14,262,60,Text(L"bkBalance"),Amount(helper.bookPages));
        Figure(dc,14,262,78,Text(L"bkLine"),helper.bookLine);
    } else if(helper.page==BoostPage) {
        Write(dc,{22,39,262,53},Text(L"bsHeadline"),Palette::OnBlue,true);
        Figure(dc,14,262,60,Text(L"tmRoom"),helper.room.empty()?std::wstring():helper.room+L" \x00B7 "+Text(L"tmSolo"));
        Figure(dc,14,262,78,Text(L"mhMeso"),Amount(helper.meso));
    } else {
        Write(dc,{22,39,262,53},Text(L"tmHeadline"),Palette::OnBlue,true);
        Figure(dc,14,262,60,Text(L"tmRoom"),helper.room.empty()?std::wstring():helper.room+L" \x00B7 "+Text(L"tmSolo"));
        Figure(dc,14,136,78,Text(L"tmCount"),std::to_wstring(helper.talismanCount));
        Figure(dc,140,262,78,Text(L"tmActiveCount"),std::to_wstring(helper.talismanActive)+L" "+Text(L"tmUnit"));
    }
    {   PixelClip clip({HelperWidth-113,27,HelperWidth-7,102});Bitmap* mark=nullptr;
        if(helper.page==TeleportPage && helper.selected>=0 && helper.selected<int(helper.teleports.size()))mark=MapMark(helper.teleports[helper.selected].id);
        auto book=helper.page==BooksPage?BookSelected():nullptr;
        if(helper.page==BooksPage && helper.bookView>0){auto picked=BookPicked();if(picked)mark=MapMark(picked->id);book=mark?nullptr:book;}
        // The transmog preview: the chosen look, else the chosen equip, else the craftsman.
        if(helper.page==TransmogPage){auto look=TgSource()?TgSource():TgBase();if(look)book=look;}
        if(helper.page==TalismanPage)Enlarged(img,HelperWidth-60,64,ItemIcon(TalismanItem),2);
        else if(book)Enlarged(img,HelperWidth-60,64,ItemIcon(book->icon),2);
        else if(helper.page==BoostPage)NpcPortrait(img,{HelperWidth-111,29,HelperWidth-9,101},helper.boostNpc);
        else if(helper.page==TransmogPage)NpcPortrait(img,{HelperWidth-111,29,HelperWidth-9,101},helper.tgNpc);
        else if(mark)Sprite(img,HelperWidth-60-mark->width/2,64-mark->height/2,mark);else NpcPortrait(img,{HelperWidth-111,29,HelperWidth-9,101},HelperNpc);
    }
    int bottom=helper.status.empty()?HelperBodyBottom:HelperBodyBottom-18;
    if(helper.page==HubPage)PaintHub(w,dc);
    else if(helper.page==TeleportPage)PaintTeleports(w,dc,bottom);
    else if(helper.page==PetPage)PaintPet(w,dc);
    else if(helper.page==BoostPage)PaintBoost(w,dc);
    else if(helper.page==BooksPage){if(helper.bookView>0)PaintBookPlaces(w,dc,bottom);else PaintBooks(w,dc);}
    else if(helper.page==TransmogPage)PaintTransmog(w,dc);
    else PaintTalisman(w,dc);
    // One clipped line inside the white body (it ends at y=367, the button bar starts at 372).
    if(!helper.status.empty())Write(dc,{12,HelperBodyBottom-16,HelperWidth-14,HelperBodyBottom-2},helper.status,helper.statusColor);
    else if(helper.loading)Write(dc,{12,HelperBodyBottom-16,HelperWidth-14,HelperBodyBottom-2},HelperLoadingText(),Palette::Faint);
    else if(helper.page==BoostPage)Write(dc,{12,HelperBodyBottom-16,HelperWidth-14,HelperBodyBottom-2},Text(L"bsHint"),Palette::Faint);
    if(helper.page==BoostPage) {
        // Opened by the exit NPC: its own dialogue is one click away, there is no hub to go back to.
        ButtonAt(w,dc,BoostExit,{10,HelperBar,80,HelperBar+17},L"bsExit",!helper.loading);
        ButtonAt(w,dc,BoostConfirm,{HelperWidth-144,HelperBar,HelperWidth-78,HelperBar+17},L"bsConfirm",BoostConfirmable());
        ButtonAt(w,dc,HelperClose,{HelperWidth-72,HelperBar,HelperWidth-10,HelperBar+17},L"close");
        GdiFlush();for(int i=0;i<width*height;++i)img.pixels[i]=(img.pixels[i]&0xFFFFFF)|(DWORD(shape[i])<<24);
        return;
    }
    if(helper.page==TransmogPage) {
        // Opened by a craftsman, so there is no hub to go back to.
        if(helper.tgTab==0) {
            ButtonAt(w,dc,TgTryButton,{10,HelperBar,90,HelperBar+17},helper.tgTrying?L"tgStopTry":L"tgTry",TgTryable());
            ButtonAt(w,dc,TgApplyButton,{HelperWidth-144,HelperBar,HelperWidth-78,HelperBar+17},helper.tgArmed?L"tgConfirm":L"tgApply",TgApplicable());
        } else {
            auto b=TgBase();
            ButtonAt(w,dc,TgRestoreButton,{HelperWidth-144,HelperBar,HelperWidth-78,HelperBar+17},helper.tgArmed?L"tgConfirm":L"tgRestore",
                b && (b->flags&MapleProtocol::TransmogChanged) && !helper.loading);
        }
        ButtonAt(w,dc,HelperClose,{HelperWidth-72,HelperBar,HelperWidth-10,HelperBar+17},L"close");
        GdiFlush();for(int i=0;i<width*height;++i)img.pixels[i]=(img.pixels[i]&0xFFFFFF)|(DWORD(shape[i])<<24);
        return;
    }
    if(helper.page==TalismanPage) {
        // Opened by the item, so there is no hub to go back to.
        ButtonAt(w,dc,HelperBorrow,{HelperWidth-144,HelperBar,HelperWidth-78,HelperBar+17},L"borrow",TalismanBorrowable());
        ButtonAt(w,dc,HelperClose,{HelperWidth-72,HelperBar,HelperWidth-10,HelperBar+17},L"close");
        GdiFlush();for(int i=0;i<width*height;++i)img.pixels[i]=(img.pixels[i]&0xFFFFFF)|(DWORD(shape[i])<<24);
        return;
    }
    if(helper.page!=HubPage)ButtonAt(w,dc,HelperBack,{10,HelperBar,70,HelperBar+17},L"back");
    if(helper.page==TeleportPage) {
        bool chosen=helper.selected>=0 && helper.selected<int(helper.teleports.size());
        bool affordable=chosen && (helper.teleports[helper.selected].flags&1) && helper.meso>=helper.teleports[helper.selected].cost;
        if(helper.category==MapleProtocol::CustomMaps)ButtonAt(w,dc,HelperRemove,{76,HelperBar,136,HelperBar+17},L"mhRemove",chosen && !helper.loading);
        ButtonAt(w,dc,HelperWarp,{HelperWidth-144,HelperBar,HelperWidth-78,HelperBar+17},L"warp",affordable && !helper.loading);
    }
    if(helper.page==BooksPage && helper.bookView>0) {
        if(helper.bookView==1)ButtonAt(w,dc,BookRouteButton,{76,HelperBar,146,HelperBar+17},L"bkRoute",BookPicked() && !helper.loading);
        ButtonAt(w,dc,BookWarpButton,{HelperWidth-144,HelperBar,HelperWidth-78,HelperBar+17},L"warp",BookWarpReady());
    } else if(helper.page==BooksPage) {
        bool picked=BookSelected() && !helper.loading;
        if(helper.bookTab==MapleProtocol::BookOffer) {
            ButtonAt(w,dc,BookMapsButton,{76,HelperBar,156,HelperBar+17},L"bkMaps",picked);
            ButtonAt(w,dc,BookExchangeButton,{HelperWidth-144,HelperBar,HelperWidth-78,HelperBar+17},L"bkExchange",BookExchangeable());
        } else {
            ButtonAt(w,dc,BookMapsButton,{162,HelperBar,232,HelperBar+17},L"bkMaps",picked);
            int count;BookAllPages(count);
            ButtonAt(w,dc,BookDecomposeAllButton,{76,HelperBar,156,HelperBar+17},helper.bookAllArmed?L"bkConfirmAllButton":L"bkDecomposeAll",count>0 && !helper.loading);
            ButtonAt(w,dc,BookDecomposeButton,{HelperWidth-144,HelperBar,HelperWidth-78,HelperBar+17},L"bkDecompose",BookSelected() && !helper.loading);
        }
    }
    ButtonAt(w,dc,HelperRefresh,{HelperWidth-72,HelperBar,HelperWidth-10,HelperBar+17},L"refresh",!helper.loading);
    if(helper.ask)PaintAsk(w,dc);
    GdiFlush();for(int i=0;i<width*height;++i)img.pixels[i]=(img.pixels[i]&0xFFFFFF)|(DWORD(shape[i])<<24);
}

// ---- Input -------------------------------------------------------------------
void HelperScroll(Window& w,int id) {
    for(auto& bar:w.scrollbars)if(id==bar.id || id==bar.id+1){*bar.value=(std::max)(0,(std::min)(bar.maximum,*bar.value+(id==bar.id?-bar.step:bar.step)));HelperDirty();return;}
}
void HelperRow(int index);
void AddCustomFromInput() {
    if(helper.customInput.empty()){HelperStatus(Text(L"mhInputEmpty"),Palette::Red);HelperDirty();return;}
    helper.status.clear();HelperRequest(MapleProtocol::CustomAdd,_wtoi(helper.customInput.c_str()));HelperDirty();
}
// Digits, Backspace and Enter go to the map id box while it has focus; Escape releases it.
bool HelperKey(unsigned key) {
    if(helper.ask){if(key==VK_RETURN){if(helper.ask!=1 || AskWarpReady())AnswerAsk(true);}else if(key==VK_ESCAPE)AnswerAsk(false);return true;}
    if(helper.page!=TeleportPage || helper.category!=MapleProtocol::CustomMaps || !helper.inputFocus)return false;
    int digit=key>='0' && key<='9'?int(key-'0'):key>=VK_NUMPAD0 && key<=VK_NUMPAD9?int(key-VK_NUMPAD0):-1;
    if(digit>=0){if(helper.customInput.size()<9 && !(helper.customInput.empty() && digit==0))helper.customInput+=wchar_t(L'0'+digit);}
    else if(key==VK_BACK){if(!helper.customInput.empty())helper.customInput.pop_back();}
    else if(key==VK_RETURN)AddCustomFromInput();
    else if(key==VK_ESCAPE)helper.inputFocus=false;
    else return false;
    HelperDirty();return true;
}
// Slider presses that are not on a button: jump to the clicked value and start dragging.
bool HelperPress(POINT p) {
    if(helper.page!=BoostPage || helper.boostPrices.empty())return false;
    for(int i=0;i<2;++i)if(PtInRect(&helper.sliderTracks[i],p)){helper.dragSlider=i;SetBoost(i,SliderValue(p.x));HelperDirty();return true;}
    return false;
}
bool HelperDragging(){return helper.dragSlider>=0;}
void HelperDrag(int x){if(helper.dragSlider>=0)SetBoost(helper.dragSlider,SliderValue(x));}
void HelperRelease(){if(helper.dragSlider>=0){helper.dragSlider=-1;HelperDirty();}}
void HelperClick(Window& w,int id) {
    if(id!=HelperInput && id!=HelperAddId)helper.inputFocus=false;
    if(id==1 || id==HelperClose){HideHelper();return;}
    if(id==HelperInput){helper.inputFocus=true;HelperDirty();return;}
    if(id==HelperAddHere){helper.status.clear();HelperRequest(MapleProtocol::CustomAdd,0);HelperDirty();return;}
    if(id==HelperAddId){AddCustomFromInput();return;}
    if(helper.ask){if(id==HelperAskYes || id==HelperAskNo)AnswerAsk(id==HelperAskYes);return;}
    if(id==HelperRemove){AskSelected(2);return;}
    if(id>=BoostStep && id<BoostStep+4) {
        // A held button already stepped while held; its release must not add one more.
        if(helper.repeated){helper.repeated=false;return;}
        BoostStepBy(id-BoostStep);return;
    }
    if(id>=BoostPreset && id<BoostPreset+4) {
        const int presets[4][2]={{MapleProtocol::BoostBudget,0},{0,MapleProtocol::BoostBudget},{MapleProtocol::BoostBudget/2,MapleProtocol::BoostBudget/2},{0,0}};
        helper.boostDraft[0]=presets[id-BoostPreset][0];helper.boostDraft[1]=presets[id-BoostPreset][1];HelperDirty();return;
    }
    if(id==BoostConfirm) {
        if(!BoostConfirmable())return;
        helper.status.clear();HelperRequest(MapleProtocol::BoostSet,helper.boostDraft[0],helper.boostDraft[1],BoostCharge());HelperDirty();return;
    }
    if(id==BoostExit){if(!helper.loading){helper.status.clear();HelperRequest(MapleProtocol::BoostExit);HelperDirty();}return;}
    if(id>=100 && id<200){HelperScroll(w,id);return;}
    if(id>=TalismanTab && id<TalismanTab+3){helper.talismanTab=id-TalismanTab;HelperDirty();return;}
    if(id>=TgTab && id<TgTab+2) {
        if(helper.tgTab!=id-TgTab){TgStopTry();helper.tgTab=id-TgTab;helper.offset=0;helper.tgBase=helper.tgSource=0;helper.tgArmed=false;helper.tgSources.clear();helper.status.clear();HelperDirty();}
        return;
    }
    if(id>=TgBaseRow && id<TgBaseRow+500){int i=id-TgBaseRow;if(i<int(helper.tgBases.size()) && !helper.loading)TgPickBase(helper.tgBases[i].id);return;}
    if(id>=TgSourceRow && id<TgSourceRow+500) {
        int i=id-TgSourceRow;if(i>=int(helper.tgSources.size()))return;
        TgStopTry();helper.tgSource=helper.tgSources[i].id;helper.tgArmed=false;helper.status.clear();HelperDirty();return;
    }
    if(id==TgTryButton) {
        if(!TgTryable())return;
        if(helper.tgTrying){TgStopTry();HelperDirty();return;}
        helper.status.clear();helper.tgTryPending=true;HelperRequest(MapleProtocol::TransmogTry,helper.tgBase,helper.tgSource);HelperDirty();return;
    }
    if(id==TgApplyButton) {
        if(!TgApplicable())return;
        // The look is consumed, so it takes a second click.
        if(!helper.tgArmed){helper.tgArmed=true;HelperStatus(Format(Text(L"tgArmedApply"),TgSource()->label),Palette::Amber);HelperDirty();return;}
        helper.tgArmed=false;helper.tgTrying=false;helper.status.clear();HelperRequest(MapleProtocol::TransmogApply,helper.tgBase,helper.tgSource);HelperDirty();return;
    }
    if(id==TgRestoreButton) {
        auto b=TgBase();if(!b || !(b->flags&MapleProtocol::TransmogChanged) || helper.loading)return;
        if(!helper.tgArmed){helper.tgArmed=true;HelperStatus(Text(L"tgArmedRestore"),Palette::Amber);HelperDirty();return;}
        helper.tgArmed=false;helper.status.clear();HelperRequest(MapleProtocol::TransmogRestore,helper.tgBase);HelperDirty();return;
    }
    if(id>=BookTab && id<BookTab+2){if(helper.bookTab!=id-BookTab){helper.offset=0;BookChoose(id-BookTab,0);helper.status.clear();}return;}
    if(id==BookExchangeButton) {
        if(!BookExchangeable())return;
        auto r=BookSelected();helper.status.clear();HelperRequest(MapleProtocol::BookExchange,r->id,0,r->cost);HelperDirty();return;
    }
    if(id==BookDecomposeButton) {
        auto r=BookSelected();if(!r || helper.loading)return;
        bool locked=(r->flags&MapleProtocol::BookLocked)!=0;
        // A book this job can still use needs a second click.
        if(locked && helper.bookConfirm!=r->id){helper.bookConfirm=r->id;HelperStatus(Text(L"bkConfirmLocked"),Palette::Amber);HelperDirty();return;}
        helper.bookConfirm=0;helper.status.clear();
        HelperRequest(MapleProtocol::BookDecompose,r->id,1|(locked?MapleProtocol::BookConfirm:0),r->cost);HelperDirty();return;
    }
    if(id==BookDecomposeAllButton) {
        int count;auto pages=BookAllPages(count);if(!count || helper.loading)return;
        if(!helper.bookAllArmed){helper.bookAllArmed=true;HelperStatus(Format(Format(Text(L"bkConfirmAll"),std::to_wstring(count)),Amount(pages)),Palette::Amber);HelperDirty();return;}
        helper.bookAllArmed=false;helper.status.clear();HelperRequest(MapleProtocol::BookDecomposeAll,0,0,pages);HelperDirty();return;
    }
    if(id>=TalismanTile && id<TalismanTile+100) {
        int index=id-TalismanTile;if(index<int(helper.talisman.size()))helper.talismanSkill=helper.talisman[index].id;
        HelperDirty();return;
    }
    if(id==HelperBorrow) {
        if(!TalismanBorrowable())return;
        helper.status.clear();HelperRequest(MapleProtocol::TalismanBorrow,helper.talismanSkill);HelperDirty();return;
    }
    if(id==HelperBack && helper.page==BooksPage && helper.bookView>0) {
        // Route -> drop maps -> book list.
        helper.status.clear();helper.offset=0;helper.bookNotice.clear();
        if(helper.bookView==2){OpenBookMaps(helper.bookItem);return;}
        helper.bookView=0;HelperDirty();return;
    }
    if(id==BookMapsButton){auto r=BookSelected();if(r && !helper.loading)OpenBookMaps(r->id);return;}
    if(id==BookRouteButton){auto r=BookPicked();if(r && !helper.loading)OpenBookRoute(r->id);return;}
    if(id==BookWarpButton) {
        if(!BookWarpReady())return;
        auto r=BookPicked();int64_t target=helper.bookView==2?helper.bookRouteMap:0;helper.status.clear();
        HelperRequest(MapleProtocol::BookWarp,r->id,helper.bookItem,(target<<32)|r->cost);HelperDirty();return;
    }
    if(id==HelperBack){helper.page=HubPage;helper.status.clear();HelperDirty();return;}
    if(id==HelperRefresh){helper.status.clear();RefreshHelperPage();HelperDirty();return;}
    if(id==HelperExpandAll || id==HelperCollapseAll) {
        for(const auto& r:helper.teleports){auto region=TeleportRegion(r);if(region.empty())continue;if(id==HelperExpandAll)helper.foldedRegions.erase(region);else helper.foldedRegions.insert(region);}
        helper.offset=0;HelperDirty();return;
    }
    if(id==HelperWarp){AskSelected(1);return;}
    if(id>=HelperHubTab && id<HelperHubTab+2){helper.hubTab=id-HelperHubTab;HelperDirty();return;}
    if(id>=HelperTab && id<HelperTab+5) {
        int category=id-HelperTab;if(category==helper.category)return;
        helper.category=category;helper.selected=-1;helper.offset=0;helper.teleports.clear();helper.notice.clear();helper.status.clear();helper.customInput.clear();
        HelperRequest(MapleProtocol::Teleports,category);HelperDirty();return;
    }
    if(id>=HelperTile+500 && id<HelperTile+500+3){HelperRow(id-HelperTile-500);return;}
    if(id>=HelperTile && id<HelperTile+500) {
        int index=id-HelperTile;if(index>=int(helper.hub.size()) || helper.loading)return;
        const auto& r=helper.hub[index];helper.status.clear();
        if(r.flags&1) {
            // Teleport and pet pages are drawn here; everything else runs on the server.
            helper.selected=-1;helper.offset=0;
            if(r.id==MapleProtocol::SalonHubFeature){OpenSalon();return;}
            if(r.id==MapleProtocol::BooksHubFeature){OpenBooks();return;}
            if(r.id==71){helper.page=TeleportPage;HelperRequest(MapleProtocol::Teleports,helper.category);}
            else{helper.page=PetPage;HelperRequest(MapleProtocol::Pet);}
        } else HelperRequest(MapleProtocol::Feature,r.id);
        HelperDirty();
    }
}
// A double click on a destination is the Teleport button (it asks first).
void HelperRowTwice(int index){if(helper.page==TeleportPage && index>=0 && index==helper.selected)AskSelected(1);}
void HelperRow(int index) {
    helper.inputFocus=false;
    if(helper.page==TeleportPage && index<0)ToggleRegion(index);
    else if(helper.page==TeleportPage){helper.selected=index;HelperDirty();}
    else if(helper.page==BooksPage && helper.bookView==1){helper.bookMapPick=index;HelperDirty();}
    else if(helper.page==BooksPage && helper.bookView==2){helper.bookStepPick=index;HelperDirty();}
    else if(helper.page==BooksPage && index>=0 && index<int(helper.books.size()))BookChoose(helper.books[index].kind,helper.books[index].id);
    else if(helper.page==PetPage && index<int(helper.pets.size()) && helper.pets[index].id!=helper.petMode && !helper.loading)
        {helper.status.clear();HelperRequest(MapleProtocol::PetSet,helper.pets[index].id);HelperDirty();}
}
void HelperWheel(int delta) {
    if(helper.ask)return;
    if(helper.page==TransmogPage) {
        // The column under the cursor scrolls; painting clamps both.
        int& value=helperWindow.hover>=TgSourceRow && helperWindow.hover<TgSourceRow+500?helper.tgRight:helper.offset;
        value=(std::max)(0,value+(delta>0?1:-1));HelperDirty();return;
    }
    if(helper.page!=TeleportPage && helper.page!=BooksPage)return;
    // delta>0 scrolls down, like the quest list; painting clamps the bottom.
    helper.offset=(std::max)(0,helper.offset+(delta>0?1:-1));HelperDirty();
}
// Holding a step button repeats after 400 ms, then every 60 ms.
void HelperRepeat(DWORD now) {
    auto& w=helperWindow;
    if(helper.page==BoostPage && w.mouseDown && w.pressed>=BoostStep && w.pressed<BoostStep+4 && w.hover==w.pressed) {
        if(!helper.repeatStart){helper.repeatStart=now;helper.repeated=false;}
        else if(now-helper.repeatStart>=400 && now-helper.lastRepeat>=60){BoostStepBy(w.pressed-BoostStep);helper.lastRepeat=now;helper.repeated=true;}
    } else helper.repeatStart=0;
}
void HelperUpdate() {
    if(!InField()){HideHelper();return;}
    DWORD now=GetTickCount();HelperFlush(now);HelperExpire(now);HelperRepeat(now);
    if(helper.inputFocus && helper.page==TeleportPage)HelperDirty(); // blinking caret
}
bool HelperReceive(const unsigned char* data,size_t size) {
    MapleProtocol::Snapshot s;if(!MapleProtocol::Parse(data,size,s))return true;
    if(s.view==MapleProtocol::Open) {
        helper.serverReady=true;
        if(s.value==MapleProtocol::OpenBoost)helper.boostNpc=s.extra;
        if(s.value==MapleProtocol::OpenTransmog)helper.tgNpc=s.extra;
        OpenHelperPage(s.value==MapleProtocol::OpenTalisman?TalismanPage:s.value==MapleProtocol::OpenBoost?BoostPage
            :s.value==MapleProtocol::OpenTransmog?TransmogPage:HubPage);
        return true;
    }
    if(s.view==MapleProtocol::TransmogLooksView){TransmogOnLooks(s);return true;}
    // The server acknowledges each field's hello; until then the help menu keeps its old route.
    if(s.sequence==0){if(s.view==MapleProtocol::Done)helper.serverReady=true;return true;}
    auto found=helper.pending.find(s.sequence);if(found==helper.pending.end())return true;
    int action=found->second.first;helper.pending.erase(found);helper.loading=!helper.pending.empty();
    helper.meso=s.meso;helper.cash=s.cash;helper.maplePoint=s.maplePoint;helper.credit=s.credit;helper.flags=s.flags;
    switch(s.view) {
        case MapleProtocol::HubView:helper.hub=std::move(s.rows);helper.title=s.title;helper.welcome=s.message;break;
        case MapleProtocol::TeleportView:
            if(s.value!=helper.category)break;
            helper.teleports=std::move(s.rows);helper.locked=s.extra;helper.notice=s.message;
            if(s.value==MapleProtocol::CustomMaps) {
                // Custom lists include unvisited maps; extra is the server's limit instead of a count.
                helper.customLimit=s.extra;helper.locked=0;for(const auto& r:helper.teleports)if(!(r.flags&1))++helper.locked;
            }
            if(helper.selected>=int(helper.teleports.size()))helper.selected=-1;
            if(!s.title.empty())HelperStatus(s.title,Palette::Red);
            break;
        case MapleProtocol::PetView:
            helper.pets=std::move(s.rows);helper.petMode=s.value;helper.notes=s.message;
            if(!s.title.empty())HelperStatus(s.title,Palette::Green);
            break;
        case MapleProtocol::Done:
            // Scripts, shops and warps take over the screen; the helper steps aside.
            if(action==MapleProtocol::Feature || action==MapleProtocol::Warp || action==MapleProtocol::BookWarp || action==MapleProtocol::BoostExit){HideHelper();return true;}
            if(!s.message.empty())HelperStatus(s.message,Palette::Green);
            if(action==MapleProtocol::TransmogTry){helper.tgTrying=helper.tgTryPending;helper.tgTryPending=false;}
            if(action==MapleProtocol::CustomAdd || action==MapleProtocol::CustomRemove) {
                if(action==MapleProtocol::CustomAdd)helper.customInput.clear();else helper.selected=-1;
                if(helper.page==TeleportPage && helper.category==MapleProtocol::CustomMaps)HelperRequest(MapleProtocol::Teleports,MapleProtocol::CustomMaps);
            }
            break;
        case MapleProtocol::SalonCatalogView:SalonOnCatalog(s);break;
        case MapleProtocol::TransmogView: {
            if(helper.page!=TransmogPage)break;
            helper.tgBases.clear();
            for(auto& r:s.rows) {
                if(r.kind==MapleProtocol::TransmogOre){helper.tgOre=r.id;helper.tgOreName=r.label;helper.tgOreHeld=int(r.cost);helper.tgOreNeed=r.max;}
                else helper.tgBases.push_back(std::move(r));
            }
            // After a transmog or restore the server already shows the real look again.
            helper.tgArmed=false;helper.tgTrying=false;helper.tgSource=0;helper.tgSources.clear();
            if(!TgBase())helper.tgBase=0;
            else if(helper.tgTab==0)HelperRequest(MapleProtocol::TransmogSources,helper.tgBase);
            if(!s.message.empty())HelperStatus(s.message,Palette::Green);
            break;
        }
        case MapleProtocol::TransmogSourcesView:
            if(helper.page!=TransmogPage || s.value!=helper.tgBase)break;
            helper.tgSources=std::move(s.rows);helper.tgSourcesNote=s.message;
            if(!TgSource())helper.tgSource=0;
            break;
        case MapleProtocol::BookMapsView:
            if(helper.page!=BooksPage || helper.bookView!=1 || s.value!=helper.bookItem)break;
            helper.bookMaps=std::move(s.rows);helper.bookMapsTitle=s.title;helper.bookNotice=s.message;
            if(helper.bookMapPick>=int(helper.bookMaps.size()))helper.bookMapPick=-1;
            break;
        case MapleProtocol::BookRouteView:
            if(helper.page!=BooksPage || helper.bookView!=2 || s.extra!=helper.bookRouteMap)break;
            helper.bookRoute=std::move(s.rows);helper.bookRouteTitle=s.title;helper.bookNotice=s.message;
            if(helper.bookStepPick>=int(helper.bookRoute.size()))helper.bookStepPick=-1;
            break;
        case MapleProtocol::BooksView: {
            if(helper.page!=BooksPage)break;
            helper.books=std::move(s.rows);helper.bookPages=s.value;helper.bookRefund=s.extra;helper.bookLine=s.title;
            if(!BookSelected())helper.bookSelected=0;
            helper.bookConfirm=0;helper.bookAllArmed=false;
            // A result is green; the "no job line yet" note is not a result.
            if(!s.message.empty())HelperStatus(s.message,BookCount(MapleProtocol::BookOffer)?Palette::Green:Palette::Amber);
            break;
        }
        case MapleProtocol::SalonStateView:SalonOnState(s);break;
        case MapleProtocol::BoostView: {
            if(helper.page!=BoostPage)break;
            bool first=helper.boostPrices.empty();
            helper.boostPrices.assign(MapleProtocol::BoostBudget+1,0);
            for(const auto& r:s.rows) {
                if(r.kind==MapleProtocol::BoostPrice && r.id>=0 && r.id<=MapleProtocol::BoostBudget)helper.boostPrices[r.id]=r.cost;
                else if(r.kind==MapleProtocol::BoostPaid){helper.boostPaid=r.cost;helper.boostPaidTotal=r.id;}
            }
            helper.boostApplied[0]=s.value;helper.boostApplied[1]=s.extra;helper.room=s.title;
            // A fresh page or a confirmed purchase starts from what is applied; a refresh keeps the draft.
            if(first || !s.message.empty()){helper.boostDraft[0]=s.value;helper.boostDraft[1]=s.extra;}
            if(!s.message.empty())HelperStatus(s.message,Palette::Green);
            break;
        }
        case MapleProtocol::TalismanView: {
            if(helper.page!=TalismanPage)break;
            helper.talisman=std::move(s.rows);helper.room=s.title;helper.talismanCount=s.value;helper.talismanActive=s.extra;
            if(!TalismanSelected())helper.talismanSkill=0;
            if(!s.message.empty())HelperStatus(s.message,Palette::Green);
            break;
        }
        case MapleProtocol::Error:
            if(action>=MapleProtocol::SalonCatalog && action<=MapleProtocol::SalonTry){SalonOnError(s.message);break;}
            HelperStatus(s.message,Palette::Red);
            if(action==MapleProtocol::Warp)HelperRequest(MapleProtocol::Teleports,helper.category);
            // A refused borrow may mean the counts changed: show the server's current state.
            if(action==MapleProtocol::TalismanBorrow && helper.page==TalismanPage)HelperRequest(MapleProtocol::TalismanList);
            if(action==MapleProtocol::TalismanList){helper.talisman.clear();helper.talismanSkill=0;}
            // A refused purchase (e.g. the price changed) reloads prices and what was paid.
            if(action==MapleProtocol::BoostSet && helper.page==BoostPage)HelperRequest(MapleProtocol::Boost);
            if(action==MapleProtocol::Boost)helper.boostPrices.clear();
            // A refused exchange or decompose may mean prices or the inventory changed.
            if(action>=MapleProtocol::BookExchange && action<=MapleProtocol::BookDecomposeAll && helper.page==BooksPage)HelperRequest(MapleProtocol::Books);
            // A refused warp (price or unlock changed) reloads the maps or the route on screen.
            if(action==MapleProtocol::BookWarp && helper.page==BooksPage)RefreshHelperPage();
            // A refused transmog or restore reloads the equips (something moved or the ore ran out).
            if((action==MapleProtocol::TransmogApply || action==MapleProtocol::TransmogRestore) && helper.page==TransmogPage)HelperRequest(MapleProtocol::TransmogList);
            if(action==MapleProtocol::TransmogTry){helper.tgTryPending=false;}
            break;
    }
    HelperDirty();return true;
}
