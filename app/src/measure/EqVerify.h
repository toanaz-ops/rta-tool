// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// L7-EQ task F (docs/plans/2026-09-07-L7-eq-impl-plan.md; decision record
// docs/dsp/2026-09-06-l7-auto-eq.md sec.8). VERIFY mode: after the operator
// applies filters, re-measure over the output path and flag honest
// disagreement. The excitation follows docs/dsp/2026-09-06-l7-output-path.md
// sec.6 verbatim -- setSource -> routeOutput -> armSource -> wait -> snapshot
// -> disarmSource -> wait sourceIsQuiescent(). Nothing here touches a gate or
// the slot by any other route (that record's sec.9), and nothing here
// re-solves: VERIFY reports, it does not correct.
#pragma once

#include "measure/OutputPolicy.h"

#include "rta/platform/OutputEngine.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rta::measure {

/// Bendat & Piersol's H1 coherent-output variance carried into dB:
/// sigma ~ (20/ln10) * sqrt((1 - gamma^2) / (2 * n_d * gamma^2)).
///
/// The equation NUMBER is unverified -- L6b sec.1 carried the same form with
/// the same caveat, and this file inherits it rather than inventing a second,
/// differently-unverified citation. What IS checked here is the shape: zero
/// at gamma^2 == 1, 1/sqrt(n_d) in the averages, monotone decreasing in
/// coherence.
///
/// A non-positive `effectiveAverages`, or a non-positive coherence, has no
/// standard deviation to report and returns +inf, NOT NaN: the comparison
/// below uses sigma as a REFUSAL term, so an unusable estimate must make the
/// bin harder to flag, never silently easier (a NaN compares false against
/// everything and would disable the term outright).
[[nodiscard]] double h1SigmaDb(double coherence, double effectiveAverages);

/// The two terms of record sec.8's honesty rule. The corridor is the
/// tolerance the operator already set; `sigmaMultiple` is the 3 in "3 sigma".
struct VerifyTolerance {
    double corridorHalfWidthDb = 3.0;
    double sigmaMultiple = 3.0;
};

struct VerifyBin {
    double deltaDb = 0.0;   ///< measured - predicted
    double sigmaDb = 0.0;   ///< the estimate's own standard deviation
    bool flagged = false;   ///< past the corridor AND past sigmaMultiple*sigma
};

struct VerifyReport {
    std::vector<VerifyBin> bins;
    std::size_t flaggedCount = 0;

    /// How many bins carried evidence at all. ZERO is a real, reachable
    /// outcome -- a capture where every bin sits under the coherence floor is
    /// precisely the case VERIFY exists to survive -- and it is the reason
    /// the two RMS fields below are optional rather than defaulted.
    std::size_t trustedBins = 0;

    /// RMS of (measured - target) over TRUSTED bins, before the filters were
    /// applied and after -- the one number that says whether the pass helped.
    ///
    /// `nullopt` when `trustedBins == 0`. A defaulted 0.0 would render an
    /// evidence-free verify bit-identical to a flawless one (0 flags, 0 dB
    /// residual, twice), which is exactly the failure
    /// memory/a-placeholder-for-an-absent-result-erases-its-state.md names:
    /// a placeholder for an absent result erases the fact that it is absent.
    std::optional<double> residualRmsBeforeDb;
    std::optional<double> residualRmsAfterDb;
};

/// One line a human reads. Says "no trusted bins" when there were none, so
/// the absent state survives the trip to the screen or the log as well as it
/// survives inside the struct.
[[nodiscard]] std::string renderVerifySummary(const VerifyReport& report);

/// Pure comparison, no engine: `predictedDb` is the session ghost
/// (EqSession::ghostDb), `measuredBeforeDb` the magnitude the filters were
/// designed against. An untrusted bin is reported but never flagged -- the
/// coherence gate already refused it as evidence (EqTrustMask.h), and
/// flagging on refused evidence is the same error twice.
[[nodiscard]] VerifyReport
compareToPrediction(std::span<const float> measuredAfterDb, std::span<const double> predictedDb,
                    std::span<const float> targetDb, std::span<const float> measuredBeforeDb,
                    std::span<const float> coherence, std::span<const std::uint8_t> trusted,
                    double effectiveAverages, VerifyTolerance tolerance);

enum class VerifyState { Idle, Waiting, Measuring, Settling, Done };

/// One reason, on purpose, mirroring DelayLocator's named-refusal shape:
/// `OutputEngine::setSource` returns false unless the engine is quiescent, so
/// arming over somebody else's running excitation would leave THEIR source in
/// the slot, solo it, and report a verify over the wrong signal at the wrong
/// level without ever saying so.
enum class VerifyRefusal { None, EngineNotQuiescent };

/// The state machine around that comparison, driving a REAL OutputEngine --
/// JUCE-free like DelayLocator, so it is provable with no audio hardware.
class EqVerify {
public:
    struct Config {
        int outputChannel = 0;
        double sampleRate = 48000.0;
        VerifyTolerance tolerance{};
        /// The excitation is pink noise by record sec.8; its level and seed
        /// are the caller's, so a verify pass is reproducible bit-for-bit
        /// against the same room -- the same reason DelayLocator takes its
        /// source from the caller rather than constructing one.
        double excitationDbFsRms = -12.0;
        std::uint64_t noiseSeed = 1;
    };

    EqVerify(rta::platform::OutputEngine& engine, Config config);

    /// Idle -> Waiting: setSource(pink noise), STRICT solo on the configured
    /// output (a verify is a measurement action, so it takes the same owner
    /// ruling a sequence does -- L7-OUT record sec.13.2), armSource.
    ///
    /// REFUSES, changing nothing at all -- not the routing, not the slot --
    /// when setSource says the engine is not quiescent; `lastRefusal()` then
    /// reads EngineNotQuiescent and the state stays Idle.
    void arm();

    /// Waiting -> Measuring once the excitation has rendered past the 10 ms
    /// that are not stationary (output-path record sec.11, the same settle
    /// DelayLocator waits out). Settling -> Done once the engine reports
    /// sourceIsQuiescent().
    void poll();

    /// Measuring -> Settling: builds the report from the re-measured
    /// snapshot, then disarmSource() -- the excitation stops the instant its
    /// job is done, exactly as the Locate sequence does.
    void submitSnapshot(std::span<const float> measuredAfterDb,
                        std::span<const double> predictedDb, std::span<const float> targetDb,
                        std::span<const float> measuredBeforeDb, std::span<const float> coherence,
                        std::span<const std::uint8_t> trusted, double effectiveAverages);

    [[nodiscard]] VerifyState state() const noexcept { return state_; }
    [[nodiscard]] VerifyRefusal lastRefusal() const noexcept { return refusal_; }
    [[nodiscard]] const std::optional<VerifyReport>& report() const noexcept { return report_; }

private:
    rta::platform::OutputEngine& engine_;
    Config config_;
    VerifyState state_ = VerifyState::Idle;
    VerifyRefusal refusal_ = VerifyRefusal::None;
    std::optional<VerifyReport> report_;
};

}  // namespace rta::measure
