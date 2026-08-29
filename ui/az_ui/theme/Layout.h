// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of az_ui -- the SODIUM RACK design system. Generic geometry only:
// nothing in this module may name a measurement quantity (project CLAUDE.md,
// "Module boundaries"). This file splits a rectangle; what goes in the pieces
// is the host application's business and is not describable from here.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <span>
#include <vector>

namespace az::ui {

/// Divide `area` into `weights.size()` stacked children, heights in proportion
/// to `weights`, separated by `gapPx` pixels.
///
/// Named `gapPx` rather than `gap` because `az::ui::gap` is a namespace-scope
/// metric in theme/Metrics.h and is the value most callers will pass here; a
/// parameter of the same name inside this namespace hides it (MSVC C4459), and
/// this project builds warning-free at /W4.
///
/// Positions are accumulated rather than each child being rounded on its own:
/// independent rounding leaves a one-pixel unpainted row between two panes at
/// most heights, which reads on a dark panel as a hairline nobody drew. Each
/// child's bottom is computed from the running fraction of the total, so the
/// last child ends exactly on `area.getBottom()` by construction rather than by
/// luck.
///
/// Returns exactly `weights.size()` rectangles, always. A non-positive weight
/// yields a zero-height child rather than a dropped one: the caller indexes
/// the result against its own child list, and a silently shorter result
/// misaligns every child after it.
[[nodiscard]] std::vector<juce::Rectangle<int>> splitVertically(
    juce::Rectangle<int> area, std::span<const float> weights, int gapPx);

}  // namespace az::ui
