#pragma once
#include <windows.h>
#include <climits>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include "INIReader.h"

namespace WindowPlacementConfig {
struct Placement {
    int width = 1280, height = 720;
    int x = 0, y = 0;
    bool hasPosition = false, maximized = false;
    int normalWidth = 1280, normalHeight = 720;
    int normalX = 0, normalY = 0;
};

inline Placement Read(const std::string& path, Placement fallback) {
    if (path.empty()) return fallback;
    INIReader reader(path);
    if (reader.ParseError() != 0) return fallback;
    auto dimension = [&](const char* key, int value) {
        const long read = reader.GetInteger("general", key, value);
        return read > 0 && read <= 32767 ? static_cast<int>(read) : value;
    };
    fallback.width = dimension("width", fallback.width);
    fallback.height = dimension("height", fallback.height);
    const long x = reader.GetInteger("general", "window_x", LONG_MIN);
    const long y = reader.GetInteger("general", "window_y", LONG_MIN);
    if (x >= -100000 && x <= 100000 && y >= -100000 && y <= 100000) {
        fallback.x = x;
        fallback.y = y;
        fallback.hasPosition = true;
    }
    fallback.maximized = reader.GetBoolean("general", "window_maximized", false);
    fallback.normalWidth = dimension("window_restore_width", fallback.width);
    fallback.normalHeight = dimension("window_restore_height", fallback.height);
    fallback.normalX = reader.GetInteger("general", "window_restore_x", fallback.x);
    fallback.normalY = reader.GetInteger("general", "window_restore_y", fallback.y);
    if (fallback.normalX < -100000 || fallback.normalX > 100000) fallback.normalX = fallback.x;
    if (fallback.normalY < -100000 || fallback.normalY > 100000) fallback.normalY = fallback.y;
    return fallback;
}

inline std::string TrimLower(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
    for (char& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return text;
}

// Change only our scalar keys. Keep all other bytes, comments and file encoding.
inline std::string Update(std::string contents, std::map<std::string, std::string> values) {
    const std::string newline = contents.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string output;
    bool general = false, found = false;
    auto appendMissing = [&]() {
        if (!output.empty() && output.back() != '\n') output += newline;
        for (const auto& value : values) output += value.first + "=" + value.second + newline;
        values.clear();
    };
    for (size_t start = 0; start < contents.size();) {
        const size_t end = contents.find('\n', start);
        std::string line = contents.substr(start, end == std::string::npos ? end : end + 1 - start);
        std::string trimmed = TrimLower(line);
        if (start == 0 && trimmed.compare(0, 3, "\xef\xbb\xbf") == 0) trimmed.erase(0, 3);
        if (!trimmed.empty() && trimmed.front() == '[') {
            if (general) appendMissing();
            const size_t close = trimmed.find(']');
            general = close != std::string::npos && TrimLower(trimmed.substr(1, close - 1)) == "general";
            found |= general;
        } else if (general && !trimmed.empty() && trimmed[0] != ';' && trimmed[0] != '#') {
            const size_t separator = line.find_first_of("=:");
            const auto value = values.find(TrimLower(line.substr(0, separator)));
            if (separator != std::string::npos && value != values.end()) {
                size_t begin = line.find_first_not_of(" \t", separator + 1);
                if (begin == std::string::npos) begin = separator + 1;
                size_t finish = line.find_first_of(";\r\n", begin);
                if (finish == std::string::npos) finish = line.size();
                while (finish > begin && (line[finish - 1] == ' ' || line[finish - 1] == '\t')) --finish;
                line.replace(begin, finish - begin, value->second);
                values.erase(value);
            }
        }
        output += line;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (!found) {
        if (!output.empty() && output.back() != '\n') output += newline;
        output += "[general]" + newline;
    }
    appendMissing();
    return output;
}

inline bool Save(const std::string& path, const Placement& state, int renderWidth, int renderHeight) {
    if (path.empty() || !state.hasPosition) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false; // Do not replace a missing/unreadable config with a partial one.
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) return false;
    input.close();
    std::map<std::string, std::string> values;
    auto put = [&](const char* key, int value) { values[key] = std::to_string(value); };
    put("width", state.width); put("height", state.height);
    put("window_x", state.x); put("window_y", state.y);
    values["window_maximized"] = state.maximized ? "true" : "false";
    put("window_restore_width", state.normalWidth); put("window_restore_height", state.normalHeight);
    put("window_restore_x", state.normalX); put("window_restore_y", state.normalY);
    // Saving a smaller/rounded window must not change the inferred render resolution next launch.
    INIReader reader(path);
    if (reader.Get("general", "render_width", "").empty()) put("render_width", renderWidth);
    if (reader.Get("general", "render_height", "").empty()) put("render_height", renderHeight);
    const std::string updated = Update(contents, values);
    const auto slash = path.find_last_of("\\/");
    const std::string directory = slash == std::string::npos ? "." : path.substr(0, slash + 1);
    char temporary[MAX_PATH]{};
    if (!GetTempFileNameA(directory.c_str(), "bwp", 0, temporary)) return false;
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.write(updated.data(), updated.size());
    output.close();
    const bool ok = output.good() && ReplaceFileA(path.c_str(), temporary, nullptr, 0, nullptr, nullptr);
    if (!ok) DeleteFileA(temporary);
    return ok;
}
}
