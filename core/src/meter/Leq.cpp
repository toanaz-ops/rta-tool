// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/meter/Leq.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rta::meter {

double percentileLevelDb(std::span<const double> levelsDb, double n) {
    if (levelsDb.empty()) {
        throw std::invalid_argument("percentileLevelDb: levelsDb must not be empty");
    }
    if (!(n >= 0.0) || !(n <= 100.0)) {
        throw std::invalid_argument("percentileLevelDb: n must be in [0,100]");
    }

    // Sorts a local copy -- the result must not depend on caller order (the
    // "shuffle-invariance" property is the whole reason this sorts here
    // rather than assuming pre-sorted input).
    std::vector<double> sorted(levelsDb.begin(), levelsDb.end());
    std::sort(sorted.begin(), sorted.end());

    const auto count = sorted.size();
    // Ln is "exceeded n% of the time", so L10 is a HIGH number: pos grows as
    // n shrinks, per the project convention (docs/dsp/2026-08-27-weighting-
    // and-meters.md) -- numpy's default 'linear' quantile at q = 1 - n/100.
    const double pos = static_cast<double>(count - 1) * (1.0 - n / 100.0);
    const auto lo = static_cast<std::size_t>(std::floor(pos));
    const double frac = pos - static_cast<double>(lo);
    const std::size_t hi = std::min(lo + 1, count - 1);
    return sorted[lo] + frac * (sorted[hi] - sorted[lo]);
}

Leq::Leq(double sampleRate, double referenceOffsetDb, TimeWeighting timeWeighting)
    : sampleRate_(sampleRate),
      referenceOffsetDb_(referenceOffsetDb),
      historyIntervalSamples_(static_cast<std::size_t>(std::lround(0.1 * sampleRate))),
      detector_(timeWeighting, sampleRate) {}

void Leq::reset() noexcept {
    sumSquares_ = 0.0;
    maxSquare_ = 0.0;
    count_ = 0;
    detector_.reset();
    history_.clear();
}

void Leq::process(std::span<const float> in) noexcept {
    for (float x : in) {
        const double xd = static_cast<double>(x);
        const double sq = xd * xd;
        sumSquares_ += sq;
        maxSquare_ = std::max(maxSquare_, sq);
        ++count_;

        // Ln/Lmax/Lmin all read from THIS series -- the internal Detector's
        // time-weighted level, sampled every historyIntervalSamples() -- and
        // never from raw samples (plan 11.4). Using count_ (post-increment)
        // means the FIRST history sample lands after a full interval has
        // elapsed, not at sample 0.
        detector_.processSample(x);
        if (count_ % historyIntervalSamples_ == 0) {
            history_.push_back(detector_.levelDb(referenceOffsetDb_));
        }
    }
}

double Leq::leqDb() const noexcept {
    if (count_ == 0) return kLevelFloorDb;  // "no data" != "measured silence"
    const double meanSq = sumSquares_ / static_cast<double>(count_);
    if (meanSq <= 0.0) return kLevelFloorDb + referenceOffsetDb_;
    const double db = 10.0 * std::log10(meanSq);
    return (db < kLevelFloorDb) ? (kLevelFloorDb + referenceOffsetDb_) : (db + referenceOffsetDb_);
}

double Leq::selDb() const noexcept {
    if (count_ == 0) return kLevelFloorDb;
    // T0 = 1s reference is what makes SEL a number, not a rate: for T < 1s,
    // log10(T/1s) is negative, so SEL reads BELOW leqDb (easy to get the sign
    // backwards -- see plan 6.2 / the L3 test).
    return leqDb() + 10.0 * std::log10(static_cast<double>(count_) / sampleRate_);
}

double Leq::peakDb() const noexcept {
    if (maxSquare_ <= 0.0) return kLevelFloorDb + referenceOffsetDb_;
    // The SQUARED form -- 10*log10(max(p^2)), equal to 20*log10(max|p|) but
    // NOT 10*log10(max|p|) (half the dB, invisible at unity amplitude; see
    // plan 11.2). maxSquare_ already tracks max(x*x), so no sqrt anywhere
    // here -- that also sidesteps ever computing max(x) and getting the wrong
    // sample for an asymmetric/negative-peaked waveform.
    return 10.0 * std::log10(maxSquare_) + referenceOffsetDb_;
}

double Leq::maxDb() const noexcept {
    if (history_.empty()) return kLevelFloorDb;
    return *std::max_element(history_.begin(), history_.end());
}

double Leq::minDb() const noexcept {
    if (history_.empty()) return kLevelFloorDb;
    return *std::min_element(history_.begin(), history_.end());
}

double Leq::percentileDb(double n) const {
    if (history_.empty()) throw std::invalid_argument("Leq::percentileDb: empty history");
    std::vector<double> sorted(history_.begin(), history_.end());
    std::sort(sorted.begin(), sorted.end());
    return percentileLevelDb(sorted, n);
}

double Leq::elapsedSeconds() const noexcept {
    return static_cast<double>(count_) / sampleRate_;
}

std::size_t Leq::sampleCount() const noexcept { return count_; }

double Leq::sumSquares() const noexcept { return sumSquares_; }

std::span<const double> Leq::levelHistory() const noexcept { return history_; }

std::size_t Leq::historyIntervalSamples() const noexcept { return historyIntervalSamples_; }

}  // namespace rta::meter
