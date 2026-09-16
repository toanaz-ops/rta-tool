// SPDX-License-Identifier: AGPL-3.0-or-later
//
// AlignmentWizard.cpp's polarity-signal half, split out when the one file
// passed the 300-line aim (CLAUDE.md "File length"). The seam is the one the
// decision record already draws: AlignmentWizard.cpp runs the SEQUENCE and the
// FIT -- the authoritative estimator -- while this file assembles the table of
// signals that are shown and never obeyed (record Sec.7).
//
// Nothing here may promote a signal. Across a crossover the two IRs of a BW2
// or LR2 pair are 180 degrees apart by design, so an un-whitened correlation
// reads NEGATIVE on correct wiring with a confident rho; across an odd order
// the aligned-lag correlation is cos 90 = 0 and the sign is whichever side the
// peak fell on. The information that decides the sign is the designer's
// convention, which is question (c) -- not a measurement (record Sec.7, and
// PR #3 Sec.5 for the two correlators disagreeing on one unchanged pair).
#include "measure/AlignmentWizard.h"

#include <utility>

namespace rta::measure {

void AlignmentWizard::buildPolaritySignals() {
    signals_.clear();
    if (!irHigh_.has_value() || !irLow_.has_value()) return;

    const rta::ir::PolarityConfig polarityConfig{};
    const std::pair<PolaritySignalKind, const rta::ir::Deconvolution*> sides[]{
        { PolaritySignalKind::FindPolarityHighSide, &*irHigh_ },
        { PolaritySignalKind::FindPolarityLowSide, &*irLow_ },
    };
    for (const auto& [kind, deconvolution] : sides) {
        const auto found = rta::ir::findPolarity(*deconvolution, polarityConfig);
        PolaritySignal signal;
        signal.kind = kind;
        signal.sign = found.sign;
        signal.refusal = found.refusal;
        signal.figure = found.confidenceDb;
        // A subwoofer gets Refusal::BandTooHigh by design (Polarity.h:30). It
        // is LISTED as refused, and no other signal is promoted to fill the gap
        // -- filling it is the row record Sec.7's table forbids outright.
        signal.standing = (found.refusal == rta::ir::Refusal::None) ? SignalStanding::Advisory
                                                                   : SignalStanding::Refused;
        signal.reason = (found.refusal == rta::ir::Refusal::None)
                            ? "absolute sign of this box's first arrival"
                            : "findPolarity refused -- see its refusal code";
        signals_.push_back(std::move(signal));
    }

    const auto rho = rta::ir::relativePolarity(*irHigh_, *irLow_, {});
    PolaritySignal rhoSignal;
    rhoSignal.kind = PolaritySignalKind::RelativePolarityRho;
    rhoSignal.sign = rho.sign;
    rhoSignal.refusal = rho.refusal;
    rhoSignal.figure = rho.rho;
    rhoSignal.standing = SignalStanding::Advisory;
    rhoSignal.reason = acrossCrossoverReason();
    signals_.push_back(std::move(rhoSignal));

    if (whitenedSign_.has_value()) {
        PolaritySignal whitened;
        whitened.kind = PolaritySignalKind::WhitenedCorrelationPeak;
        whitened.sign = *whitenedSign_;
        whitened.standing = SignalStanding::Advisory;
        whitened.reason = acrossCrossoverReason();
        signals_.push_back(std::move(whitened));
    }
}

}  // namespace rta::measure
