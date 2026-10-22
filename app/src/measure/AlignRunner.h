// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
//
// ALIGN-UI lane, task T2 (docs/plans/2026-10-20-align-ui-lane-plan.md,
// decisions 2-4, 8, 11; wizard record docs/dsp/2026-09-06-l7-alignment-wizard.md).
// AlignmentWizard (AlignmentWizard.h) is the sequence around one pair of
// solo steps; this class is the operator-facing half around it, in the shape
// EqVerifyRunner gives EqVerify: WHEN a press is allowed, WHEN a published
// snapshot may be trusted as a measurement of the step that is playing, what
// happens when a run stalls, and what lands in the library. Everything the
// real app supplies (the device, the live snapshot, the clock) arrives
// through `AlignHost`, so the whole loop is provable against a standalone
// OutputEngine rendered by hand and scripted snapshots -- no hardware, no
// JUCE.
//
// The wizard sits behind a pimpl: AlignmentWizard.h includes rta/ir/*, and no
// message-thread header may (the SWEEP isolation rule).
//
// What this class does NOT decide: the four questions. The pane writes them
// into `answers()`, each starting "not asked", and `press()` refuses until all
// are answered (record Sec.2, Sec.6: none is derived from a measurement).
#pragma once

#include "measure/AlignVerdict.h"
#include "measure/Analyser.h"
#include "measure/CrossoverTopology.h"
#include "measure/Snapshot.h"
#include "measure/SweepIrOutcome.h"
#include "measure/MeasurementDrive.h"
#include "trace/Trace.h"
#include "trace/TraceLibrary.h"

#include "rta/platform/OutputEngine.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rta::measure {

/// Decision 2: the wizard's excitation, the level VERIFY and LOCATE use.
inline constexpr double kAlignLevelDbFs = -12.0;
/// How long a step may sit without a usable snapshot, beyond the dwell,
/// before the run disarms and says so (the VERIFY / CAL precedent).
inline constexpr double kAlignTimeoutMs = 5000.0;
/// Decision 8: every capture lands in this library group.
inline constexpr const char* kAlignGroup = "ALIGN";
/// Decision 5: the fit's own bar, shown as an unmeasured default.
inline constexpr double kAlignMatchedPairAgreement = 0.5;

/// What the operator has answered. Nothing is pre-selected except the two
/// output channels (decision 4: routing facts, not designer intent).
struct AlignAnswers {
    int mainOutput = 0;
    int subOutput = 1;
    std::optional<Source> highPass;
    std::optional<Topology> topology;
    std::optional<ProcessorInversion> inversion;
    /// "NO SEED" is an explicit answer, so asking is tracked apart from the value.
    bool seedAnswered = false;
    std::optional<double> seedHz;

    [[nodiscard]] bool complete() const noexcept {
        return highPass.has_value() && topology.has_value() && inversion.has_value() && seedAnswered;
    }
};

/// What the composition root knows and the runner does not.
struct AlignEnvironment {
    bool synthetic = false;      ///< SYNTHETIC input: the device is stopped, the loopback plays
    bool deviceRunning = false;  ///< a real device is open and calling back
    /// LOCATE / CAL / SWEEP / VERIFY own the output engine's source slot or
    /// the shared capture. Read at press time only: while ALIGN itself runs,
    /// the composition root's answer includes ALIGN, which is not a conflict.
    bool captureBusy = false;
    int outputChannels = 0;  ///< outputs the device (or the loopback) really has
};

struct AlignHost {
    std::function<AlignEnvironment()> environment;
    std::function<SnapshotPtr()> latest;
    std::function<CaptureConfig()> captureConfig;
    /// Freezes a published snapshot into a stored TRANSFER Trace (STORE's own
    /// conversion), or nullopt when there is nothing to freeze.
    std::function<std::optional<rta::trace::Trace>(const Snapshot&)> freeze;
    std::function<double()> nowMs;           ///< monotonic, milliseconds
    std::function<std::string()> timeLabel;  ///< "HH:MM:SS" for a trace's name
    /// H7 T13: the level every step plays at (its outputs stay the answers'
    /// main/sub, in both modes). Optional: absent, `kAlignLevelDbFs` and the
    /// pre-H7 wording.
    std::function<MeasurementDrive()> drive;
};

enum class AlignBlock {
    None,
    NotAttached,
    Running,
    MissingAnswer,
    SameOutput,
    OutputMissing,
    DeviceNotRunning,
    CaptureBusy,
    NoTransfer,
    SampleRateMismatch,
    ExponentialAveraging,
    EngineNotQuiescent,
};

/// Why a press would be refused, before it touches the engine. `text` is what
/// the operator reads; it names the reason and, where it helps, the way out.
struct AlignBlocker {
    AlignBlock kind = AlignBlock::None;
    std::string text;
};

enum class AlignOutcome {
    None,
    Measured,         ///< both captures taken, the fit ran
    SumMeasured,      ///< the SUM capture was taken
    TimedOut,
    DeviceStopped,
    OutputChanged,    ///< the output epoch or the input mode moved mid-run
    SettingsChanged,  ///< fftSize, sample rate or averaging mode moved mid-run
    FitRefused,
};

[[nodiscard]] std::string alignBlockText(AlignBlock kind);

/// The runner's state, defined in AlignRunner.cpp. A namespace-scope name rather
/// than a nested `AlignRunner::Impl`: it keeps rta/ir/* out of this header just
/// the same, and tools/orphan_check.py resolves its methods (a nested
/// `struct AlignRunner::Impl {` reads to that tool as members of AlignRunner).
struct AlignRunnerState;

class AlignRunner {
public:
    AlignRunner();
    ~AlignRunner();
    AlignRunner(const AlignRunner&) = delete;
    AlignRunner& operator=(const AlignRunner&) = delete;

    /// Wires the runner to an engine, a library and the host. All three are
    /// borrowed and must outlive the runner.
    void attach(rta::platform::OutputEngine& engine, rta::trace::TraceLibrary& library, AlignHost host);

    /// The pane's four questions and the two output channels. Writable at any
    /// time; a run in flight uses the copy it was started with.
    [[nodiscard]] AlignAnswers& answers() noexcept;
    [[nodiscard]] const AlignAnswers& answers() const noexcept;

    /// The answers the CURRENT run (or the last one) was started with. The
    /// pane reads the verdict against these, not against its combos: the
    /// operator may re-answer a question for the NEXT run while this verdict
    /// is still on screen.
    [[nodiscard]] const AlignAnswers& runAnswers() const noexcept;
    /// Outputs the device (or the SYNTHETIC loopback) really has; the pane
    /// lists that many.
    [[nodiscard]] int outputChannels() const;

    /// Why MEASURE cannot start right now (kind None: it can).
    [[nodiscard]] AlignBlocker blocker() const;
    /// " -- plays at -30.0 dBFS RMS" for the MEASURE / SUM hints: the level the
    /// NEXT press would use ("" with no drive). AlignRunnerText.cpp.
    [[nodiscard]] std::string driveHint() const;
    /// MEASURE. Builds a fresh wizard, replays the answers and arms. false,
    /// with the reason in `status()` and `lastRefusalForTest()`, when blocked.
    bool press();
    /// SUM: both outputs routed, then one capture after the dwell. false, with
    /// the reason in `status()`, unless a verdict exists and the cycle is answered.
    bool pressSum();
    /// The coarse offset the operator typed, in milliseconds. Valid once the
    /// fit has run and no IR witness gave the integer (wave A: always).
    bool answerCycleMs(double milliseconds);
    /// Advances the run. Called from MainComponent::timerCallback at 2 Hz.
    void poll();

    /// A step is in flight (MeasuringHigh, MeasuringLow or MeasuringSum).
    /// LOCATE, CAL, SWEEP and APPLY read this and refuse while it is true.
    [[nodiscard]] bool busy() const noexcept;

    [[nodiscard]] WizardStep step() const noexcept;
    [[nodiscard]] const std::string& status() const noexcept;
    [[nodiscard]] const std::optional<Verdict>& verdict() const noexcept;
    /// The integer the run used, once there is one: the operator's typed offset,
    /// or the difference of the bound IRs' arrivals.
    [[nodiscard]] std::optional<std::ptrdiff_t> cycleSamples() const noexcept;
    /// That integer came from the bound IRs (a typed offset still overrides it).
    [[nodiscard]] bool cycleFromIr() const noexcept;
    /// What the bound IRs imply (arrival difference minus the named topology's
    /// peak lag), shown as a hint when it fits no candidate clearly enough to use.
    [[nodiscard]] std::optional<std::ptrdiff_t> irHintSamples() const noexcept;
    /// The seed and the measured crossing disagree by more than a third of an
    /// octave: the IRs were not allowed to choose, both hints are given.
    [[nodiscard]] std::optional<SeedDisagreement> seedDisagreement() const noexcept;
    /// The output engine is quiescent (no excitation playing or ramping out):
    /// the same predicate SUM's refusal reads, so the pane can disable SUM
    /// instead of showing a button that refuses on press (H5 closeout D4).
    [[nodiscard]] bool engineQuiescent() const noexcept;
    /// Milliseconds the fit and polarity table took on the message thread in
    /// the last run's low-pass step (0 before the first run).
    [[nodiscard]] double lastComputeMs() const noexcept;

    // ---- wave B: the SWEEP impulse responses the operator bound by SOURCE --
    /// Binds `ir`, taken from the SWEEP trace `traceId`, to a source. A null
    /// `ir` unbinds. Applied at the NEXT press; a run in flight keeps its own.
    void bindIr(Source source, std::string traceId, std::shared_ptr<const SweepIr> ir);
    [[nodiscard]] const std::string& boundIrId(Source source) const noexcept;
    [[nodiscard]] std::shared_ptr<const SweepIr> boundIr(Source source) const noexcept;
    /// One line per polarity signal of the last run: its standing, its figure
    /// and its reason, or the refusal. Empty with no IR pair.
    [[nodiscard]] std::vector<std::string> polaritySignalLines() const;
    [[nodiscard]] double sampleRate() const noexcept;
    /// The frozen captures of this run, by SOURCE (nullptr until taken).
    [[nodiscard]] const rta::trace::Trace* mainCapture() const noexcept;
    [[nodiscard]] const rta::trace::Trace* subCapture() const noexcept;
    [[nodiscard]] const rta::trace::Trace* measuredSum() const noexcept;
    /// Moves on every state change the pane has to redraw for.
    [[nodiscard]] std::uint64_t revision() const noexcept;

    // Read-back for the tests only (each name is referenced whole-word from
    // app/tests*; orphan_check's TEST HOOK category).
    [[nodiscard]] AlignBlock lastRefusalForTest() const noexcept;
    [[nodiscard]] AlignOutcome outcomeForTest() const noexcept;
    [[nodiscard]] WizardRefusal wizardRefusalForTest() const noexcept;

private:
    std::unique_ptr<AlignRunnerState> impl_;
};

}  // namespace rta::measure
