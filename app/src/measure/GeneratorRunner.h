// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
//
// H7 task T2 (docs/plans/2026-10-21-h7-generator-output-plan.md, design A).
// The operator generator as a client of the existing OutputEngine: Idle ->
// Playing -> (Swapping) -> Stopping -> Idle. Everything the real app knows
// and the runner does not (is a device running, how many outputs does it
// have, which measurement holds the output) arrives through `GeneratorHost`,
// so the whole machine is provable against a standalone OutputEngine rendered
// by hand -- no hardware, no JUCE.
//
// Four rules the state machine exists to keep:
//  1. A level or signal change is a SWAP (record sec.4: parameter changes are
//     swaps): disarm, wait for the master gate to fall, setSource, arm. The
//     hole is <= 2 ramps + one block + one poll (about 41 ms at 48 kHz).
//  2. The generator NEVER re-arms by itself. An epoch change (device
//     restart), a device stop, or an explicit cancel (input-mode change)
//     leaves it Idle with a reason; only the operator's GEN press starts it.
//  3. A stopped LIVE device never renders, so quiescence never arrives:
//     Stopping (and Swapping) give up after 500 ms instead of waiting.
//  4. It never takes the output from another client: start() refuses while
//     `busyBy` is non-empty, and refuses in LIVE with no device running.
#pragma once

#include "measure/GeneratorModel.h"
#include "rta/platform/OutputEngine.h"

#include <cstdint>
#include <functional>
#include <string>

namespace rta::measure {

enum class GeneratorState { Idle, Playing, Swapping, Stopping };

enum class GeneratorRefusal {
    None,
    AlreadyRunning,
    Busy,                ///< another client holds the output (named in the text)
    NoDevice,            ///< LIVE with no device running
    EngineNotQuiescent,  ///< setSource refused
    LastOutput,          ///< an apply() that would leave no output ticked
};

struct GeneratorEnvironment {
    bool synthetic = false;      ///< SYNTHETIC input: the engine is driven without a device
    bool deviceRunning = false;  ///< a real device is open and calling back
    int numOutputs = 0;          ///< outputs the current engine/device renders (0 = unknown)
    std::string busyBy;          ///< "" or the name of the client holding the output ("SWEEP")
};

struct GeneratorHost {
    std::function<GeneratorEnvironment()> environment;
    std::function<double()> nowMs;  ///< monotonic, milliseconds
};

inline constexpr double kGeneratorStopTimeoutMs = 500.0;

class GeneratorRunner {
public:
    /// `engine` is borrowed and must outlive the runner.
    GeneratorRunner(rta::platform::OutputEngine& engine, GeneratorHost host);
    /// A runner still playing owns the loudspeaker; destroying it must leave
    /// it silent. An idle runner never touches the engine (its slot may
    /// belong to LOCATE, VERIFY or SWEEP).
    ~GeneratorRunner();
    GeneratorRunner(const GeneratorRunner&) = delete;
    GeneratorRunner& operator=(const GeneratorRunner&) = delete;

    /// Idle -> Playing, or the refusal (also readable through `readout()`).
    GeneratorRefusal start();
    /// Playing/Swapping -> Stopping (disarm). No-op otherwise.
    void stop();
    /// Drops the generator at once, whatever it was doing, with a reason the
    /// readout shows: the input mode changed, say. Never waits for the engine.
    void cancel(const std::string& reason);

    /// Applies new settings (level is clamped, outputs pruned). While playing
    /// a signal/level/frequency change is a swap; an outputs change is live
    /// `routeOutput` with no swap. `LastOutput` and no change if
    /// `next.outputs` is empty.
    GeneratorRefusal apply(GeneratorSettings next);

    /// Advances Swapping/Stopping and notices an epoch change or device stop.
    void poll();

    [[nodiscard]] GeneratorState state() const noexcept { return state_; }
    /// The generator holds, or is still releasing, the output.
    [[nodiscard]] bool active() const noexcept { return state_ != GeneratorState::Idle; }
    /// A state that is waiting on the audio callback: poll quickly.
    [[nodiscard]] bool needsFastPoll() const noexcept {
        return state_ == GeneratorState::Swapping || state_ == GeneratorState::Stopping;
    }
    [[nodiscard]] const GeneratorSettings& settings() const noexcept { return settings_; }

    /// "GEN: playing pink -30.0 dBFS RMS on out 1", "GEN: stopped -- pink ...".
    [[nodiscard]] std::string readout() const;

private:
    void routeSelected() noexcept;
    void goIdle(const std::string& reason);
    [[nodiscard]] std::string signalPhrase() const;

    rta::platform::OutputEngine& engine_;
    GeneratorHost host_;
    GeneratorSettings settings_;
    GeneratorState state_ = GeneratorState::Idle;
    std::uint64_t armedEpoch_ = 0;
    double waitStartMs_ = 0.0;  ///< when Swapping/Stopping began
    std::string reason_;        ///< why the last run ended / why the last start refused
};

}  // namespace rta::measure
