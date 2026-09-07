// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/DelayLocator.h"

#include <cstdint>

namespace rta::measure {

namespace {
/// record sec.11: "wait renderedSamples() >= 480*fs/48000" -- 480 samples at
/// 48 kHz is 10 ms, the settle time L7-OUT's own record gives the source's
/// ramp before the signal is stationary enough to correlate against.
constexpr double kSettleSamplesAt48k = 480.0;
constexpr double kSettleReferenceRate = 48000.0;
}  // namespace

DelayLocator::DelayLocator(rta::platform::OutputEngine& engine,
                            rta::platform::SourceVariant source, Config config)
    : engine_(engine), source_(std::move(source)), config_(std::move(config)) {
    if (std::holds_alternative<rta::gen::Mls>(source_)) {
        refusal_ = LocateRefusal::PeriodicExcitation;
    }
}

void DelayLocator::arm() {
    if (refusal_ != LocateRefusal::None) return;
    if (state_ != LocateState::Idle) return;

    // A copy into setSource: `source_` stays intact so this locator could,
    // in principle, be re-armed for a second Locate without the caller
    // reconstructing it (not exercised by any test here, but costs nothing
    // to keep true rather than moving-from a member).
    engine_.setSource(rta::platform::SourceVariant(source_));
    soloOutput(engine_, config_.outputChannel);
    engine_.armSource();
    state_ = LocateState::Waiting;
}

void DelayLocator::feedHop(std::span<const float> referenceHop,
                            std::span<const float> measurementHop) {
    if (state_ == LocateState::Waiting) {
        const double settleSamples =
                kSettleSamplesAt48k * (config_.options.sampleRate / kSettleReferenceRate);
        if (static_cast<double>(engine_.renderedSamples()) < settleSamples) {
            return;  // still settling -- this hop's audio is not captured
        }
        buffer_.arm(config_.captureLength);
        state_ = LocateState::Capturing;
    }

    if (state_ != LocateState::Capturing) return;

    buffer_.feedHop(referenceHop, measurementHop);
    if (!buffer_.isFull()) return;

    // disarmSource() BEFORE suggestDelay (record sec.8): the excitation has
    // done its job the instant the window is full, and nothing past this
    // point should keep the loudspeaker playing.
    engine_.disarmSource();
    suggestion_ = rta::dsp::suggestDelay(buffer_.reference(), buffer_.measurement(),
                                          config_.options, config_.policy);
    state_ = LocateState::Done;
}

}  // namespace rta::measure
