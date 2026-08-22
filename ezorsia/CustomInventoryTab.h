#pragma once
#include <cstdint>
#include <vector>
#include <windows.h>

class CustomInventoryTab {
public:
    static void Initialize();
    static bool IsEnabled();
    static void SetEnabled(bool enabled);

    static bool IsCashEquip(int nItemId, uint64_t liCashSN = 0);
    static bool IsDecorMode();
    static void SetDecorMode(bool bDecor);
    static void ToggleDecorMode();
    static void RefreshInventoryUI();

    // Hooks
    static int __fastcall Hook_FilterItem(void* pThis, void* edx, int nItemId, void* pExtra);
    static void __fastcall Hook_OnChildNotify(void* pThis, void* edx, uint32_t uId, uint32_t uMsg);

private:
    static bool s_bEnabled;
    static bool s_bDecorMode;
    static int s_nCurTab;
    static decltype(&Hook_FilterItem) s_pfnOrigFilterItem;
    static decltype(&Hook_OnChildNotify) s_pfnOrigOnChildNotify;
};
