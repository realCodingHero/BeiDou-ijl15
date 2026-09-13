#pragma once
#include <d3d9.h>
#include <wrl/client.h>
#include <array>
#include <vector>

namespace NeuralUpscale {
enum class Algorithm { Cunny, Linear };
enum class Quality { Fast, Balanced };
struct Settings {
    bool enabled = false;
    Algorithm algorithm = Algorithm::Cunny;
    Quality quality = Quality::Balanced;
};
const Settings& Configuration();
void Log(const char* format, ...);

class Renderer {
public:
    explicit Renderer(IDirect3DDevice9* device) : device_(device) {}
    ~Renderer();
    HRESULT Render(IDirect3DSurface9* source, IDirect3DSurface9* destination, const Settings& settings);
    bool Present(const RECT* source, const RECT* target, HWND overrideWindow, HRESULT& result);
    void Reset();
    ULONG InternalReferences() const { return internalReferences_; }
private:
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    struct Texture {
        Ptr<IDirect3DTexture9> image;
        Ptr<IDirect3DSurface9> surface;
    };
    HRESULT CreateTexture(Texture& texture, UINT width, UINT height, D3DFORMAT format = D3DFMT_A8R8G8B8);
    HRESULT Prepare(UINT width, UINT height, UINT targetWidth, UINT targetHeight, const Settings& settings);
    HRESULT Draw(IDirect3DSurface9* target, IDirect3DPixelShader9* shader,
        const std::vector<IDirect3DTexture9*>& inputs, UINT inputWidth, UINT inputHeight,
        bool linear = false, const float* axis = nullptr);
    HRESULT RenderImpl(IDirect3DSurface9* source, IDirect3DSurface9* destination, const Settings& settings);
    ULONG References() const;
    IDirect3DDevice9* device_; // Non-owning: renderer is owned by the device wrapper.
    ULONG internalReferences_ = 0;
    UINT width_ = 0, height_ = 0, targetWidth_ = 0, targetHeight_ = 0;
    Settings preparedSettings_;
    Texture input_, groups_[2][3], residual_, doubled_, horizontal_;
    std::vector<Ptr<IDirect3DPixelShader9>> network_;
    Ptr<IDirect3DPixelShader9> copy_, resolve_, resample_;
    Ptr<IDirect3DSwapChain9> output_;
    UINT outputWidth_ = 0, outputHeight_ = 0;
    HWND outputWindow_ = nullptr;
    bool disabledUntilReset_ = false, activeLogged_ = false;
};
}
