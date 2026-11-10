// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/ResampledPairSource.h"

#include "rta/dsp/KaiserSincInterpolator.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rta::measure {

namespace {
using rta::dsp::KaiserSincInterpolator;
// Margin so the first needed tap (floor(t) - 31) is never below the buffer we ask for.
constexpr std::int64_t kTapMargin = 40;
}  // namespace

ResampledPairSource::ResampledPairSource(const SampleBlockSource& reference, int referenceChannel,
                                         const SampleBlockSource& measurement, int measurementChannel,
                                         ClockMapping mapping)
    : reference_(reference),
      referenceChannel_(referenceChannel),
      measurement_(measurement),
      measurementChannel_(measurementChannel),
      mapping_(mapping) {}

bool ResampledPairSource::tapsInside(std::int64_t n) const {
    const std::int64_t length = measurement_.lengthSamples();
    const double t = mapping_.position(n);
    if (mapping_.integerOnly) {
        const auto index = static_cast<std::int64_t>(std::llround(t));
        return index >= 0 && index < length;
    }
    // Taps are floor(t) - 31 .. floor(t) + 32 (the kernel is zero from |x| = 32).
    const double base = std::floor(t);
    return base - 31.0 >= 0.0 && base + 32.0 <= static_cast<double>(length - 1);
}

ResampledPairSource::Range ResampledPairSource::validRange() const {
    const std::int64_t refLength = reference_.lengthSamples();
    if (refLength <= 0 || measurement_.lengthSamples() <= 0) return {};
    // t is linear in n, so the valid set is an interval: solve for its ends
    // approximately, then settle the exact ends with the predicate itself.
    const double ratio = mapping_.ratio;
    const double lo = (0.0 - mapping_.offsetSamples) / ratio;
    const double hi = (static_cast<double>(measurement_.lengthSamples()) - mapping_.offsetSamples) / ratio;
    auto first = static_cast<std::int64_t>(std::floor(std::min(lo, hi))) - 2;
    auto last = static_cast<std::int64_t>(std::ceil(std::max(lo, hi))) + 2;
    first = std::clamp<std::int64_t>(first, 0, refLength - 1);
    last = std::clamp<std::int64_t>(last, 0, refLength - 1);
    while (first <= last && !tapsInside(first)) ++first;
    while (last >= first && !tapsInside(last)) --last;
    return {first, last};
}

bool ResampledPairSource::read(int channel, std::int64_t startSample, std::span<float> out) const {
    if (out.empty()) return true;
    if (channel == 0) return reference_.read(referenceChannel_, startSample, out);
    if (channel != 1) return false;
    const auto count = static_cast<std::int64_t>(out.size());
    if (startSample < 0 || startSample + count > reference_.lengthSamples()) return false;
    if (!tapsInside(startSample) || !tapsInside(startSample + count - 1)) return false;

    if (mapping_.integerOnly) {
        const auto first = static_cast<std::int64_t>(std::llround(mapping_.position(startSample)));
        return measurement_.read(measurementChannel_, first, out);
    }

    const double firstPosition = mapping_.position(startSample);
    const auto base = static_cast<std::int64_t>(std::floor(firstPosition)) - kTapMargin;
    const double relative = firstPosition - static_cast<double>(base);
    const auto need = KaiserSincInterpolator::requiredInputSpan(relative, mapping_.ratio, out.size());
    std::vector<float> input(static_cast<std::size_t>(need.last - need.first + 1));
    if (!measurement_.read(measurementChannel_, base + need.first, input)) return false;
    KaiserSincInterpolator{}.resample(input, relative - static_cast<double>(need.first), mapping_.ratio, out);
    return true;
}

}  // namespace rta::measure
