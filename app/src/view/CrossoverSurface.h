// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest, like BodeLayout.h.
//
// L7-ALIGN task I, the G18 crossover surface (docs/plans/2026-09-15-L7-align-
// impl-plan.md; decision record docs/dsp/2026-09-06-l7-alignment-wizard.md
// Sec.6). All of the surface's logic lives here, JUCE-free and tested with
// RTA_BUILD_APP=OFF; the JUCE half is the dev-preview specimen
// app/src/dev/preview/PhaseAlignPreview.{h,cpp}, repointed at this model and
// checked by the offscreen snapshot (ALIGN-R8). There is no MainComponent
// hook and nobody should hunt for one.
//
// THERE IS NO OBJECTIVE HERE, AND THAT IS THE DESIGN. "Maximise the measured
// summation" is Smaart's verification sentence promoted to a search, and it is
// the forbidden move with the topology table deleted (record Sec.6): it
// re-derives a claim about what the DESIGNER intended; it walks a correctly
// aligned BW2 pair from +3 dB to +6 dB by undoing the inversion the network
// was built with; for any odd-order Butterworth the sum is 0 dB at every delay
// that keeps quadrature, so the objective is flat and its answer is noise; and
// it optimises one microphone position when the designed offset is what makes
// the sum hold across the room. "Minimise the sum's ripple" is the same move
// in different clothes. This model SHOWS the sum; the operator reads it
// against the line the topology question drew.
#pragma once

#include "measure/CrossoverTopology.h"
#include "trace/VirtualTrace.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace rta::view {

/// The band the relative-phase series is defined on -- record Sec.4's fit
/// window, the same centre and width the band fit used, so the trace on screen
/// and the number in the verdict describe one stretch of spectrum.
struct PhaseWindow {
    double centreHz = 0.0;
    double octavesEachSide = 1.0;
};

/// One point of arg(H_A conj H_B). A vector of these rather than a full-length
/// array with zeros outside the window: zero is a legal relative phase, so a
/// zero-filled outside would draw a flat line through the middle of the plot
/// and read as "in phase everywhere else"
/// (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
struct RelativePhasePoint {
    double hz = 0.0;
    double radians = 0.0;
};

/// Reference MARKS on the summed-magnitude pane. Never pass criteria -- the
/// record is explicit, and test_crossover_surface.cpp scans this header for a
/// declaration that has started behaving like one.
struct SurfaceMarks {
    /// 20log10(2). Two coherent sources at the same level, in phase.
    double coherentSumDb = 0.0;

    /// What the ASKED topology is designed to sum to at fc.
    double designedSumDb = 0.0;
};

/// Predicted versus measured, as a number. Not a re-derivation: a gap is
/// drift, a level change or a wrong setting, and it is never read as "the
/// topology was actually something else" (record Sec.2's last paragraph).
struct SumGap {
    std::vector<float> perBinDb;  ///< measured - predicted
    double rmsDb = 0.0;
    double maxAbsDb = 0.0;
    std::size_t bins = 0;
};

class CrossoverSurface {
public:
    /// `highSide` and `lowSide` in that order -- the same A = HIGH-PASS side
    /// contract rta::dsp::crossoverBandFit carries (ALIGN-R14), because the
    /// relative-phase series this draws IS arg(H_A) - arg(H_B) and the target
    /// line it is read against is record Sec.3's arg(H_HP) - arg(H_LP).
    ///
    /// Captures the PRE-ALIGNMENT sum as the ghost at the same moment, from
    /// the sources as handed in. That is the L6b reference-display pattern:
    /// the thing you are trying to improve on stays on screen.
    void setSources(rta::trace::VirtualTrace highSide, rta::trace::VirtualTrace lowSide);

    /// The operator's answers, handed in. Nothing here derives them, and there
    /// is no field in this class that could hold a topology this model worked
    /// out for itself.
    void setAskedTopology(rta::measure::Topology topology,
                          rta::measure::ProcessorInversion inversion);

    void setWindow(PhaseWindow window);

    /// The pending G11 chain on each side. Changing one recomputes the
    /// PREDICTED sum and leaves the ghost and the measured series untouched.
    void setPendingOps(std::vector<rta::trace::VirtualOp> highSideChain,
                       std::vector<rta::trace::VirtualOp> lowSideChain);

    /// The capture from record Sec.2's last step, both outputs routed.
    void setMeasuredSum(std::vector<float> magnitudeDb);

    // ---- the four traces (record Sec.6) -----------------------------------
    [[nodiscard]] const std::vector<float>& highSideDb() const noexcept { return highDb_; }
    [[nodiscard]] const std::vector<float>& lowSideDb() const noexcept { return lowDb_; }
    [[nodiscard]] const std::vector<float>& predictedSumDb() const noexcept {
        return predictedDb_;
    }
    [[nodiscard]] const std::vector<float>& ghostSumDb() const noexcept { return ghostDb_; }
    [[nodiscard]] const std::vector<float>& measuredSumDb() const noexcept { return measuredDb_; }

    /// arg(H_A conj H_B), inside the window only.
    [[nodiscard]] const std::vector<RelativePhasePoint>& relativePhase() const noexcept {
        return relativePhase_;
    }

    // ---- the one asked line ------------------------------------------------
    [[nodiscard]] double targetRadians() const noexcept { return target_.radians; }
    [[nodiscard]] bool targetAmbiguous() const noexcept { return target_.ambiguous; }
    [[nodiscard]] double alternativeTargetRadians() const noexcept {
        return target_.alternativeRadians;
    }

    [[nodiscard]] SurfaceMarks marks() const noexcept { return marks_; }
    [[nodiscard]] const std::optional<SumGap>& gap() const noexcept { return gap_; }
    [[nodiscard]] std::size_t pointCount() const noexcept { return highDb_.size(); }
    [[nodiscard]] double binWidthHz() const noexcept { return binWidthHz_; }

private:
    void recomputePrediction();
    void recomputeGap();

    std::optional<rta::trace::VirtualTrace> highSource_;
    std::optional<rta::trace::VirtualTrace> lowSource_;
    double binWidthHz_ = 0.0;

    std::vector<float> highDb_;
    std::vector<float> lowDb_;
    std::vector<float> predictedDb_;
    std::vector<float> ghostDb_;
    std::vector<float> measuredDb_;
    std::vector<RelativePhasePoint> relativePhase_;

    PhaseWindow window_{};
    rta::measure::Topology askedTopology_{};
    rta::measure::ProcessorInversion askedInversion_ = rta::measure::ProcessorInversion::Unknown;
    rta::measure::ExpectedOffset target_{};
    SurfaceMarks marks_{};
    std::optional<SumGap> gap_;
};

}  // namespace rta::view
