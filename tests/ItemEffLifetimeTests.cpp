#include <windows.h>
#include <unknwn.h>
#include <cassert>
#include <cstdio>
#include <cstring>

struct Resource final : IUnknown {
    ULONG refs = 1;
    bool alive = true;
    unsigned releases = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { assert(alive); return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        assert(alive && refs); ++releases;
        if (!--refs) alive = false;
        return refs;
    }
};
int main(int argc, char** argv) {
    const char* dll = nullptr;
    for (int i=1;i+1<argc;++i) if (!strcmp(argv[i],"--dll")) dll=argv[i+1];
    assert(dll);
    for (unsigned run=0;run<20;++run) {
        HMODULE module=LoadLibraryA(dll); assert(module);
        auto remember=reinterpret_cast<void(__cdecl*)(int,IUnknown*)>(GetProcAddress(module,"TestRememberCape"));
        auto has=reinterpret_cast<bool(__cdecl*)(int)>(GetProcAddress(module,"TestHasCape"));
        assert(remember && has);
        Resource object;
        for (int id=1102000;id<1102407;++id) { remember(id,&object); assert(has(id)); }
        // The old global map retains 407 COM references here and fails this check.
        assert(object.refs==1 && object.releases==407);
        assert(object.Release()==0);
        const unsigned releases=object.releases;
        assert(FreeLibrary(module)); // CRT destroys the actual production catalog.
        assert(object.releases==releases); // No calls into the now-dead resource manager.
    }
    puts("PASS: actual ItemEff catalog retains no COM references; 407 resources, 20 DLL unload cycles after resource destruction.");
}
