// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
#pragma once

#include <algorithm>
#include <cstddef>

namespace rta::measure {

/// How many whole hops two rings can advance TOGETHER.
///
/// Extracted from AnalysisThread purely so this arithmetic is testable without
/// a CaptureBus and a running thread -- and because getting it wrong is
/// invisible. Draining each role's ring to exhaustion independently (which is
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
[[nodiscard]] inline std::size_t pairedHopCount(std::size_t referenceAvailable,
                                                std::size_t measurementAvailable,
                                                std::size_t hop) noexcept {
    if (hop == 0) return 0;
    return std::min(referenceAvailable, measurementAvailable) / hop;
}

}  // namespace rta::measure
