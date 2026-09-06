// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Record §8, task B5: sequencing is a capture state machine that refuses on
// two criteria needing no invented number, and drives no output.
#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace rta::measure {

enum class CaptureState { Idle, Armed, Waiting, Capturing };

/// Exactly two reasons, on purpose (record §8): the coherence-trusted-
/// fraction rule appears in no source and is a question for a real system
/// (`docs/HUMAN-QA-QUEUE.md`), not a third value here.
enum class RefusalReason { None, Overload, GateNotCleared };

struct CaptureRecord {
    std::string name;
    RefusalReason refusal = RefusalReason::None;
};

/// Walks a fixed, ordered list of members through arm -> wait -> capture ->
/// store -> advance, one member at a time. `onStep` is the ONLY thing this
/// class does toward driving anything -- it names an index, and a future
/// generator/solo path (out of scope here, record §8's own reasoning: the
/// audio callback clears every output today, and changing that is a
/// different lane) is whatever reacts to it. No method here accepts or
/// returns an output buffer of any kind.
class CaptureSequencer {
public:
    explicit CaptureSequencer(std::vector<std::string> memberNames);

    [[nodiscard]] CaptureState state() const noexcept { return state_; }
    [[nodiscard]] int currentStep() const noexcept { return step_; }
    [[nodiscard]] std::span<const CaptureRecord> captures() const noexcept { return captures_; }
    [[nodiscard]] RefusalReason lastRefusal() const noexcept { return lastRefusal_; }

    /// Idle -> Armed for the current member, or Idle -> Idle (a no-op) once
    /// every member has already been captured. Resets the overload latch
    /// for the window about to start.
    void arm();

    /// Armed -> Waiting.
    void beginWait();

    /// Waiting -> Capturing.
    void beginCapture();

    /// One hop's worth of BOTH channels of the member currently capturing.
    /// Feeds a streaming overload latch that -- unlike `rta::dsp::
    /// hasOverload` on its own -- DOES stitch a run across the hop boundary
    /// (that stitching is what "latches for the whole capture window" means
    /// one layer up from the pure per-hop detector; see .cpp). No-op
    /// outside `Capturing`.
    void feedHop(std::span<const float> referenceHop, std::span<const float> measurementHop);

    /// Ends the window: refuses (records `RefusalReason::Overload` if the
    /// latch tripped anywhere in the window, else `GateNotCleared` if
    /// `gateCleared` is false) or stores (`RefusalReason::None`, appended to
    /// `captures()`). Either way calls `onStep(currentStep())` exactly
    /// once, then advances to the next member's `Armed` state, or `Idle`
    /// once the list is exhausted.
    void endCapture(bool gateCleared);

    std::function<void(int stepIndex)> onStep;

private:
    std::vector<std::string> members_;
    std::vector<CaptureRecord> captures_;
    int step_ = 0;
    CaptureState state_ = CaptureState::Idle;
    RefusalReason lastRefusal_ = RefusalReason::None;

    // Streaming run-length state, one per channel, so a run split across
    // feedHop() calls (a hop boundary) is still caught -- see .cpp.
    int referenceRun_ = 0;
    int measurementRun_ = 0;
    bool overloadLatched_ = false;
};

}  // namespace rta::measure
