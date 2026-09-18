// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- core test support. No JUCE, no Qt, no audio-device API.
#pragma once

// Shared block builders for the L6a meter fixtures. Extracted when W1-B's
// windowed-recompute cases would have pushed test_block.cpp past CLAUDE.md's
// 400-line hard cap: two files now build their blocks from ONE closed form, so
// a "fix" to the fixture cannot make one of them agree with a bug the other
// still catches.

#include "rta/meter/Block.h"

#include <cmath>
#include <cstdint>

namespace rta::testing {

/// A block at a stated mean-square level, with a stated sample count. Built
/// from the closed form (`sumSquares = n * 10^(L/10)`), never from anything an
/// accumulator produced -- CLAUDE.md's verification standard.
inline rta::meter::Block blockAtLevel(std::uint64_t index, std::uint32_t samples,
                                      double levelDb) {
    rta::meter::Block b;
    b.blockIndex = index;
    b.blockSamples = samples;
    b.sumSquares = static_cast<double>(samples) * std::pow(10.0, levelDb / 10.0);
    return b;
}

inline constexpr std::uint32_t mask(rta::meter::BlockFlag f) {
    return static_cast<std::uint32_t>(f);
}

}  // namespace rta::testing
