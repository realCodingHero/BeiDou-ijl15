#include "stdafx.h"
#include "BossHP.h"
#include "BossHpNumber.h"

namespace {
// BeiDou v83 CField::ShowMobHpTag. The native function recreates its canvas
// on each update and releases the layer at +0x1E4 when HP reaches zero.
using ShowTag=void(__fastcall*)(void*,void*,unsigned,int,int,int,int);
ShowTag original=reinterpret_cast<ShowTag>(0x005336CA);
void __fastcall Show(void* field,void* edx,unsigned mob,int color,int background,int hp,int maximum) {
    original(field,edx,mob,color,background,hp,maximum);
    if(field && hp>0 && maximum>0)
        BossHpNumber::Draw(*reinterpret_cast<void**>(static_cast<unsigned char*>(field)+0x1E4),hp,maximum);
}
}
void BossHP::Hook() {
    Memory::SetHook(true,reinterpret_cast<void**>(&original),Show);
}
