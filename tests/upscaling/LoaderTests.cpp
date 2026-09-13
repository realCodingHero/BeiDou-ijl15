#include "UpscaleLoader.h"
#include <filesystem>
#include <cstdio>
void Require(bool ok, const char* what) { if (!ok) { printf("FAIL loader: %s\n",what);exit(1); } }
int main() {
    wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);
    const auto directory=std::filesystem::path(exe).parent_path();
    Require(directory.filename()==L"fixture","isolated directory");
    const auto custom=directory/L"BeiDouUpscale.dll";
    std::filesystem::copy_file(directory.parent_path()/L"BeiDouUpscale.dll",custom,std::filesystem::copy_options::overwrite_existing);
    HMODULE native=LoadLibraryA("d3d8.dll");Require(native!=nullptr,"system D3D8");
    Require(UpscaleLoader::Install(GetModuleHandle(nullptr),true),"install");
    HMODULE unrelated=LoadLibraryA("d3d8.dll");Require(unrelated==native,"unrelated caller stays native");FreeLibrary(unrelated);
    HMODULE fixture=LoadLibraryW((directory/L"Gr2D_DX8.dll").c_str());Require(fixture!=nullptr,"fixture caller");
    auto load=reinterpret_cast<HMODULE(WINAPI*)()>(GetProcAddress(fixture,"_LoadGraphics@0"));Require(load!=nullptr,"fixture export");
    HMODULE redirected=load();Require(redirected!=nullptr && redirected!=native,"renderer redirected");
    wchar_t actual[MAX_PATH]{};GetModuleFileNameW(redirected,actual,MAX_PATH);
    Require(std::filesystem::path(actual).filename()==L"BeiDouUpscale.dll","dedicated module name");
    FreeLibrary(redirected);
    std::filesystem::remove(custom);
    HMODULE fallback=load();Require(fallback==native,"missing module native fallback");FreeLibrary(fallback);
    Require(UpscaleLoader::Install(nullptr,false),"uninstall");
    fallback=load();Require(fallback==native,"disabled native fallback");FreeLibrary(fallback);
    FreeLibrary(fixture);FreeLibrary(native);
    puts("PASS renderer-only loader: dedicated DLL, unrelated caller, missing module, detach");
}
