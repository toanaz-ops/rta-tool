// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#include "rta/gen/Sweep.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rta::gen {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

/// Shared raised-cosine (Tukey-shaped) taper closed form: 0 at p=0, 1 at p=1,
/// with zero slope at both ends -- the C1 continuity that makes a taper
/// click-free. Used for the sweep's own start/end fades and again, verbatim,
/// on the inverse filter's fades.
double raisedCosine(double p) noexcept {
    return 0.5 * (1.0 - std::cos(kPi * p));
}

/// 10^(db/20), the peak-referenced amplitude convention this module uses for
/// every deterministic signal (decision record docs/dsp/2026-08-27-generator.md:
/// "Deterministic signals ... are peak-referenced dBFS -- their crest factors
/// are closed forms, so peak hides nothing").
double amplitudeFromDbFsPeak(double db) noexcept {
    return std::pow(10.0, db / 20.0);
}

std::size_t roundToSamples(double seconds, double sampleRate) noexcept {
    return static_cast<std::size_t>(std::llround(seconds * sampleRate));
}

}  // namespace

Sweep::Sweep(const Config& config)
    : sampleRate_(config.sampleRate),
      startHz_(config.startHz),
      endHz_(config.endHz),
      amplitude_(amplitudeFromDbFsPeak(config.levelDbFsPeak)) {
    if (!(sampleRate_ > 0.0)) {
        throw std::invalid_argument("Sweep: sampleRate must be positive");
    }
    if (!(startHz_ > 0.0)) {
        throw std::invalid_argument("Sweep: startHz must be positive");
    }
    if (!(endHz_ > startHz_)) {
        throw std::invalid_argument("Sweep: endHz must exceed startHz (ln(f2/f1) > 0)");
    }
    if (!(config.durationSec > 0.0)) {
        throw std::invalid_argument("Sweep: durationSec must be positive");
    }

    // L = T / ln(f2/f1); K = 2*pi*f1*L. See the derivation in Sweep.h.
    lengthL_ = config.durationSec / std::log(endHz_ / startHz_);
    phaseK_ = kTwoPi * startHz_ * lengthL_;
    lengthSamples_ = roundToSamples(config.durationSec, sampleRate_);

    // The fade-in is clamped up to two full cycles at the start frequency: an
    // unfaded start at f1 is a broadband click that pollutes exactly the
    // low-frequency band the sweep exists to measure, and anything shorter
    // than one full cycle at f1 is not meaningfully a fade at all. The
    // fade-out carries no such clamp -- only the start is named in the
    // decision record.
    const double minFadeInSec = 2.0 / startHz_;
    const double fadeInSec = std::max(config.fadeInSec, minFadeInSec);
    fadeInSamples_ = roundToSamples(fadeInSec, sampleRate_);
    fadeOutSamples_ = roundToSamples(config.fadeOutSec, sampleRate_);

    // Guard the degenerate case (a fade of 0 or 1 samples has no interior to
    // divide by) rather than let a caller-supplied near-zero duration produce
    // a division by zero inside fadeEnvelope.
    if (fadeInSamples_ < 2) fadeInSamples_ = 2;
    if (fadeOutSamples_ != 0 && fadeOutSamples_ < 2) fadeOutSamples_ = 2;
}

double Sweep::phaseAt(std::size_t n) const noexcept {
    const double t = static_cast<double>(n) / sampleRate_;
    return phaseK_ * (std::exp(t / lengthL_) - 1.0);
}

double Sweep::instantaneousFrequency(std::size_t n) const noexcept {
    const double t = static_cast<double>(n) / sampleRate_;
    return startHz_ * std::exp(t / lengthL_);
}

double Sweep::fadeEnvelope(std::size_t n) const noexcept {
    // Raised-cosine (Tukey-shaped) taper: g(p) = 0.5*(1 - cos(pi*p)), the same
    // closed form RampedGain uses (gen/Oscillator.h) for the same reason -- C1
    // continuity at both ends of the taper is what makes the transition
    // click-free.
    if (n < fadeInSamples_) {
        const double p = static_cast<double>(n) / static_cast<double>(fadeInSamples_ - 1);
        return raisedCosine(p);
    }
    if (fadeOutSamples_ > 0 && lengthSamples_ > 0) {
        const std::size_t lastIndex = lengthSamples_ - 1;
        if (n <= lastIndex && lastIndex - n < fadeOutSamples_) {
            const std::size_t fromEnd = lastIndex - n;
            const double p = static_cast<double>(fromEnd) / static_cast<double>(fadeOutSamples_ - 1);
            return raisedCosine(p);
        }
    }
    return 1.0;
}

float Sweep::nextSample() noexcept {
    if (n_ >= lengthSamples_) return 0.0f;
    const double sample = amplitude_ * std::sin(phaseAt(n_)) * fadeEnvelope(n_);
    ++n_;
    return static_cast<float>(sample);
}

void Sweep::process(std::span<float> out) noexcept {
    for (float& sample : out) sample = nextSample();
}

void Sweep::restart() noexcept {
    n_ = 0;
}

std::vector<float> Sweep::buildInverseFilter() const {
    // NOT REAL-TIME SAFE: allocates lengthSamples() floats twice and runs two
    // full passes. Call only on the message/control thread when parameters
    // change; hand the result to the analysis thread by pointer. Calling this
    // from an audio callback is the rule that, if broken, produces dropouts
    // at a live show, which is the exact situation this tool exists for.
    const std::size_t n = lengthSamples_;
    std::vector<float> forward(n);
    for (std::size_t i = 0; i < n; ++i) {
        forward[i] = static_cast<float>(amplitude_ * std::sin(phaseAt(i)) * fadeEnvelope(i));
    }

    std::vector<float> inv(n);
    for (std::size_t m = 0; m < n; ++m) {
        const std::size_t original = n - 1 - m;
        // +6 dB/oct envelope: see the derivation in Sweep.h. Ratio of the
        // ORIGINAL sample's instantaneous frequency to endHz -- naturally ~1
        // at m = 0 (original = N-1, where the sweep was fastest) and falling
        // as m grows (original frequency falls toward startHz).
        const double envelope = instantaneousFrequency(original) / endHz_;
        inv[m] = static_cast<float>(static_cast<double>(forward[original]) * envelope);
    }

    // Tukey-fade the inverse filter itself at both ends, same shape as the
    // forward sweep's own fades (§2.4 of the plan): without this the inverse
    // filter starts/ends with a step, which is exactly the click the forward
    // sweep's fade exists to avoid, now on the deconvolution kernel.
    for (std::size_t m = 0; m < n && m < fadeInSamples_; ++m) {
        const double p = static_cast<double>(m) / static_cast<double>(fadeInSamples_ - 1);
        inv[m] = static_cast<float>(static_cast<double>(inv[m]) * raisedCosine(p));
    }
    if (fadeOutSamples_ > 0) {
        for (std::size_t m = 0; m < n && m < fadeOutSamples_; ++m) {
            const std::size_t idx = n - 1 - m;
            const double p = static_cast<double>(m) / static_cast<double>(fadeOutSamples_ - 1);
            inv[idx] = static_cast<float>(static_cast<double>(inv[idx]) * raisedCosine(p));
        }
    }

    return inv;
}

}  // namespace rta::gen
