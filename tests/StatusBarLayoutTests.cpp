#include <string>
#include <cassert>
#include <cwchar>
#include <cstdio>
#include "StatusBarLayout.h"
#include "Client.h"

int Client::m_nGameWidth = 1920;
namespace {
struct Vector {
    void** methods;
    ULONG refs = 1;
    IUnknown* parent = nullptr;
    int x = 0, y = 0;
};
void* methods[37]{};
int created = 0, destroyed = 0, failAt = 0;
ULONG __stdcall AddRef(Vector* p) { return ++p->refs; }
ULONG __stdcall Release(Vector* p) {
    ULONG refs = --p->refs;
    if (!refs) {
        if (p->parent) p->parent->Release();
        ++destroyed; delete p;
    }
    return refs;
}
HRESULT __stdcall Origin(Vector* p, VARIANT v) {
    assert(v.vt == VT_UNKNOWN && v.punkVal);
    if (failAt == 2) return E_FAIL;
    assert(!p->parent);
    p->parent = v.punkVal; p->parent->AddRef();
    return S_OK;
}
HRESULT __stdcall Move(Vector* p, int x, int y, VARIANT a, VARIANT b) {
    assert(a.vt == VT_EMPTY && b.vt == VT_EMPTY);
    if (failAt == 3) return E_FAIL;
    p->x = x; p->y = y; return S_OK;
}
HRESULT __cdecl Create(const wchar_t* name, const GUID* iid, IUnknown** result, IUnknown* outer) {
    assert(!wcscmp(name,L"Shape2D#Vector2D") && iid->Data1 == 0xf28bd1ed && !outer);
    if (failAt == 1) return E_FAIL;
    ++created; *result = reinterpret_cast<IUnknown*>(new Vector{methods}); return S_OK;
}
}

static void TestNativeShape2D(const char* clientDir) {
    char cwd[MAX_PATH]; assert(GetCurrentDirectoryA(MAX_PATH,cwd));
    assert(SetCurrentDirectoryA(clientDir));
    assert(SetDllDirectoryA(clientDir));
    HMODULE pcom = LoadLibraryA("PCOM.dll"); assert(pcom);
    auto init = reinterpret_cast<HRESULT (__cdecl*)()>(GetProcAddress(pcom,"PcInitModule"));
    auto term = reinterpret_cast<void (__cdecl*)()>(GetProcAddress(pcom,"PcTermModule"));
    auto create = reinterpret_cast<StatusBarLayout::CreateObject>(GetProcAddress(pcom,"PcCreateObject"));
    assert(init && term && create && SUCCEEDED(init()));
    const GUID iid = {0xf28bd1ed,0x3deb,0x4f92,{0x9e,0xec,0x10,0xef,0x5a,0x1c,0x3f,0xb4}};
    IUnknown* root = nullptr;
    assert(SUCCEEDED(create(L"Shape2D#Vector2D",&iid,&root,nullptr)) && root);
    using MoveVector = HRESULT (__stdcall*)(IUnknown*,int,int,VARIANT,VARIANT);
    using GetCoord = HRESULT (__stdcall*)(IUnknown*,int*);
    auto move = reinterpret_cast<MoveVector>((*reinterpret_cast<void***>(root))[0x90/4]);
    VARIANT empty{}; assert(SUCCEEDED(move(root,10,20,empty,empty)));
    auto result = root; root->AddRef();
    assert(StatusBarLayout::CenterOrigin(&result,0x008D15EE,320,create));
    auto getX = reinterpret_cast<GetCoord>((*reinterpret_cast<void***>(result))[8]);
    auto getY = reinterpret_cast<GetCoord>((*reinterpret_cast<void***>(result))[10]);
    int x=0,y=0;
    assert(SUCCEEDED(getX(result,&x)) && x == 330);
    assert(SUCCEEDED(getY(result,&y)) && y == 20);
    assert(SUCCEEDED(move(root,100,200,empty,empty)));
    using PutTime = HRESULT (__stdcall*)(IUnknown*,int);
    auto putTime = reinterpret_cast<PutTime>((*reinterpret_cast<void***>(result))[23]);
    // Native vectors cache their absolute coordinates during one render tick.
    // Advance both clocks as Gr2D does between frames.
    assert(SUCCEEDED(putTime(root,1)) && SUCCEEDED(putTime(result,1)));
    assert(SUCCEEDED(getX(result,&x)) && x == 420);
    assert(SUCCEEDED(getY(result,&y)) && y == 200);
    result->Release(); root->Release();
    term(); FreeLibrary(pcom);
    assert(SetDllDirectoryA(nullptr)); assert(SetCurrentDirectoryA(cwd));
    puts("PASS: actual client PCOM.dll / Shape2D.dll, translated origin follows parent and releases cleanly.");
}

int main(int argc, char** argv) {
    static_assert(sizeof(void*) == 4, "Native fixture requires x86");
    methods[1] = reinterpret_cast<void*>(&AddRef); methods[2] = reinterpret_cast<void*>(&Release);
    methods[0x64/4] = reinterpret_cast<void*>(&Origin); methods[0x90/4] = reinterpret_cast<void*>(&Move);
    const uintptr_t callers[] = {0x008D15EE,0x008D1E8E,0x008DEB75,0x008DEE11};
    for (int width : {800,1280,1920,2560,3840}) for (auto caller : callers) {
        for (failAt=0; failAt<4; ++failAt) {
            auto parent = new Vector{methods}; ++created;
            auto result = reinterpret_cast<IUnknown*>(parent); result->AddRef();
            int left = StatusBarLayout::Left(width);
            bool changed = StatusBarLayout::CenterOrigin(&result,caller,left,Create);
            assert(changed == (left > 0 && failAt == 0));
            if (changed) {
                auto child = reinterpret_cast<Vector*>(result);
                assert(child->parent == reinterpret_cast<IUnknown*>(parent));
                assert(child->x == left && child->y == 0 && child->refs == 1);
                // Parent motion is inherited, rather than copied once.
                parent->x = -width/2; assert(parent->x+child->x == -640);
            } else assert(result == reinterpret_cast<IUnknown*>(parent));
            assert(parent->refs == 2);
            result->Release(); assert(parent->refs == 1);
            reinterpret_cast<IUnknown*>(parent)->Release();
            assert(created == destroyed);
        }
    }
    failAt = 0;
    auto parent = new Vector{methods}; ++created;
    auto result = reinterpret_cast<IUnknown*>(parent);
    for (uintptr_t caller : {0x009E0479,0x005F481E,0x008D3ADF,0x00400000}) {
        int before = created;
        assert(!StatusBarLayout::CenterOrigin(&result,caller,320,Create));
        assert(created == before && result == reinterpret_cast<IUnknown*>(parent));
    }
    assert(!StatusBarLayout::CenterOrigin(nullptr,0x008D15EE,320,Create));
    assert(!StatusBarLayout::CenterOrigin(&result,0x008D15EE,320,nullptr));
    result->Release(); assert(created == destroyed);
    puts("PASS: status bar origins, parent motion, original ABI slots, ownership, all allocation/COM failures, non-HUD callers unchanged.");
    if (argc == 2) TestNativeShape2D(argv[1]);
}
