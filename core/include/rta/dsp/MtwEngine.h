// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/DualFftEngine.h"
#include "rta/dsp/MtwLayout.h"
#include "rta/dsp/MtwResult.h"
#include "rta/dsp/TransferEstimator.h"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace rta::dsp {

/// `octaveCount + 1` full-rate `DualFftEngine`s, one per octave, doubling in
/// `fftSize` downward -- record §2's "no decimation" decision. Every engine
/// sees the SAME reference/measurement samples; each owns its own bins
/// (MtwLayout.h) and its own copy of the reference delay, applied at full
/// rate in integer samples (plan task 2's rationale: the skip logic lives
/// inside `DualFftEngine::process`, a file this lane may not edit, and
/// duplicating it upstream would be a second place to get alignment wrong).
///
/// Real-time contract: all allocation happens in the constructor. `process()`
/// does nothing but forward to each band engine -- it allocates nothing, ever.
/// Stitching a result allocates, so stitching is `makeMtwResult()`, a free
/// function called from the publish path, never from `process()` -- the same
/// split `makeSnapshot()` already has for one engine.
class MtwEngine {
public:
    explicit MtwEngine(const MtwConfig& config);

    [[nodiscard]] const MtwConfig& config() const noexcept { return config_; }
    [[nodiscard]] std::size_t bandCount() const noexcept { return engines_.size(); }
    [[nodiscard]] const DualFftEngine& band(std::size_t index) const noexcept {
        return *engines_[index];
    }

    /// Forwards to every band engine in turn. Throws std::invalid_argument if
    /// the spans differ in length -- delegated to the first band's own check,
    /// but every band would refuse identically, so a caller never discovers
    /// the mismatch in only one of them.
    void process(std::span<const float> reference, std::span<const float> measurement);

    /// Re-arms every band's reference-delay skip from its own Config, exactly
    /// as DualFftEngine::reset() re-arms a single engine's.
    void reset() noexcept;

private:
    MtwConfig config_;
    std::vector<MtwBand> bands_;
    /// `unique_ptr`, not a bare `DualFftEngine`: the class holds a
    /// `RingBuffer` with `std::atomic` members, so it is neither copyable nor
    /// movable, and `std::vector<DualFftEngine>` will not compile on this STL
    /// (MSVC's `_Uninitialized_move` requires one or the other). Indirection
    /// through a pointer is the only way to put several in a vector without
    /// touching the file that may not change. Built once in the constructor
    /// and never resized -- same "constructed once" contract either way.
    std::vector<std::unique_ptr<DualFftEngine>> engines_;
};

/// The one place an MtwResult is built, mirroring makeSnapshot()'s role for a
/// single engine: stitches each band's own gated TransferSnapshot into one
/// frequency-ordered curve without ever assigning a coherence value itself
/// (conflict C4 -- see MtwResult.h).
[[nodiscard]] MtwResult makeMtwResult(const MtwEngine& engine, Estimator estimator);

}  // namespace rta::dsp
