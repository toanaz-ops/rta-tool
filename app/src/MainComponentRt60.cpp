// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. RT60-UI lane, task T5
// (docs/plans/2026-10-20-rt60-ui-lane-plan.md): the composition root's half of
// the RT60 pane. The analysis itself is rta::measure::analyseRt60 (JUCE-free,
// proven with no device); this file only says which IR it reads, remembers the
// answer, and tells the SWEEP half what the pane's button asked for.
#include "MainComponent.h"

#include "measure/Rt60Analysis.h"

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

void MainComponentRt60::select(const std::string& traceId) {
    if (traceId == selectedId_ && result_ != nullptr) return;  // once per selection change
    selectedId_ = traceId;
    selectedName_ = traceId;
    if (library_ != nullptr) {
        for (const auto& entry : library_->entries()) {
            if (entry.traceId == traceId) selectedName_ = entry.name;
        }
    }
    const auto ir = sweep_.find ? sweep_.find(traceId) : nullptr;
    // A trace with no IR is analysed too: the refusal IS the result, and the pane
    // shows only that (decision 9).
    result_ = std::make_shared<const rta::measure::Rt60Result>(rta::measure::analyseRt60(ir.get()));
    ++analysisCount_;
    ++revision_;
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
    return b;
}
