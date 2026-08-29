// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
//
// Why this exists: SyntheticInput writes the SAME block into both channels, so
// the transfer function of the hardware-free path is identically H = 1 -- flat
// 0 dB, zero phase, coherence 1.0 at every bin. That is the single most
// misleading picture this application could put on screen, because it is
// exactly what a perfectly working measurement of nothing looks like, and it
// is also what several broken engines look like. A known delay and a known
// noise floor give the display something with a checkable answer.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rta::measure {

/// Integer-sample delay, carrying state across blocks.
///
/// Block-local delays are the classic mistake here: they pass a one-block test
/// and re-insert the leading zeros on every buffer in the running app, which
/// is a click train at the block rate, not a delay.
class DelayLine {
public:
    explicit DelayLine(int delaySamples)
        : history_(static_cast<std::size_t>(std::max(0, delaySamples)), 0.0f) {}

    /// `out` and `in` must be the same length; `out` may not alias `in`.
    void process(std::span<const float> in, std::span<float> out) noexcept {
        const std::size_t d = history_.size();
        for (std::size_t i = 0; i < in.size() && i < out.size(); ++i) {
            if (d == 0) {
                out[i] = in[i];
                continue;
            }
            out[i] = history_[cursor_];
            history_[cursor_] = in[i];
            cursor_ = (cursor_ + 1) % d;
        }
    }

    void reset() noexcept {
        std::fill(history_.begin(), history_.end(), 0.0f);
        cursor_ = 0;
    }

private:
    std::vector<float> history_;
    std::size_t cursor_ = 0;
};

/// Add independent noise of the given RMS, in place.
///
/// xorshift32 rather than <random>: it is three lines, it is bit-identical on
/// every platform and standard-library version, and `transfer.png` has to be
/// reviewable as a byte-for-byte diff the way `rta-view.png` already is.
/// std::uniform_real_distribution makes no such guarantee across
/// implementations.
///
/// A uniform generator on [-1, 1) has RMS 1/sqrt(3), so the scale is
/// rms*sqrt(3) -- the factor a missing conversion would silently drop, leaving
/// the noise floor 4.8 dB lower than asked for.
inline void addNoise(std::span<float> block, float rms, std::uint32_t& state) noexcept {
    const float scale = rms * 1.7320508f;  // sqrt(3)
    for (float& v : block) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const float uniform = static_cast<float>(state) / 2147483648.0f - 1.0f;
        v += uniform * scale;
    }
}

}  // namespace rta::measure
