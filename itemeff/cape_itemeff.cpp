#include "pch.h"
#include "hook.h"
#include "itemeff_patch.h"
#include "wvs/avatar.h"
#include "wvs/iteminfo.h"
#include "wvs/util.h"
#include "ztl/ztl.h"
#include <set>

namespace {

constexpr int AVATAR_OFFSET = 0x88;

bool IsCape(int itemId) {
    return itemId / 10000 == 110;
}

class CUser {
public:
    MEMBER_AT(CAvatar, AVATAR_OFFSET, m_CAvatar)

    int LoadLayer(Ztl_bstr_t uol, int left, USERLAYER& layer, int* repeatStartIndex) {
        return reinterpret_cast<int(__thiscall*)(CUser*, Ztl_bstr_t, int, USERLAYER&, int*)>(0x00941417)(
            this, uol, left, layer, repeatStartIndex);
    }
};

// The resource manager can be gone before this DLL's CRT destructors run.
// Keep only IDs across callbacks; never release WZ COM objects from a global
// container destructor during process teardown.
std::set<int> g_capeEffects;

void RememberCapeEffect(int itemId, const IWzPropertyPtr& effect) {
    if (effect) g_capeEffects.insert(itemId);
}

auto CItemInfo__IterateItemInfo = reinterpret_cast<int(__thiscall*)(CItemInfo*)>(0x005CA71C);

int __fastcall CItemInfo__IterateItemInfo_hook(CItemInfo* self, void*) {
    g_capeEffects.clear();

    IWzPropertyPtr itemEffects = get_rm()->GetObjectA(L"Effect/ItemEff.img").GetUnknown();
    if (itemEffects) {
        IEnumVARIANTPtr enumerator = itemEffects->_NewEnum;
        while (enumerator) {
            Ztl_variant_t next;
            ULONG fetched = 0;
            if (FAILED(enumerator->Next(1, &next, &fetched)) || fetched == 0) {
                break;
            }

            Ztl_bstr_t itemName = V_BSTR(&next);
            int itemId = wcstol(itemName.GetBSTR(), nullptr, 10);
            if (!IsCape(itemId)) {
                continue;
            }

            IWzPropertyPtr itemProperty = itemEffects->item[itemName].GetUnknown();
            if (!itemProperty) {
                continue;
            }

            IWzPropertyPtr effect;
            IUnknownPtr unknown = itemProperty->item[L"effect"].GetUnknown();
            if (unknown && SUCCEEDED(unknown.QueryInterface(__uuidof(IWzPropertyPtr), &effect)) && effect) {
                RememberCapeEffect(itemId, effect);
            }
        }
    }

    return CItemInfo__IterateItemInfo(self);
}

void UpdateCapeItemEffect(CUser* user) {
    if (!user) {
        return;
    }

    CAvatar* avatar = &user->m_CAvatar;
    if (!avatar->m_pCustomData || !avatar->m_pLayerUnderFace) {
        return;
    }

    for (int index = 0; index < 60; ++index) {
        int itemId = avatar->m_avatarLook.anHairEquip[index];
        ITEMEFFECTLAYER* itemEffectLayer = &avatar->m_pCustomData->aItemEffectLayer[index];
        auto effectIt = g_capeEffects.find(itemId);
        if (effectIt == g_capeEffects.end()) {
            itemEffectLayer->Reset();
            continue;
        }

        int flip = avatar->m_pLayerUnderFace->flip;
        int action = avatar->GetCurrentAction(nullptr);
        if (itemEffectLayer->nItemID == itemId &&
            itemEffectLayer->nAction == action &&
            (itemEffectLayer->l.bFixed || itemEffectLayer->bFlip == flip)) {
            continue;
        }

        itemEffectLayer->nItemID = itemId;
        itemEffectLayer->nAction = action;
        itemEffectLayer->bFlip = flip;

        Ztl_bstr_t actionName;
        reinterpret_cast<Ztl_bstr_t*(__cdecl*)(Ztl_bstr_t*, int)>(0x004A8CE6)(&actionName, action);

        // Resolve only when equipment/action/flip changes, after the fast path
        // above. The reference is confined to this callback, while WZ is alive.
        wchar_t effectPath[128]{};
        swprintf_s(effectPath, L"Effect/ItemEff.img/%d/effect", itemId);
        IWzPropertyPtr effect = get_rm()->GetObjectA(effectPath).GetUnknown();
        if (!effect) {
            itemEffectLayer->Reset();
            continue;
        }
        Ztl_variant_t actionProperty = effect->item[actionName];
        const wchar_t* branch = actionProperty.vt == VT_EMPTY ? L"default" : actionName.GetBSTR();

        wchar_t uol[1024]{};
        swprintf_s(uol, L"Effect/ItemEff.img/%d/effect/%ls", itemId, branch);

        if (user->LoadLayer(uol, flip, itemEffectLayer->l, nullptr) && itemEffectLayer->l.pLayer) {
            itemEffectLayer->l.pLayer->Animate(GA_REPEAT);
        } else {
            itemEffectLayer->Reset();
        }
    }
}

auto CUser__UpdateAdditionalLayer = reinterpret_cast<void(__thiscall*)(CUser*)>(0x00940EB7);

void __fastcall CUser__UpdateAdditionalLayer_hook(CUser* self, void*) {
    CUser__UpdateAdditionalLayer(self);
    UpdateCapeItemEffect(self);
}

auto CUser__OnAvatarModified = reinterpret_cast<void(__thiscall*)(void*)>(0x0092E916);

void __fastcall CUser__OnAvatarModified_hook(void* avatar, void*) {
    CUser__OnAvatarModified(avatar);
    UpdateCapeItemEffect(reinterpret_cast<CUser*>(reinterpret_cast<uintptr_t>(avatar) - AVATAR_OFFSET));
}

auto CUser__SetMoveAction = reinterpret_cast<void(__thiscall*)(void*, int, int)>(0x0092ECD1);

void __fastcall CUser__SetMoveAction_hook(void* avatar, void*, int moveAction, int reload) {
    CUser__SetMoveAction(avatar, moveAction, reload);
    UpdateCapeItemEffect(reinterpret_cast<CUser*>(reinterpret_cast<uintptr_t>(avatar) - AVATAR_OFFSET));
}

} // namespace

#ifdef ITEMEFF_LIFECYCLE_TESTING
extern "C" __declspec(dllexport) void __cdecl TestRememberCape(int itemId, IUnknown* object) {
    IWzPropertyPtr local(reinterpret_cast<IWzProperty*>(object));
    RememberCapeEffect(itemId, local);
}
extern "C" __declspec(dllexport) bool __cdecl TestHasCape(int itemId) {
    return g_capeEffects.find(itemId) != g_capeEffects.end();
}
#endif

void AttachCapeItemEffect() {
    static bool attached = false;
    if (attached) {
        return;
    }
    attached = true;

    ATTACH_HOOK(CItemInfo__IterateItemInfo, CItemInfo__IterateItemInfo_hook);
    ATTACH_HOOK(CUser__UpdateAdditionalLayer, CUser__UpdateAdditionalLayer_hook);
    ATTACH_HOOK(CUser__OnAvatarModified, CUser__OnAvatarModified_hook);
    ATTACH_HOOK(CUser__SetMoveAction, CUser__SetMoveAction_hook);
}
