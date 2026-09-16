#pragma once

namespace WindowScaling {
void Configure(bool resizable, bool keepAspectRatio);
void LoadPlacement(const char* configPath);
bool Hook(bool enable);
// Native CWndMan key-event bridge (Alt=0x100, key release=0x80000000).
bool HandleNativeFullscreenKey(UINT key, ULONG flags);
#ifdef WINDOW_SCALING_TESTING
void SetNativeKeyForTesting(void* entry);
#endif

// Bridge from the native mouse-message code cave to CInputSystem's cursor vector.
using CursorVectorSetter = void(__fastcall*)(void*, void*, int, int);
void SetNativeCursorPosition(void* input, int x, int y, CursorVectorSetter setter);
void __fastcall DrawNativeCursor(void* input, void*, int x, int y);
}
