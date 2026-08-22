#pragma once
#include <cstdint>
#include <vector>
#include <windows.h>

class CustomInventoryTab {
public:
    enum TabType {
        TAB_EQUIP = 0,   // Normal Equipment (isCash == false)
        TAB_USE = 1,     // Use
        TAB_SETUP = 2,   // Setup
        TAB_ETC = 3,     // Etc
        TAB_CASH = 4,    // Cash Consumables
        TAB_DECOR = 5    // Decor (Cash Equipment: isCash == true)
    };

    static void Initialize();
    static bool IsEnabled();
    static void SetEnabled(bool enabled);

    static bool IsCashEquip(int nItemId, uint64_t liCashSN = 0);
    static int GetCurrentTab();
    static void SetCurrentTab(int nTab);

    // Slot virtualization
    static int MapVirtualToRealEquipSlot(int nVirtualSlot, bool isDecorTab);
    static int MapRealToVirtualEquipSlot(int nRealSlot, bool isDecorTab);
    static void RefreshEquipSlotCache();

    // Window & Input Hooking
    static void AttachWindowHook(HWND hWnd);
    static LRESULT CALLBACK SubclassWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
    static bool s_bEnabled;
    static int s_nCurrentTab;
    static HWND s_hGameWnd;
    static WNDPROC s_pfnOrigWndProc;
    static std::vector<int> s_vecNormalEquipSlots;
    static std::vector<int> s_vecCashEquipSlots;

    static void DrawTabOverlay(HWND hWnd, HDC hdc);
};
