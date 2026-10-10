#include "stdafx.h"
#include "QuestHelperUI.h"
#include "QuestHelperProtocol.h"
#include <oleauto.h>
#include <algorithm>
#include <array>
#include <map>
#include <deque>
#include <memory>

namespace {
using QuestProtocol::Row;
using QuestProtocol::Snapshot;
constexpr int Width=600,Height=450;
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
bool enabled=false;
HFONT font=nullptr,boldFont=nullptr;
int sequence=0,character=0,selectedQuest=0,category=0,listPage=0,listOffset=0,detailOffset=0,detailPage=0;
int sourceKind=0,sourceTarget=0,sourcePage=0,routeMap=0,routePage=0,selectedRow=-1,view=1,trackedQuest=0;
int64_t meso=0;
std::vector<Row> quests,detailRows,sourceRows,routeRows,trackRows;
int questTotal=0,sourceTotal=0,detailTotal=0,routeTotal=0;
std::wstring title,message,trackTitle;
DWORD lastRefresh=0,lastListRefresh=0,lastSend=0,quoteTime=0;
Snapshot quote;
struct Request {int action,quest,kind,target,map,purpose;int64_t token=0;};
struct Pending {Request request;DWORD sent;};
std::deque<Request> queue;
std::map<int,Pending> pending;
struct Button {RECT rect;int id;std::wstring text;bool enabled;};
struct Window {
    alignas(8) unsigned char native[0x78]{};
    std::array<void*,13> mainMethods{};
    std::array<void*,19> inputMethods{};
    void* referenceDestructor=nullptr;
    bool initialized=false,shown=false,tracker=false,collapsed=false;
    int hover=-1;std::vector<Button> buttons;
    std::unique_ptr<Bitmap> image;
};
Window mainWindow,trackerWindow;
Window* Find(void* self,bool input=false) {
    for(auto* w:{&mainWindow,&trackerWindow})if(self==w->native+(input?4:0))return w;
    return nullptr;
}
const std::wstring& Text(const wchar_t* key) {
    auto it=labels.find(key);if(it!=labels.end())return it->second;
    wchar_t buffer[512]{};GetPrivateProfileStringW(language.c_str(),key,key,buffer,512,labelFile.c_str());
    return labels.emplace(key,buffer).first->second;
}
void Fill(HDC dc,RECT r,COLORREF c){HBRUSH b=CreateSolidBrush(c);FillRect(dc,&r,b);DeleteObject(b);}
void Write(HDC dc,RECT r,const std::wstring& s,COLORREF c=RGB(36,49,62),bool bold=false,UINT flags=DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS) {
    auto old=SelectObject(dc,bold?boldFont:font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,c);
    DrawTextW(dc,s.c_str(),static_cast<int>(s.size()),&r,flags);SelectObject(dc,old);
}
Bitmap* Skin(const std::wstring& path) {
    auto it=skins.find(path);if(it!=skins.end())return it->second.get();
    void* rm=*reinterpret_cast<void**>(0x00BF14E8);if(!rm)return nullptr;
    VARIANT empty{},result{};BSTR key=SysAllocString(path.c_str());
    HRESULT hr=Method<Resource>(rm,0x1C)(rm,key,empty,empty,&result);SysFreeString(key);
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
                else for(unsigned col=0;col<count;++col)reinterpret_cast<WORD*>(dst)[col]=WORD(0xF000|((src[col]>>12)&0xF00)|((src[col]>>8)&0xF0)|((src[col]>>4)&0xF));
            }
        }
        if(SUCCEEDED(hr) && FAILED(Method<Unlock>(raw,0x20)(raw,nullptr)))valid=false;
        Method<Release>(raw,8)(raw);
        if(!valid)return false;
    }
    return true;
}
void Invalidate(Window& w){invalidateRect(w.native,nullptr);}
void Dirty(){if(mainWindow.shown)Invalidate(mainWindow);if(trackerWindow.shown)Invalidate(trackerWindow);}
void Enqueue(Request r) {
    for(const auto& q:queue)if(q.action==r.action && q.quest==r.quest && q.kind==r.kind && q.target==r.target && q.map==r.map && q.purpose==r.purpose)return;
    if(queue.size()<16)queue.push_back(r);
}
void List(){Enqueue({0,0,0,category,listPage,0});}
void Detail(){if(selectedQuest)Enqueue({1,selectedQuest,0,0,detailPage,1});}
void Route(){if(selectedQuest && routeMap)Enqueue({3,selectedQuest,sourceKind,sourceTarget,routeMap,3,routePage});}
void SelectQuest(int quest,const std::wstring& name){selectedQuest=quest;title=name;detailRows.clear();view=1;selectedRow=-1;detailOffset=detailPage=0;message=Text(L"loading");Detail();Dirty();}
void Hide(Window& w){if(w.shown){w.shown=false;destroy(w.native);}w.buttons.clear();}
void Show(Window& w);
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
void ButtonAt(Window& w,HDC dc,int id,RECT r,const wchar_t* label,bool active=true) {
    const wchar_t* skin=!active?L"UI/Basic.img/BtOK/disabled/0":w.hover==id?L"UI/Basic.img/BtOK/mouseOver/0":L"UI/Basic.img/BtOK/normal/0";
    Blit(dc,r,skin);
    // Erase only the native button's old caption, retaining its gradient and borders.
    RECT inner={r.left+4,r.top+3,r.right-4,r.bottom-3};Fill(dc,inner,active?RGB(241,244,249):RGB(227,228,231));
    Write(dc,r,Text(label),active?RGB(37,60,89):RGB(145,145,145),false,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    w.buttons.push_back({r,id,Text(label),active});
}
void Paint(Window& w) {
    int width=w.tracker?223:Width,height=w.tracker?(w.collapsed?25:170):Height;
    if(!w.image || w.image->width!=width || w.image->height!=height)w.image=std::make_unique<Bitmap>(width,height);
    if(!w.image->pixels)return;
    HDC dc=w.image->dc;w.buttons.clear();Fill(dc,{0,0,width,height},RGB(250,251,253));
    if(w.tracker) {
        Blit(dc,{0,0,223,25},L"UI/UIWindow.img/QuestAlarm/backgrndmax");
        for(int y=25;y<165;y+=18)Blit(dc,{0,y,223,(std::min)(165,y+18)},L"UI/UIWindow.img/QuestAlarm/backgrndcenter");
        Blit(dc,{0,165,223,170},L"UI/UIWindow.img/QuestAlarm/backgrndbottom");
        Write(dc,{8,2,140,23},Text(L"tracker"),RGB(35,53,78),true);
        ButtonAt(w,dc,102,{146,3,177,23},w.collapsed?L"+":L"-");
        ButtonAt(w,dc,100,{180,3,218,23},L"close");
        if(!w.collapsed) {
            Write(dc,{8,29,215,49},trackTitle,RGB(27,82,129),true);
            int y=53,count=0;
            for(const auto& r:trackRows)if(r.required>0 && count++<4) {
                Write(dc,{8,y,214,y+19},r.label+L"  "+std::to_wstring(r.current)+L"/"+std::to_wstring(r.required),r.flags&1?RGB(32,126,79):RGB(55,60,66));y+=20;
            }
            ButtonAt(w,dc,101,{8,140,214,162},L"title");
        }
    } else {
        Blit(dc,{0,0,250,Height},L"UI/UIWindow.img/Quest/backgrnd");
        Blit(dc,{250,0,Width,Height},L"UI/UIWindow.img/Quest/backgrnd2");
        Fill(dc,{252,21,598,Height-3},RGB(253,253,253));
        Write(dc,{9,1,250,21},Text(L"title"),RGB(25,48,72),true);
        Write(dc,{264,1,531,22},title,RGB(25,48,72),true);
        ButtonAt(w,dc,1,{535,2,594,21},L"close");
        ButtonAt(w,dc,2,{9,30,124,55},L"progress");ButtonAt(w,dc,3,{127,30,240,55},L"ready");
        if(quests.empty())Write(dc,{13,75,235,110},Text(L"empty"));
        for(int i=listOffset;i<int(quests.size()) && i<listOffset+10;++i) {
            const auto& q=quests[i];int y=61+(i-listOffset)*32;
            Fill(dc,{9,y,239,y+31},q.id==selectedQuest?RGB(215,232,249):RGB(248,249,251));
            std::wstring status=q.flags&1?Text(L"finished"):q.flags&4?Text(L"medal"):L"Lv."+std::to_wstring(q.current);
            Write(dc,{14,y,234,y+19},q.label,RGB(35,68,103),q.id==selectedQuest);
            Write(dc,{14,y+18,234,y+31},status,q.flags&1?RGB(28,133,75):RGB(115,121,127));
        }
        Write(dc,{13,386,237,404},std::to_wstring(listPage*50+listOffset+1)+L" / "+std::to_wstring(questTotal),RGB(113,117,123));
        ButtonAt(w,dc,4,{9,412,82,439},L"prev",listPage>0);ButtonAt(w,dc,5,{87,412,160,439},L"next",(listPage+1)*50<questTotal);
        ButtonAt(w,dc,6,{165,412,239,439},L"refresh");
        if(quote.token) {
            Write(dc,{265,43,587,82},Text(L"quote"),RGB(85,94,109),false,DT_WORDBREAK);
            Write(dc,{265,85,587,115},quote.message,RGB(35,57,85),true,DT_WORDBREAK);
            Write(dc,{265,132,587,158},Text(L"cost")+L": "+std::to_wstring(quote.price),RGB(190,66,44),true);
            Write(dc,{265,163,587,189},Text(L"meso")+L": "+std::to_wstring(meso));
            ButtonAt(w,dc,20,{269,224,418,254},L"confirm",GetTickCount()-quoteTime<30000 && meso>=quote.price);
            ButtonAt(w,dc,21,{430,224,579,254},L"cancel");
        } else if(!selectedQuest) {
            Write(dc,{267,74,583,131},Text(L"select"),RGB(100,110,120),false,DT_WORDBREAK);
        } else {
            ButtonAt(w,dc,7,{264,30,355,55},L"back",view!=1);
            ButtonAt(w,dc,8,{362,30,457,55},L"track");ButtonAt(w,dc,9,{464,30,588,55},L"all",view==1);
            auto& rows=Rows();
            if(rows.empty())Write(dc,{268,66,579,141},view==2?Text(L"noSource"):message,RGB(100,110,120),false,DT_WORDBREAK);
            for(int i=detailOffset;i<int(rows.size()) && i<detailOffset+6;++i) {
                auto& r=rows[i];int y=63+(i-detailOffset)*43;
                Fill(dc,{265,y,587,y+41},i==selectedRow?RGB(224,237,249):RGB(245,247,250));
                std::wstring progress=r.required>0?L"  "+std::to_wstring(r.current)+L"/"+std::to_wstring(r.required):L"";
                Write(dc,{270,y,579,y+20},r.label+progress,r.flags&1?RGB(30,127,77):RGB(36,62,90),i==selectedRow);
                Write(dc,{270,y+20,579,y+38},r.info,RGB(100,111,120));
            }
            if(selectedRow>=0 && selectedRow<int(rows.size())) {
                const auto& r=rows[selectedRow];
                Write(dc,{268,327,584,369},r.label+L"\n"+r.info,RGB(45,61,77),false,DT_WORDBREAK);
            }
            Write(dc,{268,367,584,385},message,RGB(168,71,48));
            int total=view==2?sourceTotal:view==3?routeTotal:detailTotal;
            int page=view==2?sourcePage:view==3?routePage:detailPage;
            if(total>50 || page>0) {
                ButtonAt(w,dc,13,{265,387,370,408},L"prev",page>0);
                ButtonAt(w,dc,14,{476,387,587,408},L"next",(page+1)*50<total);
            }
            bool chosen=selectedRow>=0 && selectedRow<int(rows.size());
            ButtonAt(w,dc,10,{265,412,366,439},view==2?L"route":L"source",chosen && view!=3);
            ButtonAt(w,dc,11,{371,412,472,439},view==2?L"warp":L"purchase",chosen && (view==2?(rows[selectedRow].flags&8)!=0:(rows[selectedRow].flags&2)!=0));
            ButtonAt(w,dc,12,{477,412,588,439},L"refresh");
        }
    }
    GdiFlush();for(int i=0;i<width*height;++i)w.image->pixels[i]|=0xFF000000;
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
void __fastcall Key(void* input,void*,unsigned key,unsigned flags) {
    auto w=Find(input,true);if(!w || flags&0x80000000)return;
    if(key==VK_ESCAPE){if(quote.token){quote={};Detail();Dirty();}else Hide(*w);}
    if(key==VK_UP || key==VK_DOWN){listOffset=(std::max)(0,(std::min)(int(quests.size())-10,listOffset+(key==VK_UP?-1:1)));Dirty();}
}
int __fastcall MouseMove(void* input,void*,int x,int y) {
    auto w=Find(input,true);if(!w)return 1;
    int hover=-1;POINT p{x,y};for(const auto& b:w->buttons)if(b.enabled && PtInRect(&b.rect,p))hover=b.id;
    if(hover!=w->hover){w->hover=hover;Invalidate(*w);}return 1;
}
int __fastcall Wheel(void* input,void*,int x,int,int delta) {
    auto w=Find(input,true);if(!w || w->tracker)return 1;
    if(x<250)listOffset=(std::max)(0,(std::min)(int(quests.size())-10,listOffset+(delta>0?-3:3)));
    else detailOffset=(std::max)(0,(std::min)(int(Rows().size())-6,detailOffset+(delta>0?-2:2)));
    Dirty();return 1;
}
void Click(Window& w,int id) {
    if(w.tracker){if(id==100){trackedQuest=0;Hide(w);}else if(id==101){SelectQuest(trackedQuest,trackTitle);QuestHelperUI::Open();}else if(id==102){w.collapsed=!w.collapsed;Hide(w);Show(w);}return;}
    auto& rows=Rows();bool chosen=selectedRow>=0 && selectedRow<int(rows.size());
    if(id==1){quote={};Hide(w);return;}
    if(id==2 || id==3){category=id==3?1:0;listPage=listOffset=0;List();return;}
    if(id==4 || id==5){listPage+=(id==4?-1:1);listOffset=0;List();return;}
    if(id==6){List();Detail();return;}
    if(id==7){view=1;selectedRow=-1;detailOffset=0;Detail();return;}
    if(id==8){trackedQuest=selectedQuest;trackTitle=title;trackRows=detailRows;Show(trackerWindow);Dirty();return;}
    if(id==9){Enqueue({4,selectedQuest,7,0,0,4});return;}
    if(id==12){if(view==2)Enqueue({2,selectedQuest,sourceKind,sourceTarget,sourcePage,2});else if(view==3)Route();else Detail();return;}
    if(id==13 || id==14) {
        auto& page=view==2?sourcePage:view==3?routePage:detailPage;page+=(id==13?-1:1);detailOffset=0;selectedRow=-1;
        if(view==2)Enqueue({2,selectedQuest,sourceKind,sourceTarget,sourcePage,2});else if(view==3)Route();else Detail();return;
    }
    if(id==21){quote={};Detail();Dirty();return;}
    if(!chosen)return;
    const auto& r=rows[selectedRow];
    if(id==10 && view==1){sourceKind=r.kind;sourceTarget=r.id;sourcePage=0;Enqueue({2,selectedQuest,r.kind,r.id,0,2});}
    else if(id==10 && view==2){routeMap=r.id;routePage=0;Route();}
    else if(id==11 && view==2)Enqueue({4,selectedQuest,sourceKind,sourceTarget,r.id,4});
    else if(id==11 && view==1)Enqueue({4,selectedQuest,r.kind,r.id,0,4});
}
Request quotedRequest{};
void __fastcall MouseButton(void* input,void*,unsigned msg,unsigned,int x,int y) {
    auto w=Find(input,true);if(!w || msg!=WM_LBUTTONUP)return;
    POINT p{x,y};for(const auto& b:w->buttons)if(b.enabled && PtInRect(&b.rect,p)){
        if(b.id==20 && quote.token){Enqueue({5,quotedRequest.quest,quotedRequest.kind,quotedRequest.target,quotedRequest.map,5,quote.token});quote={};Dirty();}
        else Click(*w,b.id);return;
    }
    if(w->tracker || quote.token)return;
    if(x>=9 && x<240 && y>=61 && y<381) {
        int i=listOffset+(y-61)/32;
        if(i<int(quests.size()))SelectQuest(quests[i].id,quests[i].label);
    } else if(x>=265 && x<588 && y>=63 && y<321) {
        int i=detailOffset+(y-63)/43;if(i<int(Rows().size())){selectedRow=i;Dirty();}
    }
}
bool InField() {
    void* stage=*reinterpret_cast<void**>(0x00BEDED4);if(!stage)return false;
    void* input=static_cast<unsigned char*>(stage)+4;
    using Kind=int(__thiscall*)(void*,const void*);
    return Method<Kind>(input,0x48)(input,reinterpret_cast<void*>(0x00BED758))!=0;
}
void __fastcall Update(void* self,void*) {
    auto w=Find(self);if(!w)return;
    reinterpret_cast<NativeVoid>(0x009E067E)(self);
    if(!InField()) {Hide(mainWindow);Hide(trackerWindow);queue.clear();pending.clear();quote={};trackedQuest=selectedQuest=character=0;return;}
    // Both native windows can update in the same frame. Only one request is dispatched every 150 ms.
    DWORD now=GetTickCount();
    if(now-lastRefresh>=2000){lastRefresh=now;
        if(mainWindow.shown && view==1 && !quote.token)Detail();
        if(trackedQuest)Enqueue({1,trackedQuest,0,0,0,7});
    }
    if(mainWindow.shown && !quote.token && now-lastListRefresh>=10000){lastListRefresh=now;List();}
    if(quote.token && now-quoteTime>=30000){quote={};message=Text(L"quote");Dirty();}
    for(auto it=pending.begin();it!=pending.end();)if(now-it->second.sent>10000)it=pending.erase(it);else++it;
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
        w.mainMethods[0]=reinterpret_cast<void*>(Update);w.mainMethods[11]=reinterpret_cast<void*>(Draw);
        w.inputMethods[0]=reinterpret_cast<void*>(Key);w.inputMethods[2]=reinterpret_cast<void*>(MouseButton);
        w.inputMethods[3]=reinterpret_cast<void*>(MouseMove);w.inputMethods[4]=reinterpret_cast<void*>(Wheel);
        *reinterpret_cast<void***>(w.native)=w.mainMethods.data();*reinterpret_cast<void***>(w.native+4)=w.inputMethods.data();w.initialized=true;
        w.referenceDestructor=reinterpret_cast<void*>(DeleteReference);
        *reinterpret_cast<void***>(w.native+8)=&w.referenceDestructor;
    }
    int x=w.tracker?(std::max)(0,Client::m_nGameWidth-233):(std::max)(0,(Client::m_nGameWidth-Width)/2);
    int y=w.tracker?90:(std::max)(0,(Client::m_nGameHeight-Height)/2);
    w.shown=true;create(w.native,x,y,w.tracker?223:Width,w.tracker?(w.collapsed?25:170):Height,10,1,nullptr,w.tracker?0:1);Invalidate(w);
}
}
bool QuestHelperUI::Install(bool value,const char* config) {
    if(!value)return true;
    // The ABI is specific to the locally verified v83 executable.
    if(GetModuleHandleW(nullptr)!=reinterpret_cast<HMODULE>(0x400000) ||
        memcmp(reinterpret_cast<void*>(0x9DE383),"\xB8\x4A\x68\xAE\x00",5) ||
        memcmp(reinterpret_cast<void*>(0x9E00AF),"\xB8\x48\x6C\xAE\x00",5) ||
        memcmp(reinterpret_cast<void*>(0x9E0502),"\xB8\x94\x6C\xAE\x00",5) ||
        memcmp(reinterpret_cast<void*>(0x9E04C9),"\x33\xC0\x39\x44\x24\x04",6) ||
        *reinterpret_cast<unsigned*>(0x00B3F078+11*4)!=0x009E0502 ||
        memcmp(reinterpret_cast<void*>(0x9DE4D2),"\xB8\xD8\x68\xAE\x00",5))return false;
    wchar_t path[MAX_PATH];GetModuleFileNameW(nullptr,path,MAX_PATH);labelFile=path;
    labelFile=labelFile.substr(0,labelFile.find_last_of(L"\\/"))+L"\\QuestHelperUI.ini";
    if(GetFileAttributesW(labelFile.c_str())==INVALID_FILE_ATTRIBUTES)return false;
    language=Client::SwitchChinese?L"zh-CN":L"en-US";
    font=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"SimSun");
    boldFont=CreateFontW(-12,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"SimSun");
    trackerWindow.tracker=true;enabled=true;return true;
}
bool QuestHelperUI::Enabled(){return enabled;}
void QuestHelperUI::Open(){if(!enabled || !InField())return;Show(mainWindow);message=Text(L"loading");lastListRefresh=GetTickCount();List();Detail();}
bool QuestHelperUI::Receive(const unsigned char* data,size_t size) {
    if(!enabled)return true;
    Snapshot s;if(!QuestProtocol::Parse(data,size,s))return true;
    auto found=pending.find(s.sequence);if(found==pending.end())return true;
    Request r=found->second.request;pending.erase(found);
    if(character && character!=s.character) {queue.clear();pending.clear();quests.clear();detailRows.clear();trackRows.clear();trackedQuest=selectedQuest=0;quote={};Hide(trackerWindow);}
    character=s.character;meso=s.meso;
    if(s.view==6){if(r.quest && r.quest!=(r.purpose==7?trackedQuest:selectedQuest))return true;message=s.title;quote={};if(r.purpose==7){trackedQuest=0;Hide(trackerWindow);}else if(r.purpose==1){selectedQuest=0;detailRows.clear();List();}Dirty();return true;}
    if(r.purpose==0 && s.view==0 && r.target==category && r.map==listPage){quests=std::move(s.rows);questTotal=s.total;}
    else if(r.purpose==7 && s.view==1 && s.quest==trackedQuest){trackRows=std::move(s.rows);trackTitle=s.title;}
    else if(s.quest==selectedQuest) {
        if(s.view==1 && view==1 && r.map==detailPage && !quote.token){detailRows=std::move(s.rows);detailTotal=s.total;title=s.title;message.clear();}
        else if(s.view==2 && r.kind==sourceKind && r.target==sourceTarget && r.map==sourcePage){sourceRows=std::move(s.rows);sourceTotal=s.total;view=2;detailOffset=0;selectedRow=-1;message.clear();}
        else if(s.view==3 && r.map==routeMap && r.token==routePage){routeRows=std::move(s.rows);routeTotal=s.total;view=3;detailOffset=0;selectedRow=-1;message=s.message;}
        else if(s.view==4){quote=std::move(s);quotedRequest=r;quoteTime=GetTickCount();}
        else if(s.view==5){quote={};message=s.message;view=1;Detail();List();}
    }
    Dirty();return true;
}
void QuestHelperUI::FieldChange(){if(!enabled)return;quote={};queue.clear();pending.clear();if(mainWindow.shown)Detail();if(trackedQuest)Enqueue({1,trackedQuest,0,0,0,7});Dirty();}
