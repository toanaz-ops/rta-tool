// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
#pragma once

#include <algorithm>
#include <cstddef>
#include <span>

namespace rta::measure {

/// How many whole hops a GROUP of rings can advance TOGETHER.
///
/// `AnalysisThread::drainPaired` calls the span overload for one reference ring
/// plus every measurement ring routed against it, so the arithmetic the tests
/// check is the arithmetic that ships (audit prj010-fable-audit-20261021:
/// the header used to be dead while drainPaired re-implemented it inline).
/// Extracted from AnalysisThread so it is testable without a CaptureBus and a
/// running thread -- and because getting it wrong is invisible. Draining each role's ring to exhaustion independently (which is
/// correct for the single-channel RTA path, and is what drainRole still does)
/// lets an audio callback landing between the two calls leave one ring a hop
/// richer. Consume that surplus and every frame afterwards pairs reference t
/// with measurement t-hop: at hopSize 2048, 42 ms of permanent misalignment
/// that decorrelates the channels at HIGH frequency first, because HF has the
/// shortest period. The operator reads that as a broken loudspeaker.
///
/// Sampling both availabilities once and consuming the minimum is safe against
/// the writer: an audio callback only ever ADDS, so availability can grow
/// between this call and the drain but never shrink.
///
/// `available` holds each ring's `availableToRead()` sampled once. An empty
/// group consumes nothing: no ring means no pairing to keep aligned.
[[nodiscard]] inline std::size_t pairedHopCount(std::span<const std::size_t> available,
                                                std::size_t hop) noexcept {
    if (hop == 0 || available.empty()) return 0;
    return *std::min_element(available.begin(), available.end()) / hop;
}

/// Two-ring form of the above (reference, measurement).
[[nodiscard]] inline std::size_t pairedHopCount(std::size_t referenceAvailable,
                                                std::size_t measurementAvailable,
                                                std::size_t hop) noexcept {
    const std::size_t both[2] = {referenceAvailable, measurementAvailable};
    return pairedHopCount(std::span<const std::size_t>(both, 2), hop);
}

}  // namespace rta::measure
