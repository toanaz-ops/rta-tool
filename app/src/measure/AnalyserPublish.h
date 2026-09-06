// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Split out of Analyser.cpp's publish() (task B0,
// docs/plans/2026-09-06-L6b-impl-plan.md) to keep that file under the
// project's line cap before L6b adds more publish-site block builders to it
// -- no behaviour change. Free functions rather than members: neither reads
// Analyser's own state, only the core result handed in.
#pragma once

#include "measure/Snapshot.h"

#include "rta/dsp/MtwResult.h"
#include "rta/dsp/TransferEstimator.h"

namespace rta::measure {

/// Converts one dual-FFT `TransferSnapshot` into the app's own `TransferBlock`
/// (the one radians -> degrees crossing in the whole application; see
/// TransferBlock's own comment). `appliedDelaySamples` is carried straight
/// through, not recomputed -- it states what was compensated BEFORE the
/// transform (Analyser::Config::referenceDelaySamples), which this function
/// has no way to derive from `tf` alone.
[[nodiscard]] TransferBlock makeTransferBlock(const rta::dsp::TransferSnapshot& tf,
                                               int appliedDelaySamples);

/// Converts one stitched `MtwResult` into the app's own `MtwBlock`, including
/// the ONE place a flat per-point coherence array is written from the
/// per-band gated snapshots (record §5's guard note -- core's `MtwResult`
/// carries no coherence member of its own precisely so this copy happens
/// here, outside check_coherence_gate.cmake's reach, after the gate already
/// ran inside makeMtwResult()/makeSnapshot()).
[[nodiscard]] MtwBlock makeMtwBlock(const rta::dsp::MtwResult& mtwResult, int appliedDelaySamples);

}  // namespace rta::measure
