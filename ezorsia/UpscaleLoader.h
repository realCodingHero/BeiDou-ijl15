#pragma once
#include <windows.h>
namespace UpscaleLoader {
// Deferred load only: no graphics initialization in DllMain.
bool Install(HMODULE owner, bool enabled);
}
