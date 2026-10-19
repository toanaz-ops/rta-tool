// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// SWEEP lane (docs/plans/2026-09-29-sweep-ir-lane-plan.md) task T3, with the
// decisions of its "Owner-default decisions" table (2-5) and the record
// `docs/dsp/2026-08-30-sweep-ir-l4a.md`. This header deliberately does NOT
// include `rta/ir/*`: `SweepIrWorker.h` includes it for `SweepIrRequest`, the
// message thread includes the worker, and the deconvolver must stay on the
// far side of that line (see SweepIrOutcome.h).
//
// NOT real-time safe and NOT message-thread safe: a 10 s sweep needs 2^21-point
// transforms (about 8 MB of tables, record `:990-994`). Run it on
// `SweepIrWorker`'s thread.
#pragma once

#include "measure/SweepIrOutcome.h"
#include "rta/gen/Sweep.h"

#include <cmath>
#include <cstddef>

namespace rta::measure {

/// The one sweep configuration with a measured flatness row (record `:237`):
/// `Sweep::Config{}` -- 10 s, 20 Hz-20 kHz, two-octave fade-in -- except the
/// level, -12 dBFS peak, which matches LOCATE (owner decision 2). Level does
/// not change flatness.
[[nodiscard]] inline rta::gen::Sweep::Config shippedSweepConfig(double sampleRate) {
    rta::gen::Sweep::Config config;
    config.sampleRate = sampleRate;
    config.levelDbFsPeak = -12.0;
    return config;
}

struct SweepIrRequest {
    rta::gen::Sweep::Config sweep = shippedSweepConfig(48000.0);
    /// Required capture after the sweep has LEFT THE OUTPUT (owner decision 3):
    /// 1.5 x an assumed RT60 of 2.0 s (record sec. 9, worst error 0.02 dB at
    /// 1.5 x). Rooms with a longer RT60 need a longer value; that knob belongs
    /// to the RT60 UI (H6).
    double tailSeconds = 3.0;
};

/// `ceil(tailSeconds * fs)`: the tail the capture must hold after the sweep
/// ends, counted from the loop's arrival of the sweep START plus the sweep.
[[nodiscard]] inline std::size_t requiredTailSamples(const SweepIrRequest& request) {
    return static_cast<std::size_t>(std::ceil(request.tailSeconds * request.sweep.sampleRate));
}

/// Two cycles of the sweep's valid-band low edge -- the lead-in the window
/// keeps BEFORE t = 0 (record sec. 7). Forwards to `rta::ir::leadInSamplesFor`.
[[nodiscard]] std::size_t sweepIrLeadInSamples(double sampleRate, double lowBandEdgeHz);

/// The spectrum length N (owner decision 4): `nextPow2(ceil(0.5*fs) + leadIn)`
/// -- 32768 at 44.1 and 48 kHz, 65536 at 96 kHz. It keeps at least 500 ms
/// after t = 0, which a live 4096-point frame (85 ms) cannot.
[[nodiscard]] std::size_t sweepIrWindowSize(double sampleRate, double lowBandEdgeHz);

/// Deconvolves both channels with the same Farina inverse, sets t = 0 to the
/// loop's peak, normalises to the loop's in-band mean, and returns the
/// N-point spectrum of the window [refPeak - leadIn, refPeak - leadIn + N).
/// Never throws: a DSP exception becomes `SweepIrStatus::Failed`.
[[nodiscard]] SweepIrOutcome analyseSweepCapture(const SweepCapture& capture,
                                                 const SweepIrRequest& request);

}  // namespace rta::measure
