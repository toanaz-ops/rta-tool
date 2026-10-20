// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. RT60-UI lane, task T5
// (docs/plans/2026-10-20-rt60-ui-lane-plan.md): the composition root's half of
// the RT60 pane. The analysis itself is rta::measure::analyseRt60 (JUCE-free,
// proven with no device); this file only says which IR it reads, remembers the
// answer, and tells the SWEEP half what the pane's button asked for.
#include "MainComponent.h"

#include "measure/Rt60Analysis.h"
#include "measure/Rt60EnsembleReadout.h"

#include <algorithm>
#include <utility>

void MainComponent::wireRt60() {
    rt60_.attach(library_,
                 MainComponentRt60::SweepAccess{
                     // The RT60 pane's SWEEP: a named output, this press's tail, and
                     // the requester tag that makes the result auto-select here and
                     // nowhere else. The rail button keeps request_'s 3 s.
                     [this](int outputChannel, double tailSeconds) {
                         return sweep_.clickedOnOutput(outputChannel, MainComponentSweep::Requester::Rt60, tailSeconds);
                     },
                     [this](const std::string& traceId) { return sweep_.irStore().find(traceId); },
                     [this] { return sweep_.readoutText().toStdString(); },
                     [this] { return sweep_.busy(); },
                     [this] {
                         return isSyntheticMode() ? rta::measure::SyntheticInput::kOutputChannels
                                                  : audioIo_.currentState().numOutputChannels;
                     }});
}

std::vector<rta::view::Rt60Choice> MainComponentRt60::choices() const {
    std::vector<rta::view::Rt60Choice> out;
    if (library_ == nullptr) return out;
    const auto entries = library_->entries();
    // Newest first: the sweep just taken is the one the operator wants. Every
    // SWEEP trace is listed, with or without an IR (decision 9).
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        if (it->group != "SWEEP") continue;
        out.push_back({it->traceId, it->name, sweep_.find && sweep_.find(it->traceId) != nullptr});
    }
    return out;
}

bool MainComponentRt60::selectedIrInMemory() const {
    return !selectedId_.empty() && sweep_.find && sweep_.find(selectedId_) != nullptr;
}

std::string MainComponentRt60::nameOf(const std::string& traceId) const {
    if (library_ != nullptr) {
        for (const auto& entry : library_->entries()) {
            if (entry.traceId == traceId) return entry.name;
        }
    }
    return traceId;
}

void MainComponentRt60::select(const std::string& traceId) {
    if (traceId == selectedId_ && (result_ != nullptr || analysing_)) return;  // once per selection change
    selectedId_ = traceId;
    selectedName_ = nameOf(traceId);
    reanalyse();
}

// One place decides where an analysis runs (decision 11). The octave table of ONE
// trace stays on the message thread, as in wave A. A third-octave set (24 bands)
// or an ensemble (up to 8 IRs) is several times that and goes to the worker; the
// pane shows "analysing" until `poll` takes the answer.
void MainComponentRt60::reanalyse() {
    ++revision_;
    const auto primary = (sweep_.find && !selectedId_.empty()) ? sweep_.find(selectedId_) : nullptr;
    const bool ensembleOn = ensembleIds_.size() >= 2;
    const auto set = settings_.bandSet;
    if (set == rta::measure::Rt60BandSet::Octave && !ensembleOn) {
        // A trace with no IR is analysed too: the refusal IS the result, and the
        // pane shows only that (decision 9).
        result_ = std::make_shared<const rta::measure::Rt60Result>(rta::measure::analyseRt60(primary.get(), set));
        ensemble_.reset();
        analysing_ = false;
        ++analysisCount_;
        return;
    }
    rta::measure::Rt60Worker::Request request;
    request.ticket = ++ticket_;
    request.set = set;
    request.primary = primary;
    if (ensembleOn) {
        for (const auto& id : ensembleIds_) request.ensemble.push_back(sweep_.find ? sweep_.find(id) : nullptr);
    }
    ensemble_.reset();
    analysing_ = true;
    if (primary == nullptr) {
        // Nothing for the worker to do for the picked trace: its refusal is instant.
        result_ = selectedId_.empty() ? nullptr
                                      : std::make_shared<const rta::measure::Rt60Result>(
                                            rta::measure::analyseRt60(nullptr, set));
    } else {
        result_.reset();
    }
    worker_.submit(std::move(request));
}

void MainComponentRt60::poll() {
    if (!analysing_) return;
    auto outcome = worker_.poll();
    if (!outcome.has_value() || outcome->ticket != ticket_) return;
    if (outcome->single != nullptr) result_ = outcome->single;
    ensemble_ = outcome->ensemble;
    analysing_ = false;
    ++analysisCount_;
    ++revision_;
}

void MainComponentRt60::setBandSet(rta::measure::Rt60BandSet set) {
    if (set == settings_.bandSet) return;
    settings_.bandSet = set;
    settings_.selectedBand = -1;  // an index into the other set means a different band
    reanalyse();
}

bool MainComponentRt60::toggleEnsemble(const std::string& traceId) {
    const auto it = std::find(ensembleIds_.begin(), ensembleIds_.end(), traceId);
    if (it != ensembleIds_.end()) {
        ensembleIds_.erase(it);
    } else {
        if (ensembleIds_.size() >= rta::measure::kRt60MaxEnsembleCaptures) return false;
        if (!sweep_.find || sweep_.find(traceId) == nullptr) return false;
        ensembleIds_.push_back(traceId);
    }
    reanalyse();
    return true;
}

void MainComponentRt60::clearEnsemble() {
    if (ensembleIds_.empty()) return;
    ensembleIds_.clear();
    reanalyse();
}

rta::view::Rt60PaneBinding MainComponentRt60::binding() {
    rta::view::Rt60PaneBinding b;
    b.choices = [this] { return choices(); };
    b.selectedId = [this] { return selectedId_; };
    b.select = [this](const std::string& traceId) { select(traceId); };
    b.result = [this] { return result_; };
    b.selectedName = [this] { return selectedName_; };
    b.selectedIrInMemory = [this] { return selectedIrInMemory(); };
    b.revision = [this] {
        // The worker's answer lands here: every consumer reads the revision before
        // anything else, so asking for it is what takes a finished job.
        poll();
        // The IR leaving the store changes what the header says without a new
        // analysis, so it is part of the revision.
        return revision_ * 2 + (selectedIrInMemory() ? 1 : 0);
    };
    b.sweep = [this](int outputChannel, double tailSeconds) {
        return sweep_.start && sweep_.start(outputChannel, tailSeconds);
    };
    b.sweepBusy = [this] { return sweep_.busy && sweep_.busy(); };
    b.sweepReadout = [this] { return sweep_.readout ? sweep_.readout() : std::string(); };
    b.outputChannels = [this] { return sweep_.outputChannels ? sweep_.outputChannels() : 0; };
    b.settings = &settings_;
    b.setBandSet = [this](rta::measure::Rt60BandSet set) { setBandSet(set); };
    b.ensembleIds = [this] { return ensembleIds_; };
    b.toggleEnsemble = [this](const std::string& traceId) { return toggleEnsemble(traceId); };
    b.clearEnsemble = [this] { clearEnsemble(); };
    b.ensembleResult = [this] { return ensemble_; };
    b.ensembleNames = [this] {
        std::vector<std::string> names;
        for (const auto& id : ensembleIds_) names.push_back(nameOf(id));
        return names;
    };
    b.analysing = [this] { return analysing_; };
    return b;
}
