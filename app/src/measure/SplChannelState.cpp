// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Lane L6a task W2-E1.
#include "measure/SplChannelState.h"

namespace rta::measure {

SplChannelState::SplChannelState(const SplConfig& config, double sampleRate)
    : sampleRate_(sampleRate)
    , blockSeconds_(config.blockSeconds)
    , referenceOffsetDb_(config.referenceOffsetDb)
    , lnPercents_(config.lnPercents)
    , history_(SplHistory::capacityBlocks(config.logSpanSeconds, config.blockSeconds))
    , alarms_(config.alarms)
    , lnHistogram_(config.histogramBaseDb())
    , dose_{{rta::meter::Dose(config.dose[0]), rta::meter::Dose(config.dose[1])}} {}

void SplChannelState::onBlockClosed(const rta::meter::Block& block,
                                    std::span<const rta::meter::Block> windowThroughThisBlock) {
    history_.push(block);

    // ONE identity, reused: combineBlocks over a single-block "window" is
    // exactly that block's own Leq, offset-applied -- and it is ABSENT under
    // the SAME rule a multi-block window already obeys: a CalibrationInvalid
    // block excludes (record §2, §15 A2). Feeding the Ln histogram and both
    // dose accumulators from this one call means a block this project has
    // already decided not to trust is not trusted here either, rather than a
    // second, silently different, exclusion rule growing beside the first.
    const auto single = std::span<const rta::meter::Block>(&block, 1);
    const auto blockResult = rta::meter::combineBlocks(single, sampleRate_, referenceOffsetDb_, 1);
    if (blockResult.leqDb.has_value()) {
        lnHistogram_.add(*blockResult.leqDb);
        for (auto& dose : dose_) dose.addBlock(*blockResult.leqDb, blockResult.seconds);
    }

    // The alarms read the window ENDING AT this block -- record §15 A6's
    // sliding recompute -- never the whole channel window, which may already
    // hold blocks after this one (SplChannelState::onBlockClosed's own
    // header comment; AnalysisThread::feedSpl is what trims for that).
    alarms_.update(windowThroughThisBlock, sampleRate_, blockSeconds_, referenceOffsetDb_,
                   block.blockIndex, history_);
}

void SplChannelState::fillPublish(SplBlockView& view) const {
    view.alarms = alarms_.reports();

    for (std::size_t i = 0; i < dose_.size(); ++i) {
        // ABSENT until this accumulator has actually seen a block -- a fresh
        // Dose's percent() reads 0.0 %, which is indistinguishable from "no
        // exposure over an elapsed session" unless elapsedSeconds() is
        // checked first (memory/a-placeholder-for-an-absent-result-erases-
        // its-state.md; record §9's own "a zero dose reads as measured").
        if (dose_[i].elapsedSeconds() > 0.0) {
            view.dosePercent[i] = dose_[i].percent();
            view.doseProjected[i] = dose_[i].projectedPercent();
        }
    }

    for (std::size_t i = 0; i < lnPercents_.size(); ++i) {
        view.lnDb[i] = lnHistogram_.percentileDb(lnPercents_[i]);
    }
}

}  // namespace rta::measure
