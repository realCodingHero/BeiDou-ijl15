#include "pch.h"
#include "hook.h"
#include "itemeff_patch.h"
#include "wvs/avatar.h"
#include "wvs/wvsapp.h"

void CAvatar::Constructor_hook() {
    CAvatar::Constructor(this);
    auto* customData = new CustomData{};
    m_pCustomData = customData;
}

void CAvatar::Destructor_hook() {
    delete m_pCustomData;
    CAvatar::Destructor(this);
}

void CAvatar::RegisterNextBlink_hook() {
    m_pCustomData->bBlinking = 0;
    m_tNextBlink = CWvsApp::GetInstance()->m_tUpdateTime + (rand() % 3000) + 2000;
}

static auto CAvatar__Update_jmp1 = 0x004534CC;
static auto CAvatar__Update_ret1 = 0x004534D2;

void __declspec(naked) CAvatar__Update_hook1() {
    __asm {
        mov ecx, [ebx + 0x484]
        cmp [ecx], edi
        jmp [CAvatar__Update_ret1]
    }
}

static auto CAvatar__Update_jmp2 = 0x00453612;
static auto CAvatar__Update_ret2 = 0x0045361C;

void __declspec(naked) CAvatar__Update_hook2() {
    __asm {
        mov eax, [ebx + 0x484]
        mov [eax], 1
        jmp [CAvatar__Update_ret2]
    }
}

void AttachItemEffectStorage() {
    static bool attached = false;
    if (attached) {
        return;
    }
    attached = true;

    ATTACH_HOOK(CAvatar::Constructor, CAvatar::Constructor_hook);
    ATTACH_HOOK(CAvatar::Destructor, CAvatar::Destructor_hook);
    ATTACH_HOOK(CAvatar::RegisterNextBlink, CAvatar::RegisterNextBlink_hook);
    PatchJmp(CAvatar__Update_jmp1, CAvatar__Update_hook1);
    PatchJmp(CAvatar__Update_jmp2, CAvatar__Update_hook2);
}
