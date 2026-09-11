#include "stdafx.h"
#include "BossRoomAssistDisplay.h"

#include <climits>
#include <intrin.h>

namespace {
constexpr DWORD kPDamageAddress = 0x0078DF87;
constexpr DWORD kMDamageAddress = 0x00791617;
constexpr DWORD kPanelMagicAttackAddress = 0x0077E067;
constexpr DWORD kPhysicalPanelPatchAddress = 0x008C3352;
constexpr DWORD kPhysicalPanelReturnAddress = 0x008C335A;
constexpr DWORD kPhysicalPanelColorPatchAddress = 0x008C3391;
constexpr DWORD kPhysicalPanelColorFallbackAddress = 0x008C339A;
constexpr DWORD kPhysicalPanelColorDoneAddress = 0x008C33B4;
constexpr int kMaxMultiplier = 30;
constexpr int kMaxDamageEntries = 64;

volatile LONG g_physicalMultiplier = 1;
volatile LONG g_magicMultiplier = 1;
volatile LONG g_physicalPanelMultiplier = 1;
volatile LONG g_damageHooksReady = 0;
DWORD g_physicalPanelReturn = kPhysicalPanelReturnAddress;
DWORD g_physicalPanelColorFallback = kPhysicalPanelColorFallbackAddress;
DWORD g_physicalPanelColorDone = kPhysicalPanelColorDoneAddress;

static LONG NormalizeMultiplier(unsigned char value) {
    return value >= 1 && value <= kMaxMultiplier ? value : 1;
}

static int ReadMultiplier(volatile LONG* value) {
    return static_cast<int>(InterlockedCompareExchange(value, 0, 0));
}

static int ScalePositiveDamage(int damage, int multiplier, int cap) {
    if (damage <= 0 || multiplier <= 1) {
        return damage;
    }

    const __int64 scaled = static_cast<__int64>(damage) * multiplier;
    return scaled >= cap ? cap : static_cast<int>(scaled);
}

// Critical hits use the v83 wire encoding: encodedDamage + INT_MAX is the
// positive damage. Preserve that sign marker because the same array is used
// for both the local floating number and the outgoing attack packet.
static int ScaleEncodedDamage(int encodedDamage, int multiplier) {
    if (multiplier <= 1 || encodedDamage == 0) {
        return encodedDamage;
    }

    if (encodedDamage > 0) {
        return ScalePositiveDamage(encodedDamage, multiplier, INT_MAX);
    }

    const int decodedDamage = encodedDamage + INT_MAX;
    if (decodedDamage <= 0) {
        return encodedDamage;
    }

    const int scaledDamage = ScalePositiveDamage(decodedDamage, multiplier, INT_MAX - 1);
    return scaledDamage - INT_MAX;
}

static void ScaleDamageArray(DWORD outputAddress, DWORD countValue, int multiplier) {
    if (multiplier <= 1 || outputAddress == 0 || countValue == 0 || countValue > kMaxDamageEntries) {
        return;
    }

    __try {
        int* output = reinterpret_cast<int*>(outputAddress);
        for (DWORD i = 0; i < countValue; ++i) {
            output[i] = ScaleEncodedDamage(output[i], multiplier);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

using PDamage_t = void(__fastcall*)(
    void*, void*, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
    DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);

static PDamage_t s_PDamage = reinterpret_cast<PDamage_t>(kPDamageAddress);

static void __fastcall PDamage_Hook(
    void* pThis, void* edx, DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5,
    DWORD a6, DWORD a7, DWORD a8, DWORD a9, DWORD a10, DWORD a11, DWORD a12,
    DWORD a13, DWORD a14, DWORD a15, DWORD a16, DWORD a17, DWORD a18, DWORD a19,
    DWORD a20) {
    s_PDamage(pThis, edx, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12,
              a13, a14, a15, a16, a17, a18, a19, a20);
    ScaleDamageArray(a15, a7, ReadMultiplier(&g_physicalMultiplier));
}

using MDamage_t = void(__fastcall*)(
    void*, void*, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
    DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);

static MDamage_t s_MDamage = reinterpret_cast<MDamage_t>(kMDamageAddress);

static void __fastcall MDamage_Hook(
    void* pThis, void* edx, DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5,
    DWORD a6, DWORD a7, DWORD a8, DWORD a9, DWORD a10, DWORD a11, DWORD a12,
    DWORD a13, DWORD a14, DWORD a15) {
    s_MDamage(pThis, edx, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12,
              a13, a14, a15);
    ScaleDamageArray(a15, a6, ReadMultiplier(&g_magicMultiplier));
}

using PanelMagicAttack_t = int(__fastcall*)(void*, void*, DWORD);

static PanelMagicAttack_t s_PanelMagicAttack =
    reinterpret_cast<PanelMagicAttack_t>(kPanelMagicAttackAddress);

static bool IsMagicAttackPanelCaller(DWORD returnAddress) {
    switch (returnAddress) {
    case 0x008C3462:
    case 0x008C34FC:
    case 0x008C350D:
    case 0x008C354E:
    case 0x008C3575:
        return true;
    default:
        return false;
    }
}

// The native stat panel compares this result with the unbuffed magic attack.
// Scaling only these five panel call sites makes it take its existing positive
// Buff branch: red text formatted as "total (base+bonus)". Other callers keep
// the original value, so combat is still scaled exactly once by MDamage_Hook.
static int __fastcall PanelMagicAttack_Hook(void* pThis, void* edx, DWORD characterData) {
    const DWORD returnAddress = reinterpret_cast<DWORD>(_ReturnAddress());
    const int magicAttack = s_PanelMagicAttack(pThis, edx, characterData);
    if (!IsMagicAttackPanelCaller(returnAddress)) {
        return magicAttack;
    }
    return ScalePositiveDamage(magicAttack, ReadMultiplier(&g_magicMultiplier), INT_MAX);
}

static int __stdcall ScalePanelDamageValue(int damage) {
    return ScalePositiveDamage(damage, ReadMultiplier(&g_physicalPanelMultiplier), INT_MAX);
}

// Replaces exactly:
//   push ebx
//   push dword ptr [ebp-34h]
//   mov byte ptr [ebp-4],8
// immediately before the native "%d ~ %d" panel formatting call.
__declspec(naked) void PhysicalPanelDamage_Hook() {
    __asm {
        push eax
        push ecx
        push edx

        push ebx
        call ScalePanelDamageValue
        mov ebx, eax

        push dword ptr [ebp - 0x34]
        call ScalePanelDamageValue
        mov dword ptr [ebp - 0x34], eax

        pop edx
        pop ecx
        pop eax

        push ebx
        push dword ptr [ebp - 0x34]
        mov byte ptr [ebp - 4], 8
        jmp dword ptr [g_physicalPanelReturn]
    }
}

// Preserve the panel's normal positive/negative color selection when no Boss
// assist is active, and select its existing red brush for a physical boost.
__declspec(naked) void PhysicalPanelColor_Hook() {
    __asm {
        push eax
        mov eax, dword ptr [g_physicalPanelMultiplier]
        cmp eax, 1
        pop eax
        jg boosted

        test ecx, ecx
        jle fallback

    boosted:
        push dword ptr [ebp - 0x28]
        jmp dword ptr [g_physicalPanelColorDone]

    fallback:
        jmp dword ptr [g_physicalPanelColorFallback]
    }
}

static bool HasExpectedBytes(DWORD address, const unsigned char* expected, size_t length) {
    if (expected == nullptr || length == 0) {
        return false;
    }
    __try {
        return memcmp(reinterpret_cast<const void*>(address), expected, length) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool HasJumpTo(DWORD address, const void* target, size_t patchLength) {
    if (target == nullptr || patchLength < 5) {
        return false;
    }

    __try {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(address);
        if (bytes[0] != 0xE9) {
            return false;
        }

        const int expectedOffset = static_cast<int>(
            reinterpret_cast<DWORD>(target) - address - 5);
        if (*reinterpret_cast<const int*>(bytes + 1) != expectedOffset) {
            return false;
        }

        for (size_t i = 5; i < patchLength; ++i) {
            if (bytes[i] != 0x90) {
                return false;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
} // namespace

namespace BossRoomAssistDisplay {

void SetMultipliers(unsigned char physical, unsigned char magic, unsigned char physicalPanel) {
    InterlockedExchange(&g_physicalMultiplier, NormalizeMultiplier(physical));
    InterlockedExchange(&g_magicMultiplier, NormalizeMultiplier(magic));
    InterlockedExchange(&g_physicalPanelMultiplier, NormalizeMultiplier(physicalPanel));
}

void ResetMultipliers() {
    SetMultipliers(1, 1, 1);
}

bool Hook(bool enable) {
    if (!enable) {
        InterlockedExchange(&g_damageHooksReady, 0);
        Memory::SetHook(false, reinterpret_cast<void**>(&s_PDamage), PDamage_Hook);
        Memory::SetHook(false, reinterpret_cast<void**>(&s_MDamage), MDamage_Hook);
        Memory::SetHook(false, reinterpret_cast<void**>(&s_PanelMagicAttack), PanelMagicAttack_Hook);
        return true;
    }

    static const unsigned char expectedPDamage[] = {
        0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xA8, 0x01, 0x00, 0x00, 0x53, 0x56
    };
    static const unsigned char expectedMDamage[] = {
        0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x94, 0x00, 0x00, 0x00, 0x83, 0x65, 0xFC, 0x00
    };
    static const unsigned char expectedPanelMagicAttack[] = {
        0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x53, 0x56, 0x8B, 0xF1, 0x57
    };
    static const unsigned char expectedPanel[] = {
        0x53, 0xFF, 0x75, 0xCC, 0xC6, 0x45, 0xFC, 0x08
    };
    static const unsigned char expectedPanelColor[] = {
        0x85, 0xC9, 0x7E, 0x05, 0xFF, 0x75, 0xD8, 0xEB, 0x1A
    };

    if (!HasExpectedBytes(kPDamageAddress, expectedPDamage, sizeof(expectedPDamage))
            || !HasExpectedBytes(kMDamageAddress, expectedMDamage, sizeof(expectedMDamage))
            || !HasExpectedBytes(kPanelMagicAttackAddress, expectedPanelMagicAttack,
                sizeof(expectedPanelMagicAttack))
            || !HasExpectedBytes(kPhysicalPanelPatchAddress, expectedPanel, sizeof(expectedPanel))
            || !HasExpectedBytes(kPhysicalPanelColorPatchAddress, expectedPanelColor,
                sizeof(expectedPanelColor))) {
        return false;
    }

    const bool physicalHooked = Memory::SetHook(
        true, reinterpret_cast<void**>(&s_PDamage), PDamage_Hook);
    const bool magicHooked = Memory::SetHook(
        true, reinterpret_cast<void**>(&s_MDamage), MDamage_Hook);
    const bool panelMagicHooked = Memory::SetHook(
        true, reinterpret_cast<void**>(&s_PanelMagicAttack), PanelMagicAttack_Hook);
    if (!physicalHooked || !magicHooked || !panelMagicHooked) {
        if (physicalHooked) {
            Memory::SetHook(false, reinterpret_cast<void**>(&s_PDamage), PDamage_Hook);
        }
        if (magicHooked) {
            Memory::SetHook(false, reinterpret_cast<void**>(&s_MDamage), MDamage_Hook);
        }
        if (panelMagicHooked) {
            Memory::SetHook(false, reinterpret_cast<void**>(&s_PanelMagicAttack), PanelMagicAttack_Hook);
        }
        return false;
    }

    Memory::CodeCave(PhysicalPanelDamage_Hook, kPhysicalPanelPatchAddress, 8);
    Memory::CodeCave(PhysicalPanelColor_Hook, kPhysicalPanelColorPatchAddress, 9);
    if (!HasJumpTo(kPhysicalPanelPatchAddress, PhysicalPanelDamage_Hook, 8)
            || !HasJumpTo(kPhysicalPanelColorPatchAddress, PhysicalPanelColor_Hook, 9)) {
        Memory::SetHook(false, reinterpret_cast<void**>(&s_PDamage), PDamage_Hook);
        Memory::SetHook(false, reinterpret_cast<void**>(&s_MDamage), MDamage_Hook);
        Memory::SetHook(false, reinterpret_cast<void**>(&s_PanelMagicAttack), PanelMagicAttack_Hook);
        return false;
    }

    InterlockedExchange(&g_damageHooksReady, 1);
    return true;
}

bool IsReady() {
    return InterlockedCompareExchange(&g_damageHooksReady, 0, 0) != 0;
}

} // namespace BossRoomAssistDisplay
