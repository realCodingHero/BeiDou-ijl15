#include "stdafx.h"
#include "CustomInventoryTab.h"
#include "Memory.h"
#include "Client.h"

bool CustomInventoryTab::s_bEnabled = true;
int CustomInventoryTab::s_nCurrentTab = 0;
std::vector<int> CustomInventoryTab::s_vecNormalEquipSlots;
std::vector<int> CustomInventoryTab::s_vecCashEquipSlots;

void CustomInventoryTab::Initialize() {
    if (!s_bEnabled) return;
}

bool CustomInventoryTab::IsEnabled() {
    return s_bEnabled;
}

void CustomInventoryTab::SetEnabled(bool enabled) {
    s_bEnabled = enabled;
}

bool CustomInventoryTab::IsCashEquip(int nItemId, uint64_t liCashSN) {
    if (liCashSN > 0) {
        return true;
    }
    int nType = nItemId / 1000000;
    if (nType != 1) {
        return false;
    }
    int nSub = nItemId / 10000;
    // 170xxxx: Universal Cash Weapon
    if (nSub == 170) {
        return true;
    }
    // Classic Cash IDs: 1002xxx, 1012xxx, 1022xxx, 1032xxx, 1042xxx, 1052xxx, 1062xxx, 1072xxx, 1082xxx, 1102xxx, 1112xxx
    int nCategory = (nItemId / 1000) % 10;
    if (nCategory == 2 && nSub >= 100 && nSub <= 111) {
        return true;
    }
    return false;
}

int CustomInventoryTab::GetCurrentTab() {
    return s_nCurrentTab;
}

void CustomInventoryTab::SetCurrentTab(int nTab) {
    s_nCurrentTab = nTab;
}

int CustomInventoryTab::MapVirtualToRealEquipSlot(int nVirtualSlot, bool isDecorTab) {
    if (isDecorTab) {
        if (nVirtualSlot > 0 && nVirtualSlot <= static_cast<int>(s_vecCashEquipSlots.size())) {
            return s_vecCashEquipSlots[nVirtualSlot - 1];
        }
    } else {
        if (nVirtualSlot > 0 && nVirtualSlot <= static_cast<int>(s_vecNormalEquipSlots.size())) {
            return s_vecNormalEquipSlots[nVirtualSlot - 1];
        }
    }
    return nVirtualSlot;
}

int CustomInventoryTab::MapRealToVirtualEquipSlot(int nRealSlot, bool isDecorTab) {
    if (isDecorTab) {
        for (size_t i = 0; i < s_vecCashEquipSlots.size(); ++i) {
            if (s_vecCashEquipSlots[i] == nRealSlot) {
                return static_cast<int>(i + 1);
            }
        }
    } else {
        for (size_t i = 0; i < s_vecNormalEquipSlots.size(); ++i) {
            if (s_vecNormalEquipSlots[i] == nRealSlot) {
                return static_cast<int>(i + 1);
            }
        }
    }
    return nRealSlot;
}

void CustomInventoryTab::RefreshEquipSlotCache() {
    s_vecNormalEquipSlots.clear();
    s_vecCashEquipSlots.clear();
}
