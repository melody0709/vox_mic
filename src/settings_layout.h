#pragma once

// Settings window layout engine.
//
// The single place in the settings UI that computes a coordinate. It reads the
// field table (settings_fields.h), the metrics (settings_metrics.h) and the tab
// control's own display rectangle, then creates every widget and remembers each
// one's rectangle in *design space* so the window can be re-laid out when the
// DPI changes.
//
// Widgets keep their control ids, so message handlers keep finding them with
// GetDlgItem. The layout owns geometry only, never behaviour.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vector>

#include "settings_control_ids.h"

namespace settings {

// How a text widget paints its background. The window asks this in
// WM_CTLCOLORSTATIC instead of keeping a parallel list of handles.
enum class TextRole : int {
    Transparent = 0,  // label on a surface that is already painted - no band
    Hint = 1,         // hint text: opaque, grey, changes at runtime
    Opaque = 2,       // text that changes at runtime, needs a real clear first
};

TextRole TextRoleOf(HWND hwnd);

class Layout {
public:
    void build(HWND parent, HINSTANCE instance);

    // False until build() has finished. Sizing messages can arrive while the
    // window is still being created, and re-laying out a half-built window is
    // not meaningful.
    bool ready() const { return m_ready; }

    // Re-emits every widget at the current DPI scale. Call after setDpi().
    void relayout(HWND parent);

    void destroyFonts();

    HWND control(int id) const;  // the interactive control, by id
    HWND hint(int id) const;     // the hint line of the field with that id
    HWND footerButton(int id) const;

    const std::vector<HWND>& pageWidgets(int page) const;
    void showPage(int page);

    // Every widget belonging to one field - its label, control, value label and
    // hint. Used to enable or disable a whole row from one place.
    const std::vector<HWND>& fieldWidgets(int fieldId) const;

    HFONT bodyFont() const { return m_bodyFont; }
    HFONT sectionFont() const { return m_sectionFont; }

    int windowWidthPx() const;
    int windowHeightPx() const;

    // Content height of each page in design pixels - used by the layout to size
    // the window so the tallest page fits without scrolling.
    static int pageContentHeightDesign(int page);

private:
    struct Item {
        HWND hwnd = nullptr;
        int id = 0;
        int page = -1;  // -1 = chrome
        RECT design = {};  // page items: relative to the page origin;
                           // chrome items: relative to the client origin
        TextRole textRole = TextRole::Transparent;
        bool anchoredRight = false;
        // Top edge measured up from the client bottom instead of the client top.
        // The footer sits above the bottom edge, and the client area is shorter
        // than the window by the caption and border, so using the window height
        // as if it were the client height puts the buttons off the client area.
        bool anchoredBottom = false;
        bool sectionFont = false;
        // Width comes from the page's right edge instead of design.right.
        bool stretchRight = false;
        // Height comes from the page's bottom edge, for the page surface.
        bool stretchDown = false;
        // Cover the tab's whole display area rather than the padded content
        // area. Only the page surface uses this.
        bool fillPage = false;
    };

    void add(HWND hwnd, int id, int page, const RECT& design, TextRole role,
             bool stretchRight, bool stretchDown, bool anchoredRight,
             bool anchoredBottom, bool fillPage = false);
    void attachField(int fieldId, HWND hwnd);
    void createChrome(HWND parent, HINSTANCE instance);
    void createPageContent(HWND parent, HINSTANCE instance, int page);
    void resizeChrome();
    void footerButtonRect(int slot, int width, bool anchoredRight, RECT& out) const;
    void placeAll();
    void place(Item& item);
    void applyFont(HWND hwnd, HFONT font);
    void computePageRect(RECT& pageRect) const;
    void makeFonts();

    HWND m_parent = nullptr;
    HWND m_tab = nullptr;
    HFONT m_bodyFont = nullptr;
    HFONT m_sectionFont = nullptr;
    std::vector<Item> m_items;
    std::vector<std::vector<HWND>> m_pageWidgets;
    std::vector<std::pair<int, std::vector<HWND>>> m_fieldWidgets;
    int m_originX = 0;
    int m_originY = 0;
    int m_pageRight = 0;
    int m_pageBottom = 0;
    bool m_ready = false;
};

}  // namespace settings
