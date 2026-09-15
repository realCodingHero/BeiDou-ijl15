#include <windows.h>
namespace { bool ready = false, login = true; }
extern "C" void __cdecl BeiDouEnableLoginViewportV1() { ready = true; }
extern "C" BOOL __cdecl BeiDouQueryLoginViewportV1(UINT width, UINT height, RECT* rect) {
    if (!ready || !login || width != 64 || height != 48 || !rect) return FALSE;
    *rect = {16,12,48,36}; return TRUE;
}
extern "C" void __cdecl SetFixtureLogin(BOOL value) { login = value != FALSE; }
#pragma comment(linker, "/EXPORT:BeiDouEnableLoginViewportV1=_BeiDouEnableLoginViewportV1")
#pragma comment(linker, "/EXPORT:BeiDouQueryLoginViewportV1=_BeiDouQueryLoginViewportV1")
#pragma comment(linker, "/EXPORT:SetFixtureLogin=_SetFixtureLogin")
