// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_platform_types. No JUCE, no Qt, no audio-device API.
//
// L7-OUT (lane L7, sub-lane L7-OUT): the lock-free generator output path.
// Decision record docs/dsp/2026-09-06-l7-output-path.md sec.6 is the
// contract this class implements verbatim; docs/plans/2026-09-07-L7-out-
// impl-plan.md task B is the plan that built it. Takes plain C arrays on the
// audio side, exactly like `CaptureBus::pushFromCallback` -- provable with no
// device.
#pragma once

#include "rta/gen/Mls.h"
#include "rta/gen/Noise.h"
#include "rta/gen/Oscillator.h"
#include "rta/gen/Sweep.h"
#include "rta/platform/OutputRoutingConfig.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace rta::platform {

/// The closed set of callback-eligible sources (record sec.4, sec.1.1):
/// every alternative is `noexcept`, allocation-free, span-out. `Synthetic.h`
/// (FFT-bearing) is deliberately NOT a member -- it is outside this set by
/// name, not merely unused.
using SourceVariant = std::variant<std::monostate, gen::Oscillator, gen::DualSine,
                                    gen::WhiteNoise, gen::PinkNoise, gen::Sweep, gen::Mls>;

/// One active source, rendered synchronously by the audio callback (record
/// sec.2: no feeder thread), gated through a master `RampedGain` (source
/// start/stop/swap, record sec.3) and one independent `RampedGain` per
/// output channel (solo/mute, additive by construction -- strict solo is
/// `app/` policy over `routeOutput`, record sec.3).
class OutputEngine {
public:
    /// Chunk size `render()` processes the active source through scratch in.
    /// Any `numSamples` is handled through repeated chunks -- no growth path,
    /// no "buffer too small" branch (record sec.7).
    static constexpr std::size_t kScratchCapacity = 2048;

    OutputEngine();  // the ONLY allocation: scratch, sized once

    /// Device thread, before the callback is inserted (`CaptureBus::prepare`'s
    /// slot). Bumps `outputEpoch()`, disarms, retargets every gate to the new
    /// rate. NEVER writes the slot -- the message thread may be inside
    /// `setSource()` (record sec.4's device-reconfiguration paragraph).
    void prepare(double sampleRate, int numOutputChannels) noexcept;

    // ---- message thread: the source (quiescent-swap protocol, record sec.4) --
    [[nodiscard]] bool sourceIsQuiescent() const noexcept;  // acquire
    bool setSource(SourceVariant&& source) noexcept;        // false unless quiescent
    void armSource() noexcept;                              // release
    void disarmSource() noexcept;

    // ---- message thread: per-output gates. THE call every consumer uses. ----
    // Relaxed; false and no effect outside [0, kMaxChannels). Sets the role
    // AND the gate's target in one call -- strict solo is the caller's loop.
    bool routeOutput(int outputChannel, bool active) noexcept;
    [[nodiscard]] OutputRole role(int outputChannel) const noexcept;

    // ---- telemetry: written by the callback, read anywhere ------------------
    [[nodiscard]] std::uint64_t renderedSamples() const noexcept;  // since last arm
    [[nodiscard]] std::uint64_t outputEpoch() const noexcept;      // bumped by prepare()
    [[nodiscard]] double sampleRate() const noexcept;

    /// Audio callback: the ONLY audio-thread entry point. Wait-free. Renders
    /// (if armed, current-epoch, gate not Idle) into scratch in chunks,
    /// master-gates it, copies through each routed output's gate, clears
    /// every other channel it was handed, publishes counters.
    /// `output == nullptr` is a no-op. Bounds-checked against
    /// `numOutputChannels` RECEIVED this block, never against a wider count
    /// configured earlier (same rule `ChannelConfig`/`CaptureBus` state).
    void render(float* const* output, int numOutputChannels, int numSamples) noexcept;

private:
    static constexpr double kPlaceholderSampleRate = 48000.0;

    SourceVariant slot_;
    gen::RampedGain master_;
    std::array<gen::RampedGain, static_cast<std::size_t>(kMaxChannels)> outputGates_;
    OutputRoutingConfig routing_;
    std::vector<float> scratch_;

    std::atomic<bool> publishedIdle_{true};
    std::atomic<bool> armed_{false};
    std::atomic<std::uint64_t> renderedSamples_{0};
    std::atomic<std::uint64_t> outputEpoch_{0};
    std::atomic<std::uint64_t> slotEpoch_{0};
    std::atomic<double> sampleRate_{kPlaceholderSampleRate};
    std::atomic<int> preparedOutputChannels_{0};
};

}  // namespace rta::platform
