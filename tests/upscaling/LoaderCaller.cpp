#include <windows.h>
extern "C" __declspec(dllexport) HMODULE WINAPI LoadGraphics() {
    HMODULE volatile module = LoadLibraryA("d3d8.dll"); // Prevent tail call.
    return module;
}
