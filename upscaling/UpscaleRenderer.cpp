#include "UpscaleRenderer.h"
#include "NeuralShaders.h"
#include <algorithm>
#include <cstring>

namespace NeuralUpscale {
namespace {
using Microsoft::WRL::ComPtr;
// State blocks omit render targets and depth surfaces. Restore them explicitly,
// before applying the captured viewport and the rest of the game's state.
class StateGuard {
    IDirect3DDevice9* device;
    ComPtr<IDirect3DStateBlock9> state;
    ComPtr<IDirect3DSurface9> targets[4], depth;
    UINT count = 1;
public:
    explicit StateGuard(IDirect3DDevice9* d) : device(d) {
        D3DCAPS9 caps{};
        if (SUCCEEDED(d->GetDeviceCaps(&caps))) count = std::min<DWORD>(4, caps.NumSimultaneousRTs);
        for (UINT i = 0; i < count; ++i) d->GetRenderTarget(i, &targets[i]);
        d->GetDepthStencilSurface(&depth);
        d->CreateStateBlock(D3DSBT_ALL, &state);
    }
    bool Valid() const { return state && targets[0]; }
    ~StateGuard() {
        if (!state) return;
        for (UINT i = 0; i < count; ++i) device->SetRenderTarget(i, targets[i].Get());
        device->SetDepthStencilSurface(depth.Get());
        state->Apply();
    }
};
class SceneGuard {
    IDirect3DDevice9* device;
public:
    explicit SceneGuard(IDirect3DDevice9* d) : device(d) {}
    ~SceneGuard() { if (device) device->EndScene(); }
    HRESULT End() { auto* d = device; device = nullptr; return d->EndScene(); }
};
}

Renderer::~Renderer() { Reset(); }
ULONG Renderer::References() const { device_->AddRef(); return device_->Release(); }

void Renderer::Reset() {
    // Call before the underlying Reset; DEFAULT-pool resources and additional
    // swapchains must not keep the old device surfaces alive.
    output_.Reset(); outputWidth_ = outputHeight_ = 0; outputWindow_ = nullptr;
    input_ = {}; residual_ = {}; doubled_ = {}; horizontal_ = {};
    for (auto& bank : groups_) for (auto& texture : bank) texture = {};
    network_.clear(); copy_.Reset(); resolve_.Reset(); resample_.Reset();
    width_ = height_ = targetWidth_ = targetHeight_ = 0;
    internalReferences_ = 0;
    activeLogged_ = false;
    disabledUntilReset_ = false;
}

HRESULT Renderer::CreateTexture(Texture& texture, UINT w, UINT h, D3DFORMAT format) {
    texture = {};
    HRESULT hr = device_->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &texture.image, nullptr);
    if (SUCCEEDED(hr)) hr = texture.image->GetSurfaceLevel(0, &texture.surface);
    return hr;
}

HRESULT Renderer::Prepare(UINT w, UINT h, UINT tw, UINT th, const Settings& settings) {
    if (w == width_ && h == height_ && tw == targetWidth_ && th == targetHeight_ &&
        settings.algorithm == preparedSettings_.algorithm && settings.quality == preparedSettings_.quality) return S_OK;
    D3DCAPS9 caps{};
    HRESULT hr = device_->GetDeviceCaps(&caps);
    if (FAILED(hr)) return hr;
    if (caps.PixelShaderVersion < D3DPS_VERSION(3,0)) return D3DERR_NOTAVAILABLE;
    const bool neural = settings.algorithm == Algorithm::Cunny && tw > w && th > h;
    if (!w || !h || !tw || !th || w > caps.MaxTextureWidth || h > caps.MaxTextureHeight ||
        tw > caps.MaxTextureWidth || th > caps.MaxTextureHeight ||
        (neural && (w > caps.MaxTextureWidth / 2 || h > caps.MaxTextureHeight / 2))) return D3DERR_NOTAVAILABLE;
    auto makeShader = [&](const NeuralShaders::Blob& code, ComPtr<IDirect3DPixelShader9>& shader) {
        shader.Reset();
        return device_->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data), &shader);
    };
    if (!copy_ && FAILED(hr = makeShader(NeuralShaders::Copy, copy_))) return hr;
    if (!resolve_ && FAILED(hr = makeShader(NeuralShaders::Resolve, resolve_))) return hr;
    if (!resample_ && FAILED(hr = makeShader(NeuralShaders::Resample, resample_))) return hr;
    if (network_.size() != (settings.quality == Quality::Fast ? 6u : 9u) || settings.quality != preparedSettings_.quality) {
        network_.clear();
        const auto* code = settings.quality == Quality::Fast ? NeuralShaders::fast : NeuralShaders::balanced;
        const size_t count = settings.quality == Quality::Fast ? 6 : 9;
        for (size_t i = 0; i < count; ++i) {
            ComPtr<IDirect3DPixelShader9> shader;
            if (FAILED(hr = makeShader(code[i], shader))) return hr;
            network_.push_back(shader);
        }
    }
    if (w != width_ || h != height_ || !input_.image) {
        if (FAILED(hr = CreateTexture(input_, w, h))) return hr;
    }
    if (neural && (w != width_ || h != height_ || !doubled_.image)) {
        for (auto& bank : groups_) for (auto& texture : bank)
            if (FAILED(hr = CreateTexture(texture, w, h))) return hr;
        if (FAILED(hr = CreateTexture(residual_, w, h, D3DFMT_A16B16G16R16F))) return hr;
        if (FAILED(hr = CreateTexture(doubled_, w*2, h*2))) return hr;
    }
    const UINT activeHeight = neural ? h*2 : h;
    if (FAILED(hr = CreateTexture(horizontal_, tw, activeHeight))) return hr;
    width_ = w; height_ = h; targetWidth_ = tw; targetHeight_ = th;
    preparedSettings_ = settings;
    return S_OK;
}

HRESULT Renderer::Draw(IDirect3DSurface9* target, IDirect3DPixelShader9* shader,
    const std::vector<IDirect3DTexture9*>& inputs, UINT iw, UINT ih, bool linear, const float* axis) {
    // Unbind all previous inputs before selecting a surface that might have been read.
    for (UINT i = 0; i < 4; ++i) device_->SetTexture(i, nullptr);
    HRESULT hr = device_->SetRenderTarget(0, target);
    if (FAILED(hr)) return hr;
    D3DSURFACE_DESC desc{}; target->GetDesc(&desc);
    D3DVIEWPORT9 viewport{0,0,desc.Width,desc.Height,0,1};
    device_->SetViewport(&viewport);
    device_->SetVertexShader(nullptr);
    device_->SetPixelShader(shader);
    device_->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    for (UINT i = 0; i < inputs.size(); ++i) {
        device_->SetTexture(i, inputs[i]);
        device_->SetSamplerState(i, D3DSAMP_MINFILTER, linear && i == 0 ? D3DTEXF_LINEAR : D3DTEXF_POINT);
        device_->SetSamplerState(i, D3DSAMP_MAGFILTER, linear && i == 0 ? D3DTEXF_LINEAR : D3DTEXF_POINT);
        device_->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        device_->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        device_->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        device_->SetSamplerState(i, D3DSAMP_SRGBTEXTURE, FALSE);
    }
    const float size[4]{1.f/iw,1.f/ih,static_cast<float>(iw),static_cast<float>(ih)};
    device_->SetPixelShaderConstantF(0, size, 1);
    if (axis) device_->SetPixelShaderConstantF(1, axis, 1);
    struct Vertex { float x,y,z,w,u,v; };
    const float w = static_cast<float>(desc.Width), h = static_cast<float>(desc.Height);
    const Vertex vertices[4]{{-.5f,-.5f,0,1,0,0},{w-.5f,-.5f,0,1,1,0},{-.5f,h-.5f,0,1,0,1},{w-.5f,h-.5f,0,1,1,1}};
    return device_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(Vertex));
}

HRESULT Renderer::RenderImpl(IDirect3DSurface9* source, IDirect3DSurface9* destination, const Settings& settings) {
    D3DSURFACE_DESC src{}, dst{};
    HRESULT hr = source->GetDesc(&src);
    if (FAILED(hr)) return hr;
    if (FAILED(hr = destination->GetDesc(&dst))) return hr;
    if (FAILED(hr = Prepare(src.Width, src.Height, dst.Width, dst.Height, settings))) return hr;
    if (FAILED(hr = device_->StretchRect(source, nullptr, input_.surface.Get(), nullptr, D3DTEXF_NONE))) return hr;
    StateGuard guard(device_);
    if (!guard.Valid()) return E_FAIL;
    if (FAILED(hr = device_->BeginScene())) return hr;
    SceneGuard scene(device_);
    device_->SetDepthStencilSurface(nullptr);
    D3DCAPS9 caps{}; device_->GetDeviceCaps(&caps);
    for (UINT i = 1; i < std::min<DWORD>(4, caps.NumSimultaneousRTs); ++i) device_->SetRenderTarget(i, nullptr);
    device_->SetRenderState(D3DRS_ZENABLE, FALSE);
    device_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device_->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device_->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device_->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device_->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device_->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device_->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    device_->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device_->SetRenderState(D3DRS_DITHERENABLE, FALSE);
    device_->SetRenderState(D3DRS_WRAP0, 0);
    auto render = [&]() -> HRESULT {
        if (src.Width == dst.Width && src.Height == dst.Height)
            return Draw(destination, copy_.Get(), {input_.image.Get()}, src.Width, src.Height);
        if (settings.algorithm == Algorithm::Linear)
            return Draw(destination, copy_.Get(), {input_.image.Get()}, src.Width, src.Height, true);
        IDirect3DTexture9* active = input_.image.Get();
        UINT aw = src.Width, ah = src.Height;
        if (dst.Width > src.Width && dst.Height > src.Height) {
            const int fastCounts[4]{2,2,1,1}, balancedCounts[4]{3,3,2,1};
            const int* counts = settings.quality == Quality::Fast ? fastCounts : balancedCounts;
            std::vector<IDirect3DTexture9*> inputs{input_.image.Get()};
            size_t program = 0;
            for (int layer = 0; layer < 4; ++layer) {
                std::vector<IDirect3DTexture9*> next;
                for (int group = 0; group < counts[layer]; ++group) {
                    auto& target = layer == 3 ? residual_ : groups_[layer % 2][group];
                    HRESULT pass = Draw(target.surface.Get(), network_[program++].Get(), inputs, aw, ah);
                    if (FAILED(pass)) return pass;
                    next.push_back(target.image.Get());
                }
                inputs = std::move(next);
            }
            HRESULT pass = Draw(doubled_.surface.Get(), resolve_.Get(), {input_.image.Get(), residual_.image.Get()}, aw, ah, true);
            if (FAILED(pass)) return pass;
            active = doubled_.image.Get(); aw *= 2; ah *= 2;
        }
        if (aw == dst.Width && ah == dst.Height)
            return Draw(destination, copy_.Get(), {active}, aw, ah);
        if (aw != dst.Width) {
            const float axis[4]{1,0,static_cast<float>(aw),static_cast<float>(dst.Width)};
            HRESULT pass = Draw(horizontal_.surface.Get(), resample_.Get(), {active}, aw, ah, false, axis);
            if (FAILED(pass)) return pass;
            active = horizontal_.image.Get(); aw = dst.Width;
        }
        if (ah != dst.Height) {
            const float axis[4]{0,1,static_cast<float>(ah),static_cast<float>(dst.Height)};
            return Draw(destination, resample_.Get(), {active}, aw, ah, false, axis);
        }
        return Draw(destination, copy_.Get(), {active}, aw, ah);
    };
    hr = render();
    for (UINT i = 0; i < 4; ++i) device_->SetTexture(i, nullptr);
    const HRESULT end = scene.End();
    return FAILED(hr) ? hr : end;
}

HRESULT Renderer::Render(IDirect3DSurface9* source, IDirect3DSurface9* destination, const Settings& settings) {
    if (!source || !destination) return E_POINTER;
    const ULONG external = References() - internalReferences_;
    HRESULT result = E_FAIL;
    try { result = RenderImpl(source, destination, settings); }
    catch (...) { result = E_OUTOFMEMORY; }
    internalReferences_ = References() - external;
    return result;
}

bool Renderer::Present(const RECT* sourceRect, const RECT* targetRect, HWND overrideWindow, HRESULT& result) {
    if (!Configuration().enabled || disabledUntilReset_) return false;
    ComPtr<IDirect3DSwapChain9> original;
    if (FAILED(device_->GetSwapChain(0, &original))) return false;
    D3DPRESENT_PARAMETERS params{};
    if (FAILED(original->GetPresentParameters(&params)) || !params.Windowed) return false;
    HWND window = overrideWindow ? overrideWindow : params.hDeviceWindow;
    char className[64]{}; GetClassNameA(window, className, sizeof(className));
    if (strcmp(className, "MapleStoryClass") != 0 || IsIconic(window)) return false;
    RECT client{};
    if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0) return false;
    ComPtr<IDirect3DSurface9> source;
    if (FAILED(original->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &source))) return false;
    D3DSURFACE_DESC description{}; source->GetDesc(&description);
    // Accept explicit full-surface rectangles as well as the usual null pointers.
    // Partial presents retain the game's original cropping/dirty-region semantics.
    if (sourceRect && (sourceRect->left || sourceRect->top ||
        sourceRect->right != description.Width || sourceRect->bottom != description.Height)) return false;
    if (targetRect && !EqualRect(targetRect, &client)) return false;
    if (description.Width == client.right && description.Height == client.bottom) return false;
    if (FAILED(device_->TestCooperativeLevel())) return false;
    // Count only references owned by this renderer, never temporary game surfaces.
    const ULONG external = References() - internalReferences_;
    HRESULT hr = S_OK;
    if (!output_ || outputWidth_ != client.right || outputHeight_ != client.bottom || outputWindow_ != window) {
        output_.Reset();
        D3DPRESENT_PARAMETERS outputParams{};
        outputParams.Windowed = TRUE; outputParams.hDeviceWindow = window;
        outputParams.BackBufferWidth = client.right; outputParams.BackBufferHeight = client.bottom;
        outputParams.BackBufferFormat = params.BackBufferFormat;
        outputParams.BackBufferCount = 1; outputParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
        outputParams.PresentationInterval = params.PresentationInterval;
        hr = device_->CreateAdditionalSwapChain(&outputParams, &output_);
        outputWidth_ = client.right; outputHeight_ = client.bottom;
        outputWindow_ = window;
    }
    internalReferences_ = References() - external;
    if (SUCCEEDED(hr)) {
        ComPtr<IDirect3DSurface9> target;
        hr = output_->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &target);
        if (SUCCEEDED(hr)) hr = Render(source.Get(), target.Get(), Configuration());
        if (SUCCEEDED(hr)) {
            if (!activeLogged_) {
                Log("active: %ux%u -> %ldx%ld, CuNNy=%s", description.Width, description.Height,
                    client.right, client.bottom, Configuration().algorithm == Algorithm::Cunny ? "yes" : "no");
                activeLogged_ = true;
            }
            hr = output_->Present(nullptr, nullptr, window, nullptr, 0);
            // Let the game handle normal device-loss recovery through Reset.
            if (SUCCEEDED(hr) || hr == D3DERR_DEVICELOST) { result = hr; return true; }
        }
    }
    Log("GPU upscale failed 0x%08lX; using original presentation until device reset", hr);
    Reset();
    disabledUntilReset_ = true;
    return false;
}
}
