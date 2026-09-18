#pragma once

// The settings window's design tokens.
//
// A light palette, authored for this project rather than lifted from another
// one: the same structure the project's design language already fixed (a 4px
// grid, an 11-17px type scale, 4/8/14px corner steps, semantic status colours
// bound to the real engine state), with the base flipped from dark to light.
//
// Nothing outside this header may name a colour. Widgets that are not painted
// by us - the checkbox glyph, the trackbar thumb, the combo arrow - keep the
// system's own accent, because they are native controls and recolouring them
// would mean owner-drawing every one of them, which is the opposite of the
// direction this window is going.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace theme {

// --- surfaces -------------------------------------------------------------

// The window and the frame around the tab.
constexpr COLORREF Window = RGB(0xF4, 0xF5, 0xF7);
// The page, and every input on it. The tab control paints its display area in a
// shade of its own, so the layout puts one surface of this colour behind the
// fields to make the whole page a single colour.
constexpr COLORREF Panel = RGB(0xFF, 0xFF, 0xFF);
// Reserved for nested blocks; not used yet.
constexpr COLORREF Subtle = RGB(0xEC, 0xEE, 0xF1);

// --- lines ----------------------------------------------------------------

constexpr COLORREF Stroke = RGB(0xD8, 0xDC, 0xE1);
constexpr COLORREF FocusRing = RGB(0x9A, 0xA3, 0xAE);

// --- text -----------------------------------------------------------------

constexpr COLORREF TextPrimary = RGB(0x1B, 0x1F, 0x24);
constexpr COLORREF TextSecondary = RGB(0x4A, 0x55, 0x60);
constexpr COLORREF TextHint = RGB(0x78, 0x82, 0x8D);
constexpr COLORREF TextDisabled = RGB(0xA4, 0xAC, 0xB5);

// --- accent ---------------------------------------------------------------

// The signal/audio accent, darkened from the dark-theme teal because the
// original does not carry enough contrast on a light surface.
constexpr COLORREF Accent = RGB(0x0F, 0x76, 0x6E);
constexpr COLORREF AccentSoft = RGB(0xE4, 0xF1, 0xEF);

// --- engine state ---------------------------------------------------------
// These are not decoration: they say what the audio path is doing, and they map
// one-to-one onto the tray icon states.

constexpr COLORREF StateIdle = RGB(0x6B, 0x72, 0x80);       // resident, not streaming
constexpr COLORREF StateArmed = RGB(0x02, 0x84, 0xC7);      // link up, no consumer yet
constexpr COLORREF StateStreaming = RGB(0x0F, 0x76, 0x6E);  // signal flowing (= Accent)
constexpr COLORREF StateDegraded = RGB(0xB4, 0x53, 0x09);   // fell back / stalled
constexpr COLORREF StateFault = RGB(0xDC, 0x26, 0x26);      // socket or device lost

}  // namespace theme
