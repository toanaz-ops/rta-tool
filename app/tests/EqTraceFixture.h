// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Stored-Trace fixtures for the EQ pane tests (L7-EQ UI wave A, docs/plans/
// 2026-09-29-eq-ui-lane-plan.md). JUCE-free and header-only, so the OFF model
// tests (app/tests) and the JUCE pane tests (app/tests_juce) build their
// traces the same way. Builds on EqSessionFixture.h's grids and bump.
#pragma once

#include "EqSessionFixture.h"

#include "trace/Trace.h"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace eqfixture {

/// A stored capture on the fixed-engine grid: bin k at k*fs/fftSize, so
/// magnitudeDb.size() must be fftSize/2 + 1. An empty `coherence` / `phaseRad`
/// leaves that field out of the trace (a single-channel RTA capture has
/// neither; a TRANSFER capture below the coherence gate has no coherence).
[[nodiscard]] inline rta::trace::Trace makeEqTrace(const std::string& id, int fftSize, double fs,
                                                   std::vector<float> magnitudeDb,
                                                   std::vector<float> coherence = {},
                                                   std::vector<float> phaseRad = {}) {
    rta::trace::CaptureMeta meta;
    meta.id = id;
    meta.sampleRate = fs;
    meta.fftSize = fftSize;
    auto trace = rta::trace::Trace::make(meta, std::move(magnitudeDb));
    if (!trace.has_value()) throw std::logic_error("makeEqTrace: magnitude length disagrees with fftSize");
    if (!coherence.empty() && !trace->setCoherence(std::move(coherence))) {
        throw std::logic_error("makeEqTrace: coherence length");
    }
    if (!phaseRad.empty() && !trace->setPhase(std::move(phaseRad))) {
        throw std::logic_error("makeEqTrace: phase length");
    }
    return std::move(*trace);
}

/// The plan's `makeLinearFixture` 8 dB bump at 1 kHz (0.5 octave wide,
/// coherence 0.95, DC untrusted) as a stored trace at `fftSize` / 48 kHz,
/// with no phase unless `withPhase` (zero phase, all bins).
[[nodiscard]] inline rta::trace::Trace makeBumpTrace(const std::string& id, int fftSize = 2048,
                                                     bool withPhase = false) {
    const Fixture f = makeLinearFixture(static_cast<std::size_t>(fftSize) / 2 + 1);
    return makeEqTrace(id, fftSize, kFs, f.measuredDb, f.coherence,
                       withPhase ? std::vector<float>(f.hz.size(), 0.0f) : std::vector<float>{});
}

}  // namespace eqfixture
