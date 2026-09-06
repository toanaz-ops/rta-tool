// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/OverloadDetector.h"

#include <cmath>

namespace rta::dsp {

bool hasOverload(std::span<const float> x, int runLength, float threshold) noexcept {
    if (runLength <= 0) {
        return true;  // a non-positive run length is trivially satisfied by any span
    }

    // A single linear pass, no allocation: `run` is the count of consecutive
    // samples seen so far with |x| >= threshold, reset the instant one falls
    // below it. No state survives past the end of THIS span (header
    // comment) -- a hop boundary genuinely splits a run in two.
    int run = 0;
    for (const float sample : x) {
        if (std::abs(sample) >= threshold) {
            ++run;
            if (run >= runLength) {
                return true;
            }
        } else {
            run = 0;
        }
    }
    return false;
}

}  // namespace rta::dsp
