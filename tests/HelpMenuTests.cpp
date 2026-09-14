// Exercise the real help hooks against an isolated native-ABI fixture. This
// checks modal cleanup/packet ordering without logging in or changing a player.
#include "../ezorsia/HelpMenu.cpp"
#include <stdexcept>

namespace {
int scenario, sent, nativeCalls, nativeId, modalResult, highlights;
bool destroyed, failModal;
int positionX, positionY, popupHeight, controlY;
unsigned char packetBytes[4];
unsigned char windowStorage[0x120];
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void __fastcall FakeSend(void* socket, void*, OutPacket* packet) {
    Require(destroyed && !g_helpMode && !g_helpWindow, "packet sent before native cleanup");
    Require(socket == reinterpret_cast<void*>(123) && packet->size == 4, "invalid packet ABI");
    Require(packet->loopback == 0 && packet->offset == 0 && packet->encryptedByShanda == 0, "invalid packet metadata");
    std::memcpy(packetBytes, packet->data, 4); ++sent;
}
void __fastcall FakeHighlight(void* self, void*) { Require(self == windowStorage, "wrong input subobject"); ++highlights; }
void __fastcall FakeClick(void*, void*, int id) { modalResult = id; }
void __fastcall FakeSlide(void*, void*, int, int, int, int x, int y, int, int, int, int, int, int, int) {
    positionX = x; positionY = y;
}
void __fastcall FakeCreate(void*, void*, int width, int height, const wchar_t* background, int, int, void*) {
    Require(width == 93 && wcscmp(background, L"UI/HelpMenu.img/backgrnd") == 0, "wrong popup skin");
    popupHeight = height;
}
void __fastcall FakeButton(void*, void*, void*, int, int, int y, int, void*) { controlY = y; }
void* __fastcall FakeString(void*, void*, void* result, unsigned int) {
    *static_cast<const wchar_t**>(result) = L"native";
    return result;
}
void* __fastcall FakeAssign(void* result, void*, const wchar_t* text, int length) {
    Require(length == -1, "resource path must use native string assignment");
    *static_cast<const wchar_t**>(result) = text;
    return result;
}
void __fastcall FakeStatus(void*, void*, int id) {
    ++nativeCalls; nativeId = id;
    if (!g_helpMode) return;
    if (failModal) throw std::runtime_error("fixture modal failure");
    Require(id == 1007, "native help/MTS path was used");
    std::memset(windowStorage, 0, sizeof(windowStorage));
    *reinterpret_cast<DWORD*>(windowStorage) = 0x00B39BF8;
    SetSlideInfo(windowStorage, nullptr, 0, 255, 0, 707, 1159, 707, 1159, 707, 1159, 500, 0, 500);
    Require(g_helpWindow == windowStorage && positionX == 595 && positionY == 1268, "resolution-relative popup position");
    CreateSlide(windowStorage, nullptr, 93, 271, L"original", 10, 1, nullptr);
    Require(popupHeight == 162, "popup height");
    const wchar_t* paths[] = {L"UI/HelpMenu.img/BtHelper", L"UI/HelpMenu.img/BtAuction",
        L"UI/HelpMenu.img/BtQuestHelper", L"UI/HelpMenu.img/BtTeleport"};
    for (int i = 0; i < 4; ++i) {
        CreateButton(nullptr, nullptr, windowStorage, 1000 + i, 6, 24 + 26 * i, 0, nullptr);
        Require(controlY == 24 + 26 * i, "visible button moved");
        const wchar_t* path = nullptr;
        Require(GetStringW(nullptr, nullptr, &path, 0x8B4 + i) == &path, "native string return changed");
        Require(wcscmp(path, paths[i]) == 0, "wrong resource for visible entry");
    }
    const wchar_t* path = nullptr;
    GetStringW(nullptr, nullptr, &path, 0x8BB);
    Require(wcscmp(path, L"native") == 0, "unused native resource was replaced");
    CreateButton(nullptr, nullptr, windowStorage, 1004, 6, 128, 0, nullptr);
    Require(controlY > 1440, "unused button remained visible");
    MenuClick(windowStorage, nullptr, 1004);
    Require(modalResult == 0 && g_action == 0, "hidden native entry dispatched");
    if (scenario >= 1 && scenario <= 4) MenuClick(windowStorage, nullptr, 999 + scenario);
    if (scenario == 5) MenuKey(windowStorage + 4, nullptr, VK_ESCAPE, 0);
    if (scenario == 6) {
        MenuKey(windowStorage + 4, nullptr, VK_DOWN, 0x80000000);
        Require(WindowInt(windowStorage, 0x118) == 0, "keyup changed selection");
        for (int i = 1; i <= 4; ++i) {
            MenuKey(windowStorage + 4, nullptr, VK_DOWN, 0);
            Require(WindowInt(windowStorage, 0x118) == i % 4, "down did not cycle all four entries");
        }
        MenuKey(windowStorage + 4, nullptr, VK_UP, 0);
        Require(WindowInt(windowStorage, 0x118) == 3, "up did not wrap to teleport");
        MenuKey(windowStorage + 4, nullptr, VK_SPACE, 0);
    }
    if (scenario == 7) {
        MenuKey(windowStorage + 4, nullptr, VK_DOWN, 0);
        MenuKey(windowStorage + 4, nullptr, VK_DOWN, 0);
        Require(WindowInt(windowStorage, 0x118) == 2, "keyboard cannot select quests");
        MenuKey(windowStorage + 4, nullptr, VK_RETURN, 0);
    }
    if (scenario == 8) {
        WindowInt(windowStorage, 0x118) = 4;
        MenuKey(windowStorage + 4, nullptr, VK_RETURN, 0);
        Require(modalResult == 0 && g_action == 0, "invalid keyboard selection dispatched");
        WindowInt(windowStorage, 0x118) = -1;
        MenuKey(windowStorage + 4, nullptr, VK_TAB, 0);
        Require(WindowInt(windowStorage, 0x118) == 0, "tab did not recover initial selection");
        for (int i = 1; i <= 4; ++i) {
            MenuKey(windowStorage + 4, nullptr, VK_TAB, 0);
            Require(WindowInt(windowStorage, 0x118) == i % 4, "tab selected a hidden entry");
        }
        MenuKey(windowStorage + 4, nullptr, VK_RETURN, 0);
    }
    Require(modalResult == 2, "native inventory dispatch was not cancelled");
    destroyed = true;
}
}
int main() {
    try {
        void* socket = reinterpret_cast<void*>(123);
        s_socket = &socket; s_send = FakeSend; s_highlight = FakeHighlight;
        s_statusClick = FakeStatus; s_menuClick = FakeClick; s_slideInfo = FakeSlide;
        s_slideCreate = FakeCreate; s_buttonCreate = FakeButton;
        s_stringW = FakeString; s_assign = FakeAssign;
        const unsigned char expectedActions[] = {1, 2, 3, 4, 0, 4, 3, 1};
        for (scenario = 1; scenario <= 8; ++scenario) {
            sent = 0; modalResult = 0; destroyed = false;
            StatusClick(nullptr, nullptr, 1001);
            Require(!g_helpMode && !g_helpWindow && !g_action, "stale menu state");
            Require(sent == (scenario == 5 ? 0 : 1), "cancel/selection packet count");
            if (sent) {
                Require(packetBytes[0] == 2 && packetBytes[1] == 0x10 && packetBytes[2] == 1, "wire opcode/version");
                Require(packetBytes[3] == expectedActions[scenario - 1], "wrong server action");
            }
        }
        sent = 0; int before = nativeCalls;
        StatusClick(nullptr, nullptr, 1007);
        Require(nativeCalls == before + 1 && nativeId == 1007 && sent == 0, "original interface menu changed");
        MenuClick(windowStorage, nullptr, 1004);
        Require(modalResult == 1004, "original native menu selection intercepted");
        const wchar_t* path = nullptr;
        GetStringW(nullptr, nullptr, &path, 0x8B6);
        Require(wcscmp(path, L"native") == 0, "original Interface resource replaced");
        CreateButton(nullptr, nullptr, windowStorage, 1004, 6, 128, 0, nullptr);
        Require(controlY == 128, "original Interface button hidden");
        SendAction(0); SendAction(5); SendAction(255);
        Require(sent == 0, "invalid action sent");
        failModal = true;
        try { StatusClick(nullptr, nullptr, 1001); } catch (const std::runtime_error&) { }
        Require(!g_helpMode && !g_helpWindow && !g_action && sent == 0, "exception left help mode active");
        Require(HelpMenu::Install(false), "disabled configuration failed");
        socket = nullptr; SendAction(1);
        Require(sent == 0, "request sent without a connected socket");
        std::cout << "PASS help menu: four actions/resources, native ABI, cleanup before send, cancel, four-entry keyboard wrapping, native menu isolation, failure cleanup\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL help menu: " << error.what() << "\n"; return 1;
    }
}
