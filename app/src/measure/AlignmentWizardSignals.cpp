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

#include <cstdio>
#include <string>
#include <utility>

namespace rta::measure {

namespace {

std::string hz(double value) {
    char text[32];
    std::snprintf(text, sizeof text, "%.0f", value);  // whole hertz: CLAUDE.md "Reading out numbers"
    return text;
}

// dB keeps one decimal (CLAUDE.md "Reading out numbers"); hertz do not use this.
std::string db(double value) {
    char text[32];
    std::snprintf(text, sizeof text, "%.1f", value);
    return text;
}

}  // namespace

std::string polarityRefusalText(const rta::ir::PolarityResult& result,
                                const rta::ir::Deconvolution& measured) {
    using rta::ir::Refusal;
    switch (result.refusal) {
        case Refusal::None: return "absolute sign of this box's first arrival";
        case Refusal::BandTooLow:
            return "Measured band does not reach low enough to decide absolute polarity (low edge "
                   + hz(result.lowEdgeHz) + " Hz). Compare relatively against another measurement of this same system.";
        case Refusal::BandTooHigh:
            // Owner's sentence on BOTH sides (HUMAN-QA-QUEUE L7 polarity): it was written
            // for the subwoofer, which gets this refusal by design.
            return "Measured band does not reach high enough (high edge " + hz(result.highEdgeHz)
                   + " Hz). Measure again full-range, or compare relatively before/after a change.";
        case Refusal::SweepBandInsufficient: {
            // The result does not carry the sweep's trusted range (findPolarity returns
            // before measuring anything), so it is read from the Deconvolution the caller
            // already holds -- the same two fields the core compared. Zero means "not stated".
            const double low = measured.trustedLowHz;
            const double high = measured.trustedHighHz;
            std::string range = high > 0.0 ? ("from " + hz(low) + " to " + hz(high) + " Hz")
                                           : ("from " + hz(low) + " Hz up");
            return "This sweep is only trustworthy " + range + " -- not enough to decide.";
        }
        case Refusal::NoSignal:
            return "Arrival confidence " + db(result.confidenceDb) + " dB is below the "
                   + db(rta::ir::PolarityConfig{}.minConfidenceDb)
                   + " dB needed to decide. Measure again, louder or closer.";
        case Refusal::NoNoiseEstimate:
            return "This sweep carries no harmonic spacing, so no noise floor can be estimated. Measure again with the app's own sweep.";
        case Refusal::CaptureTooShort:
            return "The capture is too short for the band edge to be measured (needs about 10 cycles of the low edge). Lengthen the gap after the sweep.";
    }
    return {};
}

void AlignmentWizard::buildPolaritySignals() {
    signals_.clear();
    if (highSideIr() == nullptr || lowSideIr() == nullptr) return;

    const rta::ir::PolarityConfig polarityConfig{};
    const std::pair<PolaritySignalKind, const rta::ir::Deconvolution*> sides[]{
        { PolaritySignalKind::FindPolarityHighSide, &highSideIr()->deconvolution },
        { PolaritySignalKind::FindPolarityLowSide, &lowSideIr()->deconvolution },
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
        signal.reason = polarityRefusalText(found, *deconvolution);
        signals_.push_back(std::move(signal));
    }

    const auto rho = rta::ir::relativePolarity(highSideIr()->deconvolution,
                                                lowSideIr()->deconvolution, {});
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
