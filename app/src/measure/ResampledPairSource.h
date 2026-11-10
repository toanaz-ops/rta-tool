// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE.
//
// H12 T3 (docs/dsp/2026-11-10-g22b-two-file-wav-drift.md E5, E8). Two files seen
// as ONE two-channel source on the reference's clock: channel 0 is the reference
// read directly, channel 1 is the measurement read at t(n) = offset + n * ratio
// through the Kaiser-sinc interpolator. G22's `analyseWavTransfer` then runs on
// it unchanged.
#pragma once

#include "measure/WavTransfer.h"

#include <cstdint>

namespace rta::measure {

/// Where reference sample n lives on the measurement's timeline:
///   t(n) = offsetSamples + n * ratio        (n absolute, in the reference file).
/// `integerOnly` is the shared-clock branch (record E7): ratio is exactly 1 and
/// offsetSamples an integer, and the source COPIES samples -- bitwise, no kernel.
struct ClockMapping {
    double offsetSamples = 0.0;
    double ratio = 1.0;
    bool integerOnly = true;

    [[nodiscard]] double position(std::int64_t n) const noexcept {
        return offsetSamples + static_cast<double>(n) * ratio;
    }
};

class ResampledPairSource final : public SampleBlockSource {
public:
    /// `reference` and `measurement` must outlive this object.
    ResampledPairSource(const SampleBlockSource& reference, int referenceChannel,
                        const SampleBlockSource& measurement, int measurementChannel,
                        ClockMapping mapping);

    [[nodiscard]] int numChannels() const override { return 2; }
    [[nodiscard]] double sampleRate() const override { return reference_.sampleRate(); }
    /// The reference's length: the pair lives on the reference's timeline.
    [[nodiscard]] std::int64_t lengthSamples() const override { return reference_.lengthSamples(); }

    /// Channel 1 fails (false) when a needed measurement tap is outside the
    /// measurement file; `validRange()` says which reference samples are fine.
    [[nodiscard]] bool read(int channel, std::int64_t startSample, std::span<float> out) const override;

    /// First and last reference sample (inclusive) whose measurement taps all lie
    /// inside the measurement file and that lie inside the reference file. Empty
    /// when first > last.
    struct Range {
        std::int64_t first = 0;
        std::int64_t last = -1;
        [[nodiscard]] bool empty() const noexcept { return first > last; }
    };
    [[nodiscard]] Range validRange() const;

    [[nodiscard]] const ClockMapping& mapping() const noexcept { return mapping_; }

private:
    [[nodiscard]] bool tapsInside(std::int64_t n) const;

    const SampleBlockSource& reference_;
    int referenceChannel_;
    const SampleBlockSource& measurement_;
    int measurementChannel_;
    ClockMapping mapping_;
};

}  // namespace rta::measure
