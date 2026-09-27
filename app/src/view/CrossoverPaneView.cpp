// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See CrossoverPaneView.h.
#include "view/CrossoverPaneView.h"

#include "trace/Trace.h"
#include "trace/TraceLibrary.h"
#include "trace/VirtualTrace.h"
#include "view/AxisMetrics.h"
#include "view/CrossoverSurfaceRenderer.h"
#include "view/MeasureColours.h"

#include <az_ui/az_ui.h>

#include <array>

namespace rta::view {
namespace {

/// Library edits (add/rename/hide) are an operator action, not a 20 Hz
/// measurement stream -- RtaView/TransferView poll their bus at that rate
/// because a live snapshot can change every publish; this pane has no bus
/// behind it at all, only `TraceLibrary::revision()`, so a slower poll costs
/// nothing a person could perceive as lag.
constexpr int kTimerHz = 10;

/// One entry in the topology picker: a real `rta::measure::Topology` this
/// pane's `topology_` can hold, paired with the label the combo box shows.
/// A FINITE, HAND-PICKED list -- `expectedOffset` (CrossoverTopology.h) is
/// arithmetically valid at any order, but a combo box needs a finite set,
/// and these seven are what Rane Note 160 (LR-2/4/8) and ordinary practice
/// (BW-1..4) actually ship. Adding an order some rig actually uses is a
/// one-line addition here, never a reason to invent a default.
struct TopologyChoice {
    rta::measure::Topology topology;
    const char* label;
};

constexpr std::array<TopologyChoice, 7> kTopologyChoices{ {
    { { rta::measure::CrossoverFamily::LinkwitzRiley, 2 }, "LR-2" },
    { { rta::measure::CrossoverFamily::LinkwitzRiley, 4 }, "LR-4" },
    { { rta::measure::CrossoverFamily::LinkwitzRiley, 8 }, "LR-8" },
    { { rta::measure::CrossoverFamily::Butterworth, 1 }, "BW-1" },
    { { rta::measure::CrossoverFamily::Butterworth, 2 }, "BW-2" },
    { { rta::measure::CrossoverFamily::Butterworth, 3 }, "BW-3" },
    { { rta::measure::CrossoverFamily::Butterworth, 4 }, "BW-4" },
} };

/// Centred at 1 kHz, 6 octaves either side -- outside the plotted 20 Hz-
/// 20 kHz band both ways, so with no crossover frequency asked either, the
/// relative-phase trace covers the whole visible band rather than a fit
/// window centred on a frequency nobody has named. Named explicitly here
/// (docs/dsp/2026-09-06-l7-alignment-wizard.md's amendment, PR #45 round 2)
/// because this is the one number this pane fixes instead of asking: there
/// is no fifth picker for it, only this comment and the amendment's own.
constexpr PhaseWindow kDefaultWindow{ 1000.0, 6.0 };

constexpr int kSummationHeight = 200;

/// One entry in the inversion picker: wizard question (c)
/// (CrossoverTopology.h's own comment on `ProcessorInversion`) -- "has the
/// processor already inverted one output?" -- with all three answers a real
/// operator can give, never just YES/NO. `Unknown` is not a refusal to
/// answer: it is answered, and it means "draw both candidate lines" (Sec.13.3
/// via `CrossoverSurface::targetAmbiguous()`), not "this pane doesn't know".
struct InversionChoice {
    rta::measure::ProcessorInversion inversion;
    const char* label;
};

constexpr std::array<InversionChoice, 3> kInversionChoices{ {
    { rta::measure::ProcessorInversion::No, "NOT INVERTED" },
    { rta::measure::ProcessorInversion::Yes, "INVERTED" },
    { rta::measure::ProcessorInversion::Unknown, "UNKNOWN -- SHOW BOTH" },
} };

}  // namespace

CrossoverPaneView::CrossoverPaneView() {
    hpCombo_.setTextWhenNothingSelected("CHOOSE HIGH-PASS TRACE");
    lpCombo_.setTextWhenNothingSelected("CHOOSE LOW-PASS TRACE");
    topologyCombo_.setTextWhenNothingSelected("CHOOSE TOPOLOGY (NOT INFERRED)");
    inversionCombo_.setTextWhenNothingSelected("WAS THE OUTPUT INVERTED? (NOT INFERRED)");
    hpCombo_.setTextWhenNoChoicesAvailable("NO PHASE-BEARING TRACES STORED");
    lpCombo_.setTextWhenNoChoicesAvailable("NO PHASE-BEARING TRACES STORED");

    // The topology and inversion lists never change -- fixed once, here,
    // unlike the trace combos (rebuilt every refresh from whatever the
    // library currently holds).
    for (std::size_t i = 0; i < kTopologyChoices.size(); ++i) {
        topologyCombo_.addItem(kTopologyChoices[i].label, static_cast<int>(i) + 1);
    }
    for (std::size_t i = 0; i < kInversionChoices.size(); ++i) {
        inversionCombo_.addItem(kInversionChoices[i].label, static_cast<int>(i) + 1);
    }

    hpCombo_.onChange = [this] { hpComboChanged(); };
    lpCombo_.onChange = [this] { lpComboChanged(); };
    topologyCombo_.onChange = [this] { topologyComboChanged(); };
    inversionCombo_.onChange = [this] { inversionComboChanged(); };

    addAndMakeVisible(hpCombo_);
    addAndMakeVisible(lpCombo_);
    addAndMakeVisible(topologyCombo_);
    addAndMakeVisible(inversionCombo_);

    startTimerHz(kTimerHz);
}

CrossoverPaneView::~CrossoverPaneView() { stopTimer(); }

void CrossoverPaneView::setLibrary(const rta::trace::TraceLibrary* library) {
    library_ = library;
    refreshFromLibrary();
    repaint();
}

void CrossoverPaneView::timerCallback() {
    // `sequence` is always 0: this pane has no live snapshot to advance, only
    // the library's own revision.
    const std::uint64_t revision = library_ != nullptr ? library_->revision() : 0;
    if (shouldRepaint(gate_, 0, revision)) {
        refreshFromLibrary();
        repaint();
    }
}

void CrossoverPaneView::rebuildTraceCombo(juce::ComboBox& combo, std::string& selectedId) {
    combo.clear(juce::dontSendNotification);
    int selectedItemId = 0;
    for (std::size_t i = 0; i < eligibleIds_.size(); ++i) {
        const int itemId = static_cast<int>(i) + 1;
        const auto& id = eligibleIds_[i];
        const auto* entry = library_ != nullptr ? library_->entry(id) : nullptr;
        combo.addItem(entry != nullptr ? juce::String(entry->name) : juce::String(id), itemId);
        if (id == selectedId) selectedItemId = itemId;
    }
    // Not found among the current eligible set: the F3 rule -- a picked
    // entry that was removed or hidden since reverts to "not asked", not to
    // whatever now happens to sit at its old index.
    if (selectedItemId == 0) selectedId.clear();
    combo.setSelectedId(selectedItemId, juce::dontSendNotification);
}

void CrossoverPaneView::refreshFromLibrary() {
    eligibleIds_.clear();
    if (library_ != nullptr) {
        for (const auto& entry : library_->entries()) {
            // Hidden traces are excluded OUTRIGHT, not merely marked --
            // the same rule StoredTraceLayer.cpp already applies when
            // deciding what to draw (`if (!entry.visible) continue;`), kept
            // consistent here rather than inventing a second convention for
            // "hidden" in the same app.
            if (!entry.visible) continue;
            const auto* candidateTrace = library_->trace(entry.traceId);
            if (candidateTrace == nullptr || !candidateTrace->has(rta::trace::Field::Phase)) continue;
            eligibleIds_.push_back(entry.traceId);
        }
    }

    rebuildTraceCombo(hpCombo_, hpId_);
    rebuildTraceCombo(lpCombo_, lpId_);

    ready_ = false;
    // MEDIUM A (PR #45 fix round 2): both ids must be chosen AND DISTINCT --
    // without that check, the same trace picked as both HP and LP read as a
    // complete selection and fed `CrossoverSurface` a relative phase of
    // exactly zero everywhere, which looks identical to a correctly aligned
    // pair. No separate "`eligibleIds_.size() >= 2`" clause is needed
    // (round 3, LOW F1, verifier's mutant A2): `hpId_`/`lpId_` can only ever
    // hold a value `rebuildTraceCombo` found IN `eligibleIds_`, so two
    // non-empty, DISTINCT ids already prove at least two eligible entries
    // exist -- a second clause credited with that property was dead code.
    const bool distinctPick = !hpId_.empty() && !lpId_.empty() && hpId_ != lpId_;
    if (library_ != nullptr && distinctPick && topology_.has_value() && inversion_.has_value()) {
        const auto* highTrace = library_->trace(hpId_);
        const auto* lowTrace = library_->trace(lpId_);
        if (highTrace != nullptr && lowTrace != nullptr) {
            auto highVirtual = rta::trace::VirtualTrace::fromTrace(*highTrace);
            auto lowVirtual = rta::trace::VirtualTrace::fromTrace(*lowTrace);
            if (highVirtual.has_value() && lowVirtual.has_value()) {
                // MEDIUM B (PR #45 fix round 2): the inversion picker's real
                // answer, never a hardcoded one -- see this file's header
                // comment and CrossoverTopology.h's `ProcessorInversion`.
                surface_.setAskedTopology(*topology_, *inversion_);
                surface_.setWindow(kDefaultWindow);
                surface_.setSources(std::move(*highVirtual), std::move(*lowVirtual));
                ready_ = true;
            }
        }
    }
    if (!ready_) {
        // Test gap D (PR #45 fix round 2): a stale surface must not survive
        // a selection that dropped out of completeness -- otherwise a hidden
        // trace or a topology change while `!ready_` would leave the last
        // complete surface's chart drawable by a `paint()` that forgot to
        // check `ready_`, which is exactly mutant v1b.
        surface_ = CrossoverSurface{};
    }
}

void CrossoverPaneView::hpComboChanged() {
    const int id = hpCombo_.getSelectedId();
    hpId_ = (id >= 1 && static_cast<std::size_t>(id) <= eligibleIds_.size())
               ? eligibleIds_[static_cast<std::size_t>(id) - 1]
               : std::string();
    refreshFromLibrary();
    repaint();
}

void CrossoverPaneView::lpComboChanged() {
    const int id = lpCombo_.getSelectedId();
    lpId_ = (id >= 1 && static_cast<std::size_t>(id) <= eligibleIds_.size())
               ? eligibleIds_[static_cast<std::size_t>(id) - 1]
               : std::string();
    refreshFromLibrary();
    repaint();
}

void CrossoverPaneView::topologyComboChanged() {
    const int id = topologyCombo_.getSelectedId();
    topology_ = (id >= 1 && static_cast<std::size_t>(id) <= kTopologyChoices.size())
                  ? std::optional<rta::measure::Topology>(kTopologyChoices[static_cast<std::size_t>(id) - 1].topology)
                  : std::nullopt;
    refreshFromLibrary();
    repaint();
}

void CrossoverPaneView::inversionComboChanged() {
    const int id = inversionCombo_.getSelectedId();
    inversion_ = (id >= 1 && static_cast<std::size_t>(id) <= kInversionChoices.size())
                   ? std::optional<rta::measure::ProcessorInversion>(
                         kInversionChoices[static_cast<std::size_t>(id) - 1].inversion)
                   : std::nullopt;
    refreshFromLibrary();
    repaint();
}

juce::String CrossoverPaneView::refusalMessage() const {
    // With fewer than 2 eligible traces, say so with the TRUE eligible
    // count, not the library's total entry count -- an operator staring at
    // "0 STORED" while three magnitude-only captures sit in the library
    // would have no idea phase is what is missing.
    if (eligibleIds_.size() < 2) {
        return "ONLY " + juce::String(eligibleIds_.size())
             + " STORED TRACE(S) HAVE PHASE -- NEED AT LEAST 2 TO ALIGN A CROSSOVER";
    }
    if (hpId_.empty()) return "CHOOSE THE HIGH-PASS TRACE";
    if (lpId_.empty()) return "CHOOSE THE LOW-PASS TRACE";
    // MEDIUM A (PR #45 fix round 2): its own message, distinct from either
    // "choose" message above -- both pickers DO hold a choice, so telling the
    // operator to "choose" one again would read as a stuck UI, not as the
    // real problem (one trace cannot align against itself).
    if (hpId_ == lpId_) return "HIGH-PASS AND LOW-PASS ARE THE SAME TRACE";
    if (!topology_.has_value()) return "CHOOSE THE TOPOLOGY (NOT INFERRED)";
    if (!inversion_.has_value()) return "WAS THE OUTPUT INVERTED? (NOT INFERRED)";
    return {};
}

void CrossoverPaneView::paintRefusal(juce::Graphics& g, juce::Rectangle<int> area) const {
    g.setColour(emptyStateText);
    g.setFont(az::ui::legendFont(az::ui::captionFontSize, true, az::ui::trackingCaption));
    g.drawFittedText(refusalMessage(), area.reduced(az::ui::gap * 2), juce::Justification::centred, 3);
}

void CrossoverPaneView::paint(juce::Graphics& g) {
    g.fillAll(az::ui::background);
    if (!ready_) {
        paintRefusal(g, chartArea_);
        lastPaintDrewChart_ = false;
        return;
    }
    paintCrossoverPhase(g, phaseArea_, surface_);
    paintCrossoverSummation(g, summationArea_, surface_);
    lastPaintDrewChart_ = true;
}

void CrossoverPaneView::resized() {
    auto area = getLocalBounds().reduced(az::ui::gap);

    auto pickerRow = area.removeFromTop(az::ui::fieldHeight);
    const int quarterWidth = (pickerRow.getWidth() - az::ui::gap * 3) / 4;
    hpCombo_.setBounds(pickerRow.removeFromLeft(quarterWidth));
    pickerRow.removeFromLeft(az::ui::gap);
    lpCombo_.setBounds(pickerRow.removeFromLeft(quarterWidth));
    pickerRow.removeFromLeft(az::ui::gap);
    topologyCombo_.setBounds(pickerRow.removeFromLeft(quarterWidth));
    pickerRow.removeFromLeft(az::ui::gap);
    inversionCombo_.setBounds(pickerRow);
    area.removeFromTop(az::ui::gap);

    chartArea_ = area;

    area.removeFromBottom(kFrequencyLabelHeight);
    summationArea_ = area.removeFromBottom(kSummationHeight);
    area.removeFromBottom(az::ui::gap);
    phaseArea_ = area;
}

}  // namespace rta::view
