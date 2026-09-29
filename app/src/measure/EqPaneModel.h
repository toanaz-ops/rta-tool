// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
//
// L7-EQ UI task T2 (docs/plans/2026-09-29-eq-ui-lane-plan.md). Everything the
// EQ pane knows that is not a widget: which stored traces it may offer, how a
// stored Trace becomes an EqSession measurement, what the operator has picked,
// and the readout text. Owned by MainComponentEq, NOT by the pane, so the
// committed list survives the pane being rebuilt on every selector click
// (MainComponentPanes.cpp rebuilds workspace_ each time).
//
// Trace -> session, in one place (Trace.h):
//   hz_k       = k * binHz()               (bin 0 is DC)
//   measuredDb = the stored magnitude
//   coherence  = the stored coherence (a trace without one is never offered as
//                a MEASUREMENT: every bin would be untrusted, EqTrustMask.h)
//   H_k        = 10^(m_k/20) * exp(i*phi_k), phi_k in RADIANS as stored
//                (CaptureConverter's degToRadPhase) -- NOT degrees. No phase
//                -> empty H, and the allocator places boosts without the G24
//                gate (EqInput's documented fallback), which the readout says.
#pragma once

#include "export/FirExport.h"
#include "measure/EqSession.h"
#include "rta/dsp/FirDesign.h"
#include "trace/TraceLibrary.h"

#include <array>
#include <complex>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rta::measure {

/// The FIR lengths the picker offers (D9): the "below ~8k taps" convolver
/// regime of FIR record Sec.2. 65536 is excluded (its minimum-phase design is
/// the 16.8M-point transform Sec.11 flags, on the message thread).
///
/// ODD, one below the plan's 1024 / 4096 / 6144 / 8192: FirDesign realises an
/// even-length filter with twice the gain (EqFirDesign.h), so the even
/// neighbours are not offered. Each pair shares its design grid M (8N rounds
/// to the same power of two), so the resolution and latency read the same.
inline constexpr std::array<std::size_t, 4> kEqFirTapChoices{ 1023, 4095, 6143, 8191 };

/// "4096 taps  12 Hz  42.7 ms": fs/N and (N-1)/(2 fs), the two numbers FIR
/// record Sec.9 prints. Frequency in whole hertz (project convention).
[[nodiscard]] std::string eqFirTapsLabel(std::size_t taps, double sampleRate);

/// A library trace the operator picked. `name` and `removed` are the model's
/// own record: once picked the data is copied, so a hidden or deleted trace
/// changes the label ("(removed)"), never the numbers (plan risk 5).
struct EqTracePick {
    std::string id;
    std::string name;
    bool removed = false;
};

class EqPaneModel {
public:
    EqPaneModel();

    /// Visible traces that carry coherence (measurement list) / every visible
    /// trace (target list), in library order.
    [[nodiscard]] static std::vector<std::string> measurementCandidateIds(
        const rta::trace::TraceLibrary& library);
    [[nodiscard]] static std::vector<std::string> targetCandidateIds(
        const rta::trace::TraceLibrary& library);

    /// false (and status() says why) when `id` is not an eligible trace.
    bool selectMeasurement(const rta::trace::TraceLibrary& library, const std::string& id);
    /// Empty id = FLAT (D3). A stored trace is resampled onto the measurement's
    /// grid: copied bitwise when the grids are equal, else log-frequency /
    /// linear-dB interpolation (FirDesign.h's rule), clamped at the ends.
    bool selectTarget(const rta::trace::TraceLibrary& library, const std::string& id);
    /// Re-reads names and marks a picked trace that left the library.
    void syncLibrary(const rta::trace::TraceLibrary& library);

    void setMaxFilters(int maxFilters);
    void runAutoEq();
    void suggest();
    void acceptChip(std::size_t index);
    void declineChip(std::size_t index);
    void setApplied(std::size_t index, bool applied);
    void clearFilters();
    void clearDeclined();

    void setFirPhase(std::optional<rta::dsp::FirPhase> phase);
    void setFirTaps(std::optional<std::size_t> taps);
    void setPeakNormalised(bool peak);
    void setStatus(std::string text);

    [[nodiscard]] const EqSession& session() const noexcept { return session_; }
    [[nodiscard]] bool hasMeasurement() const noexcept { return sampleRate_ > 0.0; }
    [[nodiscard]] const EqTracePick& measurement() const noexcept { return measurement_; }
    [[nodiscard]] const EqTracePick& target() const noexcept { return target_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] std::span<const float> measuredDb() const noexcept { return measuredDb_; }
    [[nodiscard]] std::span<const float> targetDb() const noexcept { return targetDb_; }
    [[nodiscard]] std::span<const std::complex<double>> hHalfGrid() const noexcept { return h_; }
    [[nodiscard]] const std::vector<rta::eq::Candidate>& chips() const noexcept { return chips_; }
    [[nodiscard]] const std::string& status() const noexcept { return status_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    /// target_k + c: where the allocator actually aims (T0).
    [[nodiscard]] std::vector<double> targetLineDb() const;
    /// ghost_k - measured_k = the sum of the un-applied filters' responses.
    [[nodiscard]] std::vector<double> correctionDb() const;
    [[nodiscard]] std::string gateReadout() const;   ///< "G24 gate on" / "G24 gate off ..."
    [[nodiscard]] std::string trustReadout() const;  ///< "x of y bins trusted"
    /// The fixed limits, shown not asked (D5): "boost <= +6.0 dB, Q <= 10 / 20".
    [[nodiscard]] std::string limitsReadout() const;

    [[nodiscard]] std::size_t unappliedCount() const noexcept;
    [[nodiscard]] std::optional<rta::dsp::FirPhase> firPhase() const noexcept { return firPhase_; }
    [[nodiscard]] std::optional<std::size_t> firTaps() const noexcept { return firTaps_; }
    [[nodiscard]] rta::firexport::Normalization normalization() const noexcept;
    /// D8: enabled only once BOTH phase and length are answered, and there is
    /// something to realise.
    [[nodiscard]] bool canExportFir() const noexcept;
    [[nodiscard]] bool canExportList() const noexcept { return !session_.committed().empty(); }

private:
    void loadIntoSession();
    void bump() noexcept { ++revision_; }

    EqSession session_;
    EqTracePick measurement_;
    EqTracePick target_;
    double sampleRate_ = 0.0;
    std::vector<float> hz_;
    std::vector<float> measuredDb_;
    std::vector<float> coherence_;
    std::vector<std::complex<double>> h_;
    std::vector<float> targetSourceHz_;  ///< the picked target trace's own grid; empty = FLAT
    std::vector<float> targetSourceDb_;
    std::vector<float> targetDb_;        ///< the target on the measurement grid, before +c
    std::vector<rta::eq::Candidate> chips_;
    std::string status_;
    std::optional<rta::dsp::FirPhase> firPhase_;
    std::optional<std::size_t> firTaps_;
    bool peakNormalised_ = false;
    std::uint64_t revision_ = 0;
};

}  // namespace rta::measure
