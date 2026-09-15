#include "UpscaleRenderer.h"
#include "LoginViewportBridge.h"
#include "NeuralShaders.h"
#include "PresentationPolicy.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <chrono>

namespace NeuralUpscale {
namespace {
using Microsoft::WRL::ComPtr;
double Milliseconds() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
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
    SetLoginPresentation(nullptr, false);
    ResetTiming();
    // Call before the underlying Reset; DEFAULT-pool resources and additional
    // swapchains must not keep the old device surfaces alive.
    output_.Reset(); outputWidth_ = outputHeight_ = 0; outputWindow_ = nullptr;
    requestedInterval_ = outputInterval_ = displayRefresh_ = 0;
    displayChecked_ = 0;
    input_ = {}; residual_ = {}; doubled_ = {}; horizontal_ = {};
    horizontalWeights_ = {}; verticalWeights_ = {};
    for (auto& bank : groups_) for (auto& texture : bank) texture = {};
    network_.clear(); copy_.Reset(); resolve_.Reset(); resample_.Reset();
    resampleFast_.Reset();
    width_ = height_ = targetWidth_ = targetHeight_ = 0;
    internalReferences_ = 0;
    activeLogged_ = false;
    fullscreenLogged_ = false;
    disabledUntilReset_ = false;
}

void Renderer::ResetTiming() {
    pacer_.Reset();
    profileStart_ = lastFrame_ = renderTotal_ = presentTotal_ = 0;
    maxGap_ = maxRender_ = maxPresent_ = 0; profileFrames_ = 0;
}

void Renderer::CreateWeights(Texture& texture, UINT inputExtent, UINT outputExtent) {
    texture = {};
    const double scale = std::max(1., double(inputExtent) / outputExtent);
    // Six samples exactly cover Lanczos2's support up to this ratio.
    // Larger reductions keep the original wide low-pass shader.
    if (inputExtent == outputExtent || scale > 1.5) return;
    if (FAILED(device_->CreateTexture(outputExtent, 2, 1, 0, D3DFMT_A32B32G32R32F,
        D3DPOOL_MANAGED, &texture.image, nullptr))) return;
    D3DLOCKED_RECT lock{};
    if (FAILED(texture.image->LockRect(0, &lock, nullptr, 0))) { texture = {}; return; }
    const double pi = 3.14159265358979323846;
    for (UINT x = 0; x < outputExtent; ++x) {
        const double p = (x + .5) * inputExtent / outputExtent - .5;
        double values[6]{}, sum = 0;
        for (int k = -2; k <= 3; ++k) {
            const double d = std::abs((std::floor(p) + k - p) / scale);
            const double weight = d < .00001 ? 1 : d >= 2 ? 0 : std::sin(pi*d)*std::sin(pi*d*.5)/(pi*pi*.5*d*d);
            values[k+2] = weight; sum += weight;
        }
        auto* row0 = reinterpret_cast<float*>(static_cast<char*>(lock.pBits)) + x*4;
        auto* row1 = reinterpret_cast<float*>(static_cast<char*>(lock.pBits) + lock.Pitch) + x*4;
        for (int k = 0; k < 4; ++k) row0[k] = static_cast<float>(values[k]/sum);
        row1[0] = static_cast<float>(values[4]/sum); row1[1] = static_cast<float>(values[5]/sum);
        row1[2] = row1[3] = 0;
    }
    texture.image->UnlockRect(0);
}

void Renderer::RecordFrame(double start, double renderMs, double presentMs) {
    if (!Configuration().diagnostics) return;
    if (!profileStart_) { profileStart_ = lastFrame_ = start; return; }
    maxGap_ = std::max(maxGap_, start-lastFrame_); lastFrame_ = start;
    renderTotal_ += renderMs; presentTotal_ += presentMs;
    maxRender_ = std::max(maxRender_, renderMs); maxPresent_ = std::max(maxPresent_, presentMs);
    ++profileFrames_;
    if (start-profileStart_ >= 5000) {
        Log("timing: fps=%.1f, max_gap=%.2fms, submit_avg/max=%.2f/%.2fms, present_avg/max=%.2f/%.2fms",
            profileFrames_*1000./(start-profileStart_), maxGap_, renderTotal_/profileFrames_, maxRender_,
            presentTotal_/profileFrames_, maxPresent_);
        profileStart_ = start; renderTotal_ = presentTotal_ = maxGap_ = maxRender_ = maxPresent_ = 0;
        profileFrames_ = 0;
    }
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
    if (settings.algorithm == Algorithm::Linear) {
        if (w != width_ || h != height_ || !input_.image)
            if (FAILED(hr = CreateTexture(input_, w, h))) return hr;
        // Retain only the input texture and one copy shader. In particular, a
        // linear resize must not prepare CuNNy, 2x targets or Lanczos resources.
        residual_ = {}; doubled_ = {}; horizontal_ = {};
        horizontalWeights_ = {}; verticalWeights_ = {};
        for (auto& bank : groups_) for (auto& texture : bank) texture = {};
        network_.clear(); resolve_.Reset(); resample_.Reset(); resampleFast_.Reset();
        width_ = w; height_ = h; targetWidth_ = tw; targetHeight_ = th;
        preparedSettings_ = settings;
        return S_OK;
    }
    if (!resolve_ && FAILED(hr = makeShader(NeuralShaders::Resolve, resolve_))) return hr;
    if (!resample_ && FAILED(hr = makeShader(NeuralShaders::Resample, resample_))) return hr;
    if (!resampleFast_ && FAILED(hr = makeShader(NeuralShaders::ResampleFast, resampleFast_))) return hr;
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
    CreateWeights(horizontalWeights_, neural ? w*2 : w, tw);
    CreateWeights(verticalWeights_, activeHeight, th);
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

HRESULT Renderer::RenderImpl(IDirect3DSurface9* source, IDirect3DSurface9* destination, const Settings& settings, const RECT* viewport) {
    D3DSURFACE_DESC src{}, dst{};
    HRESULT hr = source->GetDesc(&src);
    if (FAILED(hr)) return hr;
    if (FAILED(hr = destination->GetDesc(&dst))) return hr;
    if (viewport) {
        if (viewport->left < 0 || viewport->top < 0 || viewport->right > static_cast<LONG>(src.Width) ||
            viewport->bottom > static_cast<LONG>(src.Height) || viewport->left >= viewport->right ||
            viewport->top >= viewport->bottom) return E_INVALIDARG;
        src.Width = viewport->right - viewport->left;
        src.Height = viewport->bottom - viewport->top;
    }
    if (settings.algorithm == Algorithm::Linear && src.Width == dst.Width && src.Height == dst.Height && source != destination)
        return device_->StretchRect(source, viewport, destination, nullptr, D3DTEXF_NONE);
    if (FAILED(hr = Prepare(src.Width, src.Height, dst.Width, dst.Height, settings))) return hr;
    if (FAILED(hr = device_->StretchRect(source, viewport, input_.surface.Get(), nullptr, D3DTEXF_NONE))) return hr;
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
            // Explicit pixel centers avoid driver-dependent StretchRect
            // magnification offsets, while still using one bilinear GPU pass.
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
            const bool fast = horizontalWeights_.image != nullptr;
            HRESULT pass = Draw(horizontal_.surface.Get(), fast ? resampleFast_.Get() : resample_.Get(),
                fast ? std::vector<IDirect3DTexture9*>{active, horizontalWeights_.image.Get()} : std::vector<IDirect3DTexture9*>{active}, aw, ah, false, axis);
            if (FAILED(pass)) return pass;
            active = horizontal_.image.Get(); aw = dst.Width;
        }
        if (ah != dst.Height) {
            const float axis[4]{0,1,static_cast<float>(ah),static_cast<float>(dst.Height)};
            const bool fast = verticalWeights_.image != nullptr;
            return Draw(destination, fast ? resampleFast_.Get() : resample_.Get(),
                fast ? std::vector<IDirect3DTexture9*>{active, verticalWeights_.image.Get()} : std::vector<IDirect3DTexture9*>{active}, aw, ah, false, axis);
        }
        return Draw(destination, copy_.Get(), {active}, aw, ah);
    };
    hr = render();
    for (UINT i = 0; i < 4; ++i) device_->SetTexture(i, nullptr);
    const HRESULT end = scene.End();
    return FAILED(hr) ? hr : end;
}

HRESULT Renderer::Render(IDirect3DSurface9* source, IDirect3DSurface9* destination, const Settings& settings, const RECT* viewport) {
    if (!source || !destination) return E_POINTER;
    const ULONG external = References() - internalReferences_;
    HRESULT result = E_FAIL;
    try { result = RenderImpl(source, destination, settings, viewport); }
    catch (...) { result = E_OUTOFMEMORY; }
    internalReferences_ = References() - external;
    return result;
}

void Renderer::SetLoginPresentation(HWND window, bool active) {
    if (loginWindow_ && (!active || loginWindow_ != window)) {
        RemovePropW(loginWindow_, LoginViewportBridge::kProperty);
        loginWindow_ = nullptr;
        Log("login viewport: native render coordinates restored");
    }
    if (active && window && loginWindow_ != window) {
        if (SetPropW(window, LoginViewportBridge::kProperty, reinterpret_cast<HANDLE>(1))) {
            loginWindow_ = window;
            Log("login viewport: centered 1280x720 -> full output, linear; mouse mapping active");
        }
    }
}

bool Renderer::Present(const RECT* sourceRect, const RECT* targetRect, HWND overrideWindow, HRESULT& result) {
    double frameStart = Milliseconds();
    const auto native = [&]() { SetLoginPresentation(nullptr, false); ResetTiming(); return false; };
    if (!Configuration().enabled || disabledUntilReset_) return native();
    ComPtr<IDirect3DSwapChain9> original;
    if (FAILED(device_->GetSwapChain(0, &original))) return native();
    D3DPRESENT_PARAMETERS params{};
    if (FAILED(original->GetPresentParameters(&params))) return native();
    RECT fullscreenLogin{};
    if (!params.Windowed && !LoginViewportBridge::Query(params.BackBufferWidth, params.BackBufferHeight, &fullscreenLogin)) {
        if (!fullscreenLogged_) {
            Log("fullscreen: %ux%u, presentation=native, CuNNy=no", params.BackBufferWidth, params.BackBufferHeight);
            fullscreenLogged_ = true;
        }
        return native(); // Keep ordinary fullscreen gameplay on the native fast path.
    }
    HWND window = overrideWindow ? overrideWindow : params.hDeviceWindow;
    char className[64]{}; GetClassNameA(window, className, sizeof(className));
    if (strcmp(className, "MapleStoryClass") != 0 || IsIconic(window)) return native();
    RECT client{};
    if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0) return native();
    ComPtr<IDirect3DSurface9> source;
    if (FAILED(original->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &source))) return native();
    D3DSURFACE_DESC description{}; source->GetDesc(&description);
    // Accept explicit full-surface rectangles as well as the usual null pointers.
    // Partial presents retain the game's original cropping/dirty-region semantics.
    if (sourceRect && (sourceRect->left || sourceRect->top ||
        sourceRect->right != description.Width || sourceRect->bottom != description.Height)) return native();
    if (targetRect && !EqualRect(targetRect, &client)) return native();
    if (FAILED(device_->TestCooperativeLevel())) return native();
    RECT login{};
    const bool crop = LoginViewportBridge::Query(description.Width, description.Height, &login);
    if (!params.Windowed) {
        if (crop) {
            Settings linear = Configuration(); linear.algorithm = Algorithm::Linear;
            const HRESULT hr = Render(source.Get(), source.Get(), linear, &login);
            SetLoginPresentation(window, SUCCEEDED(hr));
            ResetTiming();
            // The game's own fullscreen Present still submits the modified buffer.
            return false;
        }
        if (!fullscreenLogged_) {
            Log("fullscreen: %ux%u, presentation=native, CuNNy=no", description.Width, description.Height);
            fullscreenLogged_ = true;
        }
        return native();
    }
    fullscreenLogged_ = false;
    const unsigned limit = FrameLimit();
    const UINT requestedInterval = PresentationPolicy::Interval(limit);
    const ULONGLONG now = GetTickCount64();
    if (!displayChecked_ || now - displayChecked_ >= 1000) {
        D3DDISPLAYMODE display{};
        device_->GetDisplayMode(0, &display);
        if (displayRefresh_ != display.RefreshRate) {
            displayRefresh_ = display.RefreshRate;
            activeLogged_ = false;
            ResetTiming();
        }
        displayChecked_ = now;
    }
    // Count only references owned by this renderer, never temporary game surfaces.
    const ULONG external = References() - internalReferences_;
    HRESULT hr = S_OK;
    if (!output_ || outputWidth_ != client.right || outputHeight_ != client.bottom || outputWindow_ != window ||
        requestedInterval_ != requestedInterval) {
        output_.Reset();
        activeLogged_ = false;
        ResetTiming();
        D3DPRESENT_PARAMETERS outputParams{};
        outputParams.Windowed = TRUE; outputParams.hDeviceWindow = window;
        outputParams.BackBufferWidth = client.right; outputParams.BackBufferHeight = client.bottom;
        outputParams.BackBufferFormat = params.BackBufferFormat;
        outputParams.BackBufferCount = 1; outputParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
        outputParams.PresentationInterval = requestedInterval;
        hr = device_->CreateAdditionalSwapChain(&outputParams, &output_);
        if (FAILED(hr) && requestedInterval == D3DPRESENT_INTERVAL_ONE && hr != D3DERR_DEVICELOST) {
            Log("window vsync unavailable 0x%08lX; retrying immediate presentation with timer cap", hr);
            outputParams.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
            hr = device_->CreateAdditionalSwapChain(&outputParams, &output_);
        }
        requestedInterval_ = requestedInterval;
        outputInterval_ = outputParams.PresentationInterval;
        if (SUCCEEDED(hr)) {
            D3DPRESENT_PARAMETERS actual{};
            hr = output_->GetPresentParameters(&actual);
            if (SUCCEEDED(hr)) outputInterval_ = actual.PresentationInterval;
        }
        outputWidth_ = client.right; outputHeight_ = client.bottom;
        outputWindow_ = window;
    }
    internalReferences_ = References() - external;
    if (SUCCEEDED(hr)) {
        const unsigned timerRate = PresentationPolicy::TimerRate(limit, outputInterval_, displayRefresh_);
        if (loggedLimit_ != limit || loggedTimerRate_ != timerRate) {
            ResetTiming();
            activeLogged_ = false;
            loggedLimit_ = limit; loggedTimerRate_ = timerRate;
        }
        pacer_.Wait(timerRate);
        frameStart = Milliseconds();
        ComPtr<IDirect3DSurface9> target;
        hr = output_->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &target);
        Settings settings = Configuration();
        if (crop) settings.algorithm = Algorithm::Linear;
        if (SUCCEEDED(hr)) hr = Render(source.Get(), target.Get(), settings, crop ? &login : nullptr);
        const double submitEnd = Milliseconds();
        if (SUCCEEDED(hr)) {
            if (!activeLogged_) {
                const bool neural = Configuration().algorithm == Algorithm::Cunny &&
                    client.right > static_cast<LONG>(description.Width) && client.bottom > static_cast<LONG>(description.Height);
                Log("active: %ux%u -> %ldx%ld, CuNNy=%s, windowed=1, backend=%s, interval=0x%08x, device_refresh=%uHz, max_fps=%u, timer_fps=%u", description.Width, description.Height,
                    client.right, client.bottom, neural ? "yes" : "no",
                    description.Width == client.right && description.Height == client.bottom ? "pixel-copy" :
                    Configuration().algorithm == Algorithm::Linear ? "single-pass-linear" : "shader-resample",
                    outputInterval_, displayRefresh_, limit, timerRate);
                activeLogged_ = true;
            }
            hr = output_->Present(nullptr, nullptr, window, nullptr, 0);
            SetLoginPresentation(window, SUCCEEDED(hr) && crop);
            if (SUCCEEDED(hr)) RecordFrame(frameStart, submitEnd-frameStart, Milliseconds()-submitEnd);
            else ResetTiming();
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
