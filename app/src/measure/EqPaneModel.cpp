// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/EqPaneModel.h"

#include "measure/EqTraceGrid.h"
#include "measure/EqTrustMask.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rta::measure {
namespace {

using rta::trace::Field;

bool pickedAndVisible(const rta::trace::TraceLibrary& library, const std::string& id) {
    const auto* entry = library.entry(id);
    return entry != nullptr && entry->visible;
}

}  // namespace

std::string eqFirTapsLabel(std::size_t taps, double sampleRate) {
    char text[96];
    const double n = static_cast<double>(taps);
    std::snprintf(text, sizeof text, "%zu taps  %.0f Hz  %.1f ms", taps, sampleRate / n,
                  (n - 1.0) / (2.0 * sampleRate) * 1000.0);
    return text;
}

EqPaneModel::EqPaneModel() {
    status_ = "EQ: pick a measurement (STORE on TRANSFER with a reference feeds one)";
}

std::vector<std::string> EqPaneModel::measurementCandidateIds(const rta::trace::TraceLibrary& library) {
    std::vector<std::string> ids;
    for (const auto& entry : library.entries()) {
        const auto* trace = library.trace(entry.traceId);
        if (entry.visible && trace != nullptr && trace->has(Field::Coherence)) ids.push_back(entry.traceId);
    }
    return ids;
}

std::vector<std::string> EqPaneModel::targetCandidateIds(const rta::trace::TraceLibrary& library) {
    std::vector<std::string> ids;
    for (const auto& entry : library.entries()) {
        if (entry.visible && library.trace(entry.traceId) != nullptr) ids.push_back(entry.traceId);
    }
    return ids;
}

bool EqPaneModel::selectMeasurement(const rta::trace::TraceLibrary& library, const std::string& id) {
    const auto* entry = library.entry(id);
    const auto* trace = library.trace(id);
    const auto refuse = [this](const char* why) {
        status_ = why;
        bump();
        return false;
    };
    if (entry == nullptr || trace == nullptr || !entry->visible) return refuse("EQ: that trace is not in the library");
    if (!trace->has(Field::Coherence)) {
        return refuse("EQ: this trace has no coherence -- STORE on TRANSFER with a reference");
    }
    if (trace->binHz() <= 0.0 || !allFinite(trace->field(Field::Magnitude)) ||
        !allFinite(trace->field(Field::Phase))) {
        return refuse("EQ: this trace has no usable frequency axis or non-finite values");
    }

    hz_ = traceBinFrequencies(*trace);
    sampleRate_ = trace->meta().sampleRate;
    const auto magnitude = trace->field(Field::Magnitude);
    const auto coherence = trace->field(Field::Coherence);
    measuredDb_.assign(magnitude.begin(), magnitude.end());
    coherence_.assign(coherence.begin(), coherence.end());
    h_.clear();
    if (trace->has(Field::Phase)) h_ = traceComplexResponse(magnitude, trace->field(Field::Phase));

    measurement_ = EqTracePick{ id, entry->name, false };
    chips_.clear();
    loadIntoSession();
    status_ = "EQ: measurement \"" + entry->name + "\" -- press AUTO EQ or SUGGEST";
    bump();
    return true;
}

bool EqPaneModel::selectTarget(const rta::trace::TraceLibrary& library, const std::string& id) {
    if (id.empty()) {
        target_ = EqTracePick{};
        targetSourceHz_.clear();
        targetSourceDb_.clear();
    } else {
        const auto* entry = library.entry(id);
        const auto* trace = library.trace(id);
        if (entry == nullptr || trace == nullptr || !entry->visible ||
            !allFinite(trace->field(Field::Magnitude))) {
            status_ = "EQ: that target trace is not usable";
            bump();
            return false;
        }
        target_ = EqTracePick{ id, entry->name, false };
        targetSourceHz_ = traceBinFrequencies(*trace);
        const auto magnitude = trace->field(Field::Magnitude);
        targetSourceDb_.assign(magnitude.begin(), magnitude.end());
    }
    chips_.clear();
    if (hasMeasurement()) loadIntoSession();
    bump();
    return true;
}

void EqPaneModel::loadIntoSession() {
    targetDb_ = resampleTargetDb(targetSourceHz_, targetSourceDb_, hz_);
    // KEEPS the committed list and declines (EqSession::setMeasurement).
    session_.setMeasurement(hz_, measuredDb_, targetDb_, coherence_, h_, sampleRate_);
}

void EqPaneModel::syncLibrary(const rta::trace::TraceLibrary& library) {
    const auto refresh = [&](EqTracePick& pick) {
        if (pick.id.empty()) return;
        const bool gone = !pickedAndVisible(library, pick.id);
        const auto* entry = library.entry(pick.id);
        const std::string name = entry != nullptr ? entry->name : pick.name;
        if (gone != pick.removed || name != pick.name) {
            pick.removed = gone;
            pick.name = name;
            bump();
        }
    };
    refresh(measurement_);
    refresh(target_);
}

void EqPaneModel::setMaxFilters(int maxFilters) { session_.setMaxFilters(maxFilters); bump(); }

void EqPaneModel::runAutoEq() {
    chips_.clear();
    if (!hasMeasurement()) {
        status_ = "AUTO EQ: pick a measurement first";
    } else {
        session_.runAutoEq();
        const auto n = unappliedCount();
        status_ = n == 0 ? "AUTO EQ: nothing to correct"
                         : "AUTO EQ: " + std::to_string(n) + " filter(s) -- see the ghost curve";
    }
    bump();
}

void EqPaneModel::suggest() {
    chips_.clear();
    if (!hasMeasurement()) {
        status_ = "SUGGEST: pick a measurement first";
    } else {
        chips_ = session_.suggest(3);  // D6: the top 1-3
        status_ = chips_.empty() ? "SUGGEST: nothing to correct"
                                 : "SUGGEST: " + std::to_string(chips_.size()) + " candidate(s)";
    }
    bump();
}

void EqPaneModel::acceptChip(std::size_t index) {
    if (index >= chips_.size()) return;
    session_.acceptCandidate(chips_[index]);
    suggest();  // the ranking re-bases on the residual this filter leaves
}

void EqPaneModel::declineChip(std::size_t index) {
    if (index >= chips_.size()) return;
    session_.declineCandidate(chips_[index]);
    suggest();  // and omits the declined band
}

void EqPaneModel::setApplied(std::size_t index, bool applied) { session_.markApplied(index, applied); chips_.clear(); bump(); }

void EqPaneModel::clearFilters() { session_.clearFilters(); chips_.clear(); bump(); }

void EqPaneModel::clearDeclined() { session_.clearExclusions(); chips_.clear(); bump(); }

void EqPaneModel::setFirPhase(std::optional<rta::dsp::FirPhase> phase) { firPhase_ = phase; bump(); }

void EqPaneModel::setFirTaps(std::optional<std::size_t> taps) { firTaps_ = taps; bump(); }

void EqPaneModel::setPeakNormalised(bool peak) { peakNormalised_ = peak; bump(); }

void EqPaneModel::setStatus(std::string text) { status_ = std::move(text); bump(); }

std::vector<double> EqPaneModel::targetLineDb() const {
    const double c = session_.levelOffsetDb();
    std::vector<double> line(targetDb_.size());
    for (std::size_t k = 0; k < line.size(); ++k) line[k] = static_cast<double>(targetDb_[k]) + c;
    return line;
}

std::vector<double> EqPaneModel::correctionDb() const {
    std::vector<double> ghost = session_.ghostDb();
    for (std::size_t k = 0; k < ghost.size(); ++k) ghost[k] -= static_cast<double>(measuredDb_[k]);
    return ghost;
}

std::string EqPaneModel::gateReadout() const {
    if (!hasMeasurement()) return "";
    return h_.empty() ? "G24 gate off (trace has no phase: boosts are not checked)" : "G24 gate on";
}

std::string EqPaneModel::trustReadout() const {
    if (!hasMeasurement()) return "";
    const auto trusted = session_.trusted();
    const auto count = std::count_if(trusted.begin(), trusted.end(), [](std::uint8_t t) { return t != 0; });
    char text[64];
    std::snprintf(text, sizeof text, "%lld of %zu bins trusted (coherence >= %.2f)",
                  static_cast<long long>(count), trusted.size(), static_cast<double>(kEqTrustFloor));
    return text;
}

std::string EqPaneModel::limitsReadout() const {
    const auto& c = session_.config();
    char text[96];
    std::snprintf(text, sizeof text, "boost <= +%.1f dB   Q <= %.0f boost / %.0f cut", c.gCapDb, c.qMaxBoost,
                  c.qMaxCut);
    return text;
}

std::size_t EqPaneModel::unappliedCount() const noexcept {
    const auto filters = session_.committed();
    return static_cast<std::size_t>(std::count_if(filters.begin(), filters.end(),
                                                  [](const CommittedFilter& f) { return !f.applied; }));
}

rta::firexport::Normalization EqPaneModel::normalization() const noexcept {
    return peakNormalised_ ? rta::firexport::Normalization::Peak0dBFS
                           : rta::firexport::Normalization::AsDesigned;
}

bool EqPaneModel::canExportFir() const noexcept {
    return hasMeasurement() && firPhase_.has_value() && firTaps_.has_value() && unappliedCount() > 0;
}

}  // namespace rta::measure
