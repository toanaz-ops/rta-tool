// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// L7-EQ task E (docs/plans/2026-09-07-L7-eq-impl-plan.md; decision record
// docs/dsp/2026-09-06-l7-auto-eq.md sec.2, sec.7). The operator-facing half
// of auto-EQ: the committed FilterSpec set, the *applied* marks, the session
// exclusion mask, and the two modes (Auto EQ one-shot, Suggest one greedy
// step) expressed over the core allocator. Device-free and GUI-free, so the
// whole model is provable in ctest (EQ-R4: the chip surface is a dev-preview
// specimen, NOT a MainComponent binding -- do not hunt for a hook).
//
// EQ-R3: "apply" here writes FilterSpecs and draws the ghost. It does NOT
// filter audio -- the virtual processor is G11 (L7-ALIGN, Wave 3) and
// consumes this same list.
#pragma once

#include "measure/EqTrustMask.h"

#include "rta/eq/EqAllocator.h"
#include "rta/eq/EqGainSolve.h"
#include "rta/eq/FilterSpec.h"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rta::measure {

/// Everything the core allocator reads that is a product choice rather than
/// measured data (record sec.12.2/12.3's labelled judgements). Mirrors the
/// EqInput fields one-for-one on purpose: the session adds no second opinion
/// about any of them, it only carries them.
struct EqSessionConfig {
    int maxFilters = 6;
    double gCapDb = 6.0;
    double qMaxBoost = 10.0;
    double qMaxCut = 20.0;
    std::optional<double> roomT60Sec{};
    float trustFloor = kEqTrustFloor;
};

/// A filter the session has landed, and whether the operator has dialled it
/// into the REAL downstream DSP. An applied filter's correction now lives in
/// the room, so it leaves the residual sum (record sec.2) -- counting it in
/// the prediction as well would count it twice.
struct CommittedFilter {
    rta::eq::FilterSpec spec{};
    bool applied = false;
};

/// The band a declined chip removes from play. Peaking: the cookbook's own
/// -3 dB edges for this (fc, Q), the closed form f = fc*(sqrt(1+1/(4Q^2)) -+
/// 1/(2Q)) whose difference is exactly fc/Q and whose geometric mean is
/// exactly fc. Shelves: everything on the shelf's own side of fc, because a
/// shelf has no bandwidth to read -- its Q sets slope, not extent.
struct FilterBand {
    double lowHz = 0.0;
    double highHz = 0.0;
};

[[nodiscard]] FilterBand filterBandHz(const rta::eq::FilterSpec& spec);

class EqSession {
public:
    explicit EqSession(EqSessionConfig config = {});

    /// All of hz/measuredDb/targetDb must share a length; `coherence` either
    /// matches it or is empty (below the gate -- every bin then untrusted,
    /// EqTrustMask.h). `hHalfGrid` is the snapshot's complex H if it is
    /// available on the SAME grid, and may be empty: EqInput's own documented
    /// fallback then places boosts without the G24 gate. Resets the committed
    /// set and the exclusion mask -- a new measurement is a new session.
    void setMeasurement(std::span<const float> hz, std::span<const float> measuredDb,
                        std::span<const float> targetDb, std::span<const float> coherence,
                        std::span<const std::complex<double>> hHalfGrid, double sampleRate);

    /// Suggest: ONE greedy step, top `maxCandidates` chips (record sec.2).
    /// Empty when nothing trusted and un-excluded is left to place on -- an
    /// ordinary operator state (a low-coherence capture, everything declined),
    /// not a programming error, so the allocator's refusal is turned into an
    /// empty ranking here rather than propagated as an exception.
    [[nodiscard]] std::vector<rta::eq::Candidate> suggest(int maxCandidates) const;

    /// Auto EQ: run the allocator to `maxFilters` and land the whole set.
    /// Filters already marked *applied* are KEPT (they are in the room; the
    /// allocator sees the residual they leave); every un-applied committed
    /// filter is discarded first, so a second Auto EQ replaces its own
    /// previous suggestion instead of stacking on top of it.
    void runAutoEq();

    /// Accept one chip. Its gainDb is the single-filter estimate the ranking
    /// was made on; the next `suggest` re-bases on the residual it leaves.
    void acceptCandidate(const rta::eq::Candidate& candidate);

    /// Decline one chip: its region joins the session exclusion mask the
    /// allocator already consumes, and the next ranking omits it (record
    /// sec.2). Nothing is committed.
    void declineCandidate(const rta::eq::Candidate& candidate);

    void markApplied(std::size_t index, bool applied);
    void clearFilters();

    [[nodiscard]] std::span<const CommittedFilter> committed() const noexcept {
        return committed_;
    }
    [[nodiscard]] std::span<const std::uint8_t> trusted() const noexcept { return trusted_; }
    [[nodiscard]] std::span<const std::uint8_t> excluded() const noexcept { return excluded_; }
    [[nodiscard]] std::span<const float> hz() const noexcept { return hz_; }
    [[nodiscard]] const EqSessionConfig& config() const noexcept { return config_; }

    /// ghost_k = m_k + sum_i responseDb(spec_i, fs, f_k) over EVERY committed
    /// filter, applied or not -- the predicted post-EQ trace (record sec.7,
    /// EQ-R3/R5: exact dB add through Wave 0's own responseDb, never a second
    /// response path).
    [[nodiscard]] std::vector<double> ghostDb() const;

    /// r_k = m_k - t_k + sum_i R_i(f_k) over committed filters NOT marked
    /// applied (record sec.2). This is what the allocator is handed as
    /// EqInput::residualDb -- pre-offset: the auto-offset c is EqAllocator's
    /// own job (EqAllocator.h rule 1), and so is the negation the gain solve
    /// needs (EqAllocator.cpp's `negated()`), so this function must NOT
    /// pre-negate or pre-offset anything.
    [[nodiscard]] std::vector<float> workingResidualDb() const;

private:
    [[nodiscard]] rta::eq::EqInput makeInput(std::span<const float> residual) const;

    EqSessionConfig config_;
    double sampleRate_ = 0.0;
    std::vector<float> hz_;
    std::vector<float> measuredDb_;
    std::vector<float> targetDb_;
    std::vector<float> coherence_;
    std::vector<std::complex<double>> hHalfGrid_;
    std::vector<std::uint8_t> trusted_;
    std::vector<std::uint8_t> excluded_;
    std::vector<CommittedFilter> committed_;
};

}  // namespace rta::measure
