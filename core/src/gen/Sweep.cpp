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
/// click-free. Used for the sweep's own start and end fades, and ONLY those.
/// The inverse filter inherits its taper through the reversal; it is not faded
/// again (see buildInverseFilter).
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

    // THREE floors compete and the widest wins. They are in different units and
    // neither implies the other:
    //
    //   config.fadeInSec          what the caller asked for
    //   2 / startHz               two cycles at f1 -- an unfaded start is a
    //                             broadband click landing in the exact band the
    //                             sweep exists to measure
    //   fadeInOctaves*ln2*L       the width that governs the deconvolution's
    //                             pre-arrival artefact floor
    //
    // For a 10 s sweep from 20 Hz the cycles floor is 0.1 s and the octave floor
    // is 2.007 s; for a 0.05 s sweep the cycles floor is the larger. Both stay.
    const double octaveFloorSec = config.fadeInOctaves > 0.0
        ? config.fadeInOctaves * std::log(2.0) * lengthL_
        : 0.0;
    const double fadeInSec = std::max({ config.fadeInSec, 2.0 / startHz_, octaveFloorSec });
    fadeInSamples_ = roundToSamples(fadeInSec, sampleRate_);

    // Two cycles at the END frequency, the mirror of the fade-in's two cycles
    // at the start frequency and there for the same reason: an unfaded
    // switch-off is a broadband click, and after the reversal that builds the
    // inverse filter it lands at the very START of the deconvolution kernel,
    // where the +6 dB/oct envelope is at its maximum and amplifies it.
    //
    // Five samples at 20 kHz. Inaudible, spectrally invisible (0.02 octave is
    // already measured harmless), and dormant at every default this project
    // ships, where fadeOutSec = 0.02 s gives 960 samples. It exists so that
    // `fadeOutSec = 0` -- a legitimate request, and the only case the old
    // second fade layer in buildInverseFilter genuinely covered -- is handled
    // on the FORWARD signal, where the click actually is, rather than patched
    // on the kernel afterwards.
    fadeOutSec_ = std::max(config.fadeOutSec, 2.0 / endHz_);
    fadeOutSamples_ = roundToSamples(fadeOutSec_, sampleRate_);

    // Guard the degenerate case (a fade of 0 or 1 samples has no interior to
    // divide by) rather than let a caller-supplied near-zero duration produce
    // a division by zero inside fadeEnvelope. The fade-out's `!= 0` half went
    // with the floor above: it can no longer be zero.
    if (fadeInSamples_ < 2) fadeInSamples_ = 2;
    if (fadeOutSamples_ < 2) fadeOutSamples_ = 2;
}

double Sweep::fadeInOctavesAchieved() const noexcept {
    // Invert fadeInSec = octaves*ln2*L, using the SAMPLE count actually stored
    // rather than the requested seconds, so the rounding to whole samples is
    // included in the answer instead of being assumed away.
    const double seconds = static_cast<double>(fadeInSamples_) / sampleRate_;
    return seconds / (std::log(2.0) * lengthL_);
}

double Sweep::validBandLowHz() const noexcept {
    const double seconds = static_cast<double>(fadeInSamples_) / sampleRate_;
    return startHz_ * std::exp(seconds / lengthL_);
}

double Sweep::validBandHighHz() const noexcept {
    return endHz_ * std::exp(-fadeOutSec_ / lengthL_);
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

    // NO SECOND FADE HERE, and the reason is worth stating because a second
    // one lived here until 2026-08-30, added on the authority of §2.4 of
    // docs/plans/2026-08-27-generator-impl-plan.md (now marked superseded).
    //
    // `forward` above is already multiplied by fadeEnvelope, which is zero at
    // both ends, and the reversal carries those zeros. So `inv` already begins
    // and ends at zero: an extra Tukey layer multiplies something that is
    // already nothing.
    //
    // It was not merely redundant, it was harmful, and the damage grew with the
    // fade width. `inv[0]` corresponds to the sweep's LAST sample, so a layer
    // applied over `fadeInSamples_` from the start of `inv` tapers the kernel's
    // HIGHEST frequencies -- and a wide taper at the high edge is measured to
    // make the deconvolution's artefact floor worse, not better. With a
    // two-octave fade-in the in-band flatness went from 0.27 dB to 72.36 dB.
    // Even at today's narrow fades it was costing 13.75 dB at the default
    // configuration. See docs/dsp/2026-08-30-sweep-ir-l4a.md decision 5.
    //
    // No test caught it for four days because every fixture used equal fade-in
    // and fade-out widths, under which every candidate construction agrees.
    // `test_generator_sweep.cpp` now pins the construction at an ASYMMETRIC
    // configuration, which is the only kind that can tell them apart.
    //
    // The one case the old layer did cover -- `fadeOutSec == 0`, where the
    // forward sweep ends with a step -- is handled where it happens, by the
    // two-cycles-at-endHz floor in the constructor.
    return inv;
}

}  // namespace rta::gen
