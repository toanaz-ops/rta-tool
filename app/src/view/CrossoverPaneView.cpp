// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See CrossoverPaneView.h.
#include "view/CrossoverPaneView.h"

#include "measure/CrossoverTopology.h"
#include "trace/Trace.h"
#include "trace/TraceLibrary.h"
#include "trace/VirtualTrace.h"
#include "view/AxisMetrics.h"
#include "view/CrossoverSurfaceRenderer.h"
#include "view/MeasureColours.h"

#include <az_ui/az_ui.h>

namespace rta::view {
namespace {

/// Library edits (add/rename/hide/reorder a stored trace) are an operator
/// action, not a 20 Hz measurement stream -- RtaView/TransferView poll their
/// bus at that rate because a live snapshot can change every publish; this
/// pane has no bus behind it at all, only `TraceLibrary::revision()`, so a
/// slower poll costs nothing a person could perceive as lag.
constexpr int kTimerHz = 10;

/// The fixed default this pane draws until `AlignmentWizard` (a separate
/// lane, task brief) is wired to ask for a real answer. Linkwitz-Riley
/// order 4 is the topology a live-sound processor ships with most often
/// (docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.3's own citation of Rane
/// Note 160) -- a defensible DEFAULT for a pane with no picker yet, and the
/// corner chip (see CrossoverSurfaceRenderer's header comment on what stays
/// with the caller) says "DEFAULT", never "asked", so nobody mistakes it for
/// an answer the operator gave.
constexpr rta::measure::Topology kDefaultTopology{ rta::measure::CrossoverFamily::LinkwitzRiley,
                                                   4 };
constexpr rta::measure::ProcessorInversion kDefaultInversion = rta::measure::ProcessorInversion::No;

/// Centred at 1 kHz, 6 octaves either side: 1000 * 2^-6 ~= 15.6 Hz and
/// 1000 * 2^6 = 64 kHz, both outside the plotted 20 Hz-20 kHz band
/// (CrossoverSurfaceRenderer's own geometry). With no crossover frequency
/// asked yet either, this makes `relativePhase()` cover the WHOLE visible
/// band rather than a fit window centred on a frequency nobody has named --
/// the same "cover everything until told otherwise" reasoning the topology
/// default above gives.
constexpr PhaseWindow kDefaultWindow{ 1000.0, 6.0 };

constexpr int kSummationHeight = 200;

}  // namespace

CrossoverPaneView::CrossoverPaneView() {
    surface_.setAskedTopology(kDefaultTopology, kDefaultInversion);
    surface_.setWindow(kDefaultWindow);
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
    // the library's own revision (RepaintGate.h's two-part gate collapses to
    // "did revision change" when the other half never moves, the same
    // comment RtaView.cpp gives for the no-library case in reverse).
    const std::uint64_t revision = library_ != nullptr ? library_->revision() : 0;
    if (shouldRepaint(gate_, 0, revision)) {
        refreshFromLibrary();
        repaint();
    }
}

void CrossoverPaneView::refreshFromLibrary() {
    hasSources_ = false;
    highId_.clear();
    lowId_.clear();
    if (library_ == nullptr) return;

    const auto entries = library_->entries();
    if (entries.size() < 2) return;

    const auto* highTrace = library_->trace(entries[0].traceId);
    const auto* lowTrace = library_->trace(entries[1].traceId);
    if (highTrace == nullptr || lowTrace == nullptr) return;

    // fromTrace refuses a magnitude-only capture (no phase to build a
    // relative-phase series from -- VirtualTrace.h's own class comment): such
    // a pair stays in the empty state rather than drawing a half-built
    // surface with a flat, meaningless phase trace.
    auto highVirtual = rta::trace::VirtualTrace::fromTrace(*highTrace);
    auto lowVirtual = rta::trace::VirtualTrace::fromTrace(*lowTrace);
    if (!highVirtual.has_value() || !lowVirtual.has_value()) return;

    highId_ = entries[0].traceId;
    lowId_ = entries[1].traceId;
    surface_.setSources(std::move(*highVirtual), std::move(*lowVirtual));
    hasSources_ = true;
}

void CrossoverPaneView::paint(juce::Graphics& g) {
    g.fillAll(az::ui::background);
    if (!hasSources_) {
        paintEmptyState(g, getLocalBounds());
        return;
    }
    paintCrossoverPhase(g, phaseArea_, surface_);
    paintCrossoverSummation(g, summationArea_, surface_);

    // A COPY of phaseArea_, so removeFromTop mutates the local rectangle,
    // never the stored layout member -- phaseArea_ itself must stay exactly
    // what resized() last computed for the next repaint.
    auto labelArea = phaseArea_;
    g.setColour(az::ui::faded);
    g.setFont(az::ui::monoFont(az::ui::tableFontSize));
    g.drawText("LR4 DEFAULT -- NOT ASKED  --  " + juce::String(highId_) + " / "
                   + juce::String(lowId_),
               labelArea.removeFromTop(az::ui::fieldHeight), juce::Justification::topLeft, false);
}

void CrossoverPaneView::paintEmptyState(juce::Graphics& g, juce::Rectangle<int> area) const {
    const int have = library_ != nullptr ? static_cast<int>(library_->entries().size()) : 0;
    g.setColour(emptyStateText);
    g.setFont(az::ui::legendFont(az::ui::captionFontSize, true, az::ui::trackingCaption));
    const juce::String message = "CROSSOVER ALIGNMENT NEEDS TWO STORED TRACES ("
                                  + juce::String(have) + " STORED) -- CAPTURE THE HIGH-PASS AND "
                                  "LOW-PASS SIDES INTO THE LIBRARY";
    g.drawFittedText(message, area.reduced(az::ui::gap * 2), juce::Justification::centred, 4);
}

void CrossoverPaneView::resized() {
    auto area = getLocalBounds().reduced(az::ui::gap);
    area.removeFromBottom(kFrequencyLabelHeight);
    summationArea_ = area.removeFromBottom(kSummationHeight);
    area.removeFromBottom(az::ui::gap);
    phaseArea_ = area;
}

}  // namespace rta::view
