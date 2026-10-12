// Native Maple Helper (NPC 9900001): hub, super teleport and pet loot pages, plus the
// Lone Adventurer's Talisman page that item 2430034 opens inside a solo boss room.
// Included by QuestHelperUI.cpp inside its anonymous namespace; it reuses that file's
// native window, skin and text helpers. The server (MapleHelperUiHandler) owns every
// rule, price and script; this view only renders snapshots and sends choices.

// Quest/backgrnd3 (296x289): title, blue summary box, white body and button bar.
constexpr Axis HelperX{150,150,175,175},HelperY{150,150,200,200};
constexpr int HelperBodyBottom=366,HelperBar=376,HelperNpc=9900001,TalismanItem=2430034;
enum HelperPage {HubPage=0,TeleportPage=1,PetPage=2,TalismanPage=3,BoostPage=4};
enum HelperButton {HelperBack=3001,HelperWarp=3002,HelperRefresh=3003,HelperBorrow=3004,HelperClose=3005,
    HelperRemove=3006,HelperAddHere=3007,HelperAddId=3008,HelperInput=3009,BoostConfirm=3010,BoostExit=3011,
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
    // Boost page: applied and drafted allocations (physical, magic), the price of each total,
    // what this run has paid, the exit NPC, slider drag and press-and-hold repeat.
    int boostApplied[2]={0,0},boostDraft[2]={0,0},boostPaidTotal=0,boostNpc=0,dragSlider=-1;int64_t boostPaid=0;
    std::vector<int64_t> boostPrices;RECT sliderTracks[2]{};
    DWORD repeatStart=0,lastRepeat=0;bool repeated=false;
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
const std::wstring& HelperLoadingText(){return Text(helper.page==TalismanPage || helper.page==BoostPage?L"tmLoading":L"loading");}
void HelperExpire(DWORD now) {
    bool expired=false;
    for(auto it=helper.pending.begin();it!=helper.pending.end();)if(it->second.second && now-it->second.second>10000){it=helper.pending.erase(it);expired=true;}else ++it;
    if(expired){helper.loading=!helper.pending.empty();HelperStatus(Text(helper.page==TalismanPage?L"tmTimeout":L"timeout"),Palette::Red);HelperDirty();}
}
void RefreshHelperPage() {
    // The talisman page is opened by the item, not the helper: it needs no hub snapshot.
    if(helper.page==TalismanPage){HelperRequest(MapleProtocol::TalismanList);return;}
    if(helper.page==BoostPage){HelperRequest(MapleProtocol::Boost);return;}
    HelperRequest(MapleProtocol::Hub);
    if(helper.page==TeleportPage)HelperRequest(MapleProtocol::Teleports,helper.category);
    else if(helper.page==PetPage)HelperRequest(MapleProtocol::Pet);
}
void OpenHelperPage(int page) {
    if(!helper.enabled || !InField())return;
    helper.page=page;helper.selected=-1;helper.offset=0;helper.status.clear();
    if(page==TalismanPage){helper.talisman.clear();helper.talismanTab=0;helper.talismanSkill=0;helper.room.clear();}
    if(page==BoostPage){helper.boostPrices.clear();helper.room.clear();helper.dragSlider=-1;}
    helper.inputFocus=false;
    Show(helperWindow);RefreshHelperPage();HelperDirty();
}
void HideHelper(){Hide(helperWindow);helper.queue.clear();helper.pending.clear();helper.loading=false;}

// ---- Drawing -----------------------------------------------------------------
std::wstring HelperTitle() {
    if(helper.page==TeleportPage)return Text(L"mhTeleport");
    if(helper.page==PetPage)return Text(L"mhPet");
    if(helper.page==TalismanPage)return Text(L"tmTitle");
    if(helper.page==BoostPage)return Text(L"bsTitle");
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
    constexpr int CardHeight=34;int visible=(std::max)(1,(bottom-top)/CardHeight);
    int maximum=(std::max)(0,int(helper.teleports.size())-visible);helper.offset=(std::min)(helper.offset,maximum);
    int y=top;
    for(int i=helper.offset;i<int(helper.teleports.size()) && y+CardHeight<=bottom;++i,y+=CardHeight) {
        const auto& r=helper.teleports[i];bool selected=i==helper.selected,unlocked=(r.flags&1)!=0,affordable=unlocked && helper.meso>=r.cost;
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
        w.rowHits.push_back({card,i});
    }
    if(maximum>0)ScrollbarAt(w,dc,{HelperWidth-18,top,HelperWidth-6,bottom},helper.offset,maximum,visible,true,1);
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
        if(helper.page==TalismanPage)Enlarged(img,HelperWidth-60,64,ItemIcon(TalismanItem),2);
        else if(helper.page==BoostPage)NpcPortrait(img,{HelperWidth-111,29,HelperWidth-9,101},helper.boostNpc);
        else if(mark)Sprite(img,HelperWidth-60-mark->width/2,64-mark->height/2,mark);else NpcPortrait(img,{HelperWidth-111,29,HelperWidth-9,101},HelperNpc);
    }
    int bottom=helper.status.empty()?HelperBodyBottom:HelperBodyBottom-18;
    if(helper.page==HubPage)PaintHub(w,dc);
    else if(helper.page==TeleportPage)PaintTeleports(w,dc,bottom);
    else if(helper.page==PetPage)PaintPet(w,dc);
    else if(helper.page==BoostPage)PaintBoost(w,dc);
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
    ButtonAt(w,dc,HelperRefresh,{HelperWidth-72,HelperBar,HelperWidth-10,HelperBar+17},L"refresh",!helper.loading);
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
    if(id==HelperRemove) {
        if(helper.selected<0 || helper.selected>=int(helper.teleports.size()) || helper.loading)return;
        helper.status.clear();HelperRequest(MapleProtocol::CustomRemove,helper.teleports[helper.selected].id);HelperDirty();return;
    }
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
    if(id>=TalismanTile && id<TalismanTile+100) {
        int index=id-TalismanTile;if(index<int(helper.talisman.size()))helper.talismanSkill=helper.talisman[index].id;
        HelperDirty();return;
    }
    if(id==HelperBorrow) {
        if(!TalismanBorrowable())return;
        helper.status.clear();HelperRequest(MapleProtocol::TalismanBorrow,helper.talismanSkill);HelperDirty();return;
    }
    if(id==HelperBack){helper.page=HubPage;helper.status.clear();HelperDirty();return;}
    if(id==HelperRefresh){helper.status.clear();RefreshHelperPage();HelperDirty();return;}
    if(id==HelperWarp) {
        if(helper.selected<0 || helper.selected>=int(helper.teleports.size()) || helper.loading)return;
        const auto& r=helper.teleports[helper.selected];helper.status.clear();
        HelperRequest(MapleProtocol::Warp,r.id,helper.category,r.cost);HelperDirty();return;
    }
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
            if(r.id==71){helper.page=TeleportPage;HelperRequest(MapleProtocol::Teleports,helper.category);}
            else{helper.page=PetPage;HelperRequest(MapleProtocol::Pet);}
        } else HelperRequest(MapleProtocol::Feature,r.id);
        HelperDirty();
    }
}
void HelperRow(int index) {
    helper.inputFocus=false;
    if(helper.page==TeleportPage){helper.selected=index;HelperDirty();}
    else if(helper.page==PetPage && index<int(helper.pets.size()) && helper.pets[index].id!=helper.petMode && !helper.loading)
        {helper.status.clear();HelperRequest(MapleProtocol::PetSet,helper.pets[index].id);HelperDirty();}
}
void HelperWheel(int delta) {
    if(helper.page!=TeleportPage)return;
    helper.offset=(std::max)(0,helper.offset+(delta>0?-1:1));HelperDirty();
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
        OpenHelperPage(s.value==MapleProtocol::OpenTalisman?TalismanPage:s.value==MapleProtocol::OpenBoost?BoostPage:HubPage);
        return true;
    }
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
            if(action==MapleProtocol::Feature || action==MapleProtocol::Warp || action==MapleProtocol::BoostExit){HideHelper();return true;}
            if(!s.message.empty())HelperStatus(s.message,Palette::Green);
            if(action==MapleProtocol::CustomAdd || action==MapleProtocol::CustomRemove) {
                if(action==MapleProtocol::CustomAdd)helper.customInput.clear();else helper.selected=-1;
                if(helper.page==TeleportPage && helper.category==MapleProtocol::CustomMaps)HelperRequest(MapleProtocol::Teleports,MapleProtocol::CustomMaps);
            }
            break;
        case MapleProtocol::SalonCatalogView:SalonOnCatalog(s);break;
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
            break;
    }
    HelperDirty();return true;
}
