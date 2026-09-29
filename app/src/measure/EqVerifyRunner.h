// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
//
// L7-EQ UI wave B, tasks T7 (VERIFY) and T8 (ADOPT)
// (docs/plans/2026-09-29-eq-ui-lane-plan.md, decision D14; EQ record
// docs/dsp/2026-09-06-l7-auto-eq.md sec.8). EqVerify (EqVerify.h) is the
// state machine around one excitation; this class is the operator-facing
// half around it: WHEN a press is allowed, WHEN a published snapshot may be
// trusted as a post-excitation measurement, what happens when the run stalls,
// and what ADOPT does with the result. Everything the real app supplies (the
// device, the live snapshot, the clock) arrives through `EqVerifyHost`, so
// the whole loop is provable against a standalone OutputEngine rendered by
// hand and scripted snapshots -- no hardware, no JUCE.
//
// D14: LIVE device only. Pink noise at -12 dBFS RMS on output 0, corridor
// 3 dB, 3 sigma. SYNTHETIC mode stops the device, so there is nothing to play
// the excitation through and VERIFY refuses there BY NAME.
#pragma once

#include "measure/Analyser.h"
#include "measure/EqPaneModel.h"
#include "measure/EqVerify.h"
#include "measure/Snapshot.h"
#include "trace/Trace.h"
#include "trace/TraceLibrary.h"

#include "rta/platform/OutputEngine.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rta::measure {

/// D14's fixed choices, named so a test states them rather than repeating a
/// literal the code could drift from.
inline constexpr int kEqVerifyOutputChannel = 0;
inline constexpr double kEqVerifyLevelDbFs = -12.0;
inline constexpr double kEqVerifyCorridorDb = 3.0;
inline constexpr double kEqVerifySigmaMultiple = 3.0;
/// How long a run may sit without progress before it disarms and says so.
/// MainComponentCalibration.cpp's `kCalibrationCaptureTimeoutMs` (5000) is the
/// precedent; `captureTimedOut` (CaptureTimeout.h) is the shared decision.
inline constexpr double kEqVerifyTimeoutMs = 5000.0;
/// The library group the after-measurement lands in (plan T7).
inline constexpr const char* kEqVerifyGroup = "EQ";

/// What the composition root knows and the runner does not.
struct EqVerifyEnvironment {
    bool synthetic = false;      ///< SYNTHETIC input: the device is stopped
    bool deviceRunning = false;  ///< a real device is open and calling back
    /// LOCATE (MainComponentDelay.cpp) or CAL (MainComponentCalibration.cpp)
    /// owns the output engine's source slot or the shared capture. LOCATE
    /// ignores `setSource`'s return and disarms at the end of its capture, so
    /// letting VERIFY run beside it would silently kill VERIFY's excitation
    /// (plan risk 3); CAL would measure a calibrator under VERIFY's noise.
    bool captureBusy = false;
};

struct EqVerifyHost {
    std::function<EqVerifyEnvironment()> environment;
    std::function<SnapshotPtr()> latest;
    std::function<CaptureConfig()> captureConfig;
    /// Freezes a published snapshot into a stored TRANSFER Trace (STORE's own
    /// conversion), or nullopt when there is nothing to freeze.
    std::function<std::optional<rta::trace::Trace>(const Snapshot&)> freeze;
    std::function<double()> nowMs;           ///< monotonic, milliseconds
    std::function<std::string()> timeLabel;  ///< "HH:MM:SS" for the trace's name
};

enum class VerifyBlock {
    None,
    NotAttached,
    NoMeasurement,
    NoUnappliedFilter,
    Running,
    Synthetic,
    DeviceNotRunning,
    CaptureBusy,
    NoTransfer,
    GridMismatch,
    ExponentialAveraging,
    EngineNotQuiescent,
};

/// Why a press would be refused, before it touches the engine. `text` is what
/// the operator reads; it names the reason and, where it helps, the way out.
struct VerifyBlocker {
    VerifyBlock kind = VerifyBlock::None;
    std::string text;
};

enum class VerifyOutcome { None, Done, TimedOut, DeviceStopped, InputsChanged, SettingsChanged, Adopted };

class EqVerifyRunner {
public:
    /// `model` is borrowed and must outlive the runner (MainComponentEq owns
    /// both, model first).
    explicit EqVerifyRunner(EqPaneModel& model);

    /// Wires the runner to an engine, a library and the host. All three are
    /// borrowed. Called by MainComponent (MainComponentEqVerify.cpp).
    void attach(rta::platform::OutputEngine& engine, rta::trace::TraceLibrary& library,
                EqVerifyHost host);
    [[nodiscard]] const EqVerifyHost& hostForTest() const noexcept { return host_; }

    /// Why VERIFY cannot start right now (kind None: it can).
    [[nodiscard]] VerifyBlocker blocker() const;
    /// Idle -> Waiting. false, with the reason in `model.status()` and
    /// `lastRefusalForTest()`, when blocked or when the engine refuses.
    bool press();
    /// Advances the run. Called from MainComponent::timerCallback at 2 Hz.
    void poll();

    /// A run is in flight (Waiting, Measuring or Settling). LOCATE and CAL
    /// read this and refuse while it is true.
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] VerifyState stateForTest() const noexcept;
    /// ADOPT is enabled at Done only.
    [[nodiscard]] bool canAdopt() const noexcept;
    bool adopt();

    // Read-back for the tests only (each name is referenced whole-word from
    // app/tests*; orphan_check's TEST HOOK category). Production reads the
    // outcome through `model.status()`, which is what the operator sees.
    [[nodiscard]] VerifyBlock lastRefusalForTest() const noexcept { return refusal_; }
    [[nodiscard]] VerifyOutcome outcomeForTest() const noexcept { return outcome_; }
    [[nodiscard]] const std::optional<VerifyReport>& reportForTest() const noexcept;
    [[nodiscard]] const std::string& afterTraceIdForTest() const noexcept { return afterTraceId_; }
    /// depth * fftSize / fs, in seconds: how long after Measuring the live
    /// transfer FIFO must have been fed the excitation alone (Analyser.h
    /// CaptureConfig::transferFifoDepth).
    [[nodiscard]] static double dwellSeconds(const CaptureConfig& config, std::size_t fftSize,
                                             double sampleRate);

private:
    void abort(VerifyOutcome why, std::string text);
    void trySubmit(double now);
    [[nodiscard]] bool inputsUnchanged() const;
    [[nodiscard]] std::size_t boundFftSize() const;

    EqPaneModel& model_;
    rta::platform::OutputEngine* engine_ = nullptr;
    rta::trace::TraceLibrary* library_ = nullptr;
    EqVerifyHost host_;

    /// A FRESH EqVerify per press: `arm` refuses unless Idle (EqVerify.cpp), so
    /// a reused one would refuse every second press.
    std::unique_ptr<EqVerify> verify_;
    VerifyBlock refusal_ = VerifyBlock::None;
    VerifyOutcome outcome_ = VerifyOutcome::None;
    double armedAtMs_ = 0.0;
    double measuringAtMs_ = 0.0;
    std::uint64_t measuringRendered_ = 0;
    /// What the run was started against; ADOPT and the submit both refuse if
    /// the operator changed either while it ran.
    std::string pressedMeasurementId_;
    std::vector<CommittedFilter> pressedFilters_;
    std::string afterTraceId_;
};

/// The operator-facing sentence for each block kind (the fixed part; the
/// runner appends numbers where a reason has them).
[[nodiscard]] std::string verifyBlockText(VerifyBlock kind);

}  // namespace rta::measure
