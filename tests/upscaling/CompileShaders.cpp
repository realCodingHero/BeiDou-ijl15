#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    for (const auto& file : std::filesystem::directory_iterator(argv[1])) {
        if (file.path().extension() != ".hlsl") continue;
        ID3DBlob *code = nullptr, *errors = nullptr;
        HRESULT hr = D3DCompileFromFile(file.path().c_str(), nullptr, nullptr,
            "main", "ps_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (FAILED(hr)) {
            printf("FAIL %s: %s\n", file.path().filename().string().c_str(), errors ? static_cast<char*>(errors->GetBufferPointer()) : "compile");
            if (errors) errors->Release();
            return 1;
        }
        auto output = file.path(); output.replace_extension(".cso");
        std::ofstream stream(output, std::ios::binary);
        stream.write(static_cast<const char*>(code->GetBufferPointer()), code->GetBufferSize());
        printf("PASS %s (%zu bytes)\n", file.path().filename().string().c_str(), code->GetBufferSize());
        code->Release(); if (errors) errors->Release();
    }
}
