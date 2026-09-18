#include "settings_layout.h"

#include <commctrl.h>

#include "settings_fields.h"
#include "settings_metrics.h"
#include "settings_theme.h"

namespace metrics {
namespace {
unsigned s_dpi = 96;
}  // namespace

void setDpi(unsigned dpi) {
    s_dpi = (dpi >= 48 && dpi <= 480) ? dpi : 96;
}

unsigned dpi() { return s_dpi; }

int scalePercent() { return static_cast<int>(s_dpi * 100 / 96); }

int S(int design) {
    if (s_dpi == 96) return design;
    return static_cast<int>((static_cast<long long>(design) * s_dpi + 48) / 96);
}

}  // namespace metrics

// ---------------------------------------------------------------------------
// Coordinate systems
//
// Everything the layout remembers is in DESIGN pixels (96 DPI). Exactly one
// function converts to physical pixels: place(), which is also the only place a
// control is ever positioned or sized. Keeping the conversion in one place is
// what stops fonts from scaling while the boxes around them do not.
// ---------------------------------------------------------------------------

namespace settings {
namespace {

using metrics::S;

constexpr int kChromePage = -1;
constexpr int kRuleH = 2;
constexpr wchar_t kRoleProperty[] = L"VoxMic.TextRole";

HWND CreateStatic(HWND parent, HINSTANCE inst, int id, DWORD extraStyle) {
    return CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | extraStyle, 0, 0,
                           10, 10, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst,
                           nullptr);
}

// Row height and hint height. Both the page-height estimate and the placement
// loop call these, so the two can never disagree about how tall a page is.
int rowHeightDesign(const FieldSpec& f) {
    if (f.kind == FieldKind::Note) return f.hintLines * metrics::HintH;
    if (f.kind == FieldKind::Status) {
        const int lines = f.hintLines > 0 ? f.hintLines : 1;
        const int linesH = lines * metrics::StatusLineH;
        return (linesH > metrics::RowH) ? linesH : metrics::RowH;
    }
    return metrics::RowH;
}

int hintHeightDesign(const FieldSpec& f) {
    if (f.kind == FieldKind::Note || f.kind == FieldKind::Status) return 0;
    return f.hintLines * metrics::HintH;
}

int fieldWidthDesign(const FieldSpec& f) {
    if (f.kind == FieldKind::Number) return metrics::FieldWNumber;
    if (f.kind == FieldKind::Choice) return metrics::FieldWChoice;
    return metrics::FieldWText;
}

}  // namespace

TextRole TextRoleOf(HWND hwnd) {
    if (!hwnd) return TextRole::Transparent;
    const HANDLE v = GetPropW(hwnd, kRoleProperty);
    const int role = static_cast<int>(reinterpret_cast<INT_PTR>(v));
    if (role < 0 || role > 2) return TextRole::Transparent;
    return static_cast<TextRole>(role);
}

int Layout::pageContentHeightDesign(int page) {
    if (page < 0 || page >= kPageCount) return 0;
    const PageSpec& spec = kPages[page];
    int h = 0;
    for (int s = 0; s < spec.count; ++s) {
        const SectionSpec& sec = spec.sections[s];
        if (s > 0) h += metrics::GapBeforeSection;
        if (sec.title) {
            h += metrics::SectionTitleH + kRuleH + metrics::GapAfterSectionTitle;
        }
        for (int f = 0; f < sec.count; ++f) {
            const FieldSpec& fld = sec.fields[f];
            if (fld.sameRowAsPrevious) continue;
            h += rowHeightDesign(fld) + hintHeightDesign(fld);
        }
    }
    return h;
}

// ---------------------------------------------------------------------------

void Layout::makeFonts() {
    if (m_bodyFont) { DeleteObject(m_bodyFont); m_bodyFont = nullptr; }
    if (m_sectionFont) { DeleteObject(m_sectionFont); m_sectionFont = nullptr; }
    const char* face = metrics::FontFamily;
    m_bodyFont = CreateFontA(-S(metrics::FontBody), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                             FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, face);
    m_sectionFont = CreateFontA(-S(metrics::FontSection), 0, 0, 0, FW_SEMIBOLD, FALSE,
                                FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, face);
    makeBrushes();
}

void Layout::destroyResources() {
    if (m_bodyFont) { DeleteObject(m_bodyFont); m_bodyFont = nullptr; }
    if (m_sectionFont) { DeleteObject(m_sectionFont); m_sectionFont = nullptr; }
    if (m_windowBrush) { DeleteObject(m_windowBrush); m_windowBrush = nullptr; }
    if (m_panelBrush) { DeleteObject(m_panelBrush); m_panelBrush = nullptr; }
    if (m_strokeBrush) { DeleteObject(m_strokeBrush); m_strokeBrush = nullptr; }
}

void Layout::makeBrushes() {
    if (!m_windowBrush) m_windowBrush = CreateSolidBrush(theme::Window);
    if (!m_panelBrush) m_panelBrush = CreateSolidBrush(theme::Panel);
    if (!m_strokeBrush) m_strokeBrush = CreateSolidBrush(theme::Stroke);
}

void Layout::applyFont(HWND hwnd, HFONT font) {
    if (hwnd && font)
        SendMessageA(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

int Layout::windowWidthPx() const { return S(metrics::WinW); }
int Layout::windowHeightPx() const { return S(metrics::WinH); }

void Layout::add(HWND hwnd, int id, int page, const RECT& design, TextRole role,
                 bool stretchRight, bool stretchDown, bool anchoredRight,
                 bool anchoredBottom, bool fillPage) {
    if (!hwnd) return;
    Item it;
    it.hwnd = hwnd;
    it.id = id;
    it.page = page;
    it.design = design;
    it.textRole = role;
    it.stretchRight = stretchRight;
    it.stretchDown = stretchDown;
    it.anchoredRight = anchoredRight;
    it.anchoredBottom = anchoredBottom;
    it.fillPage = fillPage;
    m_items.push_back(it);
    SetPropW(hwnd, kRoleProperty,
             reinterpret_cast<HANDLE>(static_cast<INT_PTR>(role)));
    if (page >= 0 && page < static_cast<int>(m_pageWidgets.size())) {
        m_pageWidgets[page].push_back(hwnd);
    }
}

void Layout::attachField(int fieldId, HWND hwnd) {
    if (!fieldId || !hwnd) return;
    for (auto& entry : m_fieldWidgets) {
        if (entry.first == fieldId) {
            entry.second.push_back(hwnd);
            return;
        }
    }
    m_fieldWidgets.push_back({fieldId, {hwnd}});
}

const std::vector<HWND>& Layout::fieldWidgets(int fieldId) const {
    static const std::vector<HWND> empty;
    for (const auto& entry : m_fieldWidgets) {
        if (entry.first == fieldId) return entry.second;
    }
    return empty;
}

void Layout::computePageRect(RECT& pageRect) const {
    RECT client;
    GetClientRect(m_parent, &client);
    pageRect.left = S(metrics::PadX);
    pageRect.top = S(metrics::PadTop);
    pageRect.right = client.right - S(metrics::PadX);
    pageRect.bottom = client.bottom - S(metrics::FooterH) - S(metrics::GapAfterTab);
    if (pageRect.bottom <= pageRect.top) pageRect.bottom = pageRect.top + S(100);
    if (pageRect.right <= pageRect.left) pageRect.right = pageRect.left + S(100);
    TabCtrl_AdjustRect(m_tab, FALSE, &pageRect);
}

void Layout::place(Item& item) {
    RECT client;
    GetClientRect(m_parent, &client);

    int x = S(item.design.left);
    int y = S(item.design.top);
    if (item.page >= 0) {
        x += m_originX;
        y += m_originY;
    }
    if (item.anchoredRight) x += client.right - S(metrics::WinW);
    if (item.anchoredBottom) {
        y = client.bottom - S(item.design.bottom - item.design.top) -
            S(metrics::FooterH - metrics::FooterBtnY);
    }

    int w = S(item.design.right - item.design.left);
    int h = S(item.design.bottom - item.design.top);
    if (item.fillPage && item.page >= 0) {
        // The tab paints its own display area in a slightly different shade than
        // the window, so the surface has to cover that whole area - not just the
        // padded content area - or the page shows two colours.
        x = m_originX - S(metrics::PadPageX);
        y = m_originY - S(metrics::PadPageY);
        w = (m_pageRight + S(metrics::PadPageX)) - x;
        h = (m_pageBottom + S(metrics::PadPageY)) - y;
    } else {
        if (item.stretchRight && item.page >= 0) w = m_pageRight - x;
        if (item.stretchDown && item.page >= 0) h = m_pageBottom - y;
    }
    SetWindowPos(item.hwnd, nullptr, x, y, w > 0 ? w : 1, h > 0 ? h : 1,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void Layout::placeAll() {
    RECT pageRect;
    computePageRect(pageRect);
    m_originX = pageRect.left + S(metrics::PadPageX);
    m_originY = pageRect.top + S(metrics::PadPageY);
    m_pageRight = pageRect.right - S(metrics::PadPageX);
    m_pageBottom = pageRect.bottom - S(metrics::PadPageY);
    for (Item& it : m_items) place(it);
}

// ---------------------------------------------------------------------------

void Layout::build(HWND parent, HINSTANCE instance) {
    m_parent = parent;
    m_items.clear();
    m_fieldWidgets.clear();
    m_pageWidgets.assign(kPageCount, {});
    m_ready = false;
    makeFonts();
    createChrome(parent, instance);

    int needed = 0;
    for (int p = 0; p < kPageCount; ++p) {
        const int h = pageContentHeightDesign(p);
        if (h > needed) needed = h;
    }

    RECT pageRect;
    computePageRect(pageRect);
    const int available = (pageRect.bottom - pageRect.top) - 2 * S(metrics::PadPageY);
    // `needed` is in design pixels and `available` in physical pixels - convert
    // before comparing, then grow the window by the physical shortfall.
    const int neededPx = S(needed);
    if (neededPx > available) {
        // The window is fixed-size and does not scroll, so when the window is
        // smaller than the tallest page needs - a scaled display on a short
        // screen, say - the window is what grows.
        RECT wr;
        GetWindowRect(parent, &wr);
        SetWindowPos(parent, nullptr, 0, 0, wr.right - wr.left,
                     (wr.bottom - wr.top) + (neededPx - available),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    resizeChrome();
    for (int p = 0; p < kPageCount; ++p) createPageContent(parent, instance, p);
    placeAll();
    showPage(0);
    m_ready = true;
}

void Layout::resizeChrome() {
    if (!m_tab || !m_parent) return;
    RECT client;
    GetClientRect(m_parent, &client);
    const int tabW = client.right - 2 * S(metrics::PadX);
    const int tabH = client.bottom - S(metrics::PadTop) - S(metrics::GapAfterTab) -
                     S(metrics::FooterH);
    SetWindowPos(m_tab, nullptr, S(metrics::PadX), S(metrics::PadTop),
                 tabW > 0 ? tabW : 1, tabH > 0 ? tabH : 1,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    applyFont(m_tab, m_bodyFont);
}

// Footer buttons: x from the left edge of the design window, and the vertical
// position is taken from the client bottom by place(), because the footer sits
// above the bottom edge and the client is shorter than the window.
void Layout::footerButtonRect(int slot, int width, bool anchoredRight,
                              RECT& out) const {
    const int x = anchoredRight
                      ? (metrics::WinW - metrics::PadX -
                         slot * (metrics::FooterBtnW + metrics::FooterBtnGap) - width)
                      : metrics::PadX;
    out.left = x;
    out.top = 0;
    out.right = x + width;
    out.bottom = metrics::FooterBtnH;
}

void Layout::createChrome(HWND parent, HINSTANCE instance) {
    RECT client;
    GetClientRect(parent, &client);

    const int tabW = client.right - 2 * S(metrics::PadX);
    const int tabH = client.bottom - S(metrics::PadTop) - S(metrics::GapAfterTab) -
                     S(metrics::FooterH);
    m_tab = CreateWindowExA(
        0, WC_TABCONTROLA, "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0,
        tabW > 0 ? tabW : 1, tabH > 0 ? tabH : 1, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TAB_MAIN)), instance,
        nullptr);
    applyFont(m_tab, m_bodyFont);
    TCITEMA item = {};
    item.mask = TCIF_TEXT;
    for (int p = 0; p < kPageCount; ++p) {
        item.pszText = const_cast<LPSTR>(kPages[p].tabTitle);
        TabCtrl_InsertItem(m_tab, p, &item);
    }

    // Footer buttons. `slot` counts leftwards from the right edge; the leftmost
    // button is wider because its caption is longer.
    struct FooterBtn {
        int id;
        const char* text;
        bool right;
        int slot;
        int width;
    };
    const FooterBtn buttons[] = {
        {IDC_BTN_RESET, "Reset to Defaults", false, 0, metrics::FooterBtnWideW},
        {IDC_BTN_CANCEL, "Cancel", true, 2, metrics::FooterBtnW},
        {IDC_BTN_APPLY, "Apply", true, 1, metrics::FooterBtnW},
        {IDC_BTN_OK, "OK", true, 0, metrics::FooterBtnW},
    };
    for (const FooterBtn& b : buttons) {
        const DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                            (b.id == IDC_BTN_OK ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
        HWND hwnd = CreateWindowExA(
            0, "BUTTON", b.text, style, 0, 0, 10, 10, parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(b.id)), instance, nullptr);
        applyFont(hwnd, m_bodyFont);
        RECT design;
        footerButtonRect(b.slot, b.width, b.right, design);
        add(hwnd, b.id, kChromePage, design, TextRole::Opaque, false, false, b.right,
            true);
    }
}

void Layout::createPageContent(HWND parent, HINSTANCE instance, int page) {
    const PageSpec& spec = kPages[page];
    const int ctrlOffset = (metrics::RowH - metrics::CtrlH) / 2;

    // The page surface. The tab control paints its own display area in a
    // slightly different shade than the window, so every widget drawn on the
    // page would otherwise sit on two colours at once. One surface we own makes
    // the page a single colour. Created first, so it sits under the fields.
    {
        HWND surface = CreateStatic(parent, instance, 0, SS_LEFT);
        add(surface, 0, page, {0, 0, 0, 0}, TextRole::Opaque, false, false, false,
            false, true);
    }

    int y = 0;  // design space, relative to the page origin
    int sameRowSlot = 0;
    int lastRowY = 0;

    // Every widget of one field is registered against that field, so the window
    // can enable or disable a whole row - label, control, value label, hint -
    // from one place instead of maintaining parallel handle lists.
    int currentField = 0;
    auto push = [&](HWND hwnd, int id, int dx, int dy, int dw, int dh, TextRole role,
                    bool stretch) {
        RECT design = {dx, dy, dx + dw, dy + dh};
        add(hwnd, id, page, design, role, stretch, false, false, false);
        attachField(currentField, hwnd);
    };

    for (int s = 0; s < spec.count; ++s) {
        const SectionSpec& sec = spec.sections[s];
        currentField = 0;  // section headers and rules belong to no field
        if (s > 0) y += metrics::GapBeforeSection;

        if (sec.title) {
            HWND label = CreateStatic(parent, instance, 0, SS_LEFT);
            SetWindowTextA(label, sec.title);
            applyFont(label, m_sectionFont);
            push(label, 0, 0, y, 0, metrics::SectionTitleH, TextRole::Transparent,
                 true);
            m_items.back().sectionFont = true;
            y += metrics::SectionTitleH;

            // A full-width hairline under the title. Deliberately not measured
            // against the title text: measuring would drag physical pixels into
            // a design-space rect.
            HWND rule = CreateStatic(parent, instance, 0, SS_ETCHEDHORZ);
            push(rule, 0, 0, y, 0, kRuleH, TextRole::Transparent, true);
            y += kRuleH + metrics::GapAfterSectionTitle;
        }

        for (int f = 0; f < sec.count; ++f) {
            const FieldSpec& fld = sec.fields[f];
            currentField = fld.id;

            if (fld.sameRowAsPrevious) {
                ++sameRowSlot;
                const int cellX = sameRowSlot * metrics::ToggleColW;
                HWND box = CreateWindowExA(
                    0, "BUTTON", fld.label,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 10,
                    10, parent,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(fld.id)), instance,
                    nullptr);
                applyFont(box, m_bodyFont);
                push(box, fld.id, cellX, lastRowY + ctrlOffset, metrics::ToggleColW,
                     metrics::ToggleH, TextRole::Transparent, false);
                continue;
            }

            sameRowSlot = 0;
            lastRowY = y;
            const int rowH = rowHeightDesign(fld);

            switch (fld.kind) {
                case FieldKind::Note: {
                    HWND note = CreateStatic(parent, instance, 0, SS_LEFT);
                    if (fld.hint) SetWindowTextA(note, fld.hint);
                    applyFont(note, m_bodyFont);
                    push(note, 0, 0, y, 0, fld.hintLines * metrics::HintH,
                         TextRole::Transparent, true);
                    break;
                }
                case FieldKind::Status: {
                    // A status row can opt into owner-draw so it can carry a
                    // semantic colour from the engine state; the window paints
                    // those in WM_DRAWITEM and clears the area itself.
                    HWND status = CreateStatic(
                        parent, instance, fld.id,
                        SS_LEFT | SS_NOPREFIX | (fld.ownerDraw ? SS_OWNERDRAW : 0));
                    applyFont(status, m_bodyFont);
                    push(status, fld.id, 0, y, 0, rowH,
                         fld.ownerDraw ? TextRole::Transparent : TextRole::Opaque,
                         true);
                    break;
                }
                case FieldKind::Toggle: {
                    HWND box = CreateWindowExA(
                        0, "BUTTON", fld.label,
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 10,
                        10, parent,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(fld.id)),
                        instance, nullptr);
                    applyFont(box, m_bodyFont);
                    push(box, fld.id, 0, y + ctrlOffset, metrics::ToggleColW,
                         metrics::ToggleH, TextRole::Opaque, false);
                    break;
                }
                case FieldKind::Text:
                case FieldKind::Number:
                case FieldKind::Choice: {
                    HWND label = CreateStatic(parent, instance, 0, SS_LEFT);
                    if (fld.label) SetWindowTextA(label, fld.label);
                    applyFont(label, m_bodyFont);
                    push(label, 0, 0, y + ctrlOffset, metrics::ColLabelW,
                         metrics::CtrlH, TextRole::Transparent, false);

                    const int w = fieldWidthDesign(fld);
                    HWND ctrl = nullptr;
                    if (fld.kind == FieldKind::Choice) {
                        ctrl = CreateWindowExA(
                            0, "COMBOBOX", "",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                                WS_VSCROLL,
                            0, 0, 10, S(160), parent,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(fld.id)),
                            instance, nullptr);
                        for (int o = 0; o < fld.optionCount; ++o) {
                            SendMessageA(ctrl, CB_ADDSTRING, 0,
                                         reinterpret_cast<LPARAM>(fld.options[o]));
                        }
                        SendMessageA(ctrl, CB_SETDROPPEDWIDTH, S(w), 0);
                    } else {
                        const DWORD style =
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL |
                            (fld.kind == FieldKind::Number ? ES_NUMBER : 0);
                        ctrl = CreateWindowExA(
                            WS_EX_CLIENTEDGE, "EDIT", "", style, 0, 0, 10, 10, parent,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(fld.id)),
                            instance, nullptr);
                    }
                    applyFont(ctrl, m_bodyFont);
                    push(ctrl, fld.id, metrics::ColFieldX, y + ctrlOffset, w,
                         metrics::CtrlH, TextRole::Opaque, false);

                    if (fld.trailId) {
                        HWND trail = CreateWindowExA(
                            0, "BUTTON", fld.trailLabel,
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0,
                            10, 10, parent,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(fld.trailId)),
                            instance, nullptr);
                        applyFont(trail, m_bodyFont);
                        push(trail, fld.trailId,
                             metrics::ColFieldX + w + metrics::GapFieldTrail,
                             y + ctrlOffset, metrics::TrailW, metrics::CtrlH,
                             TextRole::Opaque, false);
                    }
                    break;
                }
                case FieldKind::Slider: {
                    HWND label = CreateStatic(parent, instance, 0, SS_LEFT);
                    if (fld.label) SetWindowTextA(label, fld.label);
                    applyFont(label, m_bodyFont);
                    push(label, 0, 0, y + ctrlOffset, metrics::ColLabelW,
                         metrics::CtrlH, TextRole::Transparent, false);

                    HWND track = CreateWindowExA(
                        0, TRACKBAR_CLASSA, "",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_TOOLTIPS,
                        0, 0, 10, 10, parent,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(fld.id)),
                        instance, nullptr);
                    SendMessageA(track, TBM_SETRANGE, TRUE,
                                 MAKELONG(fld.slider.min, fld.slider.max));
                    SendMessageA(track, TBM_SETTICFREQ, fld.slider.tic, 0);
                    push(track, fld.id, metrics::ColFieldX, y + ctrlOffset,
                         metrics::SliderW, metrics::CtrlH, TextRole::Opaque, false);

                    if (fld.valueId) {
                        // The value text changes on every slider move, so it
                        // needs its own background to clear the old string.
                        HWND value = CreateStatic(
                            parent, instance, fld.valueId, SS_LEFT | SS_NOPREFIX);
                        applyFont(value, m_bodyFont);
                        push(value, fld.valueId,
                             metrics::ColFieldX + metrics::SliderW +
                                 metrics::GapFieldTrail,
                             y + ctrlOffset, metrics::ValueW, metrics::CtrlH,
                             TextRole::Opaque, false);
                    }
                    break;
                }
            }

            y += rowH;

            if (hintHeightDesign(fld) > 0) {
                HWND hint =
                    CreateStatic(parent, instance, fld.hintId, SS_LEFT | SS_NOPREFIX);
                if (fld.hint) SetWindowTextA(hint, fld.hint);
                applyFont(hint, m_bodyFont);
                push(hint, fld.id, 0, y, 0, metrics::HintH, TextRole::Hint, true);
                y += hintHeightDesign(fld);
            }
        }
    }
}

// ---------------------------------------------------------------------------

void Layout::relayout(HWND parent) {
    if (!m_ready) return;
    m_parent = parent;
    makeFonts();
    resizeChrome();

    // Re-apply the freshly scaled fonts; section headers keep the semibold face.
    for (Item& it : m_items) {
        applyFont(it.hwnd, it.sectionFont ? m_sectionFont : m_bodyFont);
    }

    // Chrome rectangles are derived from the metrics rather than remembered, so
    // re-derive them at the new scale.
    static const struct { int id; bool right; int slot; int width; } kFooter[] = {
        {IDC_BTN_RESET, false, 0, metrics::FooterBtnWideW},
        {IDC_BTN_CANCEL, true, 2, metrics::FooterBtnW},
        {IDC_BTN_APPLY, true, 1, metrics::FooterBtnW},
        {IDC_BTN_OK, true, 0, metrics::FooterBtnW},
    };
    for (const auto& f : kFooter) {
        for (Item& it : m_items) {
            if (it.id != f.id || it.page != kChromePage) continue;
            footerButtonRect(f.slot, f.width, f.right, it.design);
            it.anchoredRight = f.right;
        }
    }
    placeAll();
}

HWND Layout::control(int id) const {
    if (!m_parent) return nullptr;
    return GetDlgItem(m_parent, id);
}

HWND Layout::footerButton(int id) const {
    for (const Item& it : m_items) {
        if (it.page == kChromePage && it.id == id) return it.hwnd;
    }
    return nullptr;
}

HWND Layout::hint(int id) const {
    for (const Item& it : m_items) {
        if (it.textRole == TextRole::Hint && it.id == id) return it.hwnd;
    }
    return nullptr;
}

const std::vector<HWND>& Layout::pageWidgets(int page) const {
    static const std::vector<HWND> empty;
    if (page < 0 || page >= static_cast<int>(m_pageWidgets.size())) return empty;
    return m_pageWidgets[page];
}

void Layout::showPage(int page) {
    for (int p = 0; p < static_cast<int>(m_pageWidgets.size()); ++p) {
        const int cmd = (p == page) ? SW_SHOW : SW_HIDE;
        for (HWND h : m_pageWidgets[p]) ShowWindow(h, cmd);
    }
}

}  // namespace settings
