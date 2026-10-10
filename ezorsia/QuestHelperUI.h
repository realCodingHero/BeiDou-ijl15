#pragma once
#include <windows.h>
#include <cstddef>
namespace QuestHelperUI {
bool Install(bool enabled, const char* configPath);
bool Enabled();
void Open();
bool Receive(const unsigned char* data, size_t size);
void FieldChange();
}
