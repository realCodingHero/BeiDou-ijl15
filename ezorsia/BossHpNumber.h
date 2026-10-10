#pragma once
#include <windows.h>
#include <oleauto.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>

// Draw on the canvas owned by the native HP tag. No independent tooltip,
// timer or stored field pointer: replacement, death and map teardown retain
// the native tag's lifetime. Called once after each native tag update.
namespace BossHpNumber {
inline std::string Format(std::uint64_t value) {
    const auto yi=value/100000000,wan=value/10000%10000,rest=value%10000;
    std::string text;
    if(yi) text+=std::to_string(yi)+"E";
    if(wan) text+=std::to_string(wan)+"W";
    if(rest || text.empty()) text+=std::to_string(rest);
    return text;
}
template<class T> inline T Method(void* p,unsigned offset) {
    return reinterpret_cast<T>((*static_cast<void***>(p))[offset/4]);
}
inline bool Draw(void* layer,int hp,int maximum) {
    if(!layer || hp<=0 || maximum<=0 || hp>maximum) return false;
    using Canvas=HRESULT(__stdcall*)(void*,VARIANT,void**);
    using Size=HRESULT(__stdcall*)(void*,unsigned*);
    using Rect=HRESULT(__stdcall*)(void*,int,int,unsigned,unsigned,unsigned);
    using Release=ULONG(__stdcall*)(void*);
    VARIANT index{}; index.vt=VT_I4; index.lVal=0;
    void* canvas=nullptr;
    // IWzGr2DLayer::get_canvas, IWzCanvas::{width,height,DrawRectangle}.
    if(FAILED(Method<Canvas>(layer,0x100)(layer,index,&canvas)) || !canvas) return false;
    unsigned width=0,height=0;
    bool ok=SUCCEEDED(Method<Size>(canvas,0x40)(canvas,&width)) &&
        SUCCEEDED(Method<Size>(canvas,0x48)(canvas,&height));
    const std::string text=Format(hp)+"/"+Format(maximum);
    const int length=int(text.size());
    // UIWindow/MobGage: the left boss portrait is 35x37; the gauge to
    // its right is 20px tall. Center in the gauge, not the portrait canvas.
    const int left=35;
    const int gaugeWidth=int(width)-left;
    const int gaugeHeight=height<20 ? int(height):20;
    const int scale=gaugeHeight>=18 && gaugeWidth>=length*12+4 ? 2:1;
    const int textWidth=(length*6-1)*scale,textHeight=7*scale;
    ok=ok && gaugeWidth>=textWidth+2 && gaugeHeight>=textHeight+2;
    // Compact pixel glyphs remain crisp in the game's small native HP strip.
    static const unsigned char glyph[13][7]={
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
        {1,1,2,4,8,16,16},
        {31,16,16,30,16,16,31}, // E: yi (100 million)
        {17,17,17,21,21,21,10}}; // W: wan (10 thousand)
    const int x=left+(gaugeWidth-textWidth)/2,y=(gaugeHeight-textHeight)/2;
    auto rectangle=Method<Rect>(canvas,0x8c);
    // Black outline first, then white foreground; no opaque label background.
    for(int pass=0;ok && pass<2;++pass) for(int i=0;ok && i<length;++i)
        for(int row=0;ok && row<7;++row) for(int col=0;ok && col<5;++col) {
            const int g=text[i]=='/' ? 10:text[i]=='E' ? 11:text[i]=='W' ? 12:text[i]-'0';
            if(!(glyph[g][row]&(16>>col))) continue;
            const int border=pass==0 ? 1:0;
            ok=SUCCEEDED(rectangle(canvas,x+i*6*scale+col*scale-border,
                y+row*scale-border,scale+border*2,scale+border*2,
                pass==0 ? 0xff000000u:0xffffffffu));
        }
    Method<Release>(canvas,8)(canvas);
    return ok;
}
}
