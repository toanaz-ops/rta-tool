// SPDX-License-Identifier: AGPL-3.0-or-later
// AlignRunner's IR half (ALIGN-UI wave B, T11): the impulse responses the
// operator bound to each SOURCE, how they reach the wizard, and how the polarity
// table they feed reads. Split from AlignRunner.cpp, which had no room left
// under the 400-line cap; the seam is the one AlignRunnerState.h draws.
#include "measure/AlignRunnerState.h"
#include "measure/AlignRunnerText.h"

#include <cstdio>
#include <utility>

namespace rta::measure {
using namespace detail;
namespace {

/// `SweepIr` mirrors `rta::ir::Deconvolution` field for field (SweepIrOutcome.h)
/// precisely so that this copy is a struct assignment and nothing more.
rta::ir::Deconvolution toDeconvolution(const SweepIr& ir) {
    rta::ir::Deconvolution out;
    out.samples = ir.samples;
    out.originIndex = ir.originIndex;
    out.sampleRate = ir.sampleRate;
    out.normalisationGain = ir.normalisationGain;
    out.harmonicSpacingL = ir.harmonicSpacingL;
    out.excitationLowHz = ir.excitationLowHz;
    out.excitationHighHz = ir.excitationHighHz;
    out.trustedLowHz = ir.trustedLowHz;
    out.trustedHighHz = ir.trustedHighHz;
    return out;
}

const char* refusalName(rta::ir::Refusal refusal) {
    switch (refusal) {
        case rta::ir::Refusal::None: return "None";
        case rta::ir::Refusal::NoSignal: return "NoSignal";
        case rta::ir::Refusal::NoNoiseEstimate: return "NoNoiseEstimate";
        case rta::ir::Refusal::BandTooLow: return "BandTooLow";
        case rta::ir::Refusal::BandTooHigh: return "BandTooHigh";
        case rta::ir::Refusal::SweepBandInsufficient: return "SweepBandInsufficient";
        case rta::ir::Refusal::CaptureTooShort: return "CaptureTooShort";
    }
    return "";
}

const char* signName(rta::ir::Sign sign) {
    switch (sign) {
        case rta::ir::Sign::Positive: return "+";
        case rta::ir::Sign::Negative: return "-";
        case rta::ir::Sign::Unknown: break;
    }
    return "?";
}

const char* signalLabel(PolaritySignalKind kind) {
    switch (kind) {
        case PolaritySignalKind::FindPolarityHighSide: return "findPolarity, high-pass side";
        case PolaritySignalKind::FindPolarityLowSide: return "findPolarity, low-pass side";
        case PolaritySignalKind::RelativePolarityRho: return "rho";
        case PolaritySignalKind::WhitenedCorrelationPeak: return "whitened peak";
    }
    return "";
}

}  // namespace

std::string AlignRunnerState::supplyBoundIrs(AlignmentWizard& fresh) const {
    std::string note;
    const std::pair<Source, const std::shared_ptr<const SweepIr>*> bound[]{
        { Source::Main, &irMain },
        { Source::Sub, &irSub },
    };
    for (const auto& [source, ir] : bound) {
        if (*ir == nullptr) continue;
        // An IR measured at another rate has arrivals in other samples: using
        // it would add a count of one clock to a count of another. Say so.
        if ((*ir)->sampleRate != engine->sampleRate()) {
            note += std::string(" (IR ") + sourceLabel(source) + " ignored: its sample rate differs from the output's)";
            continue;
        }
        fresh.supplyImpulseResponse(source, ImpulseWitness{ toDeconvolution(**ir), (*ir)->arrivalSamples });
    }
    return note;
}

void AlignRunner::bindIr(Source source, std::string traceId, std::shared_ptr<const SweepIr> ir) {
    AlignRunnerState& d = *impl_;
    auto& slot = (source == Source::Main) ? d.irMain : d.irSub;
    auto& id = (source == Source::Main) ? d.irMainId : d.irSubId;
    slot = std::move(ir);
    id = slot != nullptr ? std::move(traceId) : std::string();
    ++d.revision;
}

const std::string& AlignRunner::boundIrId(Source source) const noexcept {
    return source == Source::Main ? impl_->irMainId : impl_->irSubId;
}

std::shared_ptr<const SweepIr> AlignRunner::boundIr(Source source) const noexcept {
    return source == Source::Main ? impl_->irMain : impl_->irSub;
}

std::vector<std::string> AlignRunner::polaritySignalLines() const {
    std::vector<std::string> lines;
    if (!impl_->wizard) return lines;
    for (const auto& signal : impl_->wizard->polaritySignals()) {
        char text[200];
        if (signal.standing == SignalStanding::Refused) {
            std::snprintf(text, sizeof text, "%s: refused (%s) -- no sign listed", signalLabel(signal.kind),
                          refusalName(signal.refusal));
        } else {
            std::snprintf(text, sizeof text, "%s: %s (%.2f, advisory) -- %s", signalLabel(signal.kind),
                          signName(signal.sign), signal.figure, signal.reason.c_str());
        }
        lines.emplace_back(text);
    }
    return lines;
}

}  // namespace rta::measure
