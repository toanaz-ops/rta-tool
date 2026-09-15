// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/EqVerify.h"

#include "rta/gen/Noise.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace rta::measure {

namespace {

/// 20/ln(10): the factor that turns a relative standard deviation in nepers
/// into one in dB. Written as an expression, not a decimal literal, so the
/// identity stays readable against Bendat & Piersol's own form.
const double kDbPerNeper = 20.0 / std::log(10.0);

/// Output-path record sec.11, the same settle DelayLocator.cpp waits out:
/// the first 10 ms of an excitation are not stationary. 480 samples at
/// 48 kHz, scaled to whatever rate the session runs at.
constexpr double kSettleSamplesAt48k = 480.0;
constexpr double kSettleReferenceRate = 48000.0;

}  // namespace

double h1SigmaDb(double coherence, double effectiveAverages) {
    if (coherence >= 1.0) return 0.0;  // a perfectly coherent estimate has no spread
    if (coherence <= 0.0 || effectiveAverages <= 0.0) {
        // No usable estimate. sigma gates FLAGGING, so "unknown" must read as
        // infinitely uncertain -- the bin then cannot be flagged, which is
        // the conservative direction. A NaN here would compare false against
        // the threshold and flag everything instead.
        return std::numeric_limits<double>::infinity();
    }
    return kDbPerNeper
           * std::sqrt((1.0 - coherence) / (2.0 * effectiveAverages * coherence));
}

VerifyReport compareToPrediction(std::span<const float> measuredAfterDb,
                                 std::span<const double> predictedDb,
                                 std::span<const float> targetDb,
                                 std::span<const float> measuredBeforeDb,
                                 std::span<const float> coherence,
                                 std::span<const std::uint8_t> trusted,
                                 double effectiveAverages, VerifyTolerance tolerance) {
    const std::size_t m = measuredAfterDb.size();
    if (predictedDb.size() != m || targetDb.size() != m || measuredBeforeDb.size() != m
        || coherence.size() != m || trusted.size() != m) {
        throw std::invalid_argument("compareToPrediction: span length mismatch");
    }

    VerifyReport report;
    report.bins.resize(m);
    double sumBefore = 0.0;
    double sumAfter = 0.0;
    std::size_t trustedBins = 0;

    for (std::size_t k = 0; k < m; ++k) {
        VerifyBin& bin = report.bins[k];
        bin.deltaDb = static_cast<double>(measuredAfterDb[k]) - predictedDb[k];
        bin.sigmaDb = h1SigmaDb(static_cast<double>(coherence[k]), effectiveAverages);

        // BOTH terms, record sec.8: the corridor is what the operator cares
        // about, the 3-sigma is what makes it not noise. Either one alone
        // would either cry wolf on a noisy bin or stay silent on a real
        // error the operator had already declared out of tolerance.
        const bool pastCorridor = std::abs(bin.deltaDb) > tolerance.corridorHalfWidthDb;
        const bool pastNoise = std::abs(bin.deltaDb) > tolerance.sigmaMultiple * bin.sigmaDb;
        bin.flagged = (trusted[k] != 0) && pastCorridor && pastNoise;
        if (bin.flagged) ++report.flaggedCount;

        if (trusted[k] == 0) continue;
        const double before = static_cast<double>(measuredBeforeDb[k])
                              - static_cast<double>(targetDb[k]);
        const double after = static_cast<double>(measuredAfterDb[k])
                             - static_cast<double>(targetDb[k]);
        sumBefore += before * before;
        sumAfter += after * after;
        ++trustedBins;
    }

    report.trustedBins = trustedBins;
    if (trustedBins > 0) {
        const double n = static_cast<double>(trustedBins);
        report.residualRmsBeforeDb = std::sqrt(sumBefore / n);
        report.residualRmsAfterDb = std::sqrt(sumAfter / n);
    }
    // No else. With no trusted bin there is no residual to report, and the
    // fields stay nullopt rather than falling back to 0.0 -- see the header.
    return report;
}

std::string renderVerifySummary(const VerifyReport& report) {
    char line[160];
    // TWO absences, and they are not the same sentence. Sharing one branch
    // would print "no trusted bins" over a report with four of them -- an
    // absent result wearing another absent result's clothes, the same failure
    // the optional RMS fields exist to prevent one layer down.
    if (report.trustedBins == 0) {
        std::snprintf(line, sizeof line,
                      "VERIFY inconclusive: no trusted bins (%zu bins measured, all under the "
                      "coherence floor) -- no residual to report",
                      report.bins.size());
        return line;
    }
    // Evidence exists but the residual does not. compareToPrediction never
    // produces this, and nothing asserts that it cannot, so the summary says
    // what is true rather than dereferencing an empty optional.
    if (!report.residualRmsBeforeDb.has_value() || !report.residualRmsAfterDb.has_value()) {
        std::snprintf(line, sizeof line,
                      "VERIFY inconclusive: %zu of %zu bins trusted, residual not computed",
                      report.trustedBins, report.bins.size());
        return line;
    }
    std::snprintf(line, sizeof line,
                  "VERIFY: %zu of %zu bins trusted, %zu flagged; residual %.1f dB -> %.1f dB",
                  report.trustedBins, report.bins.size(), report.flaggedCount,
                  *report.residualRmsBeforeDb, *report.residualRmsAfterDb);
    return line;
}

EqVerify::EqVerify(rta::platform::OutputEngine& engine, Config config)
    : engine_(engine), config_(config) {}

void EqVerify::arm() {
    // Both early returns below name their own reason. Returning before the
    // refusal is set would leave lastRefusal() reading the PREVIOUS attempt's
    // reason -- an absent result wearing an old result's clothes, which is the
    // same failure shape as a 0.0 dB residual standing in for no residual.
    if (state_ != VerifyState::Idle) {
        refusal_ = VerifyRefusal::AlreadyRunning;
        return;
    }
    refusal_ = VerifyRefusal::None;
    // Output-path record sec.6, in order. setSource FIRST, and its bool is
    // the refusal: it returns false unless the engine is quiescent, and
    // ignoring that would leave somebody else's running excitation in the
    // slot, then solo it and arm it -- a verify reported over the wrong
    // signal at the wrong level, silently. Nothing after this line runs on a
    // refusal, so the routing the other owner set is left exactly as it was.
    if (!engine_.setSource(rta::platform::SourceVariant(rta::gen::PinkNoise(
                rta::gen::Pcg32(config_.noiseSeed, 1), config_.excitationDbFsRms)))) {
        refusal_ = VerifyRefusal::EngineNotQuiescent;
        return;
    }
    soloOutput(engine_, config_.outputChannel);
    engine_.armSource();
    state_ = VerifyState::Waiting;
}

void EqVerify::poll() {
    if (state_ == VerifyState::Waiting) {
        const double settle =
                kSettleSamplesAt48k * (config_.sampleRate / kSettleReferenceRate);
        if (static_cast<double>(engine_.renderedSamples()) >= settle) {
            state_ = VerifyState::Measuring;
        }
        return;
    }
    if (state_ == VerifyState::Settling && engine_.sourceIsQuiescent()) {
        state_ = VerifyState::Done;
    }
}

void EqVerify::submitSnapshot(std::span<const float> measuredAfterDb,
                              std::span<const double> predictedDb, std::span<const float> targetDb,
                              std::span<const float> measuredBeforeDb,
                              std::span<const float> coherence,
                              std::span<const std::uint8_t> trusted, double effectiveAverages) {
    if (state_ != VerifyState::Measuring) return;
    report_ = compareToPrediction(measuredAfterDb, predictedDb, targetDb, measuredBeforeDb,
                                  coherence, trusted, effectiveAverages, config_.tolerance);
    // disarmSource() the instant the snapshot is in hand (output-path record
    // sec.8): nothing past this point should keep the loudspeaker playing.
    engine_.disarmSource();
    state_ = VerifyState::Settling;
}

}  // namespace rta::measure
