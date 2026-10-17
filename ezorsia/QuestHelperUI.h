#pragma once
#include <windows.h>
#include <cstddef>
namespace QuestHelperUI {
bool Install(bool enabled, const char* configPath);
bool Enabled();
void Open();
bool Receive(const unsigned char* data, size_t size);
void FieldChange();
// Native Maple Helper (NPC 9900001); requires the quest helper and server opcode 0x1004.
bool InstallHelper(bool enabled);
// The cave dllmain installs at 0x0045464B (6 bytes) so the local avatar draws transmog looks.
void* TransmogLookCave();
bool HelperEnabled();
// True once the server acknowledged the Maple Helper protocol on this field.
bool HelperReady();
// 0 hub, 1 super teleport, 2 pet loot settings.
void OpenHelper(int page);
bool ReceiveHelper(const unsigned char* data, size_t size);
}
