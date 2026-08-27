// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#include "rta/gen/Noise.h"

#include <cmath>

namespace rta::gen {

WhiteNoise::WhiteNoise(Pcg32 rng, double levelDbFsRms) : rng_(rng) {
    setLevelDbFsRms(levelDbFsRms);
}

void WhiteNoise::setLevelDbFsRms(double db) noexcept {
    levelDbFsRms_ = db;
    // Raw nextUniform() RMS is sqrt(1/3) (Prng.h; re-derived in Noise.h),
    // so sqrt(3) normalises it to unit RMS before the target dB is applied.
    scale_ = std::pow(10.0, db / 20.0) * std::sqrt(3.0);
}

float WhiteNoise::nextSample() noexcept {
    return static_cast<float>(static_cast<double>(rng_.nextUniform()) * scale_);
}

void WhiteNoise::process(std::span<float> out) noexcept {
    for (float& sample : out) sample = nextSample();
}

PinkNoise::PinkNoise(Pcg32 rng, double levelDbFsRms) : rng_(rng) {
    setLevelDbFsRms(levelDbFsRms);
}

void PinkNoise::setLevelDbFsRms(double db) noexcept {
    levelDbFsRms_ = db;
    // Same sqrt(3) un-normalisation as WhiteNoise, then divided by the
    // filter's own RMS gain so the requested dB lands on the POST-filter
    // RMS, not the pre-filter one -- see Noise.h's kRmsGainVsWhite comment.
    scale_ = std::pow(10.0, db / 20.0) * std::sqrt(3.0) / kRmsGainVsWhite;
}

float PinkNoise::nextSample() noexcept {
    const double w = static_cast<double>(rng_.nextUniform());

    // Paul Kellett's refined 7-term IIR (Noise.h has the full derivation and
    // the citation for kRmsGainVsWhite). Each state update reads its own
    // PREVIOUS value on the right-hand side, in this exact order, before
    // `out` is formed -- reordering these six lines changes which sample of
    // `w` each pole sees relative to the others and produces a different
    // (and un-golden) filter.
    b0_ = 0.99886 * b0_ + w * 0.0555179;
    b1_ = 0.99332 * b1_ + w * 0.0750759;
    b2_ = 0.96900 * b2_ + w * 0.1538520;
    b3_ = 0.86650 * b3_ + w * 0.3104856;
    b4_ = 0.55000 * b4_ + w * 0.5329522;
    b5_ = -0.76160 * b5_ - w * 0.0168980;
    const double out = b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + w * 0.5362;
    // b6_ still holds the PREVIOUS sample's contribution above (the
    // one-sample-delayed direct term); only now does it advance to this
    // sample's, ready for the NEXT call.
    b6_ = w * 0.115926;

    return static_cast<float>(out * scale_);
}

void PinkNoise::process(std::span<float> out) noexcept {
    for (float& sample : out) sample = nextSample();
}

}  // namespace rta::gen
