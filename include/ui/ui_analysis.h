#pragma once
#include <windows.h>

struct RuntimeSnapshot;

namespace ui_analysis {

void Init();
void Destroy();
void ShowButtons(bool show);
void ResetScroll();
void Paint(HDC hdc, RECT rc, const RuntimeSnapshot& snap);
void OnCommand(HWND hwnd, WPARAM wParam);
void OnMouseWheel(int delta);

} // namespace ui_analysis
