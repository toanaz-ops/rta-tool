// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Decisions 1-5 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
#include "view/TransferView.h"

#include "measure/PhaseUnwrap.h"
#include "measure/Snapshot.h"
#include "trace/TraceLibrary.h"
#include "view/CoherenceAlpha.h"
#include "view/MeasureColours.h"
#include "view/PhaseDecimator.h"
#include "view/PlotAxes.h"
#include "view/TraceDecimator.h"
#include "view/TraceStroke.h"
#include "view/TransferRibbon.h"

#include <az_ui/az_ui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
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

/// Whole-degree axis labels, one per 90 deg -- copied from
/// TransferFunctionPreview::drawPhaseLabels (project CLAUDE.md's dB-keeps-
/// one-decimal rule is a dB rule, not a "every y axis" rule: "different
/// quantities, different rules -- do not unify them for tidiness").
void drawPhaseLabels(juce::Graphics& g, const PlotGeometry& geometry) {
    g.setFont(az::ui::monoFont(az::ui::tableFontSize));
    g.setColour(rta::view::axisText);
    for (double deg = geometry.dbTop; deg >= geometry.dbBottom - 1e-6; deg -= 90.0) {
        const float y = geometry.yForDb(deg);
        juce::Rectangle<float> cell(geometry.left - static_cast<float>(kLevelLabelWidth), y - 7.0f,
                                    static_cast<float>(kLevelLabelWidth) - 6.0f, 14.0f);
        g.drawText(juce::String(static_cast<int>(std::llround(deg))), cell,
                  juce::Justification::centredRight, false);
    }
}

void drawNoReferenceState(juce::Graphics& g, PaneRect area) {
    g.setColour(rta::view::emptyStateText);
    g.setFont(az::ui::legendFont(az::ui::captionFontSize, true, az::ui::trackingCaption));
    g.drawText("NO REFERENCE CHANNEL",
              juce::Rectangle<int>(area.x, area.y, area.width, area.height),
              juce::Justification::centred, false);
}

/// Record §5a: hiding stored phase traces while unwrapped must never be
/// SILENT -- that is the alpha floor's own rule ("the display must never
/// quietly delete data"), applying with more force to a whole trace than to
/// one dimmed column. One line, top-left of the phase pane's own plot area,
/// same legend style RtaView's "RESOLUTION LIMIT" note uses for the same
/// job: a fact about what is NOT being drawn, stated where the reader is
/// already looking.
void drawStoredPhaseHiddenState(juce::Graphics& g, const PlotGeometry& geometry) {
    g.setColour(rta::view::emptyStateText);
    g.setFont(az::ui::legendFont(az::ui::columnFontSize, true, az::ui::trackingColumn));
    juce::Rectangle<int> line(static_cast<int>(geometry.left) + az::ui::spacing,
                              static_cast<int>(geometry.top) + az::ui::spacing, 400,
                              az::ui::captionHeight);
    g.drawText("STORED PHASE HIDDEN  --  UNWRAPPED", line, juce::Justification::centredLeft, false);
}

/// Which ABSOLUTE pixel column each FFT bin lands in, using the composite's
/// own shared log mapping (`geometry.xForHz`) rather than a second one
/// derived here -- any pane's geometry works for this, because decision 1
/// guarantees every pane's x mapping is identical.
///
/// Absolute, unlike StoredTraceLayer.cpp's `columnsForBins`: that helper
/// subtracts an image origin because it rasterises into a small LOCAL
/// canvas blitted back afterwards. The live overlay drawn here paints
/// straight onto the composite's own Graphics with no local canvas, so a
/// bin's column IS the screen column, and TraceStroke.h's column-as-x
/// contract (`fillColumn` uses the column index as x with no origin term of
/// its own) is satisfied by construction instead of by a coordinate
/// transform.
std::vector<int> absoluteColumnsForBins(const PlotGeometry& geometry, double binHz,
                                        std::size_t bins, int columnCount) {
    std::vector<int> columns(bins, -1);
    for (std::size_t i = 0; i < bins; ++i) {
        const double hz = static_cast<double>(i) * binHz;
        if (hz <= 0.0) continue;
        const double column = std::floor(static_cast<double>(geometry.xForHz(hz)));
        if (column >= 0.0 && column < static_cast<double>(columnCount)) {
            columns[i] = static_cast<int>(column);
        }
    }
    return columns;
}

/// The live phase, continuous (no wrap), and the whole-multiple-of-360 axis
/// that encloses it (decision 4). Empty `unwrappedDeg` means the transfer
/// carried no phase to unwrap.
struct UnwrappedPhase {
    std::vector<float> unwrappedDeg;
    double dbTop = kWrappedPhaseDbTop;
    double dbBottom = kWrappedPhaseDbBottom;
};

UnwrappedPhase computeUnwrappedPhase(const rta::measure::TransferBlock& transfer) {
    UnwrappedPhase result;
    const std::size_t n = transfer.phaseDeg.size();
    if (n == 0) return result;

    constexpr double kDegToRad = std::numbers::pi / 180.0;
    constexpr double kRadToDeg = 180.0 / std::numbers::pi;

    std::vector<float> wrappedRad(n), unwrappedRad(n);
    for (std::size_t i = 0; i < n; ++i) {
        wrappedRad[i] = static_cast<float>(static_cast<double>(transfer.phaseDeg[i]) * kDegToRad);
    }

    // No coherence THRESHOLD is decided here -- every threshold (blanking,
    // gating, a minimum-average) is L5b's (dsp record, "what this record
    // does not decide"). The gated overload is still used when coherence
    // exists, with `minimumCoherence` left at its 0 default -- its own
    // header states 0 disables the gate -- so this call site is already
    // wired for L5b to set a real value later without this file changing.
    const rta::measure::UnwrapOptions options{};
    if (transfer.coherence.has_value() && transfer.coherence->size() == n) {
        rta::measure::unwrapPhase(wrappedRad, *transfer.coherence, unwrappedRad, options);
    } else {
        rta::measure::unwrapPhase(wrappedRad, unwrappedRad, options);
    }

    result.unwrappedDeg.resize(n);
    float lo = 0.0f, hi = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const float deg = static_cast<float>(static_cast<double>(unwrappedRad[i]) * kRadToDeg);
        result.unwrappedDeg[i] = deg;
        if (i == 0 || deg < lo) lo = deg;
        if (i == 0 || deg > hi) hi = deg;
    }

    result.dbTop = std::ceil(hi / 360.0) * 360.0;
    result.dbBottom = std::floor(lo / 360.0) * 360.0;
    // A degenerate (zero-height) enclosure -- e.g. a delay of exactly 0 --
    // still needs a real axis to plot against.
    if (result.dbTop - result.dbBottom < 360.0) result.dbTop += 360.0;
    return result;
}

}  // namespace

TransferView::TransferView(const rta::measure::SnapshotSource& source) : source_(&source) {
    startTimerHz(kTimerHz);
}

TransferView::~TransferView() {
    stopTimer();
}

void TransferView::setSource(const rta::measure::SnapshotSource& source) {
    source_ = &source;
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
    const bool hasTransfer = snapshot != nullptr && snapshot->transfer.has_value();

    UnwrappedPhase unwrapped;
    if (hasTransfer && unwrapped_) unwrapped = computeUnwrappedPhase(*snapshot->transfer);

    const PlotGeometry ribbonGeometry = paneGeometry(axis, panes.ribbon, 1.0, 0.0);
    const PlotGeometry magnitudeGeometry = paneGeometry(axis, panes.magnitude, kMagnitudeDbTop, kMagnitudeDbBottom);
    const PlotGeometry phaseGeometry = paneGeometry(
        axis, panes.phase, unwrapped_ ? unwrapped.dbTop : kWrappedPhaseDbTop,
        unwrapped_ ? unwrapped.dbBottom : kWrappedPhaseDbBottom);

    // Bin -> absolute column mapping, shared by the ribbon and both live
    // traces below (decision 1: one x mapping, read through whichever
    // pane's own geometry -- they all agree by construction).
    const std::size_t bins = hasTransfer ? snapshot->transfer->magnitudeDb.size() : 0;
    const double binHz =
        hasTransfer && snapshot->fftSize > 0 ? snapshot->sampleRate / static_cast<double>(snapshot->fftSize) : 0.0;
    const int columnCount = static_cast<int>(std::ceil(axis.right));
    const std::vector<int> columnForBin =
        bins > 0 ? absoluteColumnsForBins(magnitudeGeometry, binHz, bins, columnCount) : std::vector<int>{};

    // The ONE per-column trust vector every pane in the composite reads --
    // computed here exactly once, not once per pane, because it cannot
    // differ between them: same coherence, same shared columnForBin/
    // columnCount (decision 1). Empty coherence (nothing measured yet) means
    // an empty alpha vector, which the ribbon reads as "no fill" and
    // strokeMagnitudeExtents/strokePhaseColumns read as "draw opaque" --
    // two different, both correct, contracts for the same empty span
    // (TransferRibbon.h and TraceStroke.h each document their own).
    const std::span<const float> coherenceSource =
        hasTransfer && snapshot->transfer->coherence.has_value()
            ? std::span<const float>(*snapshot->transfer->coherence)
            : std::span<const float>{};
    const std::vector<float> liveAlpha =
        coherenceSource.empty() ? std::vector<float>{} : columnAlpha(coherenceSource, columnForBin, columnCount);

    // The ribbon shows the LIVE capture's coherence only (decision 3) -- an
    // empty alpha draws the frame alone, never a solid "fully trusted" fill
    // (TransferRibbon.h's own contract comment).
    drawTransferRibbon(g, ribbonGeometry, panes.ribbon, liveAlpha);

    drawGrid(g, magnitudeGeometry);
    drawLevelLabels(g, magnitudeGeometry);

    drawGrid(g, phaseGeometry);
    drawFrequencyLabels(g, phaseGeometry);
    drawPhaseLabels(g, phaseGeometry);

    if (!hasTransfer) {
        // A Bode plot of nothing looks like a measurement -- draw the grid
        // and stop, never a flat trace.
        drawNoReferenceState(g, panes.magnitude);
        return;
    }

    const auto& transfer = *snapshot->transfer;

    if (library_ != nullptr) storedMagnitude_.draw(g, *library_, magnitudeGeometry);

    {
        const auto extents = bridgeGaps(decimateToColumns(transfer.magnitudeDb, columnForBin, columnCount));
        strokeMagnitudeExtents(g, extents, magnitudeGeometry, 0, liveAlpha, rta::view::trace);
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

    if (!unwrapped_) {
        const auto phaseColumns = decimatePhaseToColumns(transfer.phaseDeg, columnForBin, columnCount);
        const auto drawn = wrapForDrawing(phaseColumns);
        strokePhaseColumns(g, drawn, phaseGeometry, 0, liveAlpha, rta::view::trace);
    } else {
        // Unwrapped: a continuous curve with no discontinuity to draw around,
        // so it is decimated and stroked exactly like magnitude rather than
        // through the wrap-aware path (decision 5 is about the WRAPPED
        // default; there is nothing for it to do once the trace no longer
        // wraps).
        const auto extents = bridgeGaps(decimateToColumns(unwrapped.unwrappedDeg, columnForBin, columnCount));
        strokeMagnitudeExtents(g, extents, phaseGeometry, 0, liveAlpha, rta::view::trace);
    }
}

}  // namespace rta::view
