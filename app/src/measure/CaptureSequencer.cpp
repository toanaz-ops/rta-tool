// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
#include "measure/CaptureSequencer.h"

#include "rta/dsp/OverloadDetector.h"

#include <cmath>
#include <utility>

namespace rta::measure {

namespace {

/// Advances a STREAMING run count across successive hops -- the property
/// `rta::dsp::hasOverload` deliberately does not have on its own ("a run
/// does not carry across separate calls", OverloadDetector.h's own
/// comment). Returns true the instant the run reaches 3, and keeps
/// returning true on every later call this window (the caller latches it;
/// this function only ever reports "the run reached 3 just now").
bool feedRun(std::span<const float> hop, int& run) {
    bool reachedThree = false;
    for (float x : hop) {
        if (std::abs(x) >= rta::dsp::kFullScaleThreshold) {
            ++run;
            if (run >= 3) reachedThree = true;
        } else {
            run = 0;
        }
    }
    return reachedThree;
}

}  // namespace

CaptureSequencer::CaptureSequencer(std::vector<std::string> memberNames)
    : members_(std::move(memberNames)) {}

void CaptureSequencer::arm() {
    if (state_ != CaptureState::Idle) return;
    if (step_ >= static_cast<int>(members_.size())) return;  // sequence already complete
    state_ = CaptureState::Armed;
    referenceRun_ = 0;
    measurementRun_ = 0;
    overloadLatched_ = false;
}

void CaptureSequencer::beginWait() {
    if (state_ != CaptureState::Armed) return;
    state_ = CaptureState::Waiting;
}

void CaptureSequencer::beginCapture() {
    if (state_ != CaptureState::Waiting) return;
    state_ = CaptureState::Capturing;
}

void CaptureSequencer::feedHop(std::span<const float> referenceHop,
                               std::span<const float> measurementHop) {
    if (state_ != CaptureState::Capturing) return;
    // Both channels' runs are tracked independently -- Smaart's own
    // criterion (record §8.1) is "three or more consecutive samples ... on
    // the reference OR the measurement channel", so either one alone
    // trips it.
    if (feedRun(referenceHop, referenceRun_)) overloadLatched_ = true;
    if (feedRun(measurementHop, measurementRun_)) overloadLatched_ = true;
}

void CaptureSequencer::endCapture(bool gateCleared) {
    if (state_ != CaptureState::Capturing) return;

    RefusalReason refusal = RefusalReason::None;
    if (overloadLatched_) {
        refusal = RefusalReason::Overload;
    } else if (!gateCleared) {
        refusal = RefusalReason::GateNotCleared;
    }

    lastRefusal_ = refusal;
    if (refusal == RefusalReason::None) {
        captures_.push_back(CaptureRecord{members_[static_cast<std::size_t>(step_)], refusal});
    }
    // A refused step stores NOTHING -- no degraded entry in `captures_`
    // masquerading as a real one, only `lastRefusal()` says what happened.

    if (onStep) onStep(step_);

    ++step_;
    state_ = step_ >= static_cast<int>(members_.size()) ? CaptureState::Idle : CaptureState::Armed;
    // The next member's window starts clean regardless of how it was
    // reached: an explicit arm() call after Idle, or this auto-advance
    // straight to Armed. Without this, a refusal on one member would leave
    // its overload latch (or a stale run count) bleeding into the NEXT
    // member's window -- exactly the "the sequence continues, unaffected"
    // property the record's own refusal design depends on.
    referenceRun_ = 0;
    measurementRun_ = 0;
    overloadLatched_ = false;
}

}  // namespace rta::measure
