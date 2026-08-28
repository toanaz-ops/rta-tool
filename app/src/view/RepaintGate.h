// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. See spec §4.
#pragma once

#include <cstdint>

namespace rta::view {

/// What the gate remembers between calls. `primed` exists so the FIRST call
/// repaints even when both counters are legitimately zero -- an empty session
/// on a freshly started app is exactly that case, and it must still draw.
struct GateState {
    std::uint64_t lastSequence = 0;
    std::uint64_t lastRevision = 0;
    bool primed = false;
};

/// Repaint when the live snapshot advanced OR the library changed.
///
/// The single-part gate this replaces watched only the snapshot sequence, which
/// broke in both directions once stored traces existed: they carry no sequence,
/// so they redrew on every live frame, while editing one produced no sequence
/// change and so never redrew at all.
[[nodiscard]] inline bool shouldRepaint(GateState& state, std::uint64_t sequence,
                                        std::uint64_t revision) noexcept {
    if (state.primed && sequence == state.lastSequence && revision == state.lastRevision) {
        return false;
    }
    state.lastSequence = sequence;
    state.lastRevision = revision;
    state.primed = true;
    return true;
}

}  // namespace rta::view
