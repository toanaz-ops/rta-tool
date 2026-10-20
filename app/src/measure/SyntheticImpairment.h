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

#include "rta/dsp/Biquad.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
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

    /// Processes `min(in.size(), out.size())` samples -- the loop is bounded by
    /// both rather than trusting a precondition it cannot check. `out` may not
    /// alias `in`. Call sites here always pass equal-sized buffers; the bound
    /// exists so a future caller that does not gets a short result rather than
    /// a walk off the end.
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

/// A feedback comb, y[n] = x[n] + g * y[n - D], carrying state across blocks
/// (RT60-UI lane decision 10): the one-line "room" whose decay has a closed form.
///
/// Each trip round the loop multiplies the energy by g^2, so the energy falls
/// 20*log10(1/g) dB per D samples and the 60 dB decay time is exactly
/// T = 3 * D / (fs * log10(1/g)) in EVERY frequency band -- all of the comb's
/// poles share the radius g^(1/D). An impulse comes out as pulses at k*D with
/// amplitude g^k. A gain of 0 (or a delay of 0) is INACTIVE and the caller must
/// then not run it: `x + 0.0f` changes -0.0 into +0.0, so "multiplied by zero"
/// is not bit-identical and a default must not change one bit.
class FeedbackComb {
public:
    FeedbackComb(int delaySamples, float gain)
        : history_(static_cast<std::size_t>(std::max(0, delaySamples)), 0.0f), gain_(gain) {}

    [[nodiscard]] bool active() const noexcept { return !history_.empty() && gain_ != 0.0f; }

    /// In place. The state is the last D OUTPUT samples, so a block boundary
    /// anywhere gives the same result as one long block.
    void process(std::span<float> block) noexcept {
        if (!active()) return;
        for (float& v : block) {
            v += gain_ * history_[cursor_];
            history_[cursor_] = v;
            cursor_ = (cursor_ + 1) % history_.size();
        }
    }

private:
    std::vector<float> history_;
    std::size_t cursor_ = 0;
    float gain_;
};

/// The MEASUREMENT channel's deterministic part: integer delay, then an
/// optional `BiquadCascade` band-limit (sweep-IR lane, decision 8). Noise is
/// added AFTER this, by the caller, so the noise floor stays flat -- a real
/// microphone's self-noise is not shaped by the room it listens to.
///
/// Delay first, filter second, so an impulse in comes out as the cascade's own
/// impulse response shifted by D, with every float operation the cascade's
/// (zeros through the biquads leave their state at exactly 0.0 until the
/// impulse arrives). The two are LTI and commute mathematically; the order is
/// fixed so a test can assert the shifted IR bit-exact, not "close".
///
/// An empty filter is an identity, not a pass-through of a zero-section
/// cascade -- the default must not change one bit for a caller that never
/// opted in. The optional room comb comes LAST, after the filter, so an impulse
/// through an empty filter is exactly pulses at D0 + k*D (RT60-UI T6).
class MeasurementPath {
public:
    MeasurementPath(int delaySamples, std::vector<rta::dsp::Biquad::Coeffs> filter, int combDelaySamples = 0,
                    float combGain = 0.0f)
        : delay_(delaySamples), filter_(std::move(filter)), comb_(combDelaySamples, combGain) {}

    /// `out` may not alias `in` (DelayLine's rule); sizes bounded as there.
    void process(std::span<const float> in, std::span<float> out) noexcept {
        delay_.process(in, out);
        if (filter_.size() > 0) {
            filter_.process(out, out);
        }
        comb_.process(out);  // returns at once when inactive
    }

private:
    DelayLine delay_;
    rta::dsp::BiquadCascade filter_;
    FeedbackComb comb_;
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
