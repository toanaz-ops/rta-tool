// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. No JUCE: plain ints, so a test can drive it.
// UI-test finding 2026-11-09: the main window had no minimum size (at
// ~1000x600 the masthead lost GEN/OUT and the rail lost DEVICE/START) and its
// fixed 1300x820 default does not fit a 1366x768 screen.
#pragma once

#include <algorithm>

namespace rta::app {

struct WindowSizing {
    int width = 0;
    int height = 0;
    int minWidth = 0;
    int minHeight = 0;
};

/// Smallest window that keeps every control reachable, in the units JUCE uses
/// (already DPI-scaled). Width: the masthead's generator strip needs
/// 84+44+44+96+104+64+112 px of cells, the OWN steppers 88 more, gaps, and the
/// 340 px wordmark reserve (MainComponentGenerator.cpp::layout,
/// MainComponentLayout.cpp::kBrandReserve) -- about 1030 at most, so 1100 with
/// the readout squeezed. Height: the rail scrolls (MainComponentRail.cpp), so
/// 640 only has to hold the masthead, the pane selector and a usable rail
/// viewport; nothing is cut off, it scrolls.
inline constexpr int kPreferredWidth = 1300;
inline constexpr int kPreferredHeight = 820;
inline constexpr int kMinimumWidth = 1100;
inline constexpr int kMinimumHeight = 640;

/// The default size and the resize limits for a work area of the given size
/// (the display's `userArea`: the screen minus the taskbar). Every figure is
/// clamped to the work area, so a 1366x768 screen gets a window that fits it
/// and a minimum that never exceeds it.
[[nodiscard]] constexpr WindowSizing windowSizingFor(int workAreaWidth, int workAreaHeight) {
    WindowSizing s;
    s.minWidth = std::min(kMinimumWidth, workAreaWidth);
    s.minHeight = std::min(kMinimumHeight, workAreaHeight);
    s.width = std::clamp(kPreferredWidth, s.minWidth, workAreaWidth);
    s.height = std::clamp(kPreferredHeight, s.minHeight, workAreaHeight);
    return s;
}

}  // namespace rta::app
