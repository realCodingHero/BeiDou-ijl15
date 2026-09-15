#include "StatusBarLayout.h"
#include <string>
#include "Client.h"

namespace StatusBarLayout {
namespace {
const GUID kVectorIID = {0xf28bd1ed,0x3deb,0x4f92,{0x9e,0xec,0x10,0xef,0x5a,0x1c,0x3f,0xb4}};
// IWzVector2D inherits IUnknown, IWzSerialize, IWzShape2D. Verified against
// native put_origin / RelMove call sites; these offsets are for the x86 ABI.
using PutOrigin = HRESULT (__stdcall*)(IUnknown*, VARIANT);
using RelMove = HRESULT (__stdcall*)(IUnknown*, int, int, VARIANT, VARIANT);
template<typename T> T Method(IUnknown* p, unsigned byteOffset) {
    return reinterpret_cast<T>((*reinterpret_cast<void***>(p))[byteOffset/sizeof(void*)]);
}
}

bool UsesStatusBarOrigin(uintptr_t caller) {
    return (caller >= 0x008D01B2 && caller < 0x008D3ADF) ||
        caller == 0x008DEB75 || caller == 0x008DEE11;
}

bool CenterOrigin(IUnknown** result, uintptr_t caller, int left, CreateObject create) {
    if (!result || !*result || left <= 0 || !UsesStatusBarOrigin(caller) || !create) return false;
    IUnknown* vector = nullptr;
    HRESULT hr = create(L"Shape2D#Vector2D", &kVectorIID, &vector, nullptr);
    if (FAILED(hr) || !vector) {
        if (vector) vector->Release();
        return false;
    }
    // Borrow the parent for this call. put_origin retains its own reference.
    VARIANT parent{}; parent.vt = VT_UNKNOWN; parent.punkVal = *result;
    hr = Method<PutOrigin>(vector, 0x64)(vector, parent);
    if (SUCCEEDED(hr)) {
        VARIANT empty{};
        hr = Method<RelMove>(vector, 0x90)(vector, left, 0, empty, empty);
    }
    if (FAILED(hr)) { vector->Release(); return false; }
    (*result)->Release();
    *result = vector;
    return true;
}

void __cdecl CenterNativeOrigin(IUnknown** result, uintptr_t caller) {
    const int left = Left(Client::m_nGameWidth);
    if (!left || !UsesStatusBarOrigin(caller)) return;
    // Client-owned PCOM API table, initialized before CWndMan is constructed.
    // The executable identity and the hook's complete instructions are guarded
    // by the resolution transaction before this function can run.
    const auto create = *reinterpret_cast<CreateObject*>(0x00BF0CC0);
    CenterOrigin(result, caller, left, create);
}
}
