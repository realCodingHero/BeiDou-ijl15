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
#include <cwctype>
#include <cwchar>

namespace {
using QuestProtocol::Row;
using QuestProtocol::Snapshot;
constexpr int Width=600,Height=450,HelperWidth=380,HelperHeight=400;
constexpr int ServerPageSize=50,ListViewportTop=68,ListViewportBottom=416;
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
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;DWORD* pixels=nullptr;int width,height;int alphaMode=-1;
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
struct CategoryMemory {int quest=0,offset=0;std::set<int> collapsed{100000};std::map<int,int> groupQuests;};
std::array<CategoryMemory,2> categoryMemory;
bool restoreSelection=false;
std::vector<Row> loadingQuests;bool listLoading=false;DWORD listStarted=0;
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
struct Scrollbar {RECT track,thumb;int* value=nullptr;int maximum=0,step=1,id=0;};
struct Tab {RECT rect;int id;std::wstring text;bool enabled;};
struct Window {
    alignas(8) unsigned char native[0x78]{};
    std::array<void*,13> mainMethods{};
    std::array<void*,19> inputMethods{};
    void* referenceDestructor=nullptr;
    int kind=0; // 0 quest helper, 1 its confirmation, 2 Maple Helper, 3 salon
    bool initialized=false,shown=false,modal=false,collapsed=true,positioned=false;
    int x=0,y=0,dragScroll=-1,dragStart=0,dragValue=0;
    int hover=-1,pressed=-1;bool mouseDown=false;
    bool animating=false;int drawer=0,drawerFrom=0;DWORD drawerStart=0;
    std::vector<Button> buttons;
    std::vector<Tab> tabs;
    std::vector<RowHit> rowHits;
    std::vector<ListHit> listHits;
    std::vector<Scrollbar> scrollbars;
    std::unique_ptr<Bitmap> image;
};
Window mainWindow,confirmWindow,helperWindow,salonWindow;
Request quotedRequest{};
bool confirming=false;
int WindowWidth(const Window& w){if(w.kind==3)return Width;if(w.kind==2)return HelperWidth;return w.modal?350:(w.collapsed && !w.animating)?250:Width;}
int VisibleWidth(const Window& w){if(w.kind==3)return Width;if(w.kind==2)return HelperWidth;return w.modal?350:250+w.drawer;}
int WindowHeight(const Window& w){return w.kind==3?Height:w.kind==2?HelperHeight:w.modal?245:Height;}
bool Blocked(const Window& w){return w.kind!=2 && !w.modal && confirmWindow.shown;}
Window* Find(void* self,bool input=false) {
    for(auto* w:{&mainWindow,&confirmWindow,&helperWindow,&salonWindow})if(self==w->native+(input?4:0))return w;
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
bool AnyGroupCollapsed(){for(const auto& q:quests)if(collapsedGroups.count(QuestGroup(q)))return true;return false;}
bool AnyGroupExpanded(){for(const auto& q:quests)if(!collapsedGroups.count(QuestGroup(q)))return true;return false;}
bool AllGroupsCollapsed(){if(quests.empty())return false;for(const auto& q:quests)if(!collapsedGroups.count(QuestGroup(q)))return false;return true;}
void RememberCategory(){auto& memory=categoryMemory[category];for(const auto& q:quests)if(q.id==selectedQuest)memory.quest=selectedQuest;memory.offset=listOffset;memory.collapsed=collapsedGroups;}
std::wstring NamedToken(wchar_t kind,const std::wstring& id) {
    if(kind==L'c') {for(const auto& row:detailRows)if(row.kind==2 && row.id==_wtoi(id.c_str()))return std::to_wstring(row.current);return L"0";}
    if(kind==L'a') {
        if(id.size()<2)return L"";
        auto quest=id.substr(0,id.size()-1);if(_wtoi(quest.c_str())!=selectedQuest)return L"";
        int index=(std::max)(0,int(id.back()-L'0')-1);
        int mob=_wtoi(ResourceText(L"Quest/Check.img/"+quest+L"/1/mob/"+std::to_wstring(index)+L"/id").c_str());
        for(const auto& row:detailRows)if(row.kind==1 && row.id==mob)return std::to_wstring(row.current)+(row.required>0?L"/"+std::to_wstring(row.required):L"");
        return L"";
    }
    if(kind==L'q')return ResourceText(L"Quest/QuestInfo.img/"+id+L"/name");
    if(kind==L'p' || kind==L'@' || kind==L'o') {
        auto name=ResourceText(std::wstring(L"String/")+(kind==L'o'?L"Mob.img/":L"Npc.img/")+id+L"/name");
        if(!name.empty())return name;
        for(const auto& row:detailRows)if(row.id==_wtoi(id.c_str()) && (kind==L'o'?row.kind==1:row.kind==5 || row.kind==6))return row.label;
        return L"";
    }
    if(kind==L't' || kind==L'z') {
        auto etc=ResourceText(L"String/Etc.img/Etc/"+id+L"/name");if(!etc.empty())return etc;
        for(auto file:{L"Etc.img/",L"Consume.img/",L"Ins.img/",L"Cash.img/",L"Pet.img/"}) {
            auto name=ResourceText(std::wstring(L"String/")+file+id+L"/name");if(!name.empty())return name;
        }
        for(auto category:{L"Accessory",L"Cap",L"Cape",L"Coat",L"Face",L"Glove",L"Hair",L"Longcoat",L"Pants",L"PetEquip",L"Ring",L"Shield",L"Shoes",L"Weapon"}) {
            auto name=ResourceText(std::wstring(L"String/Eqp.img/Eqp/")+category+L"/"+id+L"/name");if(!name.empty())return name;
        }
        for(const auto& row:detailRows)if(row.kind==2 && row.id==_wtoi(id.c_str()))return row.label;
    }
    if(kind==L'm')for(auto group:{L"maple",L"victoria",L"ossyria",L"elin",L"weddingGL",L"MasteriaGL",L"HalloweenGL",L"jp",L"etc",L"singapore",L"event",L"Episode1GL",L"thai",L"china",L"grandis",L"3rd",L"CT",L"bd232UnnamedSource"}) {
        auto name=ResourceText(std::wstring(L"String/Map.img/")+group+L"/"+id+L"/mapName");if(!name.empty())return name;
    }
    return L"";
}
std::wstring PlainText(const std::wstring& text);
std::wstring Description(int quest,bool marked=false) {
    auto raw=ResourceText(L"Quest/QuestInfo.img/"+std::to_wstring(quest)+L"/1");
    if(raw.empty())raw=ResourceText(L"Quest/QuestInfo.img/"+std::to_wstring(quest)+L"/summary");
    raw=std::regex_replace(raw,std::wregex(L"\\\\n"),L"\n");
    raw=std::regex_replace(raw,std::wregex(L"#[iv][0-9]+:?#[ ]?|#[fF][^#\r\n]+#"),L"");
    const std::wregex token(L"#([potmcqa@z])([0-9]+):?#");std::wstring result;
    std::wsmatch match;
    while(std::regex_search(raw,match,token)){result+=match.prefix().str()+NamedToken(match[1].str()[0],match[2]);raw=match.suffix();}
    result+=raw;
    result=std::regex_replace(result,std::wregex(L"#h[ ]?#"),Text(L"player"));
    // Colour/bold codes become "\x01<code>" markers for the rich text renderer, or vanish.
    result=std::regex_replace(result,std::wregex(L"#([bkrdegn])"),marked?L"\x01$1":L"");
    result=std::regex_replace(result,std::wregex(L"#w"),L"");
    result=std::regex_replace(result,std::wregex(L"#[a-zA-Z@][0-9]+(?::[^#\r\n]*)?#?"),L"");
    result=std::regex_replace(result,std::wregex(L"#[a-zA-Z]"),L"");
    result.erase(std::remove(result.begin(),result.end(),L'#'),result.end());
    return PlainText(result).empty()?Text(L"noDescription"):result;
}
// ---- Palette ---------------------------------------------------------------
// Colours are sampled from the v83 quest window, NPC dialog markup and gauges
// instead of being hand-picked web greys.
namespace Palette {
constexpr COLORREF Ink=RGB(0,0,0),Muted=RGB(119,119,119),Faint=RGB(153,153,153);
constexpr COLORREF Heading=RGB(34,68,119),Blue=RGB(0,51,204),Red=RGB(204,0,0),Green=RGB(0,136,0),Purple=RGB(136,34,170),Amber=RGB(187,102,0);
constexpr COLORREF OnBlue=RGB(255,255,255),OnBlueMuted=RGB(204,229,255);
constexpr COLORREF Select=RGB(221,234,247),SelectEdge=RGB(153,187,221),Rule=RGB(204,204,204);
constexpr COLORREF Strip=RGB(225,232,240),StripTop=RGB(255,255,255),StripEdge=RGB(170,187,204);
constexpr COLORREF CaptionEdge=RGB(17,85,153),CaptionDisabled=RGB(221,221,221),CaptionDisabledEdge=RGB(136,136,136);
constexpr COLORREF TabEdge=RGB(153,17,51),TabIdleEdge=RGB(119,119,119);
}
constexpr int LineHeight=14;
void Fill(HDC dc,RECT r,COLORREF c){HBRUSH b=CreateSolidBrush(c);FillRect(dc,&r,b);DeleteObject(b);}
void Write(HDC dc,RECT r,const std::wstring& s,COLORREF c=Palette::Ink,bool bold=false,UINT flags=DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX) {
    auto old=SelectObject(dc,bold?boldFont:font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,c);
    DrawTextW(dc,s.c_str(),static_cast<int>(s.size()),&r,flags);SelectObject(dc,old);
}
int TextWidth(HDC dc,const std::wstring& s,bool bold=false){auto old=SelectObject(dc,bold?boldFont:font);SIZE size{};GetTextExtentPoint32W(dc,s.c_str(),int(s.size()),&size);SelectObject(dc,old);return size.cx;}
// White captions with a one pixel halo, the way the client's baked button/tab text is drawn.
void Outlined(HDC dc,RECT r,const std::wstring& s,COLORREF fill,COLORREF edge,UINT flags=DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX) {
    auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,edge);
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx || dy){RECT q=r;OffsetRect(&q,dx,dy);DrawTextW(dc,s.c_str(),int(s.size()),&q,flags);}
    SetTextColor(dc,fill);DrawTextW(dc,s.c_str(),int(s.size()),&r,flags);SelectObject(dc,old);
}
void Dotted(HDC dc,int left,int right,int y,COLORREF c=Palette::Rule){for(int x=left;x<right;x+=2)SetPixelV(dc,x,y,c);}

// ---- Rich text -------------------------------------------------------------
// Description(quest,true) keeps #b/#r/#g/#d/#k/#e/#n as "\x01<code>" markers.
struct Glyph {wchar_t c;COLORREF color;bool bold;};
struct Placed {int x,line;size_t index;};
std::vector<Glyph> Glyphs(const std::wstring& text,COLORREF base) {
    std::vector<Glyph> out;COLORREF color=base;bool bold=false;out.reserve(text.size());
    for(size_t i=0;i<text.size();++i) {
        wchar_t c=text[i];
        if(c==L'\x01'){if(++i>=text.size())break;switch(text[i]){
            case L'b':color=Palette::Blue;break;case L'r':color=Palette::Red;break;case L'g':color=Palette::Green;break;
            case L'd':color=Palette::Purple;break;case L'k':color=base;break;case L'e':bold=true;break;case L'n':bold=false;break;}continue;}
        if(c==L'\r')continue;
        out.push_back({c,color,bold});
    }
    return out;
}
std::wstring PlainText(const std::wstring& text){std::wstring out;for(size_t i=0;i<text.size();++i){if(text[i]==L'\x01'){++i;continue;}out+=text[i];}return out;}
bool NoLineStart(wchar_t c){return c && wcschr(L"\x3001\x3002\xFF0C\xFF1B\xFF1A\xFF01\xFF1F\xFF09\x300D\x300F\x300B\x3009\x2026,.;:!?)%",c)!=nullptr;}
bool WordChar(wchar_t c){return (c>=L'0' && c<=L'9') || (c>=L'a' && c<=L'z') || (c>=L'A' && c<=L'Z') || c==L'/' || c==L'_' || c==L'-';}
// Greedy wrapping with a fixed 14 px leading: CJK may break anywhere, ASCII words
// move to the next line together and closing punctuation never starts a line.
int Layout(HDC dc,int width,const std::vector<Glyph>& glyphs,std::vector<Placed>* placed) {
    std::vector<Placed> local;auto& out=placed?*placed:local;out.clear();
    auto old=SelectObject(dc,font);int x=0,line=0,word=-1;
    auto advance=[&](size_t index){SIZE size{};wchar_t c=glyphs[index].c;GetTextExtentPoint32W(dc,&c,1,&size);return int(size.cx)+(glyphs[index].bold?1:0);};
    for(size_t i=0;i<glyphs.size();++i) {
        wchar_t c=glyphs[i].c;
        if(c==L'\n'){++line;x=0;word=-1;continue;}
        int width0=advance(i);bool wordChar=WordChar(c);if(!wordChar)word=-1;
        if(x>0 && x+width0>width) {
            ++line;
            if(NoLineStart(c) && !out.empty() && out.back().line==line-1 && out.back().x>0) {
                // Closing punctuation never starts a line: carry the previous character with it.
                out.back().x=0;out.back().line=line;x=advance(out.back().index);word=-1;
            } else if(wordChar && word>=0 && out[word].x>0) {
                // Carry the unfinished ASCII word to the new line.
                int shift=out[word].x;for(size_t k=word;k<out.size();++k){out[k].x-=shift;out[k].line=line;}
                x=out.back().x+advance(out.back().index);
            } else {x=0;word=-1;}
        }
        if(x==0 && c==L' ')continue;
        if(wordChar && word<0)word=int(out.size());
        out.push_back({x,line,i});x+=width0;
    }
    SelectObject(dc,old);
    return out.empty()?0:out.back().line+1;
}
int TextHeight(HDC dc,int width,const std::wstring& text){return Layout(dc,width,Glyphs(text,Palette::Ink),nullptr)*LineHeight;}
void DrawGlyphs(HDC dc,int left,int top,const std::vector<Glyph>& glyphs,const std::vector<Placed>& placed,int firstY=-0x7FFF) {
    auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);
    for(const auto& p:placed){const auto& g=glyphs[p.index];int y=top+p.line*LineHeight+1;if(y<firstY)continue;SetTextColor(dc,g.color);
        TextOutW(dc,left+p.x,y,&g.c,1);if(g.bold)TextOutW(dc,left+p.x+1,y,&g.c,1);}
    SelectObject(dc,old);
}
// A wrapped block without a scrollbar, clipped to its rectangle.
int TextBlock(HDC dc,RECT r,const std::wstring& text,COLORREF color) {
    auto glyphs=Glyphs(text,color);std::vector<Placed> placed;int lines=Layout(dc,r.right-r.left,glyphs,&placed);
    int saved=SaveDC(dc);IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);DrawGlyphs(dc,r.left,r.top,glyphs,placed);RestoreDC(dc,saved);
    return lines*LineHeight;
}

// ---- Native skins ----------------------------------------------------------
std::set<std::wstring> missingSkins;
Bitmap* Skin(const std::wstring& path) {
    auto it=skins.find(path);if(it!=skins.end())return it->second.get();
    if(missingSkins.count(path))return nullptr;
    VARIANT result{};HRESULT hr=ResourceValue(path,result)?S_OK:E_FAIL;
    void* canvas=nullptr;
    if(SUCCEEDED(hr) && (result.vt==VT_UNKNOWN || result.vt==VT_DISPATCH) && result.punkVal)
        hr=Method<Query>(result.punkVal,0)(result.punkVal,CanvasGuid,&canvas);
    VariantClear(&result);if(FAILED(hr) || !canvas){missingSkins.insert(path);return nullptr;}
    unsigned w=0,h=0;Method<Size>(canvas,0x40)(canvas,&w);Method<Size>(canvas,0x48)(canvas,&h);
    if(w==0 || h==0 || w>1000 || h>1000){Method<Release>(canvas,8)(canvas);missingSkins.insert(path);return nullptr;}
    auto image=std::make_unique<Bitmap>(w,h);
    if(image->pixels)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        unsigned color=0;Method<Pixel>(canvas,0x88)(canvas,x,y,&color);image->pixels[y*w+x]=color;
    }
    Method<Release>(canvas,8)(canvas);Bitmap* ptr=image.get();skins.emplace(path,std::move(image));return ptr;
}
// Skins without any alpha (e.g. 24-bit fixtures) are treated as opaque.
bool HasAlpha(Bitmap& s) {
    if(s.alphaMode<0){GdiFlush();s.alphaMode=0;for(int i=0;i<s.width*s.height;++i)if(s.pixels[i]>>24){s.alphaMode=1;break;}}
    return s.alphaMode==1;
}
RECT pixelClip{0,0,0x7FFF,0x7FFF};
struct PixelClip {RECT saved;explicit PixelClip(RECT r):saved(pixelClip){IntersectRect(&pixelClip,&saved,&r);}~PixelClip(){pixelClip=saved;}};
inline void Put(Bitmap& d,int x,int y,DWORD p,bool copy) {
    if(x<pixelClip.left || x>=pixelClip.right || y<pixelClip.top || y>=pixelClip.bottom || x<0 || y<0 || x>=d.width || y>=d.height)return;
    DWORD& q=d.pixels[y*d.width+x];
    if(copy){q=p;return;}
    unsigned a=p>>24;if(!a)return;
    if(a==255){q=(q&0xFF000000)|(p&0xFFFFFF);return;}
    DWORD o=q&0xFF000000;for(int shift:{0,8,16})o|=((((p>>shift)&255)*a+((q>>shift)&255)*(255-a)+127)/255)<<shift;q=o;
}
// Source axis split: [0,head) drawn once, [mid0,mid1) repeated, [tail,size) drawn once at the end.
struct Axis {int head,mid0,mid1,tail;};
Axis Whole(int size){return {size,0,1,size};}
int MapAxis(int offset,int length,int size,const Axis& a) {
    int tailLength=size-a.tail;
    if(offset>=length-tailLength)return (std::min)(size-1,a.tail+offset-(length-tailLength));
    if(offset<a.head)return offset;
    return a.mid0+(offset-a.head)%(std::max)(1,a.mid1-a.mid0);
}
// Nine-slice composition that TILES the middle instead of stretching it, so borders,
// corners and the dithered window texture keep their native pixels.
void Nine(Bitmap& d,RECT r,Bitmap* s,Axis ax,Axis ay,bool copy=false) {
    if(!s || !s->pixels || r.right<=r.left || r.bottom<=r.top)return;
    GdiFlush();bool alpha=HasAlpha(*s);
    int w=r.right-r.left,h=r.bottom-r.top;std::vector<int> xs(w);
    for(int x=0;x<w;++x)xs[x]=MapAxis(x,w,s->width,ax);
    for(int y=0;y<h;++y){const DWORD* row=s->pixels+MapAxis(y,h,s->height,ay)*s->width;
        for(int x=0;x<w;++x){DWORD p=row[xs[x]];if(!alpha)p|=0xFF000000;Put(d,r.left+x,r.top+y,p,copy);}}
}
void Sprite(Bitmap& d,int x,int y,Bitmap* s){if(s)Nine(d,{x,y,x+s->width,y+s->height},s,Whole(s->width),Whole(s->height));}
// Fits a sprite into a box (downscale only, nearest neighbour), bottom-centred.
void Scaled(Bitmap& d,RECT box,Bitmap* s,bool centreY=false,int opacity=255) {
    if(!s || !s->pixels)return;GdiFlush();bool alpha=HasAlpha(*s);
    double scale=(std::min)(1.0,(std::min)(double(box.right-box.left)/s->width,double(box.bottom-box.top)/s->height));
    int width=(std::max)(1,int(s->width*scale)),height=(std::max)(1,int(s->height*scale));
    int left=box.left+(box.right-box.left-width)/2,top=centreY?box.top+(box.bottom-box.top-height)/2:box.bottom-height;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){DWORD p=s->pixels[(std::min)(s->height-1,int(y/scale))*s->width+(std::min)(s->width-1,int(x/scale))];if(!alpha)p|=0xFF000000;if(opacity<255)p=(p&0xFFFFFF)|(((p>>24)*unsigned(opacity)/255)<<24);Put(d,left+x,top+y,p,false);}
}
std::wstring Padded(int id,int digits){auto s=std::to_wstring(id);while(int(s.size())<digits)s=L"0"+s;return s;}
// Follows info/link chains used by NPCs and mobs that share another sprite.
std::wstring Linked(const wchar_t* folder,int id) {
    std::wstring key=Padded(id,7);std::set<std::wstring> visited;
    while(visited.insert(key).second && visited.size()<8){auto link=ResourceText(std::wstring(folder)+L"/"+key+L".img/info/link");if(link.empty())break;key=Padded(_wtoi(link.c_str()),7);}
    return key;
}
Bitmap* NpcSprite(int npc) {
    if(npc<=0)return nullptr;auto id=Linked(L"Npc",npc);
    for(auto node:{L"/stand/0",L"/default/0",L"/default"})if(auto s=Skin(L"Npc/"+id+L".img"+node))return s;
    return nullptr;
}
Bitmap* MobSprite(int mob) {
    if(mob<=0)return nullptr;auto id=Linked(L"Mob",mob);
    for(auto node:{L"/stand/0",L"/move/0",L"/fly/0"})if(auto s=Skin(L"Mob/"+id+L".img"+node))return s;
    return nullptr;
}
Bitmap* ItemIcon(int id) {
    int type=id/1000000;
    if(id/10000==500)return Skin(L"Item/Pet/"+std::to_wstring(id)+L".img/info/icon");
    if(type>=2 && type<=5){const wchar_t* folder=type==2?L"Consume":type==3?L"Install":type==4?L"Etc":L"Cash";
        return Skin(std::wstring(L"Item/")+folder+L"/"+Padded(id/10000,4)+L".img/"+Padded(id,8)+L"/info/icon");}
    if(type==1)for(auto part:{L"Cap",L"Accessory",L"Coat",L"Longcoat",L"Pants",L"Shoes",L"Glove",L"Shield",L"Cape",L"Ring",L"Weapon",L"PetEquip"})
        if(auto s=Skin(std::wstring(L"Character/")+part+L"/"+Padded(id,8)+L".img/info/icon"))return s;
    return nullptr;
}
Bitmap* ObjectiveIcon(const Row& row){return row.kind==2?ItemIcon(row.id):row.kind==1?MobSprite(row.id):(row.kind==5 || row.kind==6)?NpcSprite(row.id):nullptr;}
void NpcPortrait(Bitmap& target,RECT bounds,int npc){Scaled(target,bounds,NpcSprite(npc));}
// Quest/Gauge2: grey 79x8 track tiled to any width with the native 1x4 blue fill.
void Gauge(Bitmap& img,int left,int top,int right,double ratio) {
    ratio=(std::max)(0.0,(std::min)(1.0,ratio));
    auto frame=Skin(L"UI/UIWindow.img/Quest/Gauge2/frame"),bar=Skin(L"UI/UIWindow.img/Quest/Gauge2/gauge");
    if(!frame)return;
    Nine(img,{left,top,right,top+frame->height},frame,{2,2,3,frame->width-3},Whole(frame->height));
    int filled=int((right-left-4)*ratio+0.5);
    if(bar && filled>0)Nine(img,{left+2,top+2,left+2+filled,top+2+bar->height},bar,{0,0,1,1},Whole(bar->height));
}
// Quest/Gauge: the 163x14 overall progress bar with its built-in icon.
void BigGauge(Bitmap& img,int left,int top,double ratio) {
    ratio=(std::max)(0.0,(std::min)(1.0,ratio));
    auto frame=Skin(L"UI/UIWindow.img/Quest/Gauge/frame"),bar=Skin(L"UI/UIWindow.img/Quest/Gauge/gauge");
    if(!frame)return;Sprite(img,left,top,frame);
    int filled=int((frame->width-17)*ratio+0.5);
    if(bar && filled>0)Nine(img,{left+15,top+1,left+15+filled,top+1+bar->height},bar,{0,0,1,1},Whole(bar->height));
}
void ScrollbarAt(Window& w,HDC dc,RECT bounds,int& value,int maximum,int visible,bool always=false,int step=1,int forcedId=0) {
    maximum=(std::max)(0,maximum);value=(std::max)(0,(std::min)(value,maximum));
    if(!maximum && !always)return;
    auto& img=*w.image;
    bool active=maximum>0 && !Blocked(w);
    int id=forcedId?forcedId:&value==&listOffset?100:&value==&descriptionScroll?102:&value==&detailOffset?104:106;
    auto path=[&](const wchar_t* part){return std::wstring(L"UI/Basic.img/VScr/")+(active?L"enabled/":L"disabled/")+part;};
    auto prevSkin=Skin(path(!active?L"prev":w.pressed==id && w.hover==id?L"prev1":L"prev0"));
    auto nextSkin=Skin(path(!active?L"next":w.pressed==id+1 && w.hover==id+1?L"next1":L"next0"));
    int arrow=prevSkin?prevSkin->height:12;
    RECT prev{bounds.left,bounds.top,bounds.left+12,bounds.top+arrow},next{bounds.left,bounds.bottom-arrow,bounds.left+12,bounds.bottom};
    RECT track{bounds.left,prev.bottom,bounds.left+12,next.top};
    if(auto base=Skin(path(L"base")))Nine(img,track,base,Whole(base->width),{0,0,base->height,base->height});
    Sprite(img,prev.left,prev.top,prevSkin);Sprite(img,next.left,next.top,nextSkin);
    // v83 scrollbars use a fixed-size thumb; it moves but never stretches.
    auto thumbSkin=active?Skin(path(w.dragScroll==int(w.scrollbars.size())?L"thumb1":L"thumb0")):nullptr;
    int length=track.bottom-track.top,thumb=(std::min)(length,thumbSkin?thumbSkin->height:12);
    int y=track.top+(length-thumb)*value/(std::max)(1,maximum);
    RECT rect{track.left,y,track.right,y+thumb};if(active)Sprite(img,rect.left,rect.top,thumbSkin);
    w.scrollbars.push_back({track,rect,&value,maximum,step,id});
    w.buttons.push_back({prev,id,L"",active && value>0});w.buttons.push_back({next,id+1,L"",active && value<maximum});
}
void TextPane(Window& w,HDC dc,RECT bounds,const std::wstring& text,int& scroll,int& maximum,COLORREF color=Palette::Ink) {
    auto glyphs=Glyphs(text,color);std::vector<Placed> placed;
    int height=Layout(dc,bounds.right-bounds.left-16,glyphs,&placed)*LineHeight;
    int visibleHeight=int(bounds.bottom-bounds.top);
    // Scroll whole lines so text is never cut through the middle.
    maximum=(std::max)(0,(height-visibleHeight+LineHeight-1)/LineHeight*LineHeight);
    scroll=(std::min)(scroll,maximum);
    int saved=SaveDC(dc);IntersectClipRect(dc,bounds.left,bounds.top,bounds.right-16,bounds.bottom);
    DrawGlyphs(dc,bounds.left,bounds.top-scroll,glyphs,placed,bounds.top);RestoreDC(dc,saved);
    ScrollbarAt(w,dc,{bounds.right-13,bounds.top,bounds.right-1,bounds.bottom},scroll,maximum,visibleHeight,false,LineHeight*2);
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
// Every list request starts again from the first server page; later pages follow on their own.
void List(){listPage=0;loadingQuests.clear();listLoading=true;listStarted=GetTickCount();Enqueue({0,0,0,category,0,0});}
void Detail(){if(selectedQuest)Enqueue({1,selectedQuest,0,0,detailPage,1});}
void Route(){if(selectedQuest && routeMap)Enqueue({3,selectedQuest,sourceKind,sourceTarget,routeMap,3,routePage});}
void SetExpanded(bool expanded);
void SelectQuest(int quest,const std::wstring& name){++navigation;restoreSelection=false;selectedQuest=quest;title=name;detailRows.clear();view=desiredView=1;selectedRow=-1;questNpc=0;questLevel=0;for(const auto& q:quests)if(q.id==quest){questLevel=q.current;collapsedGroups.erase(QuestGroup(q));categoryMemory[category].groupQuests[QuestGroup(q)]=quest;}RememberCategory();detailOffset=detailPage=textScroll=descriptionScroll=0;questDescription=Description(quest,true);message=Text(L"loading");SetExpanded(true);Detail();Dirty();}
void Hide(Window& w){if(w.shown){w.shown=false;destroy(w.native);}w.buttons.clear();w.tabs.clear();w.pressed=w.hover=-1;w.mouseDown=false;w.dragScroll=-1;}
void Show(Window& w);
void AdvanceDrawer(DWORD now);
void RestoreCategory(){const auto& memory=categoryMemory[category];listPage=0;listOffset=memory.offset;collapsedGroups=memory.collapsed;selectedQuest=0;selectedRow=-1;view=desiredView=1;quests.clear();detailRows.clear();questTotal=0;title.clear();message.clear();questDescription.clear();restoreSelection=true;}
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
const wchar_t* ButtonState(Window& w,int id,bool active){return !active?L"disabled":w.pressed==id && w.hover==id?L"pressed":w.hover==id?L"mouseOver":L"normal";}
// Quest/BtDetail is a 57x17 button with baked text. Its 3-pixel caps are kept and the
// caption-free column 3 is tiled, so any width keeps the native border and gradient.
void CaptionButton(Window& w,HDC dc,int id,RECT r,const std::wstring& caption,bool active=true) {
    active=active && !Blocked(w);
    bool pressed=active && w.pressed==id && w.hover==id;
    auto skin=Skin(std::wstring(L"UI/UIWindow.img/Quest/BtDetail/")+ButtonState(w,id,active)+L"/0");
    r.bottom=r.top+(skin?skin->height:17);
    if(skin)Nine(*w.image,r,skin,{3,3,4,skin->width-3},Whole(skin->height));
    RECT text{r.left+1,r.top,r.right-2,r.bottom-2};if(pressed)OffsetRect(&text,1,1);
    Outlined(dc,text,caption,active?RGB(255,255,255):Palette::CaptionDisabled,active?Palette::CaptionEdge:Palette::CaptionDisabledEdge);
    w.buttons.push_back({r,id,caption,active});
}
void ButtonAt(Window& w,HDC dc,int id,RECT r,const wchar_t* label,bool active=true){CaptionButton(w,dc,id,r,Text(label),active);}
// 12x12 Basic.img buttons (BtClose, BtHide2/BtHide3) drawn at their native size.
void IconButton(Window& w,int id,int x,int y,const wchar_t* name,const std::wstring& caption,bool active=true) {
    active=active && !Blocked(w);
    auto skin=Skin(std::wstring(L"UI/Basic.img/")+name+L"/"+ButtonState(w,id,active)+L"/0");
    RECT r{x,y,x+(skin?skin->width:12),y+(skin?skin->height:12)};Sprite(*w.image,x,y,skin);
    w.buttons.push_back({r,id,caption,active});
}
void CloseButton(Window& w,HDC,int x){IconButton(w,1,x,6,L"BtClose",Text(L"close"));}
void DrawTab(Window& w,HDC dc,int id,RECT r,const std::wstring& text,bool selected) {
    bool active=!Blocked(w);
    auto prefix=std::wstring(L"UI/Basic.img/Tab2/");const wchar_t* state=selected?L"1":L"0";
    auto left=Skin(prefix+L"left"+state),fill=Skin(prefix+L"fill"+state),right=Skin(prefix+L"right"+state);
    int lw=left?left->width:4,rw=right?right->width:4;
    Sprite(*w.image,r.left,r.top,left);Sprite(*w.image,r.right-rw,r.top,right);
    if(fill)Nine(*w.image,{r.left+lw,r.top,r.right-rw,r.bottom},fill,{0,0,1,1},Whole(fill->height));
    RECT caption{r.left,r.top+1,r.right,r.bottom};if(w.pressed==id && w.hover==id)OffsetRect(&caption,1,1);
    COLORREF edge=!active?Palette::CaptionDisabledEdge:selected?Palette::TabEdge:Palette::TabIdleEdge;
    Outlined(dc,caption,text,!active?Palette::CaptionDisabled:selected?RGB(255,255,255):(w.hover==id?RGB(255,255,255):RGB(238,238,238)),edge);
    w.tabs.push_back({r,id,text,active});
}
void TabAt(Window& w,HDC dc,int id,RECT r,const std::wstring& text){DrawTab(w,dc,id,r,text,(id==2 && category==0) || (id==3 && category==1));}
bool CompletionAllowed(int kind) {
    return kind==1?(completionFeatures&1)!=0:kind==2?(completionFeatures&2)!=0:kind==7?completionFeatures!=0:false;
}
std::wstring Amount(int64_t value) {
    auto text=std::to_wstring(value);
    for(int i=int(text.size())-3;i>0;i-=3)text.insert(i,1,L',');
    return text;
}
std::wstring ObjectiveCaption(const Row& row) {
    auto caption=row.label;if(row.kind==1 || row.kind==2)caption+=L" ("+std::to_wstring(row.id)+L")";
    if(row.required>0)caption+=L"  "+std::to_wstring(row.current)+L"/"+std::to_wstring(row.required);
    return caption;
}
// ---- Detail drawer geometry (window coordinates) -----------------------------
constexpr int DetailLeft=262,DetailRight=592,DescTop=124,DescBottom=194,ObjectiveTop=216,BarTop=426;
constexpr int ObjectiveRow=36,ObjectiveText=301,ObjectiveRight=574;
struct DetailGeometry {bool paging;int limit,messageTop,listBottom;};
DetailGeometry DetailLayout() {
    int total=view==2?sourceTotal:view==3?routeTotal:detailTotal,page=view==2?sourcePage:view==3?routePage:detailPage;
    DetailGeometry g{total>ServerPageSize || page>0,0,0,0};
    g.limit=g.paging?392:414;g.messageTop=g.limit-56;g.listBottom=message.empty()?g.limit:g.messageTop-6;
    return g;
}
std::wstring Trimmed(std::wstring s){while(!s.empty() && iswspace(s.back()))s.pop_back();while(!s.empty() && iswspace(s.front()))s.erase(s.begin());return s;}
double ObjectiveRatio(const Row& row){return row.required>0?double((std::min)(row.current,row.required))/row.required:(row.flags&1)?1.0:0.0;}
// Numbers inside a server hint such as "Level 10, account kills 1,234".
std::vector<long long> Numbers(const std::wstring& text) {
    std::vector<long long> out;long long value=0;bool inside=false;
    for(wchar_t c:text){if(c>=L'0' && c<=L'9'){value=value*10+(c-L'0');inside=true;}else if(c==L',' && inside)continue;else if(inside){out.push_back(value);value=0;inside=false;}}
    if(inside)out.push_back(value);return out;
}
std::wstring Format(std::wstring pattern,const std::wstring& value){auto at=pattern.find(L"{0}");if(at!=std::wstring::npos)pattern.replace(at,3,value);return pattern;}
// What an objective row shows on its second line, and what unfolds when it is selected.
struct ObjectiveView {std::wstring meta;COLORREF metaColor=Palette::Muted;std::wstring badge;COLORREF badgeColor=Palette::Faint;std::wstring detail;};
ObjectiveView Describe(const Row& row) {
    ObjectiveView v;bool done=(row.flags&1)!=0;
    if(row.kind==1) {
        // Mob: "Lv. 10" from the client's own Mob.wz; the account kill total only matters
        // while kill completion is enabled on the server.
        int level=_wtoi(ResourceText(L"Mob/"+Padded(row.id,7)+L".img/info/level").c_str());
        auto numbers=Numbers(row.info);if(level<=0 && !numbers.empty())level=int(numbers.front());
        if(level>0)v.meta=L"Lv. "+std::to_wstring(level);
        if((completionFeatures&1) && numbers.size()>=2)v.badge=Text(L"accountKills")+L" "+Amount(numbers.back());
    } else if(row.kind==2) {
        bool missingText=row.info==Format(Text(L"itemMissing"),std::to_wstring((std::max)(0,row.required-row.current)));
        if(done || (missingText && !(row.flags&2))){}
        else if(row.flags&2){v.badge=Text(L"purchasable");v.badgeColor=Palette::Green;}
        else if((completionFeatures&2) && !row.info.empty()) {
            v.badge=Text(L"purchaseLocked");v.badgeColor=Palette::Amber;
            // The server's generic "below level 70" text really means the item cannot be
            // bought before you obtain it yourself (single or quest-exclusive items).
            v.detail=row.info==Text(L"lockLevelLiteral")?Text(row.required<=1?L"lockSingle":L"lockExclusive"):row.info;
        }
    } else v.meta=row.info;
    return v;
}
int ObjectiveMetaLeft(const Row& row){return row.required>0?ObjectiveText+116:ObjectiveText;}
// Text that does not fit on the second line unfolds below it when the row is selected.
std::wstring ObjectiveDetail(HDC dc,const Row& row) {
    auto v=Describe(row);if(!v.detail.empty())return v.detail;
    int badge=v.badge.empty()?0:TextWidth(dc,v.badge)+8;
    return TextWidth(dc,v.meta)>ObjectiveRight-ObjectiveMetaLeft(row)-badge?v.meta:L"";
}
int ObjectiveHeight(HDC dc,int index) {
    if(index!=selectedRow)return ObjectiveRow;
    auto detail=ObjectiveDetail(dc,detailRows[index]);
    return detail.empty()?ObjectiveRow:ObjectiveRow+TextHeight(dc,ObjectiveRight-ObjectiveText,detail)+2;
}
int ObjectiveMaximum(HDC dc,int bottom) {
    int height=0,maximum=int(detailRows.size());
    for(int i=int(detailRows.size())-1;i>=0;--i){height+=ObjectiveHeight(dc,i)+2;if(height>bottom-ObjectiveTop && maximum<int(detailRows.size()))break;maximum=i;}
    return maximum;
}
void SelectionBox(HDC dc,RECT r){Fill(dc,r,Palette::Select);HBRUSH edge=CreateSolidBrush(Palette::SelectEdge);FrameRect(dc,&r,edge);DeleteObject(edge);}
// Objective rows: native icon, name and id, right-aligned counter, quest gauge, then
// "Lv. 10" and a right-aligned badge. Rows only grow when something did not fit.
void ObjectiveTable(Window& w,HDC dc,int bottom) {
    auto& img=*w.image;
    int maximum=ObjectiveMaximum(dc,bottom);detailOffset=(std::min)(detailOffset,maximum);int y=ObjectiveTop,end=detailOffset;
    for(int i=detailOffset;i<int(detailRows.size());++i){const auto& row=detailRows[i];int height=ObjectiveHeight(dc,i);
        if(y+height>bottom && y>ObjectiveTop)break;int rowBottom=(std::min)(bottom,y+height);
        bool selected=i==selectedRow,done=(row.flags&1)!=0;auto shown=Describe(row);
        int saved=SaveDC(dc);IntersectClipRect(dc,DetailLeft,y,ObjectiveRight+2,rowBottom);
        if(selected)SelectionBox(dc,{DetailLeft,y,ObjectiveRight+2,rowBottom});
        {PixelClip clip({DetailLeft,y,ObjectiveRight+2,rowBottom});Scaled(img,{265,y+2,297,y+34},ObjectiveIcon(row),true);}
        int countLeft=ObjectiveRight;
        if(row.required>0){auto count=std::to_wstring(row.current)+L"/"+std::to_wstring(row.required);countLeft=ObjectiveRight-TextWidth(dc,count)-2;
            Write(dc,{countLeft,y+3,ObjectiveRight,y+17},count,done || row.current>=row.required?Palette::Green:Palette::Red,false,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);}
        int nameRight=countLeft-6;
        Write(dc,{ObjectiveText,y+3,nameRight,y+17},row.label,done?Palette::Green:Palette::Ink);
        int nameWidth=TextWidth(dc,row.label);
        if((row.kind==1 || row.kind==2) && ObjectiveText+nameWidth+8<nameRight)
            Write(dc,{ObjectiveText+nameWidth+4,y+3,nameRight,y+17},L"("+std::to_wstring(row.id)+L")",Palette::Faint);
        if(row.required>0){GdiFlush();Gauge(img,ObjectiveText,y+22,ObjectiveText+110,ObjectiveRatio(row));}
        int metaLeft=ObjectiveMetaLeft(row),metaRight=ObjectiveRight;
        if(!shown.badge.empty()){int width=TextWidth(dc,shown.badge);metaRight=ObjectiveRight-width-8;
            Write(dc,{ObjectiveRight-width,y+19,ObjectiveRight,y+33},shown.badge,shown.badgeColor,false,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);}
        Write(dc,{metaLeft,y+19,metaRight,y+33},shown.meta,shown.metaColor);
        if(selected){auto detail=ObjectiveDetail(dc,row);if(!detail.empty())TextBlock(dc,{ObjectiveText,y+35,ObjectiveRight,rowBottom},detail,Palette::Muted);}
        RestoreDC(dc,saved);
        if(!selected)Dotted(dc,DetailLeft+2,ObjectiveRight,rowBottom,Palette::Rule);
        w.rowHits.push_back({{DetailLeft,y,ObjectiveRight+2,rowBottom},i});y=rowBottom+2;end=i+1;
    }
    ScrollbarAt(w,dc,{579,ObjectiveTop,591,bottom},detailOffset,maximum,(std::max)(1,end-detailOffset));
}
// A light header strip shared by list groups.
void HeaderStrip(HDC dc,RECT r){Fill(dc,r,Palette::Strip);Fill(dc,{r.left,r.top,r.right,r.top+1},Palette::StripTop);Fill(dc,{r.left,r.bottom-1,r.right,r.bottom},Palette::StripEdge);}
// ---- Map list and route map --------------------------------------------------
// World map spots from Map/MapHelper.img: big blue = start, small blue = teleport
// unlocked, orange = travel there yourself, magenta = destination.
Bitmap* Spot(int kind){return Skin(L"Map/MapHelper.img/worldMap/mapImage/"+std::to_wstring(kind));}
void SpotAt(Bitmap& img,int cx,int cy,int kind){if(auto s=Spot(kind))Sprite(img,cx-s->width/2,cy-s->height/2,s);}
constexpr int TableTop=124,LegendHeight=18,SpotX=281;
void Legend(Window& w,HDC dc,bool route) {
    struct Item {int spot;const wchar_t* label;};
    std::vector<Item> items{{3,L"legendWarp"},{1,L"legendWalk"}};if(route)items.push_back({2,L"routeEnd"});
    int x=DetailLeft+2;
    for(const auto& item:items) {
        if(auto s=Spot(item.spot)){int size=(std::min)(s->width,14);Scaled(*w.image,{x,TableTop+2,x+size,TableTop+2+size},s,true);x+=size+3;}
        auto label=Text(item.label);Write(dc,{x,TableTop+1,x+90,TableTop+16},label,Palette::Muted);x+=TextWidth(dc,label)+14;
    }
    Dotted(dc,DetailLeft+2,ObjectiveRight,TableTop+LegendHeight-1,Palette::StripEdge);
}
// "Street - Map" split so the map stands out and the region is secondary.
void SplitMapName(const std::wstring& label,std::wstring& map,std::wstring& street) {
    auto at=label.find(L" - ");
    if(at==std::wstring::npos){map=label;street.clear();return;}
    street=Trimmed(label.substr(0,at));map=Trimmed(label.substr(at+3));if(map.empty()){map=street;street.clear();}
}
// Quest tables keep the map id for reference; the Maple Helper's teleport list shows names only.
// The id outranks the street: parts that would only show cut off are left out, and the street
// that had no room is returned so the caller can place it elsewhere.
std::wstring MapNameText(HDC dc,RECT r,const std::wstring& label,int id,COLORREF color,bool showId=true) {
    std::wstring map,street;SplitMapName(label,map,street);
    int width=TextWidth(dc,map);Write(dc,{r.left,r.top,r.right,r.bottom},map,color);
    auto number=showId?std::to_wstring(id):std::wstring();
    auto extra=street.empty()?number:number.empty()?street:street+L"  "+number;
    int room=r.right-(r.left+width+8);std::wstring dropped;
    if(!street.empty() && TextWidth(dc,extra)>room){dropped=street;extra=number;}
    if(!extra.empty() && TextWidth(dc,extra)<=room)Write(dc,{r.left+width+8,r.top,r.right,r.bottom},extra,Palette::Faint);
    return dropped;
}
std::wstring MapName(HDC dc,RECT r,const Row& row,COLORREF color){return MapNameText(dc,r,row.label,row.id,color);}
// Map list: one card per map with its source, warp price and unlock state.
void MapList(Window& w,HDC dc,const std::vector<Row>& rows,int bottom) {
    Legend(w,dc,false);
    constexpr int CardHeight=36;int top=TableTop+LegendHeight+2,y=top,end=detailOffset;
    for(int i=detailOffset;i<int(rows.size());++i) {
        if(y+CardHeight>bottom && y>top)break;
        const auto& r=rows[i];bool unlocked=(r.flags&8)!=0,selected=i==selectedRow;int rowBottom=(std::min)(bottom,y+CardHeight);
        int saved=SaveDC(dc);IntersectClipRect(dc,DetailLeft,y,ObjectiveRight+2,rowBottom);
        if(selected)SelectionBox(dc,{DetailLeft,y,ObjectiveRight+2,rowBottom});
        {PixelClip clip({DetailLeft,y,ObjectiveRight+2,rowBottom});SpotAt(*w.image,SpotX-8,y+10,unlocked?3:1);}
        auto cost=Amount(r.cost)+L" "+Text(L"costColumn");int costWidth=TextWidth(dc,cost);
        auto street=MapName(dc,{DetailLeft+22,y+3,ObjectiveRight-costWidth-8,y+17},r,Palette::Ink);
        Write(dc,{ObjectiveRight-costWidth,y+3,ObjectiveRight,y+17},cost,unlocked?Palette::Green:Palette::Muted,false,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
        auto state=Text(unlocked?L"unlocked":L"locked");int stateWidth=TextWidth(dc,state);
        Write(dc,{ObjectiveRight-stateWidth,y+19,ObjectiveRight,y+33},state,unlocked?Palette::Green:Palette::Amber,false,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
        Write(dc,{DetailLeft+22,y+19,ObjectiveRight-stateWidth-8,y+33},r.info,Palette::Muted);
        // A street pushed off the first line follows the source when it fits there whole.
        int infoRight=DetailLeft+22+(r.info.empty()?0:TextWidth(dc,r.info)+8);
        if(!street.empty() && infoRight+TextWidth(dc,street)<=ObjectiveRight-stateWidth-8)
            Write(dc,{infoRight,y+19,ObjectiveRight-stateWidth-8,y+33},street,Palette::Faint);
        RestoreDC(dc,saved);
        if(!selected)Dotted(dc,DetailLeft+2,ObjectiveRight,rowBottom-1);
        w.rowHits.push_back({{DetailLeft,y,ObjectiveRight+2,rowBottom},i});y=rowBottom+1;end=i+1;
    }
    if(detailOffset>0 || end<int(rows.size()))ScrollbarAt(w,dc,{579,top,591,bottom},detailOffset,(std::max)(0,int(rows.size())-1),(std::max)(1,end-detailOffset));
}
// Route map: a vertical rail with a world-map spot per step and the way to travel
// written on the rail between two steps.
void RouteMap(Window& w,HDC dc,const std::vector<Row>& rows,int bottom) {
    auto& img=*w.image;Legend(w,dc,true);
    constexpr int NodeHeight=34,LinkHeight=14;int top=TableTop+LegendHeight+4,y=top,end=detailOffset,previousSpot=-1;
    int total=routeTotal>0?routeTotal:int(rows.size());
    for(int i=detailOffset;i<int(rows.size());++i) {
        const auto& r=rows[i];int step=routePage*ServerPageSize+i;bool first=step==0,last=step==total-1,unlocked=(r.flags&8)!=0;
        bool link=i>detailOffset && !r.info.empty();int height=(link?LinkHeight:0)+NodeHeight;
        if(y+height>bottom && y>top)break;
        int node=y+(link?LinkHeight:0),spotY=node+9;
        // Rail from the previous spot (or from above when scrolled) down to this one.
        int railTop=previousSpot>=0?previousSpot:(i>0?y:spotY);
        Fill(dc,{SpotX-1,railTop,SpotX+1,spotY},Palette::StripEdge);
        if(link)Write(dc,{SpotX+12,y,ObjectiveRight,y+LinkHeight},r.info,Palette::Faint);
        bool selected=i==selectedRow;
        if(selected)SelectionBox(dc,{DetailLeft,node,ObjectiveRight+2,node+NodeHeight-2});
        if(selected)Fill(dc,{SpotX-1,node,SpotX+1,node+NodeHeight-2},Palette::StripEdge);
        Write(dc,{DetailLeft+2,node+2,SpotX-11,node+16},std::to_wstring(step+1),Palette::Heading,false,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
        {PixelClip clip({DetailLeft,top,ObjectiveRight+2,bottom});SpotAt(img,SpotX,spotY,last?2:first?0:unlocked?3:1);}
        std::wstring tag=first?Text(L"routeStart"):last?Text(L"routeEnd"):L"";int tagWidth=tag.empty()?0:TextWidth(dc,tag)+8;
        if(!tag.empty())Write(dc,{ObjectiveRight-tagWidth+8,node+2,ObjectiveRight,node+16},tag,last?Palette::Purple:Palette::Heading,false,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
        MapName(dc,{SpotX+14,node+2,ObjectiveRight-tagWidth,node+16},r,unlocked?Palette::Green:Palette::Ink);
        std::wstring second=unlocked?Text(L"warp")+L" "+Amount(r.cost)+L" "+Text(L"costColumn"):Text(L"legendWalk");
        if(first && !r.info.empty())second=r.info+L"  "+second;
        Write(dc,{SpotX+14,node+18,ObjectiveRight,node+32},second,unlocked?Palette::Green:Palette::Muted);
        w.rowHits.push_back({{DetailLeft,node,ObjectiveRight+2,node+NodeHeight-2},i});
        previousSpot=spotY+8;y+=height;end=i+1;
    }
    // Keep the rail running off the bottom while more steps follow.
    if(end<int(rows.size()) && previousSpot>=0)Fill(dc,{SpotX-1,previousSpot,SpotX+1,bottom},Palette::StripEdge);
    if(detailOffset>0 || end<int(rows.size()))ScrollbarAt(w,dc,{579,top,591,bottom},detailOffset,(std::max)(0,int(rows.size())-1),(std::max)(1,end-detailOffset));
}
// Map mark (Map/MapHelper.img/mark/<name>) of a map, used in the blue summary box.
Bitmap* MapMark(int map) {
    if(map<=0)return nullptr;
    auto mark=ResourceText(L"Map/Map/Map"+std::to_wstring(map/100000000)+L"/"+Padded(map,9)+L".img/info/mapMark");
    return mark.empty() || mark==L"None"?nullptr:Skin(L"Map/MapHelper.img/mark/"+mark);
}
std::wstring TargetLabel() {
    if(view==2)for(const auto& r:detailRows)if(r.kind==sourceKind && r.id==sourceTarget)return r.label;
    if(view==3)for(const auto& r:sourceRows)if(r.id==routeMap)return r.label;
    return L"";
}
// Summary lines for the current target: "street - map" on one line when it fits, otherwise the
// street on the first line and the map on a second one aligned under it.
std::vector<std::wstring> TargetLines(HDC dc,const std::wstring& prefix,int width) {
    auto label=TargetLabel();
    std::wstring map,street;SplitMapName(label,map,street);
    if(street.empty() || TextWidth(dc,prefix+label)<=width)return {prefix+label};
    return {prefix+street+L" -",std::wstring(map)};
}
// Native panel slices: tile the dithered body rows/columns, keep borders, title box,
// tab strip, red rule and the blue button bar pixel-identical.
constexpr Axis MainX{120,120,125,125},MainY{200,200,260,260};
constexpr Axis DrawerX{180,150,180,180},DrawerY{200,200,260,260};
constexpr Axis ModalY{23,60,300,362};
void Paint(Window& w) {
    int width=WindowWidth(w),height=WindowHeight(w);
    if(!w.image || w.image->width!=width || w.image->height!=height)w.image=std::make_unique<Bitmap>(width,height);
    if(!w.image->pixels)return;
    auto& img=*w.image;HDC dc=img.dc;w.buttons.clear();w.tabs.clear();w.rowHits.clear();w.listHits.clear();w.scrollbars.clear();
    GdiFlush();std::fill(img.pixels,img.pixels+width*height,0);
    auto frameSkin=Skin(L"UI/UIWindow.img/Quest/backgrnd"),drawer=Skin(L"UI/UIWindow.img/Quest/backgrnd2");
    bool drawerVisible=!w.modal && (!w.collapsed || w.animating);
    // 1. Frames, copied with their alpha so the rounded corners and drop shadow survive.
    if(w.modal)Nine(img,{0,0,width,height},frameSkin,MainX,ModalY,true);
    else {Nine(img,{0,0,250,height},frameSkin,MainX,MainY,true);if(drawerVisible)Nine(img,{250,0,Width,height},drawer,DrawerX,DrawerY,true);}
    // 2. Remember the window shape. GDI clears alpha on every pixel it touches.
    std::vector<unsigned char> shape(size_t(width)*height,255);
    if(frameSkin){GdiFlush();for(int i=0;i<width*height;++i)shape[i]=static_cast<unsigned char>(img.pixels[i]>>24);}
    else Fill(dc,{0,0,width,height},RGB(240,240,240));
    if(w.modal) {
        Fill(dc,{5,5,width-5,19},RGB(255,255,255));
        Write(dc,{10,5,width-20,19},Text(L"confirm"));
        int y=32;y+=TextBlock(dc,{16,y,334,y+42},confirming?Text(L"processing"):Text(L"quote"),Palette::Muted)+8;
        y+=TextBlock(dc,{16,y,334,y+84},quote.message,Palette::Ink)+12;
        Dotted(dc,16,334,y-6,Palette::StripEdge);
        Write(dc,{16,y,334,y+14},Text(L"cost")+L": "+Amount(quote.price),Palette::Red);
        Write(dc,{16,y+18,334,y+32},Text(L"meso")+L": "+Amount(meso),meso>=quote.price?Palette::Muted:Palette::Red);
        ButtonAt(w,dc,20,{92,221,172,238},L"confirm",!confirming && quote.token && GetTickCount()-quoteTime<30000 && meso>=quote.price);
        ButtonAt(w,dc,21,{178,221,258,238},L"cancel",!confirming);
    } else {
        // Title box: erase the baked caption and use the client's plain black title text.
        Fill(dc,{5,5,245,19},RGB(255,255,255));
        Write(dc,{10,5,200,19},Text(L"title"));
        IconButton(w,15,215,6,w.collapsed?L"BtHide3":L"BtHide2",w.collapsed?L">":L"<",selectedQuest!=0);
        CloseButton(w,dc,230);
        // Tabs stand on the native red rule.
        TabAt(w,dc,2,{7,23,124,42},Text(L"progress")+(progressCount>=0?L" "+std::to_wstring(progressCount):L""));
        TabAt(w,dc,3,{126,23,243,42},Text(L"ready")+(readyCount>=0?L" "+std::to_wstring(readyCount):L""));
        Write(dc,{10,48,208,63},Text(L"questCount")+L" "+std::to_wstring(quests.size()),Palette::Muted);
        // Expand all (down arrow) / collapse all (up arrow) as native 12x12 buttons.
        IconButton(w,16,214,50,L"BtMin2",Trimmed(Text(L"expandAll")),AnyGroupCollapsed());
        IconButton(w,17,229,50,L"BtMax2",Trimmed(Text(L"collapseAll")),AnyGroupExpanded());
        auto entries=ListEntries();const int visible=ListViewportBottom-ListViewportTop;
        int maximum=ListMaximum();listOffset=(std::min)(listOffset,maximum);
        if(quests.empty())TextBlock(dc,{12,ListViewportTop+8,226,ListViewportTop+60},Text(L"empty"),Palette::Muted);
        {
            PixelClip clip({6,ListViewportTop,228,ListViewportBottom});
            int savedList=SaveDC(dc);IntersectClipRect(dc,6,ListViewportTop,228,ListViewportBottom);
            int y=ListViewportTop-listOffset;
            for(int i=0;i<int(entries.size());++i) {
                const auto& entry=entries[i];int rowHeight=EntryHeight(entry);
                if(y+rowHeight<=ListViewportTop){y+=rowHeight;continue;}if(y>=ListViewportBottom)break;
                RECT bounds{6,y,228,y+rowHeight-2};
                if(entry.index<0) {
                    HeaderStrip(dc,bounds);bool collapsed=collapsedGroups.count(entry.group)!=0;
                    Sprite(img,9,y+3,Skin(collapsed?L"UI/Basic.img/BtMax/normal/0":L"UI/Basic.img/BtMin/normal/0"));
                    auto count=L" ("+std::to_wstring(entry.count)+L")";int nameWidth=(std::min)(150,TextWidth(dc,entry.name));
                    Write(dc,{25,y,25+nameWidth,y+18},entry.name,Palette::Heading);
                    Write(dc,{25+nameWidth,y,224,y+18},count,Palette::Muted);
                } else {
                    const auto& q=quests[entry.index];bool selected=q.id==selectedQuest;
                    if(selected)SelectionBox(dc,bounds);
                    auto icon=Skin(category==1?L"UI/UIWindow.img/Quest/icon0":L"UI/UIWindow.img/Quest/icon2/0");
                    if(icon)Sprite(img,10+(15-icon->width)/2,y+(category==1?3:2),icon);
                    Write(dc,{28,y+1,224,y+(category==1?19:16)},q.label,selected?Palette::Blue:Palette::Ink);
                    if(category!=1)Write(dc,{28,y+16,224,y+29},L"Lv. "+std::to_wstring(q.current),Palette::Muted);
                    if(!selected)Dotted(dc,8,226,y+rowHeight-2);
                }
                bounds.top=(std::max)(LONG(ListViewportTop),bounds.top);bounds.bottom=(std::min)(LONG(ListViewportBottom),bounds.bottom);
                w.listHits.push_back({bounds,entry});y+=rowHeight;
            }
            RestoreDC(dc,savedList);
        }
        ScrollbarAt(w,dc,{231,ListViewportTop,243,ListViewportBottom},listOffset,maximum,visible,true,20);
        ButtonAt(w,dc,6,{172,BarTop,236,BarTop+17},L"refresh");
        if(drawerVisible) {
            Write(dc,{262,5,590,19},title);
            auto& rows=Rows();auto geometry=DetailLayout();bool mapView=view==2 || view==3;
            // The native blue summary box: name, id/level or the current target, overall gauge, NPC.
            GdiFlush();
            Write(dc,{282,40,482,54},title,Palette::OnBlue,true);
            if(mapView) {
                auto prefix=Text(view==2?L"sourceTarget":L"routeTarget")+L": ";auto lines=TargetLines(dc,prefix,200);
                // Two target lines tighten the rows so the quest id still sits above the gauge.
                int y=lines.size()>1?56:58,step=lines.size()>1?14:16,indent=lines.size()>1?TextWidth(dc,prefix):0;
                for(size_t i=0;i<lines.size();++i)Write(dc,{282+(i?indent:0),y+int(i)*step,482,y+int(i)*step+14},lines[i],Palette::OnBlueMuted);
                y+=int(lines.size())*step;
                Write(dc,{282,y,482,y+14},Text(L"questId")+L": "+std::to_wstring(selectedQuest)+L"   Lv. "+std::to_wstring(questLevel),Palette::OnBlueMuted);
            } else {
                Write(dc,{282,58,482,72},Text(L"questId")+L": "+std::to_wstring(selectedQuest),Palette::OnBlueMuted);
                Write(dc,{282,74,482,88},L"Lv. "+std::to_wstring(questLevel),Palette::OnBlueMuted);
            }
            int gaugeY=98;
            if(mapView && TargetLines(dc,Text(view==2?L"sourceTarget":L"routeTarget")+L": ",200).size()>1)gaugeY=102;
            int need=0,have=0;for(const auto& r:detailRows)if(r.required>0){need+=r.required;have+=(std::min)(r.current,r.required);}
            if(need>0){BigGauge(img,272,gaugeY,double(have)/need);Write(dc,{440,gaugeY,484,gaugeY+14},std::to_wstring(have*100/need)+L"%",Palette::OnBlue,false,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);}
            {   // Map views show the region mark of the chosen map; objectives show the NPC.
                PixelClip clip({486,27,593,118});Bitmap* mark=nullptr;
                if(mapView){int map=view==3?routeMap:selectedRow>=0 && selectedRow<int(rows.size())?rows[selectedRow].id:rows.empty()?0:rows.front().id;mark=MapMark(map);}
                if(mark)Sprite(img,539-mark->width/2,72-mark->height/2,mark);else NpcPortrait(img,{488,29,592,117},questNpc);
            }
            if(view==2)MapList(w,dc,rows,geometry.listBottom);
            else if(view==3)RouteMap(w,dc,rows,geometry.listBottom);
            else {
                TextPane(w,dc,{DetailLeft,DescTop,DetailRight,DescBottom},questDescription,descriptionScroll,descriptionScrollMax);
                int headingWidth=TextWidth(dc,Text(L"objectives"));
                Write(dc,{DetailLeft+2,198,DetailLeft+2+headingWidth+2,212},Text(L"objectives"),Palette::Heading);
                Dotted(dc,DetailLeft+headingWidth+10,DetailRight-2,205,Palette::StripEdge);
                ObjectiveTable(w,dc,geometry.listBottom);
            }
            if(rows.empty() && view==2)TextBlock(dc,{DetailLeft+4,TableTop+LegendHeight+8,DetailRight-4,TableTop+120},Text(L"noSource"),Palette::Muted);
            if(!message.empty()){Dotted(dc,DetailLeft+2,DetailRight-2,geometry.messageTop-3,Palette::StripEdge);TextPane(w,dc,{DetailLeft,geometry.messageTop,DetailRight,geometry.limit},message,textScroll,textScrollMax,Palette::Muted);}
            else textScroll=textScrollMax=0;
            if(geometry.paging) {
                int page=view==2?sourcePage:view==3?routePage:detailPage,total=view==2?sourceTotal:view==3?routeTotal:detailTotal;
                ButtonAt(w,dc,13,{DetailLeft,396,DetailLeft+68,413},L"prev",page>0);
                ButtonAt(w,dc,14,{DetailRight-68,396,DetailRight,413},L"next",(page+1)*ServerPageSize<total);
            }
            bool chosen=selectedRow>=0 && selectedRow<int(rows.size());
            ButtonAt(w,dc,7,{262,BarTop,318,BarTop+17},L"back");
            ButtonAt(w,dc,10,{323,BarTop,387,BarTop+17},view==2?L"route":L"source",chosen && view!=3);
            if(mapView || (view==1 && chosen && CompletionAllowed(rows[selectedRow].kind)))
                ButtonAt(w,dc,11,{392,BarTop,456,BarTop+17},mapView?L"warp":L"purchase",chosen && (mapView?(rows[selectedRow].flags&8)!=0:(rows[selectedRow].flags&2)!=0));
            if(completionFeatures)ButtonAt(w,dc,9,{461,BarTop,525,BarTop+17},L"all",view==1 && selectedQuest!=0);
            ButtonAt(w,dc,12,{534,BarTop,592,BarTop+17},L"refresh");
        }
    }
    // 3. Restore the frame alpha over everything GDI drew.
    GdiFlush();for(int i=0;i<width*height;++i)img.pixels[i]=(img.pixels[i]&0xFFFFFF)|(DWORD(shape[i])<<24);
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
// Two releases on the same row of the same window within the system double-click time.
struct RowClick {const Window* window=nullptr;int index=0;DWORD at=0;} lastRowClick;
bool RowClickedTwice(const Window& w,int index) {
    DWORD now=GetTickCount();
    bool twice=lastRowClick.window==&w && lastRowClick.index==index && now-lastRowClick.at<=GetDoubleClickTime();
    lastRowClick=twice?RowClick{}:RowClick{&w,index,now};
    return twice;
}
#include "MapleHelperUI.inl"
#include "SalonUI.inl"
void __fastcall Draw(void* self,void*,RECT* rect) {
    auto w=Find(self);if(!w)return;
    baseDraw(self,rect);if(w->kind==3)PaintSalon(*w);else if(w->kind==2)PaintHelper(*w);else Paint(*w);
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
    for(const auto& tab:w->tabs)if(PtInRect(&tab.rect,p))return 2;
    return y<25?1:2;
}
void __fastcall Key(void* input,void*,unsigned key,unsigned flags) {
    auto w=Find(input,true);if(!w || flags&0x80000000 || Blocked(*w))return;
    if(w->kind==3){if(!SalonKey(key) && key==VK_ESCAPE)HideSalon();return;}
    if(w->kind==2){if(!HelperKey(key) && key==VK_ESCAPE)HideHelper();return;}
    if(key==VK_ESCAPE){if(w->modal){if(!confirming)DismissQuote();}else {RememberCategory();++navigation;Hide(*w);}return;}
    if(w->modal){if(key==VK_RETURN && !confirming && quote.token && meso>=quote.price && GetTickCount()-quoteTime<30000){Enqueue({5,quotedRequest.quest,quotedRequest.kind,quotedRequest.target,quotedRequest.map,5,quote.token});confirming=true;Dirty();}return;}
    if(key==VK_UP || key==VK_DOWN){listOffset=(std::max)(0,(std::min)(ListMaximum(),listOffset+(key==VK_UP?-20:20)));Dirty();}
}
int __fastcall InputEnabled(void* input,void*) {
    auto w=Find(input,true);return w && !Blocked(*w)?1:0;
}
int __fastcall MouseMove(void* input,void*,int x,int y) {
    auto w=Find(input,true);if(!w || Blocked(*w))return 1;
    if(w->kind==2 && HelperDragging()){HelperDrag(x);return 1;}
    if(w->dragScroll>=0 && w->dragScroll<int(w->scrollbars.size())) {
        auto& bar=w->scrollbars[w->dragScroll];int distance=bar.track.bottom-bar.track.top-(bar.thumb.bottom-bar.thumb.top);
        *bar.value=(std::max)(0,(std::min)(bar.maximum,w->dragValue+(y-w->dragStart)*bar.maximum/(std::max)(1,distance)));Invalidate(*w);Dirty();return 1;
    }
    int hover=-1;POINT p{x,y};for(const auto& b:w->buttons)if(b.enabled && PtInRect(&b.rect,p))hover=b.id;
    for(const auto& tab:w->tabs)if(tab.enabled && PtInRect(&tab.rect,p))hover=tab.id;
    if(hover!=w->hover){w->hover=hover;Invalidate(*w);}return 1;
}
void __fastcall MouseEnter(void* input,void*,int entered){auto w=Find(input,true);if(!w || entered)return;w->hover=w->pressed=-1;w->mouseDown=false;w->dragScroll=-1;Invalidate(*w);}
int __fastcall Wheel(void* input,void*,int x,int y,int delta) {
    auto w=Find(input,true);if(!w || w->modal || Blocked(*w))return 1;
    if(w->kind==3){SalonWheel(delta);return 1;}
    if(w->kind==2){HelperWheel(delta);return 1;}
    if(x<250){listOffset=(std::max)(0,(std::min)(ListMaximum(),listOffset+(delta>0?60:-60)));}
    else if(w->animating)return 1;
    else {
        auto geometry=DetailLayout();
        if(view==1 && y>=DescTop && y<=DescBottom)descriptionScroll=(std::max)(0,(std::min)(descriptionScrollMax,descriptionScroll+(delta>0?LineHeight*2:-LineHeight*2)));
        else if(!message.empty() && y>=geometry.messageTop && y<geometry.limit)textScroll=(std::max)(0,(std::min)(textScrollMax,textScroll+(delta>0?LineHeight*2:-LineHeight*2)));
        else {int maximum=view==1 && mainWindow.image?ObjectiveMaximum(mainWindow.image->dc,geometry.listBottom):(std::max)(0,int(Rows().size())-1);detailOffset=(std::max)(0,(std::min)(maximum,detailOffset+(delta>0?2:-2)));}
    }
    Dirty();return 1;
}
void Click(Window& w,int id) {
    if(w.kind==3){SalonClick(w,id);return;}
    if(w.kind==2){HelperClick(w,id);return;}
    if(Blocked(w))return;
    if(w.modal){if(id==21 && !confirming)DismissQuote();return;}
    if(id>=100){for(auto& bar:w.scrollbars)if(id==bar.id || id==bar.id+1){*bar.value=(std::max)(0,(std::min)(bar.maximum,*bar.value+(id==bar.id?-bar.step:bar.step)));Dirty();return;}return;}
    auto& rows=Rows();bool chosen=selectedRow>=0 && selectedRow<int(rows.size());
    if(id==1){RememberCategory();++navigation;Hide(w);return;}
    if(id==15){SetExpanded(w.collapsed);return;}
    if(id==16 || id==17){bool expand=id==16;for(const auto& q:quests){int group=QuestGroup(q);if(expand)collapsedGroups.erase(group);else collapsedGroups.insert(group);}listOffset=0;Dirty();return;}
    if(id==2 || id==3){int next=id==3?1:0;if(next==category)return;RememberCategory();++navigation;category=next;RestoreCategory();SetExpanded(false);List();Dirty();return;}
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
        for(const auto& tab:w->tabs)if(tab.enabled && PtInRect(&tab.rect,p)){w->pressed=w->hover=tab.id;Invalidate(*w);return;}
        for(const auto& b:w->buttons)if(b.enabled && PtInRect(&b.rect,p)){w->pressed=w->hover=b.id;Invalidate(*w);return;}
        if(w->kind==2 && HelperPress(p)){Invalidate(*w);return;}
        for(int i=0;i<int(w->scrollbars.size());++i){auto& bar=w->scrollbars[i];if(PtInRect(&bar.track,p)){
            if(!PtInRect(&bar.thumb,p)){int range=bar.track.bottom-bar.track.top-(bar.thumb.bottom-bar.thumb.top);*bar.value=(std::max)(0,(std::min)(bar.maximum,int((y-bar.track.top-(bar.thumb.bottom-bar.thumb.top)/2)*bar.maximum/(std::max)(1,range))));}
            w->dragScroll=i;w->dragStart=y;w->dragValue=*bar.value;Invalidate(*w);Dirty();return;
        }}return;
    }
    if(msg!=WM_LBUTTONUP)return;
    if(w->kind==2 && HelperDragging()){HelperRelease();w->mouseDown=false;w->pressed=-1;return;}
    bool wasDown=w->mouseDown;int pressed=w->pressed;w->pressed=-1;w->mouseDown=false;Invalidate(*w);
    if(w->dragScroll>=0){w->dragScroll=-1;return;}
    if(!wasDown)return;
    for(const auto& tab:w->tabs)if(tab.enabled && PtInRect(&tab.rect,p)){if(pressed==tab.id)Click(*w,tab.id);return;}
    for(const auto& b:w->buttons)if(b.enabled && PtInRect(&b.rect,p)){
        if(pressed!=b.id)return;
        if(b.id==20 && w->modal && quote.token && !confirming){Enqueue({5,quotedRequest.quest,quotedRequest.kind,quotedRequest.target,quotedRequest.map,5,quote.token});confirming=true;Dirty();}
        else Click(*w,b.id);return;
    }
    if(w->modal || pressed>=0)return;
    if(w->kind==2){for(const auto& hit:w->rowHits)if(PtInRect(&hit.rect,p)){bool twice=RowClickedTwice(*w,hit.index);HelperRow(hit.index);if(twice)HelperRowTwice(hit.index);break;}return;}
    for(const auto& hit:w->listHits)if(PtInRect(&hit.rect,p)) {
        auto entry=hit.entry;
        if(entry.index<0){if(collapsedGroups.count(entry.group)){collapsedGroups.erase(entry.group);int remembered=categoryMemory[category].groupQuests[entry.group];for(const auto& q:quests)if(q.id==remembered){SelectQuest(q.id,q.label);break;}}else collapsedGroups.insert(entry.group);Dirty();}
        else SelectQuest(quests[entry.index].id,quests[entry.index].label);return;
    }
    // A double click on a map (source list or route step) is the Teleport button, which asks first.
    for(const auto& hit:w->rowHits)if(PtInRect(&hit.rect,p)){bool twice=RowClickedTwice(*w,hit.index);selectedRow=hit.index;textScroll=0;Dirty();if(twice && (view==2 || view==3))Click(*w,11);break;}
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
    if(w->kind==3){SalonUpdate();return;}
    if(w->kind==2){HelperUpdate();return;}
    if(!InField()) {Hide(mainWindow);DismissQuote();queue.clear();pending.clear();selectedQuest=character=completionFeatures=lastFeatureSequence=0;categoryMemory={};restoreSelection=false;progressCount=readyCount=-1;return;}
    // Both native windows can update in the same frame. Only one request is dispatched every 150 ms.
    DWORD now=GetTickCount();
    if(w==&mainWindow)AdvanceDrawer(now);
    if(now-lastRefresh>=2000){lastRefresh=now;
        if(mainWindow.shown && !mainWindow.collapsed && view==1 && !quote.token)Detail();
    }
    // A refresh waits for a running multi-page fetch unless its replies went missing.
    if(mainWindow.shown && now-lastListRefresh>=(view!=1 || quote.token?2000u:10000u) && (!listLoading || now-listStarted>=10000)){lastListRefresh=now;List();}
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
    if(!w.positioned){int cw=w.kind==2?HelperWidth:Width,ch=w.kind==2?HelperHeight:Height;w.x=(std::max)(0,(Client::m_nGameWidth-cw)/2);w.y=(std::max)(0,(Client::m_nGameHeight-ch)/2);w.positioned=true;}
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
    if(r.purpose==0 && s.view==0 && r.target==category && r.map==listPage && listLoading){
        // The list is one scrollable, foldable list: fetch the server's pages one after another
        // and show them together; a quest that moved between two pages is kept once.
        questTotal=s.total;progressCount=s.progress;readyCount=s.ready;
        for(auto& row:s.rows)if(std::none_of(loadingQuests.begin(),loadingQuests.end(),[&](const Row& q){return q.id==row.id;}))loadingQuests.push_back(std::move(row));
        if(!s.rows.empty() && (listPage+1)*ServerPageSize<questTotal){++listPage;Enqueue({0,0,0,category,listPage,0});if(quests.empty())quests=loadingQuests;Dirty();return true;}
        listLoading=false;listPage=0;quests=std::move(loadingQuests);loadingQuests.clear();listOffset=(std::min)(listOffset,ListMaximum());
        if(restoreSelection && mainWindow.shown){int remembered=categoryMemory[category].quest;auto foundQuest=std::find_if(quests.begin(),quests.end(),[&](const Row& q){return q.id==remembered;});
            if(foundQuest!=quests.end()){SelectQuest(foundQuest->id,foundQuest->label);int top=0;for(const auto& entry:ListEntries()){int height=EntryHeight(entry);if(entry.index>=0 && quests[entry.index].id==remembered){if(top<listOffset)listOffset=top;else if(top+height>listOffset+ListViewportBottom-ListViewportTop)listOffset=top+height-(ListViewportBottom-ListViewportTop);break;}top+=height;}}
            else {restoreSelection=false;categoryMemory[category].quest=0;if(message==Text(L"loading"))message.clear();}
        }
    }
    else if(s.quest==selectedQuest) {
        if(s.view==1 && view==1 && r.map==detailPage && !quote.token){detailRows=std::move(s.rows);detailTotal=s.total;title=s.title;questDescription=Description(selectedQuest,true);int npc=0;for(const auto& row:detailRows)if(row.kind==5 || (row.kind==6 && !npc))npc=row.id;if(npc)questNpc=npc;if(message==Text(L"loading") || message==Text(L"timeout"))message.clear();}
        else if(s.view==2 && desiredView==2 && r.kind==sourceKind && r.target==sourceTarget && r.map==sourcePage){sourceRows=std::move(s.rows);sourceTotal=s.total;view=2;detailOffset=textScroll=0;selectedRow=-1;message.clear();}
        else if(s.view==3 && desiredView==3 && r.map==routeMap && r.token==routePage){routeRows=std::move(s.rows);routeTotal=s.total;view=3;detailOffset=textScroll=0;selectedRow=-1;message=routeRows.empty()?s.message:std::wstring();}
        else if(s.view==4){if(!mainWindow.shown)return true;if(r.map==0 && s.features!=completionFeatures){message=Text(L"featuresChanged");return true;}quote=std::move(s);quotedRequest=r;quoteTime=GetTickCount();confirming=false;Show(confirmWindow);}
        else if(s.view==5){DismissQuote();message=s.message;textScroll=0;RefreshView();List();}
    }
    Dirty();return true;
}
void QuestHelperUI::FieldChange(){
    if(!enabled)return;DismissQuote();queue.clear();pending.clear();if(mainWindow.shown){RefreshView();List();}Dirty();
    if(helper.enabled) {
        // Announce the native Maple Helper so NPC 9900001 opens it instead of its dialog.
        auto hello=MapleProtocol::Request(MapleProtocol::Hello,0);SendBytes(hello);
        if(helperWindow.shown && helper.page==TeleportPage)HelperRequest(MapleProtocol::Teleports,helper.category);
        // Talisman Buffs belong to one boss room; the item reopens the window when needed.
        if(helperWindow.shown && (helper.page==TalismanPage || helper.page==BoostPage))HideHelper();
        if(salonWindow.shown)HideSalon(); // the next map load also restores any try-on
    }
}
void* QuestHelperUI::TransmogLookCave(){return reinterpret_cast<void*>(&TransmogLoadActionCave);}
bool QuestHelperUI::InstallHelper(bool value){if(!value || !enabled)return false;helperWindow.kind=2;salonWindow.kind=3;helper.enabled=true;return true;}
bool QuestHelperUI::HelperEnabled(){return helper.enabled;}
bool QuestHelperUI::HelperReady(){return helper.enabled && helper.serverReady;}
void QuestHelperUI::OpenHelper(int page){if(page>=HubPage && page<=PetPage)OpenHelperPage(page);}
bool QuestHelperUI::ReceiveHelper(const unsigned char* data,size_t size){return !helper.enabled || HelperReceive(data,size);}
