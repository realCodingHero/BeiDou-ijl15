#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>

class CustomInventoryTab {
public:
    enum TabType {
        TAB_EQUIP = 0,   // Equip (Normal)
        TAB_USE = 1,     // Use
        TAB_SETUP = 2,   // Setup
        TAB_ETC = 3,     // Etc
        TAB_CASH = 4,    // Cash
        TAB_DECOR = 5    // Decor (Cash Equip)
    };

    static void Initialize();
    static bool IsEnabled();
    static void SetEnabled(bool enabled);

    static bool IsCashEquip(int nItemId, uint64_t liCashSN = 0);
    static int GetCurrentTab();
    static void SetCurrentTab(int nTab);

    static int MapVirtualToRealEquipSlot(int nVirtualSlot, bool isDecorTab);
    static int MapRealToVirtualEquipSlot(int nRealSlot, bool isDecorTab);
    static void RefreshEquipSlotCache();

private:
    static bool s_bEnabled;
    static int s_nCurrentTab;
    static std::vector<int> s_vecNormalEquipSlots;
    static std::vector<int> s_vecCashEquipSlots;
};
