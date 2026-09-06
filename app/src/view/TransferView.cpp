// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Decisions 1-5 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
#include "view/TransferView.h"

#include "measure/Snapshot.h"
#include "trace/TraceLibrary.h"
#include "view/ColumnMap.h"
#include "view/CoherenceAlpha.h"
#include "view/MeasureColours.h"
#include "view/MtwLayer.h"
#include "view/MtwReadout.h"
#include "view/PhaseDecimator.h"
#include "view/PlotAxes.h"
#include "view/TraceDecimator.h"
#include "view/TraceStroke.h"
#include "view/TransferRibbon.h"
#include "view/TransferSourceToggle.h"
#include "view/TransferViewDraw.h"

#include <az_ui/az_ui.h>

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace rta::view {

namespace {

constexpr int kTimerHz = 20;

/// Fixed magnitude axis, matching TransferFunctionPreview.cpp's own
/// `makeGeometry(row, 18.0, -18.0)`: a transfer function's magnitude is a
/// RELATIVE dB (measurement against reference), not an absolute SPL/dBFS
/// reading, so there is no "0 down to the noise floor" the way RtaView's
/// band bars have -- a symmetric range around 0 dB is the honest default,
/// and matching the mockup's own number is what keeps the picture
/// recognisable (record preamble).
constexpr double kMagnitudeDbTop = 18.0;
constexpr double kMagnitudeDbBottom = -18.0;

/// The default wrapped phase axis (decision 4).
constexpr double kWrappedPhaseDbTop = 180.0;
constexpr double kWrappedPhaseDbBottom = -180.0;

}  // namespace

TransferView::TransferView(const rta::measure::SnapshotSource& source) : source_(&source) {
    startTimerHz(kTimerHz);

    // Built here, in the constructor BODY, not the mem-initialiser list:
    // each toggle's own constructor reads `sources_` back through `*this`
    // (TransferSourceToggle::refresh -> TransferView::source), and the body
    // only starts once every member's own initialiser -- `sources_`
    // included -- has already run, regardless of declaration order.
    for (std::size_t i = 0; i < toggles_.size(); ++i) {
        toggles_[i] = std::make_unique<TransferSourceToggle>(*this, static_cast<TransferPane>(i));
        addAndMakeVisible(*toggles_[i]);
    }
}

TransferView::~TransferView() {
    stopTimer();
}

void TransferView::setSource(const rta::measure::SnapshotSource& source) {
    source_ = &source;
    repaint();
}

void TransferView::setSource(TransferPane pane, TransferSource newSource) {
    sources_[static_cast<std::size_t>(pane)] = newSource;
    repaint();
}

void TransferView::setLibrary(const rta::trace::TraceLibrary* library) {
    library_ = library;
    storedMagnitude_.forget();
    storedPhase_.forget();
    repaint();
}

void TransferView::setPhaseUnwrapped(bool unwrapped) {
    unwrapped_ = unwrapped;
    repaint();
}

void TransferView::timerCallback() {
    const auto snapshot = source_->latest();
    const std::uint64_t sequence = snapshot ? snapshot->sequence : 0;
    const std::uint64_t revision = library_ != nullptr ? library_->revision() : 0;
    if (shouldRepaint(gate_, sequence, revision)) repaint();
}

void TransferView::paint(juce::Graphics& g) {
    renderTo(g, getLocalBounds());
}

void TransferView::resized() {
    panes_ = bodePanes(PaneRect{ 0, 0, getWidth(), getHeight() }, az::ui::gap);

    // Top-right corner of each pane's own rectangle, height trimmed for the
    // ribbon's own 34 px furniture -- see placeInPane's own comment.
    constexpr int kToggleHeight = 18;
    constexpr int kRibbonToggleHeight = 16;
    toggles_[static_cast<std::size_t>(TransferPane::Magnitude)]->placeInPane(panes_.magnitude,
                                                                             kToggleHeight);
    toggles_[static_cast<std::size_t>(TransferPane::Phase)]->placeInPane(panes_.phase, kToggleHeight);
    toggles_[static_cast<std::size_t>(TransferPane::Coherence)]->placeInPane(panes_.ribbon,
                                                                             kRibbonToggleHeight);
}

TransferSourceToggle& TransferView::sourceToggle(TransferPane pane) noexcept {
    return *toggles_[static_cast<std::size_t>(pane)];
}

void TransferView::renderTo(juce::Graphics& g, juce::Rectangle<int> area) const {
    g.setColour(az::ui::background);
    g.fillRect(area);

    const PaneRect content{ area.getX(), area.getY(), area.getWidth(), area.getHeight() };
    const BodePanes panes = bodePanes(content, az::ui::gap);
    // Computed exactly ONCE (decision 1) and fed into all three panes below;
    // no other call to frequencyAxis or a second x mapping appears anywhere
    // in this file.
    const FrequencyAxis axis = frequencyAxis(content);

    const auto snapshot = source_->latest();
    const bool hasFixed = snapshot != nullptr && snapshot->transfer.has_value();
    const bool hasMtw = snapshot != nullptr && snapshot->mtw.has_value();
    const bool hasAny = hasFixed || hasMtw;

    // The preferred per-pane source, falling back to whichever block
    // actually exists (record §6 makes Mtw the DEFAULT PREFERENCE, not a
    // requirement that the fixed engine's own coverage vanish the moment MTW
    // hasn't engaged -- no reference fed yet, or mtwEnabled false). An
    // EXPLICIT setSource is still honoured the instant its own block exists.
    const auto effectiveSource = [&](TransferPane pane) noexcept {
        const TransferSource preferred = source(pane);
        const bool preferredAvailable = preferred == TransferSource::Mtw ? hasMtw : hasFixed;
        if (preferredAvailable) return preferred;
        return hasFixed ? TransferSource::Fixed : TransferSource::Mtw;
    };
    const TransferSource magnitudeSource = effectiveSource(TransferPane::Magnitude);
    const TransferSource phaseSource = effectiveSource(TransferPane::Phase);
    const TransferSource coherenceSource = effectiveSource(TransferPane::Coherence);

    // Unwrap (decision 4) applies only to the FIXED phase curve: an MTW
    // curve is stitched from independent bands' own atan2 results with no
    // single continuous phase to unwrap across a seam.
    const bool drawUnwrappedFixed = hasFixed && unwrapped_ && phaseSource == TransferSource::Fixed;
    UnwrappedPhase unwrapped;
    if (drawUnwrappedFixed) unwrapped = computeUnwrappedPhase(*snapshot->transfer);

    const PlotGeometry ribbonGeometry = paneGeometry(axis, panes.ribbon, 1.0, 0.0);
    const PlotGeometry magnitudeGeometry = paneGeometry(axis, panes.magnitude, kMagnitudeDbTop, kMagnitudeDbBottom);
    const PlotGeometry phaseGeometry = paneGeometry(
        axis, panes.phase, drawUnwrappedFixed ? unwrapped.dbTop : kWrappedPhaseDbTop,
        drawUnwrappedFixed ? unwrapped.dbBottom : kWrappedPhaseDbBottom);

    const std::size_t columnCount = static_cast<std::size_t>(std::ceil(axis.right));
    const int columnCountInt = static_cast<int>(columnCount);

    // Fixed engine's own bin -> absolute column mapping and per-column trust,
    // built once and reused by whichever pane resolves to Fixed (decision 1:
    // one x mapping per engine). Empty coherence (nothing measured yet) means
    // an empty alpha vector -- the ribbon reads that as "no fill" and
    // strokeMagnitudeExtents/strokePhaseColumns read it as "draw opaque"
    // (TransferRibbon.h and TraceStroke.h each document their own contract).
    std::vector<int> fixedColumns;
    std::vector<float> fixedAlpha;
    if (hasFixed) {
        const std::size_t bins = snapshot->transfer->magnitudeDb.size();
        const double binHz = snapshot->fftSize > 0
            ? snapshot->sampleRate / static_cast<double>(snapshot->fftSize) : 0.0;
        fixedColumns = absoluteColumnsForBins(magnitudeGeometry, binHz, bins, columnCount);
        const std::span<const float> coherenceValues =
            snapshot->transfer->coherence.has_value()
                ? std::span<const float>(*snapshot->transfer->coherence)
                : std::span<const float>{};
        fixedAlpha = coherenceValues.empty()
            ? std::vector<float>{}
            : columnAlpha(coherenceValues, fixedColumns, columnCountInt);
    }

    // The MTW engine's own EXPLICIT frequency vector -> column mapping
    // (ColumnMap.h's sibling function), and its per-band-aware trust
    // (MtwLayer.h -- an ungated band reads fully trusted, never the
    // coherence-alpha floor; see that function's own contract comment).
    std::vector<int> mtwColumns;
    std::vector<float> mtwAlpha;
    if (hasMtw) {
        mtwColumns = absoluteColumnsForFrequencies(magnitudeGeometry, snapshot->mtw->frequencyHz, columnCount);
        mtwAlpha = mtwColumnAlpha(*snapshot->mtw, mtwColumns, columnCount);
    }

    // The ribbon shows whichever source Coherence resolved to -- an empty
    // alpha draws the frame alone, never a solid "fully trusted" fill
    // (TransferRibbon.h's own contract comment; mtwAlpha is never empty once
    // hasMtw, by MtwLayer.h's own contract, so this only degrades for Fixed).
    const std::vector<float>& ribbonAlpha =
        coherenceSource == TransferSource::Mtw ? mtwAlpha : fixedAlpha;
    drawTransferRibbon(g, ribbonGeometry, panes.ribbon, ribbonAlpha);

    drawGrid(g, magnitudeGeometry);
    drawLevelLabels(g, magnitudeGeometry);

    drawGrid(g, phaseGeometry);
    drawFrequencyLabels(g, phaseGeometry);
    drawPhaseLabels(g, phaseGeometry);

    if (!hasAny) {
        // A Bode plot of nothing looks like a measurement -- draw the grid
        // and stop, never a flat trace.
        drawNoReferenceState(g, panes.magnitude);
        return;
    }

    // Finding 3 (station-4 fix pass): the coherence pane draws seams too,
    // once the ribbon's own source resolves to Mtw -- record §6's own
    // decision is that seams mark where the window changes, and coherence
    // is the quantity most affected by the window, so it needs the mark
    // most, not least. Placed after the `hasAny` gate, same as the
    // magnitude/phase call sites below: `coherenceSource == Mtw` here
    // implies `hasMtw` by the same fallback logic `effectiveSource` states
    // in its own comment, so `*snapshot->mtw` is never dereferenced empty.
    if (coherenceSource == TransferSource::Mtw) {
        drawMtwSeams(g, *snapshot->mtw, ribbonGeometry, rta::view::mtwSeam);
    }

    // Finding 1 (station-4 fix pass): record §5 requires the per-band
    // integration time on screen "because a coherence trace that fills in
    // from the top over five seconds is otherwise read as a fault" --
    // visible whenever ANY pane's source resolved to Mtw, not only when
    // every pane did, since the strip states a fact about the MTW engine's
    // own layout rather than about which curve happens to be on screen.
    if (magnitudeSource == TransferSource::Mtw || phaseSource == TransferSource::Mtw ||
        coherenceSource == TransferSource::Mtw) {
        drawMtwIntegrationStrip(g, *snapshot->mtw, magnitudeGeometry);
    }

    if (library_ != nullptr) storedMagnitude_.draw(g, *library_, magnitudeGeometry);

    {
        const bool useMtw = magnitudeSource == TransferSource::Mtw;
        const auto& magnitudeDb = useMtw ? snapshot->mtw->magnitudeDb : snapshot->transfer->magnitudeDb;
        const auto& columns = useMtw ? mtwColumns : fixedColumns;
        const auto& alpha = useMtw ? mtwAlpha : fixedAlpha;
        const auto extents = bridgeGaps(decimateToColumns(magnitudeDb, columns, columnCountInt));
        strokeMagnitudeExtents(g, extents, magnitudeGeometry, 0, alpha, rta::view::trace);
        if (useMtw) drawMtwSeams(g, *snapshot->mtw, magnitudeGeometry, rta::view::mtwSeam);
    }

    // Stored traces are always drawn WRAPPED (StoredTraceLayer's own
    // decision-5 cache always ends in `wrapForDrawing`, and that file is not
    // touched by this task). Plotting that cached, wrapped image against an
    // EXTENDED (whole-multiples-of-360) axis would misplace every pixel it
    // contains -- a stored -170 degree reading would land near the axis's
    // new top rather than near its own -170 gridline. Skipping the stored
    // layer in unwrapped mode is what keeps every pixel honest; recalled
    // captures come back the moment the toggle returns to wrapped. Record
    // §5a: the skip must not be SILENT, so the pane says so whenever there is
    // a library that could otherwise have something to hide.
    if (library_ != nullptr) {
        if (!unwrapped_) {
            storedPhase_.draw(g, *library_, phaseGeometry);
        } else {
            drawStoredPhaseHiddenState(g, phaseGeometry);
        }
    }

    if (drawUnwrappedFixed) {
        // Unwrapped: a continuous curve with no discontinuity to draw around,
        // so it is decimated and stroked exactly like magnitude rather than
        // through the wrap-aware path (decision 5 is about the WRAPPED
        // default; there is nothing for it to do once the trace no longer
        // wraps).
        const auto extents = bridgeGaps(decimateToColumns(unwrapped.unwrappedDeg, fixedColumns, columnCountInt));
        strokeMagnitudeExtents(g, extents, phaseGeometry, 0, fixedAlpha, rta::view::trace);
    } else {
        const bool useMtw = phaseSource == TransferSource::Mtw;
        const auto& phaseDeg = useMtw ? snapshot->mtw->phaseDeg : snapshot->transfer->phaseDeg;
        const auto& columns = useMtw ? mtwColumns : fixedColumns;
        const auto& alpha = useMtw ? mtwAlpha : fixedAlpha;
        const auto phaseColumns = decimatePhaseToColumns(phaseDeg, columns, columnCountInt);
        const auto drawn = wrapForDrawing(phaseColumns);
        strokePhaseColumns(g, drawn, phaseGeometry, 0, alpha, rta::view::trace);
        if (useMtw) drawMtwSeams(g, *snapshot->mtw, phaseGeometry, rta::view::mtwSeam);
    }
}

}  // namespace rta::view
