// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#include "rta/gen/Oscillator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rta::gen {

namespace {

/// 20*log10(2): the level offset between "the pair's peak" and "each tone's
/// own amplitude" when two sines are each scaled to amplitude/2 so their
/// worst-case in-phase sum reaches the pair's requested peak. A named
/// constant, not a runtime std::log10(2.0) call, so DualSine's constructor
/// stays noexcept and the value is visible at the call site without tracing
/// into a helper.
constexpr double kPairToPerToneDb = 6.0205999132796239;

double dbToAmplitude(double db) noexcept {
    return std::pow(10.0, db / 20.0);
}

}  // namespace

Oscillator::Oscillator(double sampleRate, double frequencyHz, double levelDbFsPeak) noexcept
    : sampleRateHz_(sampleRate), frequencyHz_(frequencyHz), amplitude_(dbToAmplitude(levelDbFsPeak)) {}

void Oscillator::setFrequency(double hz) noexcept {
    frequencyHz_ = hz;
}

void Oscillator::setLevelDbFsPeak(double db) noexcept {
    amplitude_ = dbToAmplitude(db);
}

float Oscillator::nextSample() noexcept {
    // Evaluate at the CURRENT phase, then advance -- so sample 0 is always
    // sin(0) = 0, matching the "starts at zero turns" contract phaseTurns()
    // is checked against.
    float sample = static_cast<float>(amplitude_ * std::sin(2.0 * std::numbers::pi * phase_));

    phase_ += frequencyHz_ / sampleRateHz_;
    // `while`, not `if`: a frequency at or above sampleRateHz_ (caller error,
    // but not this class's job to reject) must not desynchronise phase_ by
    // leaving it >= 1.0 -- the wrap has to be exhaustive every sample.
    while (phase_ >= 1.0) phase_ -= 1.0;

    return sample;
}

void Oscillator::process(std::span<float> out) noexcept {
    for (float& sample : out) sample = nextSample();
}

DualSine::DualSine(double sampleRate, double f1Hz, double f2Hz, double levelDbFsPeak) noexcept
    : osc1_(sampleRate, f1Hz, levelDbFsPeak - kPairToPerToneDb),
      osc2_(sampleRate, f2Hz, levelDbFsPeak - kPairToPerToneDb) {}

float DualSine::nextSample() noexcept {
    return osc1_.nextSample() + osc2_.nextSample();
}

void DualSine::process(std::span<float> out) noexcept {
    for (float& sample : out) sample = nextSample();
}

RampedGain::RampedGain(double sampleRate, double rampSeconds) noexcept
    : rampLenSamples_(std::max(1, static_cast<int>(std::lround(sampleRate * rampSeconds)))),
      rampSeconds_(rampSeconds) {}

void RampedGain::requestOn() noexcept {
    target_.store(true, std::memory_order_relaxed);
}

void RampedGain::requestOff() noexcept {
    target_.store(false, std::memory_order_relaxed);
}

float RampedGain::nextGain() noexcept {
    const bool wantsOn = target_.load(std::memory_order_relaxed);

    // A target change only ever flips which way `pos_` will move from here.
    // It never touches `pos_` itself -- that is what keeps g(pos_/L)
    // continuous through a reversal instead of jumping to a stale endpoint.
    if (wantsOn && (state_ == State::Idle || state_ == State::Falling)) {
        state_ = State::Rising;
    } else if (!wantsOn && (state_ == State::Running || state_ == State::Rising)) {
        state_ = State::Falling;
    }

    float gain = 0.0f;  // MSVC's flow analysis doesn't see the switch below as exhaustive
    switch (state_) {
        case State::Idle:
            gain = 0.0f;
            break;
        case State::Running:
            gain = 1.0f;
            break;
        case State::Rising:
        case State::Falling: {
            // Half a raised-cosine period: g and dg/dp both vanish at p=0,
            // g=1 and dg/dp=0 at p=1 -- the C1 continuity at both ends that
            // makes the ramp click-free. Falling is not a separately coded
            // mirror formula: driving the SAME p=pos_/L downward through this
            // one formula traces out exactly g(1-p) by the cosine's own
            // symmetry, which is what keeps the reversal case (test 11)
            // jump-free without special-casing it here.
            double p = static_cast<double>(pos_) / static_cast<double>(rampLenSamples_);
            gain = static_cast<float>(0.5 * (1.0 - std::cos(std::numbers::pi * p)));
            break;
        }
    }

    if (state_ == State::Rising) {
        if (pos_ < rampLenSamples_) ++pos_;
        if (pos_ >= rampLenSamples_) state_ = State::Running;
    } else if (state_ == State::Falling) {
        if (pos_ > 0) --pos_;
        if (pos_ <= 0) state_ = State::Idle;
    }

    return gain;
}

void RampedGain::apply(std::span<float> block) noexcept {
    for (float& sample : block) sample *= nextGain();
}

void RampedGain::prepare(double sampleRate) noexcept {
    // Same clamp the ctor uses, applied to the STORED rampSeconds_ rather
    // than a fresh parameter, so a rate change reproduces the ctor's
    // original ramp duration in samples at the new rate.
    rampLenSamples_ = std::max(1, static_cast<int>(std::lround(sampleRate * rampSeconds_)));
    pos_ = 0;
    state_ = State::Idle;
    // target_ is deliberately untouched -- see the header's contract.
}

}  // namespace rta::gen
