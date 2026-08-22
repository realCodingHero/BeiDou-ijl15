#include "stdafx.h"
#include "CustomInventoryTab.h"
#include "Memory.h"
#include "Client.h"

bool CustomInventoryTab::s_bEnabled = true;
int CustomInventoryTab::s_nCurrentTab = 0;
HWND CustomInventoryTab::s_hGameWnd = nullptr;
WNDPROC CustomInventoryTab::s_pfnOrigWndProc = nullptr;
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

void CustomInventoryTab::AttachWindowHook(HWND hWnd) {
    if (!hWnd || s_hGameWnd == hWnd) return;
    s_hGameWnd = hWnd;
    s_pfnOrigWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SubclassWndProc)));
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

void CustomInventoryTab::DrawTabOverlay(HWND hWnd, HDC hdc) {
    void* pUIItem = *reinterpret_cast<void**>(0x00BEDCD0);
    if (!pUIItem || !hWnd) return;

    bool bOwnDC = false;
    if (!hdc) {
        hdc = GetDC(hWnd);
        bOwnDC = true;
    }
    if (!hdc) return;

    // Window coordinates
    int winX = *reinterpret_cast<int*>(reinterpret_cast<char*>(pUIItem) + 8);
    int winY = *reinterpret_cast<int*>(reinterpret_cast<char*>(pUIItem) + 12);

    // GBK escaped tab names: 装备, 消耗, 设置, 其它, 特殊, 装饰
    const char* tabNames[6] = {
        "\xD7\xB0\xB1\xB8",
        "\xCF\xFB\xBA\xC4",
        "\xC9\xE8\xD6\xC3",
        "\xC6\xE4\xCB\xFC",
        "\xCC\xD8\xCA\xE2",
        "\xD7\xB0\xCA\xCE"
    };
    int tabW = 28;
    int tabH = 16;
    int startX = winX + 2;
    int startY = winY + 23;

    HFONT hFont = CreateFontA(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, GB2312_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "SimSun");
    HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, hFont));
    int oldBkMode = SetBkMode(hdc, TRANSPARENT);

    for (int i = 0; i < 6; ++i) {
        int x = startX + i * tabW;
        int y = startY;
        RECT rc = { x, y, x + tabW, y + tabH };

        bool isActive = (s_nCurrentTab == i);

        // Background brush
        COLORREF bgCol = isActive ? RGB(235, 110, 140) : RGB(170, 175, 185);
        COLORREF borderCol = isActive ? RGB(160, 40, 70) : RGB(110, 115, 125);
        COLORREF textCol = isActive ? RGB(255, 255, 255) : RGB(50, 50, 50);

        HBRUSH hBrush = CreateSolidBrush(bgCol);
        HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
        HBRUSH hOldBrush = static_cast<HBRUSH>(SelectObject(hdc, hBrush));
        HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));

        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);

        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBrush);
        DeleteObject(hPen);

        // Text
        SetTextColor(hdc, textCol);
        DrawTextA(hdc, tabNames[i], -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SetBkMode(hdc, oldBkMode);
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);

    if (bOwnDC) {
        ReleaseDC(hWnd, hdc);
    }
}

LRESULT CALLBACK CustomInventoryTab::SubclassWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_LBUTTONDOWN) {
        void* pUIItem = *reinterpret_cast<void**>(0x00BEDCD0);
        if (pUIItem) {
            int winX = *reinterpret_cast<int*>(reinterpret_cast<char*>(pUIItem) + 8);
            int winY = *reinterpret_cast<int*>(reinterpret_cast<char*>(pUIItem) + 12);
            int mx = LOWORD(lParam);
            int my = HIWORD(lParam);

            if (my >= winY + 22 && my <= winY + 39 && mx >= winX + 2 && mx <= winX + 170) {
                int clickedTab = (mx - (winX + 2)) / 28;
                if (clickedTab >= 0 && clickedTab <= 5) {
                    s_nCurrentTab = clickedTab;
                    DrawTabOverlay(hWnd, nullptr);
                }
            }
        }
    } else if (uMsg == WM_PAINT) {
        LRESULT res = CallWindowProcA(s_pfnOrigWndProc, hWnd, uMsg, wParam, lParam);
        DrawTabOverlay(hWnd, nullptr);
        return res;
    }

    if (s_pfnOrigWndProc) {
        return CallWindowProcA(s_pfnOrigWndProc, hWnd, uMsg, wParam, lParam);
    }
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}
