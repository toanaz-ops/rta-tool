// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// L7-DELAY task F1 (docs/plans/2026-09-07-L7-delay-impl-plan.md; decision
// record docs/dsp/2026-09-06-l7-auto-delay.md sec.1.1, sec.3, sec.11.2).
// The Locate sequence: setSource(pink noise) -> soloOutput -> armSource ->
// wait for the first non-stationary samples to pass -> capture L samples of
// both channels -> disarmSource -> suggestDelay -> hold a DelaySuggestion.
// Mirrors CaptureSequencer's named-refusal shape; drives no device (JUCE-
// free like OutputEngine and CaptureSequencer themselves), so this is
// tested against a REAL rta::platform::OutputEngine with no audio hardware.
#pragma once

#include "measure/OutputPolicy.h"
#include "measure/RawCaptureBuffer.h"

#include "rta/dsp/DelayPolicy.h"
#include "rta/gen/Mls.h"
#include "rta/platform/OutputEngine.h"

#include <cstddef>
#include <optional>
#include <span>

namespace rta::measure {

enum class LocateState { Idle, Waiting, Capturing, Done };

/// One reason, on purpose (record sec.1.5): a periodic excitation puts
/// correlation peaks at `D + k*period` and the one-shot cannot tell them
/// apart from the true arrival, so `gen::Mls` -- L7-OUT's own periodic
/// source -- is refused before it ever reaches `OutputEngine`.
enum class LocateRefusal { None, PeriodicExcitation };

class DelayLocator {
public:
    struct Config {
        int outputChannel = 0;

        /// L, in samples (record sec.11.5: L >= 4*maxLag keeps overlap loss
        /// under the derivation's 0.77 row -- a convenience default the
        /// CALLER computes from a stated `policy.maxLag`, not derived here:
        /// the unrestricted default policy's maxLag is
        /// numeric_limits<ptrdiff_t>::max(), for which "4x" has no sensible
        /// value). Must be > 0.
        std::size_t captureLength = 0;

        rta::dsp::PhatOptions options{};
        rta::dsp::DelayPolicy policy{};
    };

    /// `source` is checked immediately, before anything touches `engine`:
    /// gen::Mls sets `lastRefusal() == PeriodicExcitation` and `arm()`
    /// becomes a no-op. Construction alone never calls into `engine` either
    /// way -- only `arm()` does.
    DelayLocator(rta::platform::OutputEngine& engine, rta::platform::SourceVariant source,
                 Config config);

    /// Idle -> Waiting: `setSource` the held excitation, `soloOutput` the
    /// configured channel (record sec.3, DEL-R4: a Locate is a measurement
    /// action and uses STRICT solo, sharing L7-OUT's owner ruling for a
    /// sequence), `armSource`. No-op if construction already refused, or if
    /// not currently Idle.
    void arm();

    /// One hop of both channels. While Waiting: counts `engine`'s
    /// `renderedSamples()` and stays Waiting until the excitation has
    /// rendered at least 480 samples at 48 kHz worth (record sec.11: the
    /// first 10 ms are not stationary), scaled by the configured sample
    /// rate -- the hop's audio is NOT accumulated during this wait. Once
    /// past it: Waiting -> Capturing, and every hop from here feeds
    /// `RawCaptureBuffer` until full, at which point `engine.disarmSource()`
    /// runs BEFORE `suggestDelay` (record sec.8: the tracker/Locate never
    /// leaves the excitation running past the point it is used), the result
    /// is held, and state becomes Done.
    void feedHop(std::span<const float> referenceHop, std::span<const float> measurementHop);

    [[nodiscard]] LocateState state() const noexcept { return state_; }
    [[nodiscard]] LocateRefusal lastRefusal() const noexcept { return refusal_; }
    [[nodiscard]] const std::optional<rta::dsp::DelaySuggestion>& suggestion() const noexcept {
        return suggestion_;
    }

private:
    rta::platform::OutputEngine& engine_;
    rta::platform::SourceVariant source_;
    Config config_;

    RawCaptureBuffer buffer_;
    LocateState state_ = LocateState::Idle;
    LocateRefusal refusal_ = LocateRefusal::None;
    std::optional<rta::dsp::DelaySuggestion> suggestion_;
};

}  // namespace rta::measure
