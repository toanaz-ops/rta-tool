// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest (app/tests/
// CMakeLists.txt), the same guard PlotGeometry.h sits under. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §5.8 (T12, steps M1/M3).
#pragma once

#include "measure/Snapshot.h"

#include <cmath>
#include <format>
#include <string>
#include <string_view>

namespace rta::view {

/// The explanation hardware-pass step M1 needs and today does not get: JUCE
/// silently keeps the current device type when the one the user picked is
/// not registered (e.g. "ASIO" with no driver installed), so
/// `AudioIo::start()` succeeds and records no fault
/// (platform/src/AudioIo.cpp:32-39 -- a recorded decision that
/// `currentState()`, not a fault, is how a caller finds the truth; the
/// platform layer deliberately does not know what the user asked for). Only
/// the app layer holds both the request and the truth, so the sentence
/// comparing them lives here.
///
/// Empty on either side, or an exact match, means there is nothing to
/// explain: an empty `requested` means the user has not touched the combo
/// yet, and an empty `actual` means the device has not reported a type at
/// all (nothing running to compare against).
[[nodiscard]] inline std::string deviceTypeNotice(std::string_view requested, std::string_view actual) {
    if (requested.empty() || actual.empty() || requested == actual) return {};
    // Plain ASCII hyphen, not an em dash -- this string draws in an
    // embedded mono UI face, and the project has been bitten by encoding on
    // read-modify-write before (CLAUDE.md rule 6).
    return std::string(requested) + " unavailable - using " + std::string(actual);
}

/// The RTA plot's one-line readout: peak frequency, peak level, and (M3)
/// how many analysis frames have accumulated since the last restart. Three
/// quantities, three different formatting rules (CLAUDE.md "Reading out
/// numbers" -- whole hertz, one-decimal dB, a plain integer count with
/// neither) is exactly why this moved out of `RtaView.cpp`'s inline
/// `juce::String` concatenation and into a function small enough to pin
/// with `test_readouts.cpp`.
///
/// The Hz/dB half reproduces the pre-existing inline code byte for byte
/// (same placeholder text, same "    " separators) so `rta-view.png` only
/// gains the appended frame segment, nothing upstream of it moves. The
/// frame count is deliberately NOT gated on `bands` being non-empty: a
/// running-but-silent analysis chain (frames counting up, no bands yet) is
/// exactly the state M3 exists to let a human check, so it must be visible
/// even when the peak readout cannot show anything.
[[nodiscard]] inline std::string readoutLine(const rta::measure::Snapshot* snapshot) {
    if (snapshot == nullptr) return "-- Hz    -- dB    -- FRAMES";

    std::string head;
    if (!snapshot->bands.empty()) {
        const long long hz = std::llround(static_cast<double>(snapshot->peakBandCentreHz));
        head = std::format("{} Hz    {:.1f} dB", hz, static_cast<double>(snapshot->peakBandLevelDb));
    } else {
        head = "-- Hz    -- dB";
    }

    return std::format("{}    {} FRAMES", head, snapshot->framesAnalysed);
}

/// Task B7, CLAUDE.md's "Reading out numbers": frequency is a whole number
/// of hertz -- one-hertz resolution is already finer than any decision made
/// while tuning a system, and a decimal that never carries information costs
/// a character on every readout and invites a false sense of precision.
[[nodiscard]] inline std::string formatHz(double hz) {
    return std::format("{} Hz", std::llround(hz));
}

/// dB keeps one decimal (CLAUDE.md: "0.1 dB is a real, actionable
/// difference; 0.1 Hz is not"). Used for a position's own trim (record §7),
/// always signed by `std::format`'s own default for a floating value.
[[nodiscard]] inline std::string formatTrim(double trimDb) {
    return std::format("{:.1f} dB", trimDb);
}

/// Coherence and `phaseAgreement` (record §2's `R`) are 0..1 with two
/// decimals -- CLAUDE.md's own rule, restated here because `R` is not a
/// coherence estimate (record §4) and must not be tempted into the dB or Hz
/// rule by proximity to either on screen.
[[nodiscard]] inline std::string formatAgreement(double agreement) {
    return std::format("{:.2f}", agreement);
}

/// A contributor count is a bare integer pair, no decimal of any kind --
/// "3 of 4" states a fact a fractional readout would only obscure.
[[nodiscard]] inline std::string formatContributors(int contributors, int total) {
    return std::format("{} of {}", contributors, total);
}

}  // namespace rta::view
