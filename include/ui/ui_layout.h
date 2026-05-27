#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  UI Layout Engine — Deterministic Region-Based Rendering            ║
// ║  Shared by all tabs: Dashboard, Settings, Log, Analysis             ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <windows.h>

namespace ui {
    HFONT GetTitleFont();
    HFONT GetBodyFont();
    HFONT GetSmallFont();
    HBRUSH GetPanelBrush();
}

namespace layout {

// ════════════════════════════════════════════════════════════════
//  CLIP GUARD — RAII clip region push/restore
// ════════════════════════════════════════════════════════════════
// Usage: { ClipGuard cg(hdc, rect); /* render safely */ }
struct ClipGuard {
    HDC hdc;
    HRGN prevRgn;
    bool hadPrevious;

    ClipGuard(HDC hdc, const RECT& clipRect);
    ~ClipGuard();

    // Non-copyable
    ClipGuard(const ClipGuard&) = delete;
    ClipGuard& operator=(const ClipGuard&) = delete;
};

// ════════════════════════════════════════════════════════════════
//  PANEL STACK — Deterministic vertical stacking cursor
// ════════════════════════════════════════════════════════════════
// Tracks position and prevents overflow. All panels are stacked
// top-to-bottom with the cursor advancing after each one.
struct PanelStack {
    int x;              // Left edge
    int y;              // Current cursor Y position
    int width;          // Available width
    int bottomLimit;    // Absolute bottom Y (must never exceed)
    int startY;         // Original start Y for diagnostics

    // Initialize from tab content rect
    void Init(const RECT& tabContent, int margin);

    // Reserve a panel rect of given height, advance cursor by height + gap
    RECT NextPanel(int height, int gap);

    // Reserve two side-by-side panels (left and right halves)
    void NextPanelPair(int height, int gap, int innerGap, RECT& outLeft, RECT& outRight);

    // Check if a panel of given height would overflow
    bool WouldOverflow(int height) const;

    // Remaining vertical space
    int Remaining() const;

    // Current Y (read-only access)
    int CursorY() const { return y; }
};

// ════════════════════════════════════════════════════════════════
//  PANEL DRAWING — Shared bordered panel with header
// ════════════════════════════════════════════════════════════════

// Total chrome (header + padding) added by DrawPanel for a titled panel.
// Content of height H needs a panel of height PanelHeight(H).
constexpr int TITLED_PANEL_CHROME = 30 + 6 + 10;  // header(30) + topPad(6) + botPad(PANEL_PAD_BOTTOM=10) = 46

// Calculate total panel height needed for a given content height.
// Use this instead of manual arithmetic to prevent chrome miscalculation.
inline int PanelHeight(int contentH) { return contentH + TITLED_PANEL_CHROME; }

// Draw a bordered panel frame with optional title header.
// Returns the inner content RECT (below header, inside padding).
RECT DrawPanel(HDC hdc, const RECT& panelRect, const wchar_t* title);

// ════════════════════════════════════════════════════════════════
//  ROW DRAWING — Shared label:value row
// ════════════════════════════════════════════════════════════════

// Draw a label:value row. labelW is the label column width.
void DrawRow(HDC hdc, int x, int y, int w, int rowH,
             const wchar_t* label, const wchar_t* value, COLORREF valClr,
             int labelW = 220);

// Draw a label:value row using custom fonts for label and value
void DrawRowCustom(HDC hdc, int x, int y, int labelW, int valW, int rowH,
                   const wchar_t* label, const wchar_t* value, COLORREF valClr);



// ════════════════════════════════════════════════════════════════
//  SAFE TEXT — Bounds-validated text rendering
// ════════════════════════════════════════════════════════════════

// Draw text safely within bounds. Clips to rect, supports ellipsis.
void DrawTextSafe(HDC hdc, const RECT& bounds, const wchar_t* text,
                  COLORREF color, HFONT font, UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER);

// ════════════════════════════════════════════════════════════════
//  LAYOUT VALIDATION — Debug collision detection
// ════════════════════════════════════════════════════════════════

#ifdef _DEBUG
// Validate that two rects don't overlap. Logs warning if they do.
bool ValidateNoOverlap(const RECT& a, const RECT& b, const char* nameA, const char* nameB);

// Validate that child rect fits inside parent. Logs warning if not.
bool ValidateContainment(const RECT& parent, const RECT& child, const char* parentName, const char* childName);
#endif

} // namespace layout
