// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
#include "measure/AverageGroup.h"

#include "measure/AnalyserPublish.h"

#include <numeric>
#include <utility>

namespace rta::measure {

MemberRefusal AverageGroup::addMember(int tfIndex, int referenceChannel, std::string name,
                                      double trim) {
    if (!members_.empty() && members_.front().referenceChannel != referenceChannel) {
        return MemberRefusal::DifferentReference;
    }
    members_.push_back(AverageGroupMember{tfIndex, referenceChannel, std::move(name), trim});
    return MemberRefusal::None;
}

namespace {

/// The one scalar this file stands in for a whole magnitude (or coherence)
/// curve with -- a plain arithmetic mean over every bin, including floored
/// ones (a position sitting mostly at the floor should read as quiet, not
/// be excluded from its own summary).
float meanOf(std::span<const float> values) {
    if (values.empty()) return 0.0f;
    const double sum = std::accumulate(values.begin(), values.end(), 0.0,
                                       [](double acc, float v) { return acc + static_cast<double>(v); });
    return static_cast<float>(sum / static_cast<double>(values.size()));
}

}  // namespace

AverageGroupPublish AverageGroup::publish(
    std::span<const rta::dsp::TransferSnapshot> positions) const {
    AverageGroupPublish result;
    result.positions.resize(members_.size());

    // A size mismatch (a caller mid routing-change, say) gets one
    // default-constructed summary per member and no average, never a
    // crash and never a curve computed from misaligned data.
    if (positions.size() != members_.size()) {
        for (std::size_t i = 0; i < members_.size(); ++i) {
            result.positions[i].tfIndex = members_[i].tfIndex;
            result.positions[i].name = members_[i].name;
        }
        return result;
    }

    std::vector<double> weights(members_.size());
    for (std::size_t i = 0; i < members_.size(); ++i) {
        weights[i] = members_[i].trim;

        PositionSummary& summary = result.positions[i];
        summary.tfIndex = members_[i].tfIndex;
        summary.name = members_[i].name;

        const auto& snap = positions[i];
        summary.effectiveAverages = snap.effectiveAverages;
        summary.gatePassed = snap.coherence.has_value();
        summary.levelDb = meanOf(snap.magnitudeDb);
        if (snap.coherence.has_value()) {
            summary.weightedCoherence = meanOf(*snap.coherence);
        }
        // B3 never sets `overloaded` -- there is no overload check upstream
        // of this class yet; that is B5's capture sequencer.

        if (members_[i].tfIndex == solo_) {
            // appliedDelaySamples is not this class's to know (it lives on
            // Analyser::Config, one layer up) -- 0 here states "not
            // reported through this path", not "nothing was compensated".
            result.soloTransfer = makeTransferBlock(snap, 0);
        }
    }

    // Reads `positions`/`weights` through spans straight into
    // spatialAverage -- no local copy of either scales with N (see this
    // function's own header comment and SpatialAverage.cpp's own body:
    // every allocation there is sized `bins`, never `positions.size()`).
    const auto average = rta::dsp::spatialAverage(positions, weights, mode_);
    if (average.has_value()) {
        result.average = makeAverageBlock(*average);
    }

    return result;
}

}  // namespace rta::measure
