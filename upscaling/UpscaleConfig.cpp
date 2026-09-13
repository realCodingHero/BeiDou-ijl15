#include "UpscaleRenderer.h"
#include "INIReader.h"
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace NeuralUpscale {
static std::string Directory() {
    char path[MAX_PATH]{};
    const DWORD size = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (!size || size >= MAX_PATH) return {};
    std::string result(path);
    return result.substr(0, result.find_last_of("\\/") + 1);
}
const Settings& Configuration() {
    static const Settings settings = [] {
        Settings result;
        INIReader config(Directory() + "config.ini");
        if (config.ParseError()) return result;
        result.enabled = config.GetBoolean("upscaling", "enabled", false);
        result.diagnostics = config.GetBoolean("upscaling", "diagnostics", false);
        const std::string algorithm = config.Get("upscaling", "algorithm", "cunny");
        if (algorithm == "linear") result.algorithm = Algorithm::Linear;
        else if (algorithm != "cunny") result.enabled = false;
        const std::string quality = config.Get("upscaling", "quality", "balanced");
        if (quality == "fast") result.quality = Quality::Fast;
        else if (quality != "balanced") result.enabled = false;
        return result;
    }();
    return settings;
}
unsigned FrameLimit() {
    // This one inexpensive control can be changed live for A/B diagnosis.
    // Other settings keep their startup-only behavior.
    thread_local ULONGLONG checked = 0;
    thread_local unsigned limit = 60;
    const auto now = GetTickCount64();
    if (!checked || now-checked >= 1000) {
        checked = now;
        const UINT value = GetPrivateProfileIntA("upscaling", "max_fps", 60, (Directory()+"config.ini").c_str());
        limit = value == 0 ? 0 : value >= 15 && value <= 240 ? value : 60;
    }
    return limit;
}
void Log(const char* format, ...) {
    static std::mutex lock;
    const std::lock_guard<std::mutex> guard(lock);
    static bool first = true;
    FILE* file = fopen((Directory() + "upscaling.log").c_str(), first ? "w" : "a");
    first = false;
    if (!file) return;
    va_list args; va_start(args, format); vfprintf(file, format, args); va_end(args);
    fputc('\n', file); fclose(file);
}
}
