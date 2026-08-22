#include "stdafx.h"
#include "CustomInventoryTab.h"
#include "Memory.h"
#include "Client.h"

bool CustomInventoryTab::s_bEnabled = true;
bool CustomInventoryTab::s_bDecorMode = false;
int CustomInventoryTab::s_nCurTab = 0;
decltype(&CustomInventoryTab::Hook_FilterItem) CustomInventoryTab::s_pfnOrigFilterItem = reinterpret_cast<decltype(&CustomInventoryTab::Hook_FilterItem)>(0x008A2378);
decltype(&CustomInventoryTab::Hook_OnChildNotify) CustomInventoryTab::s_pfnOrigOnChildNotify = reinterpret_cast<decltype(&CustomInventoryTab::Hook_OnChildNotify)>(0x0089E185);

void CustomInventoryTab::Initialize() {
    if (!s_bEnabled) return;
    Memory::SetHook(true, reinterpret_cast<void**>(&s_pfnOrigFilterItem), Hook_FilterItem);
    Memory::SetHook(true, reinterpret_cast<void**>(&s_pfnOrigOnChildNotify), Hook_OnChildNotify);
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

bool CustomInventoryTab::IsDecorMode() {
    return s_bDecorMode;
}

void CustomInventoryTab::SetDecorMode(bool bDecor) {
    if (s_bDecorMode != bDecor) {
        s_bDecorMode = bDecor;
        RefreshInventoryUI();
    }
}

void CustomInventoryTab::ToggleDecorMode() {
    SetDecorMode(!s_bDecorMode);
}

void CustomInventoryTab::RefreshInventoryUI() {
    void* pUIItem = *reinterpret_cast<void**>(0x00BEDCD0);
    if (pUIItem) {
        // CUIItem::UpdateInventorySlots
        typedef void(__thiscall* UpdateSlots_t)(void* pThis);
        auto pfnUpdate = reinterpret_cast<UpdateSlots_t>(0x0089E020);
        pfnUpdate(pUIItem);
    }
}

int __fastcall CustomInventoryTab::Hook_FilterItem(void* pThis, void* edx, int nItemId, void* pExtra) {
    int res = s_pfnOrigFilterItem(pThis, edx, nItemId, pExtra);
    if (res != 0 && s_bEnabled) {
        // Check if item is Equipment
        int nType = nItemId / 1000000;
        if (nType == 1) {
            bool isCash = IsCashEquip(nItemId);
            if (s_bDecorMode) {
                // Decor Mode: only show cash equips
                return isCash ? 1 : 0;
            } else {
                // Normal Mode: only show non-cash equips
                return isCash ? 0 : 1;
            }
        }
    }
    return res;
}

void __fastcall CustomInventoryTab::Hook_OnChildNotify(void* pThis, void* edx, uint32_t uId, uint32_t uMsg) {
    s_pfnOrigOnChildNotify(pThis, edx, uId, uMsg);
}
