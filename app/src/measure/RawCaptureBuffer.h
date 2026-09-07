// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// L7-DELAY task F1 (docs/plans/2026-09-07-L7-delay-impl-plan.md; decision
// record docs/dsp/2026-09-06-l7-auto-delay.md sec.1.1, sec.11). The raw-
// capture path AnalysisThread::drainPaired never built: for a Locate, collect
// L samples of the reference and of the measurement channel the active
// transfer function names, from the same hops the engine already receives,
// allocated once at arm time, never in the callback.
#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace rta::measure {

/// Two pre-sized spans, filled hop by hop until full. Not a ring buffer: a
/// Locate capture is a single ONE-SHOT window of exactly `length` samples,
/// not a running accumulator -- `arm()` is called once per capture, not
/// once per publish.
class RawCaptureBuffer {
public:
    /// Sizes both channels to `length` and resets the write cursor -- the
    /// one allocation this class performs (RingBuffer/CaptureBus's own
    /// "allocate once, at an explicit arm point" convention).
    void arm(std::size_t length) {
        length_ = length;
        reference_.assign(length, 0.0f);
        measurement_.assign(length, 0.0f);
        written_ = 0;
    }

    /// Appends up to `length() - written()` samples from each hop, in
    /// order, no gap and no double-count across calls -- allocation-free
    /// (both spans are already sized by `arm()`). A hop that overshoots the
    /// remaining space is truncated at the boundary rather than growing the
    /// buffer; the caller is expected to stop feeding once `isFull()`.
    /// `referenceHop`/`measurementHop` need not be the same length as each
    /// other -- the shorter of the two bounds how much this call advances,
    /// so the two channels never drift out of step with one another.
    void feedHop(std::span<const float> referenceHop, std::span<const float> measurementHop) {
        const std::size_t hopLen = std::min(referenceHop.size(), measurementHop.size());
        const std::size_t remaining = (written_ < length_) ? (length_ - written_) : 0;
        const std::size_t take = std::min(hopLen, remaining);
        if (take == 0) return;
        std::copy_n(referenceHop.begin(), take,
                    reference_.begin() + static_cast<std::ptrdiff_t>(written_));
        std::copy_n(measurementHop.begin(), take,
                    measurement_.begin() + static_cast<std::ptrdiff_t>(written_));
        written_ += take;
    }

    [[nodiscard]] bool isFull() const noexcept { return written_ >= length_; }
    [[nodiscard]] std::size_t length() const noexcept { return length_; }
    [[nodiscard]] std::size_t written() const noexcept { return written_; }

    /// Valid to read at any time; only meaningful once `isFull()`. Size is
    /// always exactly `length()` -- the unwritten tail (before a capture
    /// completes) reads as the zero-fill `arm()` left it, never garbage.
    [[nodiscard]] std::span<const float> reference() const noexcept { return reference_; }
    [[nodiscard]] std::span<const float> measurement() const noexcept { return measurement_; }

private:
    std::vector<float> reference_;
    std::vector<float> measurement_;
    std::size_t length_ = 0;
    std::size_t written_ = 0;
};

}  // namespace rta::measure
