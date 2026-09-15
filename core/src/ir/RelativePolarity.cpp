// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/ir/RelativePolarity.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace rta::ir {
namespace {

/// The arrival window, copied out and treated as zero everywhere else.
///
/// The zero-fill is not a convenience. It is what makes the Cauchy-Schwarz
/// bound exact at EVERY lag: a shifted window can only lose energy, so
/// |r(l)| <= ||a|| ||shifted b|| <= sqrt(E_a E_b) for all l. Normalising by a
/// fixed E_b without it would let a lag whose shifted window happens to be
/// louder read rho > 1, and the bound the whole design rests on would be a
/// claim rather than a fact.
std::vector<double> windowOf(const Deconvolution& source, std::size_t length) {
    std::vector<double> out;
    out.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        const std::size_t index = source.originIndex + i;
        out.push_back(index < source.samples.size() ? static_cast<double>(source.samples[index])
                                                    : 0.0);
    }
    return out;
}

double energyOf(const std::vector<double>& values) noexcept {
    double total = 0.0;
    for (const double v : values) total += v * v;
    return total;
}

}  // namespace

RelativePolarity relativePolarity(const Deconvolution& a, const Deconvolution& b,
                                  const RelativePolarityConfig& config) {
    if (a.sampleRate <= 0.0 || b.sampleRate <= 0.0 || a.sampleRate != b.sampleRate) {
        throw std::invalid_argument(
            "relativePolarity: the two deconvolutions must share one positive sample rate");
    }
    if (config.searchSeconds <= 0.0) {
        throw std::invalid_argument("relativePolarity: searchSeconds must be positive");
    }

    RelativePolarity result;

    const auto requested =
        static_cast<std::size_t>(std::llround(config.searchSeconds * a.sampleRate));
    const std::size_t availableA =
        a.originIndex < a.samples.size() ? a.samples.size() - a.originIndex : 0;
    const std::size_t availableB =
        b.originIndex < b.samples.size() ? b.samples.size() - b.originIndex : 0;
    const std::size_t length = std::min({ requested, availableA, availableB });
    if (length < 2) {
        // Not enough capture after the arrival to correlate anything. Refused
        // rather than answered over one sample, which would read rho == 1 for
        // any two non-zero numbers.
        result.refusal = Refusal::CaptureTooShort;
        return result;
    }

    const std::vector<double> wa = windowOf(a, length);
    const std::vector<double> wb = windowOf(b, length);
    const double energyA = energyOf(wa);
    const double energyB = energyOf(wb);
    if (energyA <= 0.0 || energyB <= 0.0) {
        result.refusal = Refusal::NoSignal;
        return result;
    }

    // argmax over |r(l)|, NOT over r(l). Taking the signed maximum would find
    // the largest POSITIVE lobe, which on an inverted pair is a sidelobe some
    // way off the true alignment -- the answer would be "these agree, slightly
    // late" instead of "these disagree".
    const auto span = static_cast<std::ptrdiff_t>(length);
    double bestAbsolute = -1.0;
    double bestSigned = 0.0;
    std::ptrdiff_t bestLag = 0;
    for (std::ptrdiff_t lag = -(span - 1); lag < span; ++lag) {
        const std::ptrdiff_t from = std::max<std::ptrdiff_t>(0, -lag);
        const std::ptrdiff_t to = std::min(span, span - lag);
        double sum = 0.0;
        for (std::ptrdiff_t n = from; n < to; ++n) {
            sum += wa[static_cast<std::size_t>(n)] * wb[static_cast<std::size_t>(n + lag)];
        }
        const double magnitude = std::abs(sum);
        if (magnitude > bestAbsolute) {
            bestAbsolute = magnitude;
            bestSigned = sum;
            bestLag = lag;
        }
    }

    result.rho = bestAbsolute / std::sqrt(energyA * energyB);
    result.lag = bestLag;
    result.sign = bestSigned < 0.0 ? Sign::Negative : Sign::Positive;
    result.refusal = Refusal::None;
    return result;
}

}  // namespace rta::ir
