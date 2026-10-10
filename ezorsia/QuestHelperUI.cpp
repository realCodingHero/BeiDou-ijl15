#include "stdafx.h"
#include "QuestHelperUI.h"
#include "QuestHelperProtocol.h"
#include <oleauto.h>
#include <algorithm>
#include <array>
#include <map>
#include <deque>
#include <memory>
#include <set>
#include <regex>

namespace {
using QuestProtocol::Row;
using QuestProtocol::Snapshot;
constexpr int Width=600,Height=450;
constexpr int ServerPageSize=50,ListViewportTop=73,ListViewportBottom=391;
constexpr DWORD DrawerDuration=200;
constexpr GUID CanvasGuid={0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};
template<class T> T Method(void* p,unsigned offset){return reinterpret_cast<T>((*static_cast<void***>(p))[offset/4]);}
using Release=ULONG(__stdcall*)(void*);
using Size=HRESULT(__stdcall*)(void*,unsigned*);
using GetCanvas=HRESULT(__stdcall*)(void*,VARIANT,void**);
using GetRaw=HRESULT(__stdcall*)(void*,int,int,void**);
using Lock=HRESULT(__stdcall*)(void*,int*,VARIANT*);
using Unlock=HRESULT(__stdcall*)(void*,RECT*);
using Pixel=HRESULT(__stdcall*)(void*,int,int,unsigned*);
using Query=HRESULT(__stdcall*)(void*,REFIID,void**);
using Resource=HRESULT(__stdcall*)(void*,BSTR,VARIANT,VARIANT,VARIANT*);
using NativeCtor=void*(__thiscall*)(void*);
using NativeCreate=void(__thiscall*)(void*,int,int,int,int,int,int,void*,int);
using NativeVoid=void(__thiscall*)(void*);
using NativeDraw=void(__thiscall*)(void*,RECT*);
using NativeInvalidate=void(__thiscall*)(void*,RECT*);
NativeCtor ctor=reinterpret_cast<NativeCtor>(0x009DE383);
NativeCreate create=reinterpret_cast<NativeCreate>(0x009DE4D2);
NativeVoid destroy=reinterpret_cast<NativeVoid>(0x009E00AF);
// CWnd::InvalidateRect queues a redraw; 0x009E00A6 only resets mouse coordinates.
NativeInvalidate invalidateRect=reinterpret_cast<NativeInvalidate>(0x009E04C9);
NativeDraw baseDraw=reinterpret_cast<NativeDraw>(0x009E0502);
using NativePosition=int(__thiscall*)(void*);
NativePosition leftPosition=reinterpret_cast<NativePosition>(0x009E03C5),topPosition=reinterpret_cast<NativePosition>(0x009E0447);

struct Bitmap {
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;DWORD* pixels=nullptr;int width,height;
    Bitmap(int w,int h):width(w),height(h) {
        dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);
        if(bitmap)old=SelectObject(dc,bitmap);
    }
    ~Bitmap(){if(old)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
};
std::map<std::wstring,std::unique_ptr<Bitmap>> skins;
std::map<std::wstring,std::wstring> labels;
std::wstring labelFile,language;
bool enabled=false,liveResources=false;
HFONT font=nullptr,boldFont=nullptr;
int sequence=0,character=0,selectedQuest=0,category=0,listPage=0,listOffset=0,detailOffset=0,detailPage=0;
int sourceKind=0,sourceTarget=0,sourcePage=0,routeMap=0,routePage=0,selectedRow=-1,view=1,desiredView=1,navigation=0;
int64_t meso=0;
int completionFeatures=0,lastFeatureSequence=0,textScroll=0,textScrollMax=0;
std::vector<Row> quests,detailRows,sourceRows,routeRows;
int progressCount=-1,readyCount=-1,questNpc=0,questLevel=0,descriptionScroll=0,descriptionScrollMax=0;
std::wstring questDescription;
std::set<int> collapsedGroups{100000};
struct CategoryMemory {int quest=0,page=0,offset=0;std::set<int> collapsed{100000};std::map<int,int> groupQuests;};
std::array<CategoryMemory,2> categoryMemory;
bool restoreSelection=false;
std::set<int> restorePages;
int questTotal=0,sourceTotal=0,detailTotal=0,routeTotal=0;
std::wstring title,message;
DWORD lastRefresh=0,lastListRefresh=0,lastSend=0,quoteTime=0;
Snapshot quote;
struct Request {int action,quest,kind,target,map,purpose;int64_t token=0;int navigation=-1;};
struct Pending {Request request;DWORD sent;};
std::deque<Request> queue;
std::map<int,Pending> pending;
struct Button {RECT rect;int id;std::wstring text;bool enabled;};
struct RowHit {RECT rect;int index;};
struct ListEntry {int index,group;std::wstring name;int count=0;};
struct ListHit {RECT rect;ListEntry entry;};
struct Scrollbar {RECT track,thumb;int* value=nullptr;int maximum=0;};
struct Window {
    alignas(8) unsigned char native[0x78]{};
    std::array<void*,13> mainMethods{};
    std::array<void*,19> inputMethods{};
    void* referenceDestructor=nullptr;
    bool initialized=false,shown=false,modal=false,collapsed=true,positioned=false;
    int x=0,y=0,dragScroll=-1,dragStart=0,dragValue=0;
    int hover=-1,pressed=-1;bool mouseDown=false;
    bool animating=false;int drawer=0,drawerFrom=0;DWORD drawerStart=0;
    std::vector<Button> buttons;
    std::vector<RowHit> rowHits;
    std::vector<ListHit> listHits;
    std::vector<Scrollbar> scrollbars;
    std::unique_ptr<Bitmap> image;
};
Window mainWindow,confirmWindow;
Request quotedRequest{};
bool confirming=false;
int WindowWidth(const Window& w){return w.modal?350:(w.collapsed && !w.animating)?250:Width;}
int VisibleWidth(const Window& w){return w.modal?350:250+w.drawer;}
int WindowHeight(const Window& w){return w.modal?245:Height;}
bool Blocked(const Window& w){return !w.modal && confirmWindow.shown;}
Window* Find(void* self,bool input=false) {
    for(auto* w:{&mainWindow,&confirmWindow})if(self==w->native+(input?4:0))return w;
    return nullptr;
}
const std::wstring& Text(const wchar_t* key) {
    auto it=labels.find(key);if(it!=labels.end())return it->second;
    wchar_t buffer[512]{};GetPrivateProfileStringW(language.c_str(),key,key,buffer,512,labelFile.c_str());
    return labels.emplace(key,buffer).first->second;
}
std::map<std::wstring,std::wstring> resourceText;
bool ResourceValue(const std::wstring& path,VARIANT& result) {
    if(!liveResources)return false;
    void* rm=*reinterpret_cast<void**>(0x00BF14E8);if(!rm)return false;
    VARIANT empty{};BSTR key=SysAllocString(path.c_str());
    HRESULT hr=Method<Resource>(rm,0x1C)(rm,key,empty,empty,&result);SysFreeString(key);
    return SUCCEEDED(hr);
}
std::wstring ResourceText(const std::wstring& path) {
    auto cached=resourceText.find(path);if(cached!=resourceText.end())return cached->second;
    VARIANT value{};std::wstring text;
    if(ResourceValue(path,value)) {
        if(value.vt==VT_BSTR && value.bstrVal)text.assign(value.bstrVal,SysStringLen(value.bstrVal));
        else if(value.vt==VT_I4)text=std::to_wstring(value.lVal);
        else if(value.vt==VT_I2)text=std::to_wstring(value.iVal);
    }
    VariantClear(&value);resourceText.emplace(path,text);return text;
}
int QuestGroup(const Row& row) {
    if(row.flags&4)return 100000;
    const auto area=ResourceText(L"Quest/QuestInfo.img/"+std::to_wstring(row.id)+L"/area");
    return area.empty()?0:_wtoi(area.c_str());
}
std::wstring GroupName(int group) {
    if(group==100000)return Text(L"medal");
    auto name=ResourceText(L"Etc/QuestCategory.img/"+std::to_wstring(group));
    return name.empty() || name==L"empty"?Text(L"other"):name;
}
std::vector<ListEntry> ListEntries() {
    std::map<int,std::vector<int>> groups;
    for(int i=0;i<int(quests.size());++i)groups[QuestGroup(quests[i])].push_back(i);
    std::vector<ListEntry> result;
    for(const auto& group:groups) {
        result.push_back({-1,group.first,GroupName(group.first),int(group.second.size())});
        if(!collapsedGroups.count(group.first))for(int i:group.second)result.push_back({i,group.first,L"",0});
    }
    return result;
}
int EntryHeight(const ListEntry& entry){return entry.index<0?20:category==1?22:32;}
int ListHeight(){int height=0;for(const auto& entry:ListEntries())height+=EntryHeight(entry);return height;}
int ListMaximum(){return (std::max)(0,ListHeight()-(ListViewportBottom-ListViewportTop));}
bool AllGroupsCollapsed(){if(quests.empty())return false;for(const auto& q:quests)if(!collapsedGroups.count(QuestGroup(q)))return false;return true;}
void RememberCategory(){auto& memory=categoryMemory[category];for(const auto& q:quests)if(q.id==selectedQuest){memory.quest=selectedQuest;memory.page=listPage;}if(!memory.quest)memory.page=listPage;memory.offset=listOffset;memory.collapsed=collapsedGroups;}
std::wstring NamedToken(wchar_t kind,const std::wstring& id) {
    if(kind==L'c') {for(const auto& row:detailRows)if(row.kind==2 && row.id==_wtoi(id.c_str()))return std::to_wstring(row.current);return L"0";}
    if(kind==L'q')return ResourceText(L"Quest/QuestInfo.img/"+id+L"/name");
    if(kind==L'p' || kind==L'o')return ResourceText(std::wstring(L"String/")+(kind==L'p'?L"Npc.img/":L"Mob.img/")+id+L"/name");
    if(kind==L't') {
        for(auto file:{L"Etc.img/",L"Consume.img/",L"Ins.img/",L"Cash.img/",L"Pet.img/"}) {
            auto name=ResourceText(std::wstring(L"String/")+file+id+L"/name");if(!name.empty())return name;
        }
        for(auto category:{L"Accessory",L"Cap",L"Cape",L"Coat",L"Face",L"Glove",L"Hair",L"Longcoat",L"Pants",L"PetEquip",L"Ring",L"Shield",L"Shoes",L"Weapon"}) {
            auto name=ResourceText(std::wstring(L"String/Eqp.img/Eqp/")+category+L"/"+id+L"/name");if(!name.empty())return name;
        }
    }
    if(kind==L'm')for(auto group:{L"maple",L"victoria",L"ossyria",L"elin",L"weddingGL",L"MasteriaGL",L"HalloweenGL",L"jp",L"etc",L"singapore",L"event",L"Episode1GL",L"thai",L"china",L"grandis",L"3rd",L"CT",L"bd232UnnamedSource"}) {
        auto name=ResourceText(std::wstring(L"String/Map.img/")+group+L"/"+id+L"/mapName");if(!name.empty())return name;
    }
    return id;
}
std::wstring Description(int quest) {
    auto raw=ResourceText(L"Quest/QuestInfo.img/"+std::to_wstring(quest)+L"/1");
    if(raw.empty())raw=ResourceText(L"Quest/QuestInfo.img/"+std::to_wstring(quest)+L"/summary");
    raw=std::regex_replace(raw,std::wregex(L"\\\\n"),L"\n");
    raw=std::regex_replace(raw,std::wregex(L"#i[0-9]+:?#[ ]?"),L"");
    const std::wregex token(L"#([potmcq])([0-9]+):?#");std::wstring result;
    std::wsmatch match;
    while(std::regex_search(raw,match,token)){result+=match.prefix().str()+NamedToken(match[1].str()[0],match[2]);raw=match.suffix();}
    result+=raw;
    result=std::regex_replace(result,std::wregex(L"#h[ ]?#"),Text(L"player"));
    result=std::regex_replace(result,std::wregex(L"#[a-zA-Z]"),L"");
    return result.empty()?Text(L"noDescription"):result;
}
void Fill(HDC dc,RECT r,COLORREF c){HBRUSH b=CreateSolidBrush(c);FillRect(dc,&r,b);DeleteObject(b);}
void Write(HDC dc,RECT r,const std::wstring& s,COLORREF c=RGB(36,49,62),bool bold=false,UINT flags=DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS) {
    auto old=SelectObject(dc,bold?boldFont:font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,c);
    DrawTextW(dc,s.c_str(),static_cast<int>(s.size()),&r,flags);SelectObject(dc,old);
}
int WrappedHeight(HDC dc,int width,const std::wstring& text,bool bold=false) {
    RECT bounds{0,0,width,0};auto old=SelectObject(dc,bold?boldFont:font);
    DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&bounds,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
    SelectObject(dc,old);return bounds.bottom;
}
void ScrollbarAt(Window& w,HDC dc,RECT track,int& value,int maximum,int visible) {
    maximum=(std::max)(0,maximum);value=(std::max)(0,(std::min)(value,maximum));
    if(!maximum)return;
    Fill(dc,track,RGB(223,232,240));
    int length=track.bottom-track.top,thumb=(std::max)(18,length*visible/(visible+maximum));
    int y=track.top+(length-thumb)*value/maximum;
    RECT rect{track.left+2,y,track.right-2,y+thumb};Fill(dc,rect,RGB(100,147,180));
    w.scrollbars.push_back({track,rect,&value,maximum});
}
void TextPane(Window& w,HDC dc,RECT bounds,const std::wstring& text,int& scroll,int& maximum) {
    int height=WrappedHeight(dc,bounds.right-bounds.left-14,text);
    int visibleHeight=int(bounds.bottom-bounds.top);
    maximum=(std::max)(0,height-visibleHeight);
    scroll=(std::min)(scroll,maximum);
    int saved=SaveDC(dc);IntersectClipRect(dc,bounds.left,bounds.top,bounds.right-12,bounds.bottom);
    RECT content{bounds.left,bounds.top-scroll,bounds.right-14,bounds.top-scroll+height};
    Write(dc,content,text,RGB(66,85,102),false,DT_WORDBREAK|DT_NOPREFIX);RestoreDC(dc,saved);
    ScrollbarAt(w,dc,{bounds.right-11,bounds.top,bounds.right,bounds.bottom},scroll,maximum,visibleHeight);
}
Bitmap* Skin(const std::wstring& path) {
    auto it=skins.find(path);if(it!=skins.end())return it->second.get();
    VARIANT result{};HRESULT hr=ResourceValue(path,result)?S_OK:E_FAIL;
    void* canvas=nullptr;
    if(SUCCEEDED(hr) && (result.vt==VT_UNKNOWN || result.vt==VT_DISPATCH) && result.punkVal)
        hr=Method<Query>(result.punkVal,0)(result.punkVal,CanvasGuid,&canvas);
    VariantClear(&result);if(FAILED(hr) || !canvas)return nullptr;
    unsigned w=0,h=0;Method<Size>(canvas,0x40)(canvas,&w);Method<Size>(canvas,0x48)(canvas,&h);
    if(w==0 || h==0 || w>1000 || h>1000){Method<Release>(canvas,8)(canvas);return nullptr;}
    auto image=std::make_unique<Bitmap>(w,h);
    if(image->pixels)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        unsigned color=0;Method<Pixel>(canvas,0x88)(canvas,x,y,&color);image->pixels[y*w+x]=color;
    }
    Method<Release>(canvas,8)(canvas);Bitmap* ptr=image.get();skins.emplace(path,std::move(image));return ptr;
}
void Blit(HDC dc,RECT r,const wchar_t* resource) {
    auto s=Skin(resource);if(s && s->bitmap)StretchBlt(dc,r.left,r.top,r.right-r.left,r.bottom-r.top,s->dc,0,0,s->width,s->height,SRCCOPY);
}
void NpcPortrait(Bitmap& target,RECT bounds,int npc) {
    if(npc<=0)return;
    std::wstring id=std::to_wstring(npc);while(id.size()<7)id=L"0"+id;
    std::set<std::wstring> visited;
    while(visited.insert(id).second && visited.size()<8) {
        auto linked=ResourceText(L"Npc/"+id+L".img/info/link");if(linked.empty())break;
        id=linked;while(id.size()<7)id=L"0"+id;
    }
    auto sprite=Skin(L"Npc/"+id+L".img/stand/0");
    if(!sprite)sprite=Skin(L"Npc/"+id+L".img/default/0");if(!sprite)sprite=Skin(L"Npc/"+id+L".img/default");if(!sprite)return;
    double scale=(std::min)(1.0,(std::min)(double(bounds.right-bounds.left)/sprite->width,double(bounds.bottom-bounds.top)/sprite->height));
    int width=int(sprite->width*scale),height=int(sprite->height*scale);
    int left=bounds.left+(bounds.right-bounds.left-width)/2,top=bounds.bottom-height;
    GdiFlush();
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        DWORD src=sprite->pixels[int(y/scale)*sprite->width+int(x/scale)],alpha=src>>24;
        DWORD& dst=target.pixels[(top+y)*target.width+left+x];DWORD color=0xFF000000;
        for(int shift:{0,8,16})color|=((((src>>shift)&255)*alpha+((dst>>shift)&255)*(255-alpha)+127)/255)<<shift;
        dst=color;
    }
}
bool Upload(void* canvas,const Bitmap& image) {
    unsigned tw=0,th=0;Method<Size>(canvas,0x38)(canvas,&tw);Method<Size>(canvas,0x3C)(canvas,&th);
    if(!tw || !th || tw>4096 || th>4096)return false;
    for(int y=0;y<image.height;y+=th)for(int x=0;x<image.width;x+=tw) {
        void* raw=nullptr;
        // IWzCanvas::get_rawCanvas selects the tile containing a PIXEL coordinate.
        // Passing tile indices repeatedly selects (0,0) on the GPU allocator.
        if(FAILED(Method<GetRaw>(canvas,0x34)(canvas,x,y,&raw)) || !raw)return false;
        int format=0;Method<Size>(raw,0x0C)(raw,reinterpret_cast<unsigned*>(&format));
        unsigned rw=0,rh=0;Method<Size>(raw,0x14)(raw,&rw);Method<Size>(raw,0x18)(raw,&rh);
        unsigned copyWidth=(std::min)(tw,unsigned(image.width-x)),copyHeight=(std::min)(th,unsigned(image.height-y));
        int pitch=0;VARIANT address{};
        HRESULT hr=Method<Lock>(raw,0x1C)(raw,&pitch,&address);
        void* destination=address.vt==VT_I4?reinterpret_cast<void*>(address.lVal):(address.vt&VT_BYREF)?address.byref:nullptr;
        bool valid=SUCCEEDED(hr) && destination && (format==1 || format==2) && rw>=copyWidth && rh>=copyHeight && pitch>=int(copyWidth*(format==2?4:2));
        if(valid) {
            for(unsigned row=0;row<copyHeight;++row) {
                const DWORD* src=image.pixels+(y+row)*image.width+x;
                auto dst=static_cast<unsigned char*>(destination)+row*pitch;
                unsigned count=copyWidth;
                if(format==2)memcpy(dst,src,count*4);
                else for(unsigned col=0;col<count;++col)reinterpret_cast<WORD*>(dst)[col]=WORD(((src[col]>>16)&0xF000)|((src[col]>>12)&0xF00)|((src[col]>>8)&0xF0)|((src[col]>>4)&0xF));
            }
        }
        // A null rectangle unlocks without notifying an already cached GPU texture.
        RECT changed{0,0,LONG(copyWidth),LONG(copyHeight)};
        if(SUCCEEDED(hr) && FAILED(Method<Unlock>(raw,0x20)(raw,valid?&changed:nullptr)))valid=false;
        Method<Release>(raw,8)(raw);
        if(!valid)return false;
    }
    return true;
}
void Invalidate(Window& w){invalidateRect(w.native,nullptr);}
void Dirty(){if(mainWindow.shown)Invalidate(mainWindow);if(confirmWindow.shown)Invalidate(confirmWindow);}
void Enqueue(Request r) {
    if(r.navigation<0)r.navigation=navigation;
    for(const auto& entry:pending){const auto& q=entry.second.request;
        if(q.action==r.action && q.quest==r.quest && q.kind==r.kind && q.target==r.target && q.map==r.map && q.purpose==r.purpose && q.token==r.token && q.navigation==r.navigation)return;
    }
    for(const auto& q:queue)if(q.action==r.action && q.quest==r.quest && q.kind==r.kind && q.target==r.target && q.map==r.map && q.purpose==r.purpose && q.token==r.token && q.navigation==r.navigation)return;
    if(queue.size()<16)queue.push_back(r);
}
void List(){Enqueue({0,0,0,category,listPage,0});}
void Detail(){if(selectedQuest)Enqueue({1,selectedQuest,0,0,detailPage,1});}
void Route(){if(selectedQuest && routeMap)Enqueue({3,selectedQuest,sourceKind,sourceTarget,routeMap,3,routePage});}
void SetExpanded(bool expanded);
void SelectQuest(int quest,const std::wstring& name){++navigation;restoreSelection=false;selectedQuest=quest;title=name;detailRows.clear();view=desiredView=1;selectedRow=-1;questNpc=0;questLevel=0;for(const auto& q:quests)if(q.id==quest){questLevel=q.current;collapsedGroups.erase(QuestGroup(q));categoryMemory[category].groupQuests[QuestGroup(q)]=quest;}RememberCategory();detailOffset=detailPage=textScroll=descriptionScroll=0;questDescription=Description(quest);message=Text(L"loading");SetExpanded(true);Detail();Dirty();}
void Hide(Window& w){if(w.shown){w.shown=false;destroy(w.native);}w.buttons.clear();w.pressed=w.hover=-1;w.mouseDown=false;w.dragScroll=-1;}
void Show(Window& w);
void AdvanceDrawer(DWORD now);
void RestoreCategory(){const auto& memory=categoryMemory[category];listPage=memory.page;listOffset=memory.offset;collapsedGroups=memory.collapsed;selectedQuest=0;selectedRow=-1;view=desiredView=1;quests.clear();detailRows.clear();questTotal=0;title.clear();message.clear();questDescription.clear();restoreSelection=true;restorePages.clear();}
void DismissQuote(){quote={};confirming=false;Hide(confirmWindow);Dirty();}
void RefreshView(){if(view==2)Enqueue({2,selectedQuest,sourceKind,sourceTarget,sourcePage,2});else if(view==3)Route();else Detail();}
void* __fastcall DeleteReference(void* reference,void*,unsigned) {
    auto w=Find(static_cast<unsigned char*>(reference)-8);
    if(!w)return nullptr;
    Hide(*w);
    // Native ZRef cleanup may invoke a deleting destructor. Our storage has static
    // lifetime, so release the base members without passing it to ZAllocEx.
    reinterpret_cast<NativeVoid>(0x009DE438)(reference);
    w->initialized=false;return w->native;
}
std::vector<Row>& Rows(){return view==2?sourceRows:view==3?routeRows:detailRows;}
void CaptionButton(Window& w,HDC dc,int id,RECT r,const std::wstring& caption,bool active=true) {
    active=active && !Blocked(w);
    bool pressed=active && w.pressed==id && w.hover==id;
    const wchar_t* state=!active?L"disabled":pressed?L"pressed":w.hover==id?L"mouseOver":L"normal";
    auto path=std::wstring(L"UI/UIWindow.img/Quest/BtDetail/")+state+L"/0";
    Blit(dc,r,path.c_str());
    // Extend a caption-free column of the original skin to preserve its native gradient.
    if(auto skin=Skin(path))for(int y=r.top+2;y<r.bottom-2;++y){int sy=(y-r.top)*skin->height/(r.bottom-r.top);auto color=GetPixel(skin->dc,3,sy);if(pressed)color=RGB((std::max)(0,int(GetRValue(color))-12),(std::max)(0,int(GetGValue(color))-12),(std::max)(0,int(GetBValue(color))-12));Fill(dc,{r.left+3,y,r.right-3,y+1},color);}
    bool selected=(id==2 && category==0) || (id==3 && category==1);
    if(selected && active)Fill(dc,{r.left+3,r.top+2,r.right-3,r.bottom-2},pressed?RGB(35,94,133):RGB(57,124,168));
    RECT text=r;if(pressed)OffsetRect(&text,1,1);
    Write(dc,text,caption,active?RGB(255,255,255):RGB(145,145,145),selected,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    w.buttons.push_back({r,id,caption,active});
}
void ButtonAt(Window& w,HDC dc,int id,RECT r,const wchar_t* label,bool active=true){CaptionButton(w,dc,id,r,Text(label),active);}
void CloseButton(Window& w,HDC dc,int x){RECT r{x,5,x+12,17};bool active=!Blocked(w);const wchar_t* state=!active?L"disabled":w.pressed==1 && w.hover==1?L"pressed":w.hover==1?L"mouseOver":L"normal";Blit(dc,r,(std::wstring(L"UI/Basic.img/BtClose/")+state+L"/0").c_str());w.buttons.push_back({r,1,Text(L"close"),active});}
bool CompletionAllowed(int kind) {
    return kind==1?(completionFeatures&1)!=0:kind==2?(completionFeatures&2)!=0:kind==7?completionFeatures!=0:false;
}
std::wstring Amount(int64_t value) {
    auto text=std::to_wstring(value);
    for(int i=int(text.size())-3;i>0;i-=3)text.insert(i,1,L',');
    return text;
}
void WriteAmount(HDC dc,RECT bounds,const std::wstring& text) {
    LOGFONTW description{};GetObjectW(font,sizeof(description),&description);
    HFONT fitted=nullptr;HGDIOBJ old=SelectObject(dc,font);SIZE size{};
    GetTextExtentPoint32W(dc,text.c_str(),int(text.size()),&size);
    while(size.cx>bounds.right-bounds.left && description.lfHeight<-5){
        ++description.lfHeight;auto smaller=CreateFontIndirectW(&description);if(!smaller)break;
        SelectObject(dc,smaller);if(fitted)DeleteObject(fitted);fitted=smaller;
        GetTextExtentPoint32W(dc,text.c_str(),int(text.size()),&size);
    }
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(86,100,114));
    DrawTextW(dc,text.c_str(),int(text.size()),&bounds,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX);
    SelectObject(dc,old);if(fitted)DeleteObject(fitted);
}
void MapTable(Window& w,HDC dc,const std::vector<Row>& rows) {
    bool route=view==3;
    int first=route?301:441,second=route?477:518;
    Fill(dc,{265,63,587,87},RGB(217,231,245));
    Write(dc,{270,63,first-4,87},Text(route?L"stepColumn":L"mapColumn"),RGB(42,73,105),true);
    Write(dc,{first+5,63,second-4,87},Text(route?L"mapColumn":L"costColumn"),RGB(42,73,105),true);
    Write(dc,{second+5,63,580,87},Text(route?L"viaColumn":L"accessColumn"),RGB(42,73,105),true);
    int y=87,end=detailOffset;
    for(int i=detailOffset;i<int(rows.size());++i) {
        const auto& r=rows[i];int mapLeft=route?first:265,mapRight=route?second:first;
        int nameHeight=WrappedHeight(dc,mapRight-mapLeft-12,r.label,i==selectedRow);
        std::wstring last=route?r.info:Text((r.flags&8)?L"unlocked":L"locked");
        std::wstring middle=route?r.label:Amount(r.cost);
        int height=(std::max)(nameHeight+24+(route && (r.flags&8)?16:0),(std::max)(WrappedHeight(dc,second-first-10,middle),WrappedHeight(dc,60+(route?42:0),last))+12);
        height=(std::max)(48,height);
        if(y+height>321 && y>87)break;
        int bottom=(std::min)(321,y+height);
        Fill(dc,{265,y,587,bottom},i==selectedRow?RGB(225,239,252):(i%2?RGB(246,249,253):RGB(255,255,255)));
        int saved=SaveDC(dc);IntersectClipRect(dc,265,y,581,bottom);
        Write(dc,{mapLeft+6,y+5,mapRight-6,y+5+nameHeight},r.label,route && (r.flags&8)?RGB(36,125,86):RGB(33,66,99),i==selectedRow,DT_WORDBREAK|DT_NOPREFIX);
        Write(dc,{mapLeft+6,y+5+nameHeight,mapRight-6,y+21+nameHeight},std::to_wstring(r.id),RGB(126,138,150));
        if(route && (r.flags&8))Write(dc,{mapLeft+6,y+21+nameHeight,mapRight-6,y+37+nameHeight},Text(L"costColumn")+L": "+Amount(r.cost),RGB(36,125,86));
        if(route)Write(dc,{269,y+7,first-4,bottom-5},std::to_wstring(routePage*ServerPageSize+i+1),RGB(59,98,140),true,DT_CENTER|DT_TOP|DT_WORDBREAK);
        else WriteAmount(dc,{first+5,y+7,second-5,bottom-5},middle);
        Write(dc,{second+5,y+7,579,bottom-5},last,route?RGB(92,108,122):(r.flags&8)?RGB(36,125,86):RGB(157,110,54),false,DT_WORDBREAK|DT_NOPREFIX);
        RestoreDC(dc,saved);
        Fill(dc,{265,bottom-1,587,bottom},RGB(224,232,240));
        for(int x:{first,second})Fill(dc,{x,y,x+1,bottom},RGB(231,236,242));
        w.rowHits.push_back({{265,y,581,bottom},i});y=bottom;end=i+1;
    }
    if(detailOffset>0 || end<int(rows.size()))ScrollbarAt(w,dc,{580,87,592,321},detailOffset,(std::max)(0,int(rows.size())-1),(std::max)(1,end-detailOffset));
}
void Paint(Window& w) {
    int width=WindowWidth(w),height=WindowHeight(w);
    if(!w.image || w.image->width!=width || w.image->height!=height)w.image=std::make_unique<Bitmap>(width,height);
    if(!w.image->pixels)return;
    HDC dc=w.image->dc;w.buttons.clear();w.rowHits.clear();w.listHits.clear();w.scrollbars.clear();
    Fill(dc,{0,0,width,height},RGB(253,253,253));
    if(w.modal) {
        Fill(dc,{0,0,width,25},RGB(217,231,245));
        Write(dc,{12,2,330,23},Text(L"confirm"),RGB(25,48,72),true);
        Write(dc,{14,37,335,76},confirming?Text(L"processing"):Text(L"quote"),RGB(85,94,109),false,DT_WORDBREAK);
        Write(dc,{14,82,335,130},quote.message,RGB(35,57,85),true,DT_WORDBREAK);
        Write(dc,{14,139,335,161},Text(L"cost")+L": "+Amount(quote.price),RGB(190,66,44),true);
        Write(dc,{14,165,335,187},Text(L"meso")+L": "+Amount(meso));
        ButtonAt(w,dc,20,{42,211,164,230},L"confirm",!confirming && quote.token && GetTickCount()-quoteTime<30000 && meso>=quote.price);
        ButtonAt(w,dc,21,{192,211,308,230},L"cancel",!confirming);
    } else {
        Blit(dc,{0,0,250,Height},L"UI/UIWindow.img/Quest/backgrnd");
        Fill(dc,{7,2,247,21},RGB(253,253,253));
        Write(dc,{9,1,178,21},Text(L"title"),RGB(25,48,72),true);
        CaptionButton(w,dc,15,{210,4,225,21},w.collapsed?L">":L"<",selectedQuest!=0);
        CloseButton(w,dc,230);
        CaptionButton(w,dc,2,{9,28,124,47},Text(L"progress")+(progressCount>=0?L" "+std::to_wstring(progressCount):L""));
        CaptionButton(w,dc,3,{127,28,240,47},Text(L"ready")+(readyCount>=0?L" "+std::to_wstring(readyCount):L""));
        RECT banner{9,51,242,70};bool bannerActive=!Blocked(w) && !quests.empty();
        Fill(dc,banner,w.pressed==16 && w.hover==16?RGB(117,155,177):w.hover==16?RGB(181,209,225):RGB(203,223,235));
        std::wstring bannerText=(AllGroupsCollapsed()?L"+  ":L"-  ")+Text(AllGroupsCollapsed()?L"expandAll":L"collapseAll");
        RECT bannerCaption=banner;if(w.pressed==16 && w.hover==16)OffsetRect(&bannerCaption,1,1);
        Write(dc,bannerCaption,bannerText,bannerActive?RGB(41,74,98):RGB(130,139,147),false,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        w.buttons.push_back({banner,16,bannerText,bannerActive});
        auto entries=ListEntries();const int visible=ListViewportBottom-ListViewportTop;
        int maximum=ListMaximum();listOffset=(std::min)(listOffset,maximum);
        if(quests.empty())Write(dc,{13,82,225,115},Text(L"empty"));
        int savedList=SaveDC(dc);IntersectClipRect(dc,9,ListViewportTop,225,ListViewportBottom);
        int y=ListViewportTop-listOffset;
        for(int i=0;i<int(entries.size());++i) {
            const auto& entry=entries[i];int rowHeight=EntryHeight(entry);
            if(y+rowHeight<=ListViewportTop){y+=rowHeight;continue;}if(y>=ListViewportBottom)break;
            RECT bounds{9,y,225,y+rowHeight-2};
            if(entry.index<0) {
                Fill(dc,bounds,RGB(148,181,198));
                Write(dc,{13,y,31,y+18},collapsedGroups.count(entry.group)?L"+":L"-",RGB(255,255,255),true,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                Write(dc,{34,y,221,y+18},entry.name+L" ("+std::to_wstring(entry.count)+L")",RGB(255,255,255),true);
            } else {
                const auto& q=quests[entry.index];
                Fill(dc,bounds,q.id==selectedQuest?RGB(215,232,249):RGB(248,249,251));
                Write(dc,{20,y,221,y+(category==1?20:18)},q.label,RGB(35,68,103),q.id==selectedQuest);
                if(category!=1)Write(dc,{20,y+17,221,y+30},L"Lv."+std::to_wstring(q.current),RGB(115,121,127));
            }
            bounds.top=(std::max)(LONG(ListViewportTop),bounds.top);bounds.bottom=(std::min)(LONG(ListViewportBottom),bounds.bottom);
            w.listHits.push_back({bounds,entry});y+=rowHeight;
        }
        RestoreDC(dc,savedList);
        ScrollbarAt(w,dc,{229,ListViewportTop,242,ListViewportBottom},listOffset,maximum,visible);
        int pages=(std::max)(1,(questTotal+ServerPageSize-1)/ServerPageSize);
        Write(dc,{13,395,237,411},std::to_wstring(listPage+1)+L" / "+std::to_wstring(pages)+L"  "+Text(L"pageCount")+L" "+std::to_wstring(quests.size()),RGB(113,117,123));
        ButtonAt(w,dc,4,{14,420,83,439},L"prev",listPage>0);
        ButtonAt(w,dc,5,{90,420,159,439},L"next",(listPage+1)*ServerPageSize<questTotal);
        ButtonAt(w,dc,6,{166,420,235,439},L"refresh");
        if(!w.collapsed || w.animating) {
            Blit(dc,{250,0,Width,Height},L"UI/UIWindow.img/Quest/backgrnd2");
            Fill(dc,{252,21,598,Height-3},RGB(253,253,253));
            Write(dc,{264,1,587,22},title,RGB(25,48,72),true);
            ButtonAt(w,dc,7,{265,30,322,49},L"back");
            if(completionFeatures)ButtonAt(w,dc,9,{507,30,588,49},L"all",view==1 && selectedQuest!=0);
            auto& rows=Rows();
            if(view==2 || view==3)MapTable(w,dc,rows);
            else {
                Fill(dc,{265,63,587,125},RGB(223,237,250));
                Write(dc,{271,68,491,99},title,RGB(34,72,106),true,DT_WORDBREAK|DT_NOPREFIX);
                Write(dc,{271,103,491,121},L"Lv."+std::to_wstring(questLevel),RGB(74,109,138));
                NpcPortrait(*w.image,{493,66,580,123},questNpc);
                TextPane(w,dc,{268,130,587,201},questDescription,descriptionScroll,descriptionScrollMax);
                Write(dc,{270,204,581,222},Text(L"objectives"),RGB(42,73,105),true);
                detailOffset=(std::min)(detailOffset,(std::max)(0,int(rows.size())-2));
                for(int i=detailOffset;i<int(rows.size()) && i<detailOffset+2;++i) {
                    auto& r=rows[i];int y=225+(i-detailOffset)*47;
                    Fill(dc,{265,y,575,y+44},i==selectedRow?RGB(224,237,249):RGB(245,247,250));
                    std::wstring progress=r.required>0?L"  "+std::to_wstring(r.current)+L"/"+std::to_wstring(r.required):L"";
                    Write(dc,{270,y,570,y+20},r.label+progress,r.flags&1?RGB(30,127,77):RGB(36,62,90),i==selectedRow);
                    Write(dc,{270,y+20,570,y+42},r.info,RGB(100,111,120),false,DT_WORDBREAK|DT_NOPREFIX);
                    w.rowHits.push_back({{265,y,575,y+44},i});
                }
                ScrollbarAt(w,dc,{579,225,592,319},detailOffset,(std::max)(0,int(rows.size())-2),2);
            }
            if(rows.empty())Write(dc,{268,view==1?230:93,579,300},view==2?Text(L"noSource"):message,RGB(100,110,120),false,DT_WORDBREAK);
            std::wstring description;
            if(view!=3 && selectedRow>=0 && selectedRow<int(rows.size())) {
                const auto& r=rows[selectedRow];
                description=r.label+(view==2?L" ("+std::to_wstring(r.id)+L")":L"")+L"\n"+r.info;
                if(view==2)description+=L"\n"+Text(L"cost")+L": "+Amount(r.cost);
            }
            if(!message.empty())description+=(description.empty()?L"":L"\n\n")+message;
            Fill(dc,{265,326,587,327},RGB(224,232,240));
            TextPane(w,dc,{268,333,587,383},description,textScroll,textScrollMax);
            int total=view==2?sourceTotal:view==3?routeTotal:detailTotal;
            int page=view==2?sourcePage:view==3?routePage:detailPage;
            if(total>50 || page>0) {
                ButtonAt(w,dc,13,{265,387,370,408},L"prev",page>0);
                ButtonAt(w,dc,14,{476,387,587,408},L"next",(page+1)*50<total);
            }
            bool chosen=selectedRow>=0 && selectedRow<int(rows.size());bool mapView=view==2 || view==3;
            ButtonAt(w,dc,10,{265,420,346,439},view==2?L"route":L"source",chosen && view!=3);
            if(mapView || (view==1 && chosen && CompletionAllowed(rows[selectedRow].kind)))
                ButtonAt(w,dc,11,{382,420,463,439},mapView?L"warp":L"purchase",chosen && (mapView?(rows[selectedRow].flags&8)!=0:(rows[selectedRow].flags&2)!=0));
            ButtonAt(w,dc,12,{531,420,588,439},L"refresh");
        }
    }
    GdiFlush();for(int i=0;i<width*height;++i)w.image->pixels[i]|=0xFF000000;
    if(!w.modal && w.animating) {
        int shift=350-w.drawer;
        for(int y=0;y<height;++y){auto row=w.image->pixels+y*width;memmove(row+250,row+250+shift,w.drawer*sizeof(DWORD));std::fill(row+250+w.drawer,row+width,0);}
        // Move interactive bounds with the visible drawer and clip the part behind the list.
        auto move=[&](RECT& r){if(r.left<250)return true;OffsetRect(&r,-shift,0);r.left=(std::max)(250L,r.left);r.right=(std::min)(LONG(250+w.drawer),r.right);return r.right>r.left;};
        w.buttons.erase(std::remove_if(w.buttons.begin(),w.buttons.end(),[&](Button& b){return !move(b.rect);}),w.buttons.end());
        w.rowHits.erase(std::remove_if(w.rowHits.begin(),w.rowHits.end(),[&](RowHit& hit){return !move(hit.rect);}),w.rowHits.end());
        // Scrollbar dragging and detail interactions resume at the end of the short animation.
        w.scrollbars.erase(std::remove_if(w.scrollbars.begin(),w.scrollbars.end(),[](const Scrollbar& bar){return bar.track.left>=250;}),w.scrollbars.end());
    }
}
void __fastcall Draw(void* self,void*,RECT* rect) {
    auto w=Find(self);if(!w)return;
    baseDraw(self,rect);Paint(*w);
    void* layer=*reinterpret_cast<void**>(w->native+0x18);if(!layer || !w->image)return;
    VARIANT index{};index.vt=VT_I4;void* canvas=nullptr;
    if(SUCCEEDED(Method<GetCanvas>(layer,0x100)(layer,index,&canvas)) && canvas) {
        Upload(canvas,*w->image);Method<Release>(canvas,8)(canvas);
    }
}
int __fastcall HitTest(void* self,void*,int x,int y,void** control) {
    if(control)*control=nullptr;
    auto w=Find(self);if(!w)return 0;
    if(x<0 || y<0 || x>=VisibleWidth(*w) || y>=WindowHeight(*w))return 0;
    if(Blocked(*w))return 2;
    POINT p{x,y};for(const auto& b:w->buttons)if(PtInRect(&b.rect,p))return 2;
    return y<25?1:2;
}
void __fastcall Key(void* input,void*,unsigned key,unsigned flags) {
    auto w=Find(input,true);if(!w || flags&0x80000000 || Blocked(*w))return;
    if(key==VK_ESCAPE){if(w->modal){if(!confirming)DismissQuote();}else {RememberCategory();++navigation;Hide(*w);}return;}
    if(w->modal){if(key==VK_RETURN && !confirming && quote.token && meso>=quote.price && GetTickCount()-quoteTime<30000){Enqueue({5,quotedRequest.quest,quotedRequest.kind,quotedRequest.target,quotedRequest.map,5,quote.token});confirming=true;Dirty();}return;}
    if(key==VK_UP || key==VK_DOWN){listOffset=(std::max)(0,(std::min)(ListMaximum(),listOffset+(key==VK_UP?-20:20)));Dirty();}
}
int __fastcall InputEnabled(void* input,void*) {
    auto w=Find(input,true);return w && !Blocked(*w)?1:0;
}
int __fastcall MouseMove(void* input,void*,int x,int y) {
    auto w=Find(input,true);if(!w || Blocked(*w))return 1;
    if(w->dragScroll>=0 && w->dragScroll<int(w->scrollbars.size())) {
        auto& bar=w->scrollbars[w->dragScroll];int distance=bar.track.bottom-bar.track.top-(bar.thumb.bottom-bar.thumb.top);
        *bar.value=(std::max)(0,(std::min)(bar.maximum,w->dragValue+(y-w->dragStart)*bar.maximum/(std::max)(1,distance)));Dirty();return 1;
    }
    int hover=-1;POINT p{x,y};for(const auto& b:w->buttons)if(b.enabled && PtInRect(&b.rect,p))hover=b.id;
    if(hover!=w->hover){w->hover=hover;Invalidate(*w);}return 1;
}
void __fastcall MouseEnter(void* input,void*,int entered){auto w=Find(input,true);if(!w || entered)return;w->hover=w->pressed=-1;w->mouseDown=false;w->dragScroll=-1;Invalidate(*w);}
int __fastcall Wheel(void* input,void*,int x,int y,int delta) {
    auto w=Find(input,true);if(!w || w->modal || Blocked(*w))return 1;
    if(x<250){listOffset=(std::max)(0,(std::min)(ListMaximum(),listOffset+(delta>0?60:-60)));}
    else if(w->animating)return 1;
    else if(view==1 && y>=130 && y<=201)descriptionScroll=(std::max)(0,(std::min)(descriptionScrollMax,descriptionScroll+(delta>0?24:-24)));
    else if(y>=326 && y<385)textScroll=(std::max)(0,(std::min)(textScrollMax,textScroll+(delta>0?24:-24)));
    else detailOffset=(std::max)(0,(std::min)(int(Rows().size())-(view==1?2:1),detailOffset+(delta>0?2:-2)));
    Dirty();return 1;
}
void Click(Window& w,int id) {
    if(Blocked(w))return;
    if(w.modal){if(id==21 && !confirming)DismissQuote();return;}
    auto& rows=Rows();bool chosen=selectedRow>=0 && selectedRow<int(rows.size());
    if(id==1){RememberCategory();++navigation;Hide(w);return;}
    if(id==15){SetExpanded(w.collapsed);return;}
    if(id==16){bool expand=AllGroupsCollapsed();for(const auto& q:quests){int group=QuestGroup(q);if(expand)collapsedGroups.erase(group);else collapsedGroups.insert(group);}listOffset=0;Dirty();return;}
    if(id==2 || id==3){int next=id==3?1:0;if(next==category)return;RememberCategory();++navigation;category=next;RestoreCategory();SetExpanded(false);List();Dirty();return;}
    if(id==4 || id==5){int page=listPage+(id==4?-1:1);if(page<0 || page*ServerPageSize>=questTotal)return;restoreSelection=false;listPage=page;listOffset=0;quests.clear();List();Dirty();return;}
    if(id==6){List();RefreshView();return;}
    if(id==7){
        ++navigation;
        if(view==3){view=2;selectedRow=-1;for(int i=0;i<int(sourceRows.size());++i)if(sourceRows[i].id==routeMap)selectedRow=i;detailOffset=textScroll=0;message.clear();Enqueue({2,selectedQuest,sourceKind,sourceTarget,sourcePage,2});}
        else if(view==2){view=1;selectedRow=-1;for(int i=0;i<int(detailRows.size());++i)if(detailRows[i].kind==sourceKind && detailRows[i].id==sourceTarget)selectedRow=i;detailOffset=(std::max)(0,selectedRow-1);textScroll=0;message.clear();Detail();}
        else SetExpanded(false);
        desiredView=view;Dirty();return;
    }
    if(id==9 && view==1 && CompletionAllowed(7)){message=Text(L"loading");textScroll=0;Enqueue({4,selectedQuest,7,0,0,4});Dirty();return;}
    if(id==12){RefreshView();return;}
    if(id==13 || id==14) {
        auto& page=view==2?sourcePage:view==3?routePage:detailPage;int total=view==2?sourceTotal:view==3?routeTotal:detailTotal;
        int next=page+(id==13?-1:1);if(next<0 || next*ServerPageSize>=total)return;page=next;detailOffset=textScroll=0;selectedRow=-1;RefreshView();return;
    }
    if(!chosen)return;const auto& r=rows[selectedRow];
    if(id==10 && view==1){++navigation;desiredView=2;sourceKind=r.kind;sourceTarget=r.id;sourcePage=0;Enqueue({2,selectedQuest,r.kind,r.id,0,2});}
    else if(id==10 && view==2){++navigation;desiredView=3;routeMap=r.id;routePage=0;Route();}
    else if(id==11 && (view==2 || view==3) && (r.flags&8))Enqueue({4,selectedQuest,sourceKind,sourceTarget,r.id,4,view==3?routeMap:0});
    else if(id==11 && view==1 && CompletionAllowed(r.kind) && (r.flags&2))Enqueue({4,selectedQuest,r.kind,r.id,0,4});
}
void __fastcall MouseButton(void* input,void*,unsigned msg,unsigned,int x,int y) {
    auto w=Find(input,true);if(!w || Blocked(*w))return;
    if(!w->modal && w->animating && x>=250)return;
    POINT p{x,y};
    if(msg==WM_LBUTTONDOWN) {
        w->mouseDown=true;w->pressed=w->hover=-1;
        for(const auto& b:w->buttons)if(b.enabled && PtInRect(&b.rect,p)){w->pressed=w->hover=b.id;Invalidate(*w);return;}
        for(int i=0;i<int(w->scrollbars.size());++i){auto& bar=w->scrollbars[i];if(PtInRect(&bar.track,p)){
            if(!PtInRect(&bar.thumb,p)){int range=bar.track.bottom-bar.track.top-(bar.thumb.bottom-bar.thumb.top);*bar.value=(std::max)(0,(std::min)(bar.maximum,int((y-bar.track.top-(bar.thumb.bottom-bar.thumb.top)/2)*bar.maximum/(std::max)(1,range))));}
            w->dragScroll=i;w->dragStart=y;w->dragValue=*bar.value;Dirty();return;
        }}return;
    }
    if(msg!=WM_LBUTTONUP)return;
    bool wasDown=w->mouseDown;int pressed=w->pressed;w->pressed=-1;w->mouseDown=false;Invalidate(*w);
    if(w->dragScroll>=0){w->dragScroll=-1;return;}
    if(!wasDown)return;
    for(const auto& b:w->buttons)if(b.enabled && PtInRect(&b.rect,p)){
        if(pressed!=b.id)return;
        if(b.id==20 && w->modal && quote.token && !confirming){Enqueue({5,quotedRequest.quest,quotedRequest.kind,quotedRequest.target,quotedRequest.map,5,quote.token});confirming=true;Dirty();}
        else Click(*w,b.id);return;
    }
    if(w->modal || pressed>=0)return;
    for(const auto& hit:w->listHits)if(PtInRect(&hit.rect,p)) {
        auto entry=hit.entry;
        if(entry.index<0){if(collapsedGroups.count(entry.group)){collapsedGroups.erase(entry.group);int remembered=categoryMemory[category].groupQuests[entry.group];for(const auto& q:quests)if(q.id==remembered){SelectQuest(q.id,q.label);break;}}else collapsedGroups.insert(entry.group);Dirty();}
        else SelectQuest(quests[entry.index].id,quests[entry.index].label);return;
    }
    for(const auto& hit:w->rowHits)if(PtInRect(&hit.rect,p)){selectedRow=hit.index;textScroll=0;Dirty();break;}
}
bool InField() {
    void* stage=*reinterpret_cast<void**>(0x00BEDED4);if(!stage)return false;
    void* input=static_cast<unsigned char*>(stage)+4;
    using Kind=int(__thiscall*)(void*,const void*);
    return Method<Kind>(input,0x48)(input,reinterpret_cast<void*>(0x00BED758))!=0;
}
void ExpirePending(DWORD now) {
    bool timedOut=false;
    for(auto it=pending.begin();it!=pending.end();)if(now-it->second.sent>10000){
        const auto& r=it->second.request;
        if(r.quest==selectedQuest && message==Text(L"loading"))timedOut=true;
        if(r.action==5){DismissQuote();timedOut=true;}
        it=pending.erase(it);
    }else++it;
    if(timedOut){message=Text(L"timeout");Dirty();}
}
void __fastcall Update(void* self,void*) {
    auto w=Find(self);if(!w)return;
    reinterpret_cast<NativeVoid>(0x009E067E)(self);
    if(!InField()) {Hide(mainWindow);DismissQuote();queue.clear();pending.clear();selectedQuest=character=completionFeatures=lastFeatureSequence=0;categoryMemory={};restoreSelection=false;progressCount=readyCount=-1;return;}
    // Both native windows can update in the same frame. Only one request is dispatched every 150 ms.
    DWORD now=GetTickCount();
    if(w==&mainWindow)AdvanceDrawer(now);
    if(now-lastRefresh>=2000){lastRefresh=now;
        if(mainWindow.shown && !mainWindow.collapsed && view==1 && !quote.token)Detail();
    }
    if(mainWindow.shown && now-lastListRefresh>=(view!=1 || quote.token?2000u:10000u)){lastListRefresh=now;List();}
    if(quote.token && !confirming && now-quoteTime>=30000){DismissQuote();message=Text(L"quoteExpired");Dirty();}
    ExpirePending(now);
    if(!queue.empty() && now-lastSend>=150) {
        void* socket=*reinterpret_cast<void**>(0x00BE7914);if(!socket)return;
        Request r=queue.front();queue.pop_front();int seq=++sequence;
        auto bytes=QuestProtocol::Request(r.action,seq,r.quest,r.kind,r.target,r.map,r.token);
        struct Packet{int loopback;unsigned char* data;unsigned long size;unsigned offset;int shanda;};
        Packet packet{0,bytes.data(),static_cast<unsigned long>(bytes.size()),0,0};
        pending.emplace(seq,Pending{r,now});lastSend=now;
        using Send=void(__thiscall*)(void*,Packet*);reinterpret_cast<Send>(0x0049637B)(socket,&packet);
    }
}
void Show(Window& w) {
    if(w.shown){Invalidate(w);return;}
    if(!w.initialized) {
        ctor(w.native);memcpy(w.mainMethods.data(),reinterpret_cast<void*>(0x00B3F078),w.mainMethods.size()*4);
        memcpy(w.inputMethods.data(),reinterpret_cast<void*>(0x00B3F02C),w.inputMethods.size()*4);
        w.mainMethods[0]=reinterpret_cast<void*>(Update);w.mainMethods[9]=reinterpret_cast<void*>(HitTest);w.mainMethods[11]=reinterpret_cast<void*>(Draw);
        w.inputMethods[0]=reinterpret_cast<void*>(Key);w.inputMethods[2]=reinterpret_cast<void*>(MouseButton);
        w.inputMethods[3]=reinterpret_cast<void*>(MouseMove);w.inputMethods[4]=reinterpret_cast<void*>(Wheel);
        w.inputMethods[5]=reinterpret_cast<void*>(MouseEnter);
        // CWndMan checks IUIMsgHandler::IsEnabled before changing input focus.
        w.inputMethods[8]=reinterpret_cast<void*>(InputEnabled);
        *reinterpret_cast<void***>(w.native)=w.mainMethods.data();*reinterpret_cast<void***>(w.native+4)=w.inputMethods.data();w.initialized=true;
        w.referenceDestructor=reinterpret_cast<void*>(DeleteReference);
        *reinterpret_cast<void***>(w.native+8)=&w.referenceDestructor;
    }
    if(!w.positioned){w.x=(std::max)(0,(Client::m_nGameWidth-Width)/2);w.y=(std::max)(0,(Client::m_nGameHeight-Height)/2);w.positioned=true;}
    if(w.modal){w.x=(std::max)(0,(Client::m_nGameWidth-350)/2);w.y=(std::max)(0,(Client::m_nGameHeight-245)/2);}
    w.shown=true;create(w.native,w.x,w.y,WindowWidth(w),WindowHeight(w),w.modal?20:10,1,nullptr,1);Invalidate(w);
}
void SetExpanded(bool expanded) {
    if(mainWindow.animating)AdvanceDrawer(GetTickCount());
    if(mainWindow.collapsed==!expanded)return;
    int oldWidth=WindowWidth(mainWindow);mainWindow.collapsed=!expanded;
    if(!mainWindow.shown){mainWindow.animating=false;mainWindow.drawer=expanded?350:0;return;}
    mainWindow.drawerFrom=mainWindow.drawer;mainWindow.drawerStart=GetTickCount();mainWindow.animating=true;
    if(oldWidth!=WindowWidth(mainWindow)){mainWindow.x=leftPosition(mainWindow.native+4);mainWindow.y=topPosition(mainWindow.native+4);mainWindow.positioned=true;Hide(mainWindow);Show(mainWindow);}Dirty();
}
void AdvanceDrawer(DWORD now) {
    auto& w=mainWindow;if(!w.animating)return;
    DWORD elapsed=now-w.drawerStart;double t=(std::min)(1.0,double(elapsed)/DrawerDuration);t=t*t*(3-2*t);
    int target=w.collapsed?0:350;w.drawer=w.drawerFrom+int((target-w.drawerFrom)*t);
    if(elapsed>=DrawerDuration){w.animating=false;w.drawer=target;
        if(w.shown && w.collapsed){w.x=leftPosition(w.native+4);w.y=topPosition(w.native+4);w.positioned=true;Hide(w);Show(w);}
    }
    if(w.shown)Invalidate(w);
}
void OpenMain(){if(!mainWindow.shown){++navigation;RestoreCategory();mainWindow.animating=false;mainWindow.collapsed=true;mainWindow.drawer=0;}Show(mainWindow);message=Text(L"loading");lastListRefresh=GetTickCount();List();if(!restoreSelection)Detail();}
}
bool QuestHelperUI::Install(bool value,const char* config) {
    if(!value)return true;
    // The ABI is specific to the locally verified v83 executable.
    if(GetModuleHandleW(nullptr)!=reinterpret_cast<HMODULE>(0x400000) ||
        memcmp(reinterpret_cast<void*>(0x9DE383),"\xB8\x4A\x68\xAE\x00",5) ||
        memcmp(reinterpret_cast<void*>(0x9E00AF),"\xB8\x48\x6C\xAE\x00",5) ||
        memcmp(reinterpret_cast<void*>(0x9E0502),"\xB8\x94\x6C\xAE\x00",5) ||
        memcmp(reinterpret_cast<void*>(0x9E04C9),"\x33\xC0\x39\x44\x24\x04",6) ||
        memcmp(reinterpret_cast<void*>(0x9E03C5),"\xB8\x5C\x6C\xAE\x00",5) ||
        memcmp(reinterpret_cast<void*>(0x9E0447),"\xB8\x70\x6C\xAE\x00",5) ||
        *reinterpret_cast<unsigned*>(0x00B3F078+11*4)!=0x009E0502 ||
        memcmp(reinterpret_cast<void*>(0x9DE4D2),"\xB8\xD8\x68\xAE\x00",5))return false;
    wchar_t path[MAX_PATH];GetModuleFileNameW(nullptr,path,MAX_PATH);labelFile=path;
    labelFile=labelFile.substr(0,labelFile.find_last_of(L"\\/"))+L"\\QuestHelperUI.ini";
    if(GetFileAttributesW(labelFile.c_str())==INVALID_FILE_ATTRIBUTES)return false;
    language=Client::SwitchChinese?L"zh-CN":L"en-US";
    font=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"SimSun");
    boldFont=CreateFontW(-12,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"SimSun");
    confirmWindow.modal=true;liveResources=true;enabled=true;return true;
}
bool QuestHelperUI::Enabled(){return enabled;}
void QuestHelperUI::Open(){if(!enabled || !InField() || confirmWindow.shown)return;OpenMain();}
bool QuestHelperUI::Receive(const unsigned char* data,size_t size) {
    if(!enabled)return true;
    Snapshot s;if(!QuestProtocol::Parse(data,size,s))return true;
    auto found=pending.find(s.sequence);if(found==pending.end())return true;
    Request r=found->second.request;pending.erase(found);
    if(r.navigation>=0 && r.navigation!=navigation && r.purpose!=0)return true;
    if(character && character!=s.character) {queue.clear();pending.clear();quests.clear();detailRows.clear();selectedQuest=0;categoryMemory={};restoreSelection=false;progressCount=readyCount=-1;DismissQuote();SetExpanded(false);}
    if(character!=s.character)lastFeatureSequence=0;
    character=s.character;meso=s.meso;
    if(s.sequence>=lastFeatureSequence){
        lastFeatureSequence=s.sequence;
        if(completionFeatures!=s.features){
            completionFeatures=s.features;
            if(quote.token && quotedRequest.map==0){DismissQuote();message=Text(L"featuresChanged");textScroll=0;}
            queue.erase(std::remove_if(queue.begin(),queue.end(),[](const Request& q){
                return q.map==0 && (q.action==4 || q.action==5) && (!CompletionAllowed(q.kind) || q.action==5);
            }),queue.end());
        }
    }
    if(s.view==6){if(r.quest && r.quest!=selectedQuest)return true;message=s.title;if(r.action==4 || r.action==5)DismissQuote();if(r.purpose==1){selectedQuest=0;detailRows.clear();SetExpanded(false);List();}Dirty();return true;}
    if(r.purpose==0 && s.view==0 && r.target==category && r.map==listPage){
        questTotal=s.total;
        progressCount=s.progress;readyCount=s.ready;
        if(listPage>0 && listPage*ServerPageSize>=questTotal){listPage=(std::max)(0,(questTotal-1)/ServerPageSize);listOffset=0;List();}
        else{quests=std::move(s.rows);listOffset=(std::min)(listOffset,ListMaximum());
            if(restoreSelection && mainWindow.shown){int remembered=categoryMemory[category].quest;auto foundQuest=std::find_if(quests.begin(),quests.end(),[&](const Row& q){return q.id==remembered;});
                if(foundQuest!=quests.end()){SelectQuest(foundQuest->id,foundQuest->label);int top=0;for(const auto& entry:ListEntries()){int height=EntryHeight(entry);if(entry.index>=0 && quests[entry.index].id==remembered){if(top<listOffset)listOffset=top;else if(top+height>listOffset+ListViewportBottom-ListViewportTop)listOffset=top+height-(ListViewportBottom-ListViewportTop);break;}top+=height;}}
                else {
                    // New quests can move the remembered selection to another server page.
                    restorePages.insert(listPage);int next=-1;
                    if(remembered)for(int page=0;page<(questTotal+ServerPageSize-1)/ServerPageSize;++page)if(!restorePages.count(page)){next=page;break;}
                    if(next>=0){listPage=next;listOffset=0;quests.clear();List();}
                    else {restoreSelection=false;categoryMemory[category].quest=0;if(message==Text(L"loading"))message.clear();}
                }
            }
        }
    }
    else if(s.quest==selectedQuest) {
        if(s.view==1 && view==1 && r.map==detailPage && !quote.token){detailRows=std::move(s.rows);detailTotal=s.total;title=s.title;questDescription=Description(selectedQuest);int npc=0;for(const auto& row:detailRows)if(row.kind==5 || (row.kind==6 && !npc))npc=row.id;if(npc)questNpc=npc;if(message==Text(L"loading") || message==Text(L"timeout"))message.clear();}
        else if(s.view==2 && desiredView==2 && r.kind==sourceKind && r.target==sourceTarget && r.map==sourcePage){sourceRows=std::move(s.rows);sourceTotal=s.total;view=2;detailOffset=textScroll=0;selectedRow=-1;message.clear();}
        else if(s.view==3 && desiredView==3 && r.map==routeMap && r.token==routePage){routeRows=std::move(s.rows);routeTotal=s.total;view=3;detailOffset=textScroll=0;selectedRow=-1;message=s.message;}
        else if(s.view==4){if(!mainWindow.shown)return true;if(r.map==0 && s.features!=completionFeatures){message=Text(L"featuresChanged");return true;}quote=std::move(s);quotedRequest=r;quoteTime=GetTickCount();confirming=false;Show(confirmWindow);}
        else if(s.view==5){DismissQuote();message=s.message;textScroll=0;RefreshView();List();}
    }
    Dirty();return true;
}
void QuestHelperUI::FieldChange(){if(!enabled)return;DismissQuote();queue.clear();pending.clear();if(mainWindow.shown){RefreshView();List();}Dirty();}
