#pragma once

// Setting-window metrics: the single source of truth for every size, gap and
// font height used by the settings UI.
//
// Values are written in design pixels at 96 DPI. S() converts them to physical
// pixels for the window's current DPI. Nothing outside this header may hardcode
// a control position, size or gap - that is exactly what this table exists to
// prevent, so that adding a new option never means inventing new geometry.
//
// This header deliberately includes no Windows headers: it is shared with the
// field table, which must stay pure data.

namespace metrics {

// --- DPI ------------------------------------------------------------------

// Current window DPI. 96 = 100% scaling. Set once per window and updated from
// WM_DPICHANGED.
void setDpi(unsigned dpi);
unsigned dpi();
int scalePercent();

// Design pixels -> physical pixels, rounded to nearest.
int S(int design);
// --- window / chrome ------------------------------------------------------

constexpr int WinW = 560;
constexpr int WinH = 620;
constexpr int PadX = 16;         // window edge -> tab control
constexpr int PadTop = 12;
constexpr int GapAfterTab = 8;
constexpr int FooterH = 60;      // divider + button row
constexpr int FooterBtnW = 84;
constexpr int FooterBtnWideW = 150;  // "Reset to Defaults" needs more room
constexpr int FooterBtnH = 28;
constexpr int FooterBtnGap = 8;
constexpr int FooterBtnY = 22;   // inside the footer band, below the divider

// --- page content ---------------------------------------------------------

constexpr int PadPageX = 16;     // page edge -> content
constexpr int PadPageY = 12;
constexpr int ColLabelW = 110;   // left label column
constexpr int GapLabelField = 10;
constexpr int ColFieldX = ColLabelW + GapLabelField;

constexpr int RowH = 30;         // one label+control row
constexpr int CtrlH = 24;        // every interactive control is this tall
constexpr int ToggleH = 24;
constexpr int SectionTitleH = 20;
constexpr int GapBeforeSection = 14;
constexpr int GapAfterSectionTitle = 6;
constexpr int HintH = 20;
constexpr int StatusLineH = 20;

constexpr int FieldWText = 280;
constexpr int FieldWNumber = 110;
constexpr int FieldWChoice = 280;
constexpr int SliderW = 260;
constexpr int ValueW = 56;       // slider value label
constexpr int GapFieldTrail = 8;
constexpr int TrailW = 76;       // trailing action button, e.g. Refresh
constexpr int ToggleW = 360;     // full-width checkbox row
constexpr int ToggleColW = 240;  // paired checkboxes (240 * 2 <= 488)

// --- typography -----------------------------------------------------------

// Pixel height of the em box at 96 DPI. Negative in CreateFont means character
// height, which is what we want - it keeps the rendered size predictable
// instead of depending on the face's cell metrics.
constexpr int FontBody = 12;
constexpr int FontSection = 12;

constexpr const char* FontFamily = "Segoe UI";

}  // namespace metrics
