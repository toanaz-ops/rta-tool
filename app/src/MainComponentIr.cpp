// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. H9 / L4c task A3: the composition root's half of
// the IR pane. The gate itself is rta::ir::gateSpectrum (core, proven with no
// device); this file only says which IR it reads, remembers the answer, and puts
// the stored result into the library.
#include "MainComponent.h"

#include "measure/IrGateModel.h"
#include "trace/CaptureConverter.h"

#include <algorithm>
#include <utility>

void MainComponent::wireIr() {
    ir_.attach(library_, [this](const std::string& traceId) { return sweep_.irStore().find(traceId); });
}

std::vector<rta::view::IrChoice> MainComponentIr::choices() const {
    std::vector<rta::view::IrChoice> out;
    if (library_ == nullptr || !find_) return out;
    const auto entries = library_->entries();
    // Newest first: the sweep just taken is the one the operator wants. Only SWEEP
    // traces whose IR is still in memory are offered -- nothing can be gated
    // without one, and the pane's empty state says why the list may be empty.
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        if (it->group != "SWEEP" || find_(it->traceId) == nullptr) continue;
        out.push_back({it->traceId, it->name});
    }
    return out;
}

std::string MainComponentIr::nameOf(const std::string& traceId) const {
    if (library_ != nullptr) {
        for (const auto& entry : library_->entries()) {
            if (entry.traceId == traceId) return entry.name;
        }
    }
    return traceId;
}

void MainComponentIr::select(const std::string& traceId) {
    if (traceId == selectedId_ && ir_ != nullptr) return;
    ir_ = find_ ? find_(traceId) : nullptr;
    ++revision_;
    if (ir_ == nullptr) {
        selectedId_.clear();
        selectedName_.clear();
        etc_.reset();
        analysis_.reset();
        excess_.reset();
        excessRequested_ = false;
        job_.invalidate();
        return;
    }
    selectedId_ = traceId;
    selectedName_ = nameOf(traceId);
    etc_ = std::make_shared<const rta::measure::IrEtc>(rta::measure::buildEtc(*ir_));
    reanalyse();
    requestExcess();  // a pick is a settled gate
}

void MainComponentIr::setMarkers(const rta::measure::IrGateMarkers& markers) {
    if (markers.startMs == markers_.startMs && markers.endMs == markers_.endMs) return;
    markers_ = markers;
    reanalyse();
}

void MainComponentIr::reanalyse() {
    ++revision_;
    // Whatever the worker is computing was for the previous markers: drop it.
    excess_.reset();
    excessRequested_ = false;
    job_.invalidate();
    analysis_ = ir_ ? std::make_shared<const rta::measure::IrGateAnalysis>(rta::measure::analyseIrGate(*ir_, markers_))
                    : nullptr;
}

void MainComponentIr::setLowerPlot(rta::measure::IrLowerPlot mode) {
    if (mode == lowerPlot_) return;
    lowerPlot_ = mode;
    ++revision_;
    requestExcess();
}

void MainComponentIr::requestExcess() {
    if (lowerPlot_ == rta::measure::IrLowerPlot::Magnitude || ir_ == nullptr || analysis_ == nullptr || !analysis_->ok ||
        excess_ != nullptr || excessRequested_) {
        return;
    }
    excessRequested_ = true;
    ++revision_;
    // trustedHighHz is the SWEEP's own ceiling; the gate's fMin is the floor. A hand-built IR
    // that states no ceiling (0) trusts nothing, and the pane says so.
    // The sweep's own validity floor binds as well as the gate's: a long gate gives fMin below it.
    job_.submit({analysis_->bins, analysis_->sampleRate, std::max(analysis_->fMinHz, ir_->trustedLowHz),
                 ir_->trustedHighHz});
}

void MainComponentIr::pollExcess() {
    if (auto done = job_.poll()) {
        excess_ = std::move(done);
        excessRequested_ = false;
        ++revision_;
    }
}

std::string MainComponentIr::storeGated() {
    if (library_ == nullptr || ir_ == nullptr || analysis_ == nullptr) return "nothing to store -- pick an IR first";
    if (!analysis_->ok) return "not stored -- " + analysis_->refusal;
    // Device and channel roles come from the SWEEP trace the IR belongs to, so
    // ALIGN's equality check on roles still means what it meant.
    std::string device, roles;
    if (const auto* source = library_->trace(selectedId_)) {
        device = source->meta().deviceName;
        roles = source->meta().channelRoles;
    }
    auto trace = rta::measure::gatedTrace(*ir_, *analysis_, rta::trace::nextCaptureId(), std::move(device), std::move(roles));
    if (!trace.has_value()) return "not stored -- the gated response could not be built";
    const std::string name = selectedName_ + " gated";
    const auto id = library_->add(std::move(*trace), name, kGroup);
    if (id.empty()) return "not stored -- duplicate capture id";
    return "stored: " + name + " \xc2\xb7 " + rta::measure::validAboveText(analysis_->fMinHz);
}

rta::view::IrPaneBinding MainComponentIr::binding() {
    rta::view::IrPaneBinding b;
    b.choices = [this] { return choices(); };
    b.selectedId = [this] { return selectedId_; };
    b.select = [this](const std::string& traceId) { select(traceId); };
    b.etc = [this] { return etc_; };
    b.analysis = [this] { return analysis_; };
    b.markers = [this] { return markers_; };
    b.setMarkers = [this](const rta::measure::IrGateMarkers& markers) { setMarkers(markers); };
    b.storeGated = [this] { return storeGated(); };
    b.lowerPlot = [this] { return lowerPlot_; };
    b.setLowerPlot = [this](rta::measure::IrLowerPlot mode) { setLowerPlot(mode); };
    b.excess = [this] { return excess_; };
    b.excessPending = [this] { return excessRequested_ && excess_ == nullptr; };
    b.settle = [this] { settle(); };
    b.revision = [this] {
        // The IR leaving the store (a ninth sweep pops the oldest) clears the
        // selection rather than leaving a pane that gates a ghost.
        if (!selectedId_.empty() && find_ && find_(selectedId_) == nullptr) select(std::string());
        pollExcess();
        return revision_;
    };
    return b;
}
