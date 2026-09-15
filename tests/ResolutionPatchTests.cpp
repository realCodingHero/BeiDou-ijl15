#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "Client.h"
#include "ResolutionPatch.h"
#include "ResolutionPatchSites.h"
#include "StatusBarLayout.h"
#include "AdaptiveLayout.h"

using namespace ResolutionPatch;
static unsigned char* mapped;
static std::vector<unsigned char> original;
static unsigned char* At(uint32_t va) { return mapped+va-kImageBase; }
static std::vector<unsigned char> Snapshot() { return {mapped, mapped+kImageSize}; }
static DWORD Protection(uint32_t va) {
    MEMORY_BASIC_INFORMATION info{};
    assert(VirtualQuery(At(va), &info, sizeof(info)));
    return info.Protect;
}
static void Change(uint32_t va, unsigned char value) {
    DWORD old, ignored;
    assert(VirtualProtect(At(va), 1, PAGE_EXECUTE_READWRITE, &old));
    *At(va) = value;
    assert(VirtualProtect(At(va), 1, old, &ignored));
}
static void Reset() {
    DWORD old, ignored;
    assert(VirtualProtect(mapped, kImageSize, PAGE_READWRITE, &old));
    memcpy(mapped, original.data(), original.size());
    assert(VirtualProtect(mapped, kImageSize, PAGE_EXECUTE_READ, &ignored));
    SetImageForTesting(mapped);
    FailAfterWritesForTesting(-1);
}
static bool Apply(unsigned width, unsigned height) {
    Client::m_nGameWidth = width; Client::m_nGameHeight = height;
    Client::m_nWindowWidth = width*2; Client::m_nWindowHeight = height*2;
    Client::m_bEnableScaling = true;
    return Client::UpdateResolution(); // Actual production body, not a mirrored list.
}
static void ExecuteRegressionInstructions() {
    unsigned char* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE));
    assert(code);
    // cmp ecx,height with EAX deliberately equal to the crash dump's invalid pointer.
    const unsigned char prefix[] = {0x8B,0x4C,0x24,0x04, 0xB8,0x93,0,0,0};
    const unsigned char suffix[] = {0x0F,0x9F,0xC0,0x0F,0xB6,0xC0,0xC2,0x04,0};
    memcpy(code, prefix, sizeof(prefix)); memcpy(code+sizeof(prefix), At(0x004D59B2), 6);
    memcpy(code+sizeof(prefix)+6, suffix, sizeof(suffix));
    // A real stack local must receive width without corrupting EBP-relative addressing.
    const unsigned char prologue[] = {0x55,0x8B,0xEC,0x83,0xEC,0x10};
    const unsigned char epilogue[] = {0x8B,0x45,0xF0,0xC9,0xC3};
    memcpy(code+64, prologue, sizeof(prologue)); memcpy(code+64+sizeof(prologue), At(0x004CC160), 7);
    memcpy(code+64+sizeof(prologue)+7, epilogue, sizeof(epilogue));
    DWORD old; assert(VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &old));
    assert(FlushInstructionCache(GetCurrentProcess(), code, 4096));
    auto compare = reinterpret_cast<unsigned (__stdcall*)(unsigned)>(code);
    assert(compare(1079) == 0 && compare(1080) == 0 && compare(1081) == 1);
    assert(reinterpret_cast<unsigned (__cdecl*)()>(code+64)() == 1920);
    assert(VirtualFree(code, 0, MEM_RELEASE));
}

static void ExecuteMapAndHotkeyInstructions(unsigned width, unsigned height) {
    auto code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE));
    assert(code);
    // Execute the actual EXE's short-map clamping block, including this patch.
    // ESI points 0xF0 bytes before our RECT, as it does in CMapLoadable.
    const unsigned char mapPrefix[] = {0x56,0x8B,0x74,0x24,0x08,0x81,0xEE,0xF0,0,0,0};
    memcpy(code, mapPrefix, sizeof(mapPrefix));
    const size_t mapBytes = 0x006420FE-0x006420B0;
    memcpy(code+sizeof(mapPrefix), At(0x006420B0), mapBytes);
    memcpy(code+sizeof(mapPrefix)+mapBytes, "\x5E\xC2\x04\x00", 4);
    auto clamp = reinterpret_cast<void (__stdcall*)(RECT*)>(code);

    // GetShortCutIndexByPos only calls PtInRect. Redirect that import slot;
    // retain the production table, bounds comparison, and local XY offsets.
    auto hotkey = code+256;
    memcpy(hotkey, At(0x008DE8D5), 0x008DE93D-0x008DE8D5);
    auto ptInRect = &::PtInRect;
    *reinterpret_cast<uintptr_t*>(hotkey+0x008DE918-0x008DE8D5) = reinterpret_cast<uintptr_t>(&ptInRect);
    auto hit = reinterpret_cast<int (__stdcall*)(int,int)>(hotkey);
    DWORD old; assert(VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &old));
    assert(FlushInstructionCache(GetCurrentProcess(), code, 4096));

    const RECT mapBounds[] = {
        {-2740,-748,809,179}, // reported flight map 200090510
        {-400,-300,400,300}, // compact map, both axes smaller than 1080p
        {-5000,-8000,5000,4000}, // tall/wide scrolling map
        {-200,100,200,200}, // very short map; negative camera center
        {-960,-540,960,540}, // exactly 1080p
        {-901,-301,0,300} // odd and negative coordinates
    };
    for (auto bounds : mapBounds) {
        RECT range{bounds.left+LONG(width/2), bounds.top+LONG(height/2),
                   bounds.right-LONG(width/2), bounds.bottom-LONG(height/2)};
        const RECT before = range;
        clamp(&range);
        if (before.left < before.right) assert(range.left == before.left && range.right == before.right);
        else assert(range.left == (before.left+before.right)/2 && range.right == range.left);
        if (before.top < before.bottom) assert(range.top == before.top && range.bottom == before.bottom);
        else if (height > 720) {
            assert(range.top == before.bottom && range.bottom == before.bottom);
            assert(range.bottom+LONG(height/2) == bounds.bottom);
        } else assert(range.top == (before.top+before.bottom)/2 && range.bottom == range.top);
    }
    assert(*reinterpret_cast<unsigned*>(At(0x008CFD51)) == StatusBarLayout::Width(width));
    const int left = StatusBarLayout::Left(width), top = int(height)-578;
    assert(*reinterpret_cast<int*>(At(0x00849E40)) == 666+left);
    assert(*reinterpret_cast<int*>(At(0x0084A5BE)) == 707+left);
    // Start from screen coordinates of the DRAWN keys, then let the native
    // handler consume coordinates local to the translated CWnd.
    for (int row=0; row<2; ++row) for (int col=0; col<13; ++col) {
        int x = left+815+7+35*col, y = int(height)-79+8+33*row;
        assert(hit(x-left, y-top) == row*13+col);
        assert(hit(x+31-left, y+31-top) == row*13+col);
        assert(hit(x+32-left, y-top) == -1); // gap between keys
    }
    // Empty space must terminate after the 26 entries, without following data.
    for (int y : {-100,0,100,498,506,573,578,2000})
        for (int x : {-100,0,814,815,1280,2000}) assert(hit(x,y) == -1);
    assert(memcmp(At(0x00642105), original.data()+0x00642105-kImageBase, 10) == 0);
    assert(VirtualFree(code, 0, MEM_RELEASE));
}


extern void AdaptiveBackground();
extern void AdaptiveLoginFrame();
extern DWORD backgroundLayoutResume, loginFrameResume;
static int observedX, observedY;
static DWORD observedEax, observedEbx;
__declspec(naked) void BackgroundReturnFixture() { __asm { ret } }
__declspec(naked) void LoginReturnFixture() {
    __asm { pop observedX }
    __asm { pop observedY }
    __asm { ret }
}
static void ExecuteLayoutCaves() {
    int storage[64]{};
    int* frame = storage+48;
    uintptr_t field = 0x00400000;
    frame[-0x48/4] = reinterpret_cast<int>(&field);
    frame[-0x74/4] = -226; frame[-0x70/4] = -13; frame[-0x68/4] = 0;
    DWORD resume = backgroundLayoutResume;
    backgroundLayoutResume = reinterpret_cast<DWORD>(&BackgroundReturnFixture);
    __asm {
        push ebp
        push ebx
        push esi
        mov ecx, frame
        mov ebp, ecx
        xor esi, esi
        mov eax, 0x12345678
        mov ebx, 0x76543210
        call AdaptiveBackground
        mov observedEax, eax
        mov observedEbx, ebx
        pop esi
        pop ebx
        pop ebp
    }
    backgroundLayoutResume = resume;
    assert(frame[-0x74/4] == -497);
    assert(frame[-0x34/4] == 0x12345678 && frame[-0x3c/4] == 0x76543210);
    assert(observedEax == 0x12345678 && observedEbx == 0x76543210);

    AdaptiveLayout::ConfigureLogin(true); BeiDouEnableLoginViewportV1();
    DWORD source = 0x98765432, destination = 0;
    resume = loginFrameResume;
    loginFrameResume = reinterpret_cast<DWORD>(&LoginReturnFixture);
    __asm {
        push esi
        push edi
        lea esi, source
        lea edi, destination
        mov eax, 0x12345678
        cld
        call AdaptiveLoginFrame
        mov observedEax, eax
        pop edi
        pop esi
    }
    loginFrameResume = resume;
    assert(observedX == -640 && observedY == -360);
    assert(destination == source && observedEax == 0x12345678);
    AdaptiveLayout::ConfigureLogin(false);
}

int main(int argc, char** argv) {
    const char* executable = nullptr;
    for (int i=1; i+1<argc; ++i) if (!strcmp(argv[i], "--exe")) executable = argv[i+1];
    assert(executable);
    std::ifstream file(executable, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    assert(bytes.size() > 1024);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
    assert(nt->OptionalHeader.SizeOfImage == kImageSize);
    mapped = static_cast<unsigned char*>(VirtualAlloc(nullptr, kImageSize, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE));
    assert(mapped);
    memcpy(mapped, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    const auto sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i=0; i<nt->FileHeader.NumberOfSections; ++i)
        memcpy(mapped+sections[i].VirtualAddress, bytes.data()+sections[i].PointerToRawData, sections[i].SizeOfRawData);
    original = Snapshot();
    Reset();
    const unsigned dimensions[][2] = {{800,600},{1280,720},{1920,1080},{2560,1440}};
    for (unsigned options=0; options<16; ++options) {
        Client::WindowedMode = (options&1) != 0;
        Client::RemoveLogos = (options&2) != 0;
        Client::CustomLoginFrame = (options&4) != 0;
        Client::bigLoginFrame = (options&8) != 0;
        for (const auto& size : dimensions) {
            assert(Apply(size[0], size[1]));
            if (!options) ExecuteMapAndHotkeyInstructions(size[0], size[1]);
            const auto first = Snapshot();
            assert(Apply(size[0], size[1]) && first == Snapshot());
            assert(Protection(0x004D59B2) == PAGE_EXECUTE_READ);
            assert(*At(0x004D59B2) == 0x81 && *At(0x004D59B3) == 0xF9);
            assert(*reinterpret_cast<unsigned*>(At(0x004D59B4)) == size[1]);
            assert(memcmp(At(0x004CC160), "\xC7\x45\xF0", 3) == 0);
            assert(*reinterpret_cast<unsigned*>(At(0x004CC163)) == size[0]);
            assert(memcmp(At(0x0064061D), original.data()+0x0064061D-kImageBase, 5) == 0);
            assert(memcmp(At(0x00A5FC2B), original.data()+0x00A5FC2B-kImageBase, 5) == 0);
        }
    }
    assert(Apply(1920,1080)); ExecuteRegressionInstructions(); ExecuteLayoutCaves();
    // Corruption late in the manifest must reject the whole batch without earlier writes.
    Reset(); Change(0x009F7B1D, 0x90);
    auto before = Snapshot(); assert(!Apply(1920,1080)); assert(before == Snapshot());
    Reset(); assert(Apply(1280,720)); Change(0x004D59B3, 0x38);
    before = Snapshot(); assert(!Apply(1920,1080)); assert(before == Snapshot());
    // Registry rejects the historic wrong offset even if the original code is intact.
    Reset(); Batch wrong; wrong.WriteInt(0x004D59B3,1080); std::string error;
    before = Snapshot(); assert(!wrong.Apply(error)); assert(before == Snapshot());
    assert(error.find("Unregistered resolution write") != std::string::npos);
    // Overlapping alternatives are only legal when one branch is selected.
    Batch overlap; overlap.WriteInt(0x005F464E,800); overlap.CodeCave(mapped,0x005F464D,10);
    assert(!overlap.Apply(error)); assert(before == Snapshot());
    assert(error.find("Overlapping resolution write") != std::string::npos);
    // Fail at several points after protection and verify complete byte/protection rollback.
    for (int fail : {0,1,80}) {
        Reset(); FailAfterWritesForTesting(fail); before = Snapshot();
        assert(!Apply(1920,1080)); assert(before == Snapshot());
        assert(Protection(0x004D59B2) == PAGE_EXECUTE_READ);
        FailAfterWritesForTesting(-1); assert(Apply(1920,1080));
    }
    Reset(); Change(kImageBase+dos->e_lfanew+8, 0);
    before = Snapshot(); assert(!Apply(1920,1080)); assert(before == Snapshot());
    assert(VirtualFree(mapped, 0, MEM_RELEASE));
    puts("PASS: real EXE, 64 resolution/layout combinations, map clamps, all 26 hotkeys and empty space at 4 resolutions, repeat/reconfigure, signatures, rollback, page protection, executed crash-site regressions.");
}
