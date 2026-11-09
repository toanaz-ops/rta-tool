// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
//
// H9 / L4c task A2 (docs/plans/2026-11-09-h9-l4c-ir-gate-plan.md, record
// docs/dsp/2026-11-09-l4c-ir-gate.md sec. 5): the IR pane's numbers and words,
// with no window. An IR from the `IrStore` goes in; out come the energy-time
// curve the operator drags two markers over, the gated response, the readout
// line, and -- on STORE GATED -- a library `Trace`.
//
// The gate itself is `rta::ir::gateSpectrum` (core, proven there against closed
// forms). Nothing here re-derives a window or a phase rotation.
#pragma once

#include "measure/SweepIrOutcome.h"
#include "trace/Trace.h"

#include <complex>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rta::measure {

/// The two markers, in ms relative to the arrival. The fades are the record's
/// fixed defaults (0.5 ms in, 25 % of the gate out); the operator drags the
/// markers, not the ramps.
struct IrGateMarkers {
    double startMs = -1.0;
    double endMs = 10.0;
};

/// What the ETC plot draws: dB against ms after the arrival, 0 dB = the
/// strongest point drawn.
///
/// ## Which envelope, and why not a Hilbert one
///
/// A running maximum of |h| over a 0.25 ms centred window. A sweep-deconvolved
/// IR is band-limited and oscillates, so plain |h| dips to zero at every zero
/// crossing and the ETC would look like a comb; the peak-hold fills those in
/// for anything above about 2 kHz and leaves honest dips for a low-frequency
/// tail, whose period is longer than the window. The analytic-signal envelope
/// (Hilbert) would fill those too but costs a transform of the whole IR
/// (about 2^21 points at the shipped sweep) on the message thread per
/// selection, to move a picture the operator only uses to place two markers.
struct IrEtc {
    double startMs = 0.0;  ///< ms after the arrival of `db[0]`
    double stepMs = 0.0;
    std::vector<float> db;
    [[nodiscard]] double endMs() const noexcept {
        return db.empty() ? startMs : startMs + stepMs * static_cast<double>(db.size() - 1);
    }
};

/// The window the ETC shows: 5 ms before the arrival to 50 ms after it, cut to
/// what the IR actually holds.
inline constexpr double kEtcLeadMs = 5.0;
inline constexpr double kEtcSpanMs = 50.0;

[[nodiscard]] IrEtc buildEtc(const SweepIr& ir);

/// One gate placement, analysed.
struct IrGateAnalysis {
    bool ok = false;
    /// The core refusal sentence when `ok` is false; empty otherwise.
    std::string refusal;
    IrGateMarkers markers;
    double fMinHz = 0.0;
    double sampleRate = 0.0;
    std::size_t transformSize = 0;
    std::vector<std::complex<float>> bins;  ///< transformSize/2 + 1, phase referenced to the arrival
    /// `GATE -1.0..10.0 ms . valid above 104 Hz`, or `GATE refused -- <sentence>`.
    std::string readout;
    /// The stored trace's `window` meta: `gate -1.0..10.0 ms, valid above 104 Hz`.
    std::string windowText;
};

[[nodiscard]] IrGateAnalysis analyseIrGate(const SweepIr& ir, const IrGateMarkers& markers);

/// The gated response as a library trace: magnitude and PHASE, never
/// coherence (the gate has no second channel to ask). `deviceName` and
/// `channelRoles` come from the source trace, so ALIGN's equality check on
/// roles still means what it meant.
///
/// `appliedDelaySamples` carries the arrival, because the gated phase is
/// referenced to it: XOVER restores each trace's applied delay before summing
/// (#103, #104), and a 0 here would sum two gated responses as if both sources
/// were at the same distance. Refuses (nullopt) a refused analysis.
///
/// `captureId` comes from the caller (`rta::trace::nextCaptureId()`), so the id
/// sequence stays the one place STORE and SWEEP already share and this file does
/// not need the converter linked in.
[[nodiscard]] std::optional<rta::trace::Trace> gatedTrace(const SweepIr& ir, const IrGateAnalysis& analysis,
                                                          std::string captureId, std::string deviceName,
                                                          std::string channelRoles);

/// "valid above 104 Hz" -- whole hertz (project CLAUDE.md), rounded UP so the
/// printed figure is never below what the formula says.
[[nodiscard]] std::string validAboveText(double fMinHz);

}  // namespace rta::measure
