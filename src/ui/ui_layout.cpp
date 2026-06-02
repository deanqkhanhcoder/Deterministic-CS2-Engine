// ╔══════════════════════════════════════════════════════════════════════╗
// ║  UI Layout Engine — Implementation                                  ║
// ║  Deterministic region-based rendering for all tabs                  ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "ui_layout.h"
#include "ui_theme.h"
#include "debug_logger.h"
#include <algorithm>

namespace layout {

// ════════════════════════════════════════════════════════════════
//  CLIP GUARD
// ════════════════════════════════════════════════════════════════

ClipGuard::ClipGuard(HDC hdc_, const RECT& clipRect) : hdc(hdc_), prevRgn(nullptr), hadPrevious(false) {
    // Save the current clip region
    prevRgn = CreateRectRgn(0, 0, 0, 0);
    int result = GetClipRgn(hdc, prevRgn);
    hadPrevious = (result == 1);  // 1 = had a clip region

    // Apply new clip
    HRGN newRgn = CreateRectRgnIndirect(&clipRect);
    if (hadPrevious) {
        // Intersect with existing clip (don't expand it)
        CombineRgn(newRgn, newRgn, prevRgn, RGN_AND);
    }
    SelectClipRgn(hdc, newRgn);
    DeleteObject(newRgn);
}

ClipGuard::~ClipGuard() {
    if (hadPrevious) {
        SelectClipRgn(hdc, prevRgn);
    } else {
        SelectClipRgn(hdc, nullptr);
    }
    if (prevRgn) DeleteObject(prevRgn);
}

// ════════════════════════════════════════════════════════════════
//  PANEL STACK
// ════════════════════════════════════════════════════════════════

void PanelStack::Init(const RECT& tabContent, int margin) {
    x = tabContent.left + margin;
    y = tabContent.top + margin;
    width = (tabContent.right - tabContent.left) - margin * 2;
    bottomLimit = tabContent.bottom - margin;
    startY = y;
}

RECT PanelStack::NextPanel(int height, int gap) {
    // Clamp height if it would overflow
    int availH = bottomLimit - y;
    if (height > availH) height = availH;
    if (height < 0) height = 0;

    RECT r = { x, y, x + width, y + height };
    y += height + gap;
    return r;
}

void PanelStack::NextPanelPair(int height, int gap, int innerGap, RECT& outLeft, RECT& outRight) {
    int availH = bottomLimit - y;
    if (height > availH) height = availH;
    if (height < 0) height = 0;

    int halfW = (width - innerGap) / 2;
    outLeft = { x, y, x + halfW, y + height };
    outRight = { x + halfW + innerGap, y, x + width, y + height };
    y += height + gap;
}

bool PanelStack::WouldOverflow(int height) const {
    return (y + height) > bottomLimit;
}

int PanelStack::Remaining() const {
    int r = bottomLimit - y;
    return r > 0 ? r : 0;
}

// ════════════════════════════════════════════════════════════════
//  PANEL DRAWING
// ════════════════════════════════════════════════════════════════

RECT DrawPanel(HDC hdc, const RECT& pr, const wchar_t* title) {
    // Background fill
    FillRect(hdc, &pr, ui::GetPanelBrush());

    // Border
    HPEN borderPen = CreatePen(PS_SOLID, 1, theme::CLR_BORDER);
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, nullBrush);
    Rectangle(hdc, pr.left, pr.top, pr.right, pr.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);

    int headerH = 0;

    if (title && title[0]) {
        // Title text
        HFONT oldF = (HFONT)SelectObject(hdc, ui::GetTitleFont());
        TEXTMETRICW tm{};
        GetTextMetricsW(hdc, &tm);
        int fontH = tm.tmHeight + tm.tmExternalLeading;
        headerH = fontH + 12; // 6 padding top and bottom

        SetTextColor(hdc, theme::CLR_ACCENT);
        RECT rTitle = { pr.left + 12, pr.top + 6, pr.right - 12, pr.top + 6 + fontH };
        DrawTextW(hdc, title, -1, &rTitle, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(hdc, oldF);

        // Divider line under header
        HPEN divPen = CreatePen(PS_SOLID, 1, theme::CLR_BORDER);
        HPEN oldDiv = (HPEN)SelectObject(hdc, divPen);
        MoveToEx(hdc, pr.left, pr.top + headerH, nullptr);
        LineTo(hdc, pr.right, pr.top + headerH);
        SelectObject(hdc, oldDiv);
        DeleteObject(divPen);
    }

    // Inner content rect: below header, with padding
    RECT inner = {
        pr.left + 12,
        pr.top + headerH + 6,
        pr.right - 12,
        pr.bottom - theme::PANEL_PAD_BOTTOM
    };
    return inner;
}

// ════════════════════════════════════════════════════════════════
//  ROW DRAWING
// ════════════════════════════════════════════════════════════════

void DrawRow(HDC hdc, int x, int y, int w, int rowH,
             const wchar_t* label, const wchar_t* value, COLORREF valClr, int labelW) {
    HFONT old = (HFONT)SelectObject(hdc, ui::GetBodyFont());
    SetTextColor(hdc, theme::FG_LABEL);
    RECT rl = { x, y, x + labelW, y + rowH };
    DrawTextW(hdc, label, -1, &rl, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SetTextColor(hdc, valClr);
    RECT rv = { x + labelW, y, x + w, y + rowH };
    DrawTextW(hdc, value, -1, &rv, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(hdc, old);
}

void DrawRowCustom(HDC hdc, int x, int y, int labelW, int valW, int rowH,
                   const wchar_t* label, const wchar_t* value, COLORREF valClr) {
    HFONT old = (HFONT)SelectObject(hdc, ui::GetBodyFont());
    SetTextColor(hdc, theme::FG_LABEL);
    RECT rl = { x, y, x + labelW, y + rowH };
    DrawTextW(hdc, label, -1, &rl, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    SelectObject(hdc, ui::GetSmallFont());
    SetTextColor(hdc, valClr);
    RECT rv = { x + labelW, y, x + labelW + valW, y + rowH };
    DrawTextW(hdc, value, -1, &rv, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(hdc, old);
}



// ════════════════════════════════════════════════════════════════
//  SAFE TEXT
// ════════════════════════════════════════════════════════════════

void DrawTextSafe(HDC hdc, const RECT& bounds, const wchar_t* text,
                  COLORREF color, HFONT font, UINT flags) {
    // Validate bounds
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) return;

    ClipGuard cg(hdc, bounds);
    HFONT old = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, color);
    RECT r = bounds;
    DrawTextW(hdc, text, -1, &r, flags | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(hdc, old);
}

// ════════════════════════════════════════════════════════════════
//  LAYOUT VALIDATION (debug builds)
// ════════════════════════════════════════════════════════════════

#ifdef _DEBUG
static bool RectsOverlap(const RECT& a, const RECT& b) {
    return !(a.right <= b.left || b.right <= a.left ||
             a.bottom <= b.top || b.bottom <= a.top);
}

bool ValidateNoOverlap(const RECT& a, const RECT& b, const char* nameA, const char* nameB) {
    if (RectsOverlap(a, b)) {
        DLOG_WARN(UI, "COLLISION: %s [%d,%d,%d,%d] overlaps %s [%d,%d,%d,%d]",
            nameA, a.left, a.top, a.right, a.bottom,
            nameB, b.left, b.top, b.right, b.bottom);
        return false;
    }
    return true;
}

bool ValidateContainment(const RECT& parent, const RECT& child, const char* parentName, const char* childName) {
    if (child.left < parent.left || child.top < parent.top ||
        child.right > parent.right || child.bottom > parent.bottom) {
        ALOG_WARN("Layout", "OVERFLOW: %s [%d,%d,%d,%d] escapes %s [%d,%d,%d,%d]",
                  childName, child.left, child.top, child.right, child.bottom,
                  parentName, parent.left, parent.top, parent.right, parent.bottom);
        return false;
    }
    return true;
}
#endif

} // namespace layout
