// SPDX-License-Identifier: AGPL-3.0-or-later
// Fixture-only: the LR4 high-pass that pairs with measure/SyntheticCrossover.h's
// low-pass. Production's default SYNTHETIC path never builds a high-pass, so it
// lives with the tests and the snapshot tool rather than in app/src (a function
// no production caller reaches is an orphan, tools/orphan_check.py).
#pragma once

#include "measure/SyntheticCrossover.h"

namespace rta::measure {

/// Two identical sections: LR4 high-pass at `fcHz`.
[[nodiscard]] inline std::vector<rta::dsp::Biquad::Coeffs> lr4HighPass(double fcHz, double fs) {
    const auto s = detail::rbjSection(fcHz, fs, true);
    return {s, s};
}

}  // namespace rta::measure
