#include "stdafx.h"
#include "HelpMenu.h"
#include "detours.h"
#include <cstring>

namespace {
// BeiDou.exe SHA256: 1198fa57ca5a7c489bae43ec13c69681d9cabe0f96762f3dc0357facf2e7d4df.
// Preserve the native CUIShortCut allocation, controls, modal loop and ZRef cleanup.
constexpr int kHelpButton = 1001;
constexpr int kShortCutButton = 1007;
constexpr int kNativeHeight = 271;
constexpr int kEntryCount = 4;
constexpr int kHelpHeight = 162;
constexpr const wchar_t* kButtonPaths[] = {
    L"UI/HelpMenu.img/BtHelper", L"UI/HelpMenu.img/BtAuction",
    L"UI/HelpMenu.img/BtQuestHelper", L"UI/HelpMenu.img/BtTeleport"
};
thread_local bool g_helpMode = false;
thread_local void* g_helpWindow = nullptr;
thread_local unsigned char g_action = 0;

using Click = void(__fastcall*)(void*, void*, int);
using SlideInfo = void(__fastcall*)(void*, void*, int, int, int, int, int, int, int, int, int, int, int, int);
using SlideCreate = void(__fastcall*)(void*, void*, int, int, const wchar_t*, int, int, void*);
using ButtonCreate = void(__fastcall*)(void*, void*, void*, int, int, int, int, void*);
using StringW = void*(__fastcall*)(void*, void*, void*, unsigned int);
using Key = void(__fastcall*)(void*, void*, unsigned int, unsigned int);
struct OutPacket {
    int loopback;
    unsigned char* data;
    unsigned long size;
    unsigned int offset;
    int encryptedByShanda;
};
static_assert(sizeof(OutPacket) == 20, "v83 packet layout requires x86");
using Send = void(__fastcall*)(void*, void*, OutPacket*);
using Assign = void*(__fastcall*)(void*, void*, const wchar_t*, int);
using Highlight = void(__fastcall*)(void*, void*);
Click s_statusClick = reinterpret_cast<Click>(0x008D423D);
Click s_menuClick = reinterpret_cast<Click>(0x0084AEA0);
SlideInfo s_slideInfo = reinterpret_cast<SlideInfo>(0x0051F93D);
SlideCreate s_slideCreate = reinterpret_cast<SlideCreate>(0x0051F9AD);
ButtonCreate s_buttonCreate = reinterpret_cast<ButtonCreate>(0x004BFFFB);
StringW s_stringW = reinterpret_cast<StringW>(0x00406276);
Key s_menuKey = reinterpret_cast<Key>(0x0084AD3A);
void** s_socket = reinterpret_cast<void**>(0x00BE7914);
Send s_send = reinterpret_cast<Send>(0x0049637B);
Assign s_assign = reinterpret_cast<Assign>(0x0041C06C);
Highlight s_highlight = reinterpret_cast<Highlight>(0x0084AF14);

int& WindowInt(void* window, size_t offset) {
    return *reinterpret_cast<int*>(static_cast<unsigned char*>(window) + offset);
}

void SendAction(unsigned char action) {
    if (action < 1 || action > kEntryCount) return;
    void* socket = *s_socket;
    if (!socket) return;
    unsigned char bytes[] = {0x02, 0x10, 0x01, action};
    OutPacket packet = {0, bytes, sizeof(bytes), 0, 0};
    s_send(socket, nullptr, &packet);
}

struct HelpScope {
    HelpScope() { g_action = 0; g_helpWindow = nullptr; g_helpMode = true; }
    ~HelpScope() { g_helpMode = false; g_helpWindow = nullptr; g_action = 0; }
};

void __fastcall StatusClick(void* self, void* edx, int id) {
    if (id != kHelpButton) {
        s_statusClick(self, edx, id);
        return;
    }
    if (g_helpMode) return;
    unsigned char action = 0;
    {
        HelpScope scope;
        s_statusClick(self, edx, kShortCutButton);
        action = g_action;
    }
    // Only dispatch after DoModal has returned and the native popup was destroyed.
    SendAction(action);
}

void __fastcall SetSlideInfo(void* self, void* edx, int a0, int a1, int a2,
    int x0, int y0, int x1, int y1, int x2, int y2, int t0, int t1, int t2) {
    if (g_helpMode && !g_helpWindow &&
        *reinterpret_cast<void**>(self) == reinterpret_cast<void*>(0x00B39BF8)) {
        g_helpWindow = self;
        // Original Help and ShortCut buttons are 112 pixels apart. Keep the
        // bottom edge aligned, including Client::UpdateResolution's native Y patch.
        x0 -= 112; x1 -= 112; x2 -= 112;
        y0 += kNativeHeight - kHelpHeight;
        y1 += kNativeHeight - kHelpHeight;
        y2 += kNativeHeight - kHelpHeight;
    }
    s_slideInfo(self, edx, a0, a1, a2, x0, y0, x1, y1, x2, y2, t0, t1, t2);
}

void __fastcall CreateSlide(void* self, void* edx, int width, int height,
    const wchar_t* background, int z, int screenCoordinates, void* data) {
    if (g_helpMode && self == g_helpWindow) {
        height = kHelpHeight;
        background = L"UI/HelpMenu.img/backgrnd";
    }
    s_slideCreate(self, edx, width, height, background, z, screenCoordinates, data);
}

void* __fastcall GetStringW(void* self, void* edx, void* result, unsigned int index) {
    void* value = s_stringW(self, edx, result, index);
    if (g_helpMode && g_helpWindow && index >= 0x8B4 && index < 0x8B4 + kEntryCount) {
        // Assign through ZXString<wchar_t>; a literal pointer has no native
        // reference-count header and cannot be substituted directly.
        s_assign(result, nullptr, kButtonPaths[index - 0x8B4], -1);
    }
    return value;
}

void __fastcall CreateButton(void* self, void* edx, void* parent, int id,
    int x, int y, int argument, void* params) {
    if (g_helpMode && parent == g_helpWindow && id >= 1000 + kEntryCount && id <= 1007) {
        // Native OnCreate/destruction/highlight logic requires all eight ZRefs.
        // Initialize unused controls below the screen; only the first four can
        // receive mouse or keyboard input in this popup.
        y += 4096;
    }
    s_buttonCreate(self, edx, parent, id, x, y, argument, params);
}

void __fastcall MenuClick(void* self, void* edx, int id) {
    if (!g_helpMode || self != g_helpWindow) {
        s_menuClick(self, edx, id);
        return;
    }
    if (id >= 1000 && id < 1000 + kEntryCount) {
        g_action = static_cast<unsigned char>(id - 999);
        // Cancel native ShortCut dispatch; our status-bar wrapper sends the
        // selected server action after the modal loop and cleanup finish.
        s_menuClick(self, edx, 2);
    } else if (id == 2) {
        g_action = 0;
        s_menuClick(self, edx, 2);
    }
}

void __fastcall MenuKey(void* input, void* edx, unsigned int key, unsigned int flags) {
    void* window = static_cast<unsigned char*>(input) - 4;
    if (!g_helpMode || window != g_helpWindow) {
        s_menuKey(input, edx, key, flags);
        return;
    }
    if (flags & 0x80000000u) return;
    if (key == VK_ESCAPE) {
        MenuClick(window, nullptr, 2);
    } else if (key == VK_RETURN || key == VK_SPACE) {
        int selected = WindowInt(window, 0x118);
        if (selected >= 0 && selected < kEntryCount) MenuClick(window, nullptr, 1000 + selected);
    } else if (key == VK_UP || key == VK_DOWN || key == VK_TAB) {
        WindowInt(window, 0x114) = 1;
        int& selected = WindowInt(window, 0x118);
        if (selected < 0 || selected >= kEntryCount) {
            selected = key == VK_UP ? kEntryCount - 1 : 0;
        } else {
            selected = (selected + (key == VK_UP ? kEntryCount - 1 : 1)) % kEntryCount;
        }
        s_highlight(window, nullptr);
    }
}

bool Matches(DWORD address, const char* expected, size_t size) {
    return std::memcmp(reinterpret_cast<void*>(address), expected, size) == 0;
}
}

bool HelpMenu::Install(bool enabled) {
    static bool installed = false;
    if (!enabled || installed) return true;
    if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x00400000) ||
        !Matches(0x008D423D, "\x56\x57\x8B\xF1\x8B\x0D\x98\xBF", 8) ||
        !Matches(0x0084AEA0, "\x8B\x44\x24\x04\x3D\xE8\x03\x00", 8) ||
        !Matches(0x0051F93D, "\x55\x8B\xEC\x8B\x45\x08\x89\x41", 8) ||
        !Matches(0x0051F9AD, "\xB8\x4C\x93\xA8\x00", 5) ||
        !Matches(0x004BFFFB, "\xB8\x48\x43\xA8\x00", 5) ||
        !Matches(0x00406276, "\x55\x8B\xEC\x51\x83\x65\xFC\x00", 8) ||
        !Matches(0x0084AD3A, "\x8B\x44\x24\x08\xA9\x00\x00\x00", 8)) {
        OutputDebugStringA("[HelpMenu] Unsupported executable; hooks not installed.\n");
        return false;
    }
    if (DetourTransactionBegin() != NO_ERROR) return false;
    LONG result = DetourUpdateThread(GetCurrentThread());
    struct Hook { PVOID* target; PVOID replacement; };
    Hook hooks[] = {
        {reinterpret_cast<PVOID*>(&s_statusClick), reinterpret_cast<PVOID>(StatusClick)},
        {reinterpret_cast<PVOID*>(&s_slideInfo), reinterpret_cast<PVOID>(SetSlideInfo)},
        {reinterpret_cast<PVOID*>(&s_slideCreate), reinterpret_cast<PVOID>(CreateSlide)},
        {reinterpret_cast<PVOID*>(&s_stringW), reinterpret_cast<PVOID>(GetStringW)},
        {reinterpret_cast<PVOID*>(&s_buttonCreate), reinterpret_cast<PVOID>(CreateButton)},
        {reinterpret_cast<PVOID*>(&s_menuClick), reinterpret_cast<PVOID>(MenuClick)},
        {reinterpret_cast<PVOID*>(&s_menuKey), reinterpret_cast<PVOID>(MenuKey)}
    };
    for (const Hook& hook : hooks) {
        if (result == NO_ERROR) result = DetourAttach(hook.target, hook.replacement);
    }
    if (result != NO_ERROR) { DetourTransactionAbort(); return false; }
    installed = DetourTransactionCommit() == NO_ERROR;
    OutputDebugStringA(installed ? "[HelpMenu] Native help popup enabled.\n" : "[HelpMenu] Hook transaction failed.\n");
    return installed;
}
