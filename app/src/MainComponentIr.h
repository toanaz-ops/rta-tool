// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src.
//
// H9 / L4c task A3 (docs/plans/2026-11-09-h9-l4c-ir-gate-plan.md). MainComponent's
// IR-gate half, in the shape MainComponentRt60 gives its own: a small class
// MainComponent owns as a member. It keeps what must survive the pane being
// rebuilt by a selector click -- the selection, the two gate markers and the
// cached analysis -- and NOT the pane, which MainComponentPanes.cpp rebuilds.
//
// What it needs from the real app (the SWEEP half's IR store) arrives as one
// callback wired in MainComponentIr.cpp; the library is attached directly, as
// RT60's is. The gate runs on the message thread: one 16384-point transform per
// marker move (about a millisecond), and the energy-time curve once per
// selection. The IR itself is never copied -- the span overload of
// `gateSpectrum` reads the stored one.
#pragma once

#include "measure/IrGateModel.h"
#include "trace/TraceLibrary.h"
#include "view/IrPaneView.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class MainComponentIr {
public:
    /// The library group a stored gated response goes under. Its own group, not
    /// "SWEEP": the RT60 and ALIGN pickers list SWEEP traces as IR sources, and a
    /// gated response has no IR behind it.
    static constexpr const char* kGroup = "GATED";

    void attach(rta::trace::TraceLibrary& library,
                std::function<std::shared_ptr<const rta::measure::SweepIr>(const std::string&)> findIr) {
        library_ = &library;
        find_ = std::move(findIr);
    }

    /// The wiring `makePaneFactory` gives every IrPaneView it builds.
    [[nodiscard]] rta::view::IrPaneBinding binding();

    /// Picks an IR and analyses it at the current markers. An id with no IR in
    /// memory clears the selection.
    void select(const std::string& traceId);
    /// Moves the markers and re-analyses (the ETC is unchanged).
    void setMarkers(const rta::measure::IrGateMarkers& markers);
    /// STORE GATED: returns the sentence the pane shows.
    std::string storeGated();

    // --- test hooks, each referenced by spelling from app/tests_juce ---------
    [[nodiscard]] const std::string& selectedIdForTest() const noexcept { return selectedId_; }
    [[nodiscard]] const rta::measure::IrGateMarkers& markersForTest() const noexcept { return markers_; }
    [[nodiscard]] std::shared_ptr<const rta::measure::IrGateAnalysis> analysisForTest() const noexcept {
        return analysis_;
    }

private:
    void reanalyse();
    [[nodiscard]] std::vector<rta::view::IrChoice> choices() const;
    [[nodiscard]] std::string nameOf(const std::string& traceId) const;

    rta::trace::TraceLibrary* library_ = nullptr;
    std::function<std::shared_ptr<const rta::measure::SweepIr>(const std::string&)> find_;
    std::string selectedId_;
    std::string selectedName_;
    /// Held while selected, so the IR the analysis was made from cannot leave
    /// under the pane when the store evicts it.
    std::shared_ptr<const rta::measure::SweepIr> ir_;
    rta::measure::IrGateMarkers markers_;
    std::shared_ptr<const rta::measure::IrEtc> etc_;
    std::shared_ptr<const rta::measure::IrGateAnalysis> analysis_;
    std::uint64_t revision_ = 0;
};
