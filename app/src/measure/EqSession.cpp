// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/EqSession.h"

#include "rta/eq/BiquadDesign.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace rta::measure {

FilterBand filterBandHz(const rta::eq::FilterSpec& spec) {
    if (spec.type == rta::eq::FilterType::LowShelf) {
        return FilterBand{ 0.0, spec.fcHz };
    }
    if (spec.type == rta::eq::FilterType::HighShelf) {
        return FilterBand{ spec.fcHz, std::numeric_limits<double>::infinity() };
    }
    if (spec.q <= 0.0) return FilterBand{ spec.fcHz, spec.fcHz };
    // RBJ's own Q <-> bandwidth relation, solved for the two -3 dB edges:
    // f2 - f1 == fc/Q and sqrt(f1*f2) == fc simultaneously. Keeping the exact
    // roots rather than the fc +- fc/(2Q) linearisation matters at low Q,
    // where the two disagree by more than the bin spacing.
    const double half = 1.0 / (2.0 * spec.q);
    const double root = std::sqrt(1.0 + half * half);
    return FilterBand{ spec.fcHz * (root - half), spec.fcHz * (root + half) };
}

EqSession::EqSession(EqSessionConfig config) : config_(std::move(config)) {}

void EqSession::setMeasurement(std::span<const float> hz, std::span<const float> measuredDb,
                               std::span<const float> targetDb, std::span<const float> coherence,
                               std::span<const std::complex<double>> hHalfGrid,
                               double sampleRate) {
    if (hz.size() != measuredDb.size() || hz.size() != targetDb.size()) {
        throw std::invalid_argument("EqSession: hz/measuredDb/targetDb length mismatch");
    }
    if (sampleRate <= 0.0) throw std::invalid_argument("EqSession: sampleRate must be positive");

    hz_.assign(hz.begin(), hz.end());
    measuredDb_.assign(measuredDb.begin(), measuredDb.end());
    targetDb_.assign(targetDb.begin(), targetDb.end());
    coherence_.assign(coherence.begin(), coherence.end());
    hHalfGrid_.assign(hHalfGrid.begin(), hHalfGrid.end());
    sampleRate_ = sampleRate;

    trusted_ = buildTrustMask(coherence_, hz_.size(), config_.trustFloor);
    // committed_ and declinedBands_ SURVIVE: a re-measurement is a new view of
    // the same session (see CommittedFilter's doc comment). The bin-indexed
    // exclusion mask cannot survive a grid change, so it is rebuilt from the
    // bands, which are grid-independent by construction.
    rebuildExclusionMask();
}

void EqSession::rebuildExclusionMask() {
    excluded_.assign(hz_.size(), static_cast<std::uint8_t>(0));
    for (const FilterBand& band : declinedBands_) {
        for (std::size_t k = 0; k < hz_.size(); ++k) {
            const double f = static_cast<double>(hz_[k]);
            if (f >= band.lowHz && f <= band.highHz) {
                excluded_[k] = static_cast<std::uint8_t>(1);
            }
        }
    }
}

std::vector<float> EqSession::workingResidualDb() const {
    std::vector<float> residual(hz_.size(), 0.0f);
    for (std::size_t k = 0; k < hz_.size(); ++k) {
        double r = static_cast<double>(measuredDb_[k]) - static_cast<double>(targetDb_[k]);
        for (const auto& filter : committed_) {
            if (filter.applied) continue;  // already in the room (record sec.2)
            r += rta::eq::responseDb(filter.spec, sampleRate_, static_cast<double>(hz_[k]));
        }
        residual[k] = static_cast<float>(r);
    }
    return residual;
}

std::vector<double> EqSession::ghostDb() const {
    std::vector<double> ghost(hz_.size(), 0.0);
    for (std::size_t k = 0; k < hz_.size(); ++k) {
        double g = static_cast<double>(measuredDb_[k]);
        for (const auto& filter : committed_) {
            // Same membership rule as workingResidualDb, and for the same
            // reason: an applied filter is already in measuredDb_, so adding
            // its response would predict a room corrected twice.
            if (filter.applied) continue;
            g += rta::eq::responseDb(filter.spec, sampleRate_, static_cast<double>(hz_[k]));
        }
        ghost[k] = g;
    }
    return ghost;
}

rta::eq::EqInput EqSession::makeInput(std::span<const float> residual) const {
    rta::eq::EqInput input;
    input.hz = hz_;
    input.residualDb = residual;
    input.coherence = coherence_.size() == hz_.size() ? std::span<const float>(coherence_)
                                                      : std::span<const float>{};
    input.trusted = trusted_;
    input.excluded = excluded_;
    input.sampleRate = sampleRate_;
    input.maxFilters = config_.maxFilters;
    input.gCapDb = config_.gCapDb;
    input.qMaxBoost = config_.qMaxBoost;
    input.qMaxCut = config_.qMaxCut;
    input.roomT60Sec = config_.roomT60Sec;
    input.hHalfGrid = hHalfGrid_;
    return input;
}

std::vector<rta::eq::Candidate> EqSession::suggest(int maxCandidates) const {
    if (hz_.empty()) return {};
    const std::vector<float> residual = workingResidualDb();
    try {
        return rta::eq::rankCandidates(makeInput(residual), maxCandidates);
    } catch (const std::invalid_argument&) {
        // "every bin untrusted or excluded" is the allocator's own refusal
        // (EqAllocator.h). At this layer that is a state the operator can
        // reach by declining every chip, so it reads as an empty ranking.
        return {};
    }
}

void EqSession::runAutoEq() {
    if (hz_.empty()) return;
    committed_.erase(std::remove_if(committed_.begin(), committed_.end(),
                                    [](const CommittedFilter& f) { return !f.applied; }),
                     committed_.end());

    const std::vector<float> residual = workingResidualDb();
    std::vector<rta::eq::FilterSpec> specs;
    try {
        specs = rta::eq::autoEq(makeInput(residual));
    } catch (const std::invalid_argument&) {
        return;  // same refusal as suggest(); nothing lands
    }
    for (const auto& spec : specs) committed_.push_back(CommittedFilter{ spec, false });
}

void EqSession::acceptCandidate(const rta::eq::Candidate& candidate) {
    committed_.push_back(CommittedFilter{ candidate.spec, false });
}

void EqSession::declineCandidate(const rta::eq::Candidate& candidate) {
    declinedBands_.push_back(filterBandHz(candidate.spec));
    rebuildExclusionMask();
}

void EqSession::markApplied(std::size_t index, bool applied) {
    if (index >= committed_.size()) return;
    committed_[index].applied = applied;
}

void EqSession::clearFilters() { committed_.clear(); }

void EqSession::clearExclusions() {
    declinedBands_.clear();
    rebuildExclusionMask();
}

}  // namespace rta::measure
