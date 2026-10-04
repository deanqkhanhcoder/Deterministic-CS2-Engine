#pragma once
#include <windows.h>

namespace tray {
bool Init(HWND window, HINSTANCE instance, bool suspended);
void Update(bool suspended);
bool HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
void Shutdown();
} // namespace tray
