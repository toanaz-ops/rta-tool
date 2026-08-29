// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
//
// The plot's axis furniture, split out of PlotAxes.h so a framework-free
// caller can have the numbers without the drawing. PlotAxes.h includes
// juce_gui_basics because it draws; BodeLayout.h only needs to know how much
// room the labels take, and the guard forbids it the framework. One
// definition, two readers -- not two definitions and a test hoping they agree.
#pragma once

namespace rta::view {

/// The margin `RtaView` must reserve below `PlotGeometry::bottom` for
/// `drawFrequencyLabels` to draw into. A single source of truth: the
/// component that lays out the geometry and the function that draws text
/// outside it must agree on this figure, or the labels either get no room
/// or the plot area shrinks for nothing.
inline constexpr int kFrequencyLabelHeight = 18;

/// The margin `RtaView` must reserve to the left of `PlotGeometry::left`
/// for `drawLevelLabels` -- wide enough for "-90.0" in the axis mono face.
inline constexpr int kLevelLabelWidth = 40;

/// Half the width of a frequency label's cell either side of its tick's x
/// (`drawFrequencyLabels`). `RtaView` reserves this much again to the right
/// of `PlotGeometry::right`, or the "20000" label at the top of the range
/// draws half off the edge of the component.
inline constexpr float kFrequencyLabelHalfWidth = 26.0f;

}  // namespace rta::view
