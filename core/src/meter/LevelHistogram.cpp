// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#include "rta/meter/LevelHistogram.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace rta::meter {

namespace {

/// Saturating, because a silent wrap would make a bin read LOW after 13.6
/// years of continuous logging and nothing downstream could tell. The
/// saturation point is unreachable inside a session (W1-A1 asserts the
/// arithmetic); this exists so the unreachable case is wrong in the safe
/// direction rather than catastrophically.
void addSaturating(std::uint32_t& counter, std::uint64_t amount) noexcept {
    constexpr std::uint64_t kMax = std::numeric_limits<std::uint32_t>::max();
    const std::uint64_t sum = static_cast<std::uint64_t>(counter) + amount;
    counter = static_cast<std::uint32_t>(sum > kMax ? kMax : sum);
}

}  // namespace

void LevelHistogram::reset() noexcept {
    counts_.fill(0u);
    belowSpan_ = 0;
    aboveSpan_ = 0;
    total_ = 0;
}

void LevelHistogram::add(double levelDb) noexcept {
    ++total_;

    // +inf is the only non-finite value that means "over the top"; NaN and
    // -inf both land below. Either way the answer becomes an ABSENCE with a
    // reason, which is the honest outcome for a level that is not a number.
    if (!std::isfinite(levelDb)) {
        if (levelDb > 0.0) {
            addSaturating(aboveSpan_, 1);
        } else {
            addSaturating(belowSpan_, 1);
        }
        return;
    }

    // floor, not a truncating cast: a level below the base must go to
    // belowSpan, and a cast towards zero would fold [-0.1, 0) dB of it into
    // bin 0.
    const double pos = std::floor((levelDb - baseDb_) / kBinWidthDb);
    if (pos < 0.0) {
        addSaturating(belowSpan_, 1);
        return;
    }
    if (pos >= static_cast<double>(kBinCount)) {
        addSaturating(aboveSpan_, 1);
        return;
    }
    addSaturating(counts_[static_cast<std::size_t>(pos)], 1);
}

LevelHistogram::Pick LevelHistogram::pick(std::uint64_t k) const noexcept {
    if (k < static_cast<std::uint64_t>(belowSpan_)) {
        return {false, LnAbsence::BelowSpan, 0.0};
    }
    std::uint64_t remaining = k - static_cast<std::uint64_t>(belowSpan_);
    for (std::size_t bin = 0; bin < kBinCount; ++bin) {
        const auto c = static_cast<std::uint64_t>(counts_[bin]);
        if (remaining < c) return {true, LnAbsence::None, binCentreDb(bin)};
        remaining -= c;
    }
    return {false, LnAbsence::AboveSpan, 0.0};
}

LnResult LevelHistogram::percentile(double n) const {
    if (!(n >= 0.0) || !(n <= 100.0)) {
        throw std::invalid_argument("LevelHistogram::percentile: n must be in [0,100]");
    }
    if (total_ == 0) return {std::nullopt, LnAbsence::NoData};

    // The project's ONE convention (percentileLevelDb, Leq.cpp:26-32): Ln is
    // "the level exceeded n% of the time", so pos grows as n shrinks and L10
    // is a HIGH number.
    const double pos = static_cast<double>(total_ - 1) * (1.0 - n / 100.0);
    const auto lo = static_cast<std::uint64_t>(std::floor(pos));
    const double frac = pos - static_cast<double>(lo);
    const std::uint64_t hi = std::min<std::uint64_t>(lo + 1, total_ - 1);

    const Pick a = pick(lo);
    if (!a.inSpan) return {std::nullopt, a.side};

    // `frac == 0` is not an optimisation: the reference computes
    // `v[lo] + frac*(v[hi]-v[lo])`, which at frac = 0 is v[lo] exactly and
    // does not read v[hi] at all. Demanding that v[hi] be in-span there would
    // report an absence for a value the arithmetic pins exactly.
    if (frac == 0.0) return {a.db, LnAbsence::None};

    const Pick b = pick(hi);
    if (!b.inSpan) return {std::nullopt, b.side};
    return {a.db + frac * (b.db - a.db), LnAbsence::None};
}

std::optional<double> LevelHistogram::percentileDb(double n) const {
    return percentile(n).db;
}

void LevelHistogram::merge(const LevelHistogram& other) {
    if (!(baseDb_ == other.baseDb_)) {
        throw std::invalid_argument(
            "LevelHistogram::merge: bases differ -- merging misaligned bins would "
            "shift every level in one of the two histograms");
    }
    for (std::size_t bin = 0; bin < kBinCount; ++bin) {
        addSaturating(counts_[bin], static_cast<std::uint64_t>(other.counts_[bin]));
    }
    addSaturating(belowSpan_, static_cast<std::uint64_t>(other.belowSpan_));
    addSaturating(aboveSpan_, static_cast<std::uint64_t>(other.aboveSpan_));
    total_ += other.total_;
}

}  // namespace rta::meter
