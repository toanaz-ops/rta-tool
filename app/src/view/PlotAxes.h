// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.5.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "view/AxisMetrics.h"
#include "view/PlotGeometry.h"

namespace rta::view {

/// Hairline grid: one vertical line per `decadeTicks(fLowHz, fHighHz)`, one
/// horizontal line per 10 dB step within `[dbBottom, dbTop]`. Drawn INSIDE
/// `[left,right] x [top,bottom]` only -- the labels live in the margins
/// `kFrequencyLabelHeight` / `kLevelLabelWidth` reserve outside it.
void drawGrid(juce::Graphics& g, const PlotGeometry& geometry);

/// Whole-hertz labels centred under each decade tick, no `k` abbreviation
/// (project CLAUDE.md "Reading out numbers"; plan trap T-9): `20 50 100
/// 200 500 1000 2000 5000 10000 20000`.
void drawFrequencyLabels(juce::Graphics& g, const PlotGeometry& geometry);

/// One-decimal dB labels every 10 dB (plan trap T-9: dB carries one
/// decimal, frequency does not -- different quantities, different rules).
void drawLevelLabels(juce::Graphics& g, const PlotGeometry& geometry);

}  // namespace rta::view
