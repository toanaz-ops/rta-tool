// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See EqPaneView.h.
#include "view/EqPaneView.h"

#include "view/EqChartRenderer.h"
#include "view/MeasureColours.h"

#include <az_ui/az_ui.h>

#include <algorithm>
#include <cstdio>

namespace rta::view {
namespace {

constexpr int kControlsWidth = 300;
constexpr int kMaxBands = 16;
constexpr int kButtonHeight = 30;
constexpr int kReadoutHeight = 76;
/// Combo item id for a pick that has left the library ("(removed)").
constexpr int kRemovedItemId = 9999;
constexpr int kFlatItemId = 1;

}  // namespace

EqPaneView::EqPaneView(EqPaneBinding binding) : binding_(std::move(binding)), model_(*binding_.model) {
    jassert(binding_.model != nullptr);

    measurementCombo_.setTextWhenNothingSelected("CHOOSE MEASUREMENT (NOT PRE-SELECTED)");
    measurementCombo_.setTextWhenNoChoicesAvailable("NO TRANSFER TRACE WITH COHERENCE STORED");
    targetCombo_.setTextWhenNothingSelected("TARGET");
    firPhaseCombo_.setTextWhenNothingSelected("FIR PHASE (ASK)");
    firPhaseCombo_.addItem("LINEAR PHASE", 1);
    firPhaseCombo_.addItem("MINIMUM PHASE", 2);
    firTapsCombo_.setTextWhenNothingSelected("FIR LENGTH (ASK)");
    for (int n = 1; n <= kMaxBands; ++n) bandsCombo_.addItem("BANDS: " + juce::String(n), n);

    measurementCombo_.onChange = [this] { measurementChanged(); };
    targetCombo_.onChange = [this] { targetChanged(); };
    bandsCombo_.onChange = [this] {
        model_.setMaxFilters(bandsCombo_.getSelectedId());
        refreshFromModel();
    };
    firPhaseCombo_.onChange = [this] {
        const int id = firPhaseCombo_.getSelectedId();
        model_.setFirPhase(id == 1 ? std::optional<rta::dsp::FirPhase>(rta::dsp::FirPhase::Linear)
                           : id == 2 ? std::optional<rta::dsp::FirPhase>(rta::dsp::FirPhase::Minimum)
                                     : std::nullopt);
        refreshFromModel();
    };
    firTapsCombo_.onChange = [this] {
        const int id = firTapsCombo_.getSelectedId();
        model_.setFirTaps(id >= 1 && id <= static_cast<int>(rta::measure::kEqFirTapChoices.size())
                              ? std::optional<std::size_t>(rta::measure::kEqFirTapChoices[static_cast<std::size_t>(id) - 1])
                              : std::nullopt);
        refreshFromModel();
    };

    autoEqButton_.onClick = [this] { model_.runAutoEq(); refreshFromModel(); };
    suggestButton_.onClick = [this] { model_.suggest(); refreshFromModel(); };
    clearButton_.onClick = [this] { model_.clearFilters(); refreshFromModel(); };
    undeclineButton_.onClick = [this] { model_.clearDeclined(); refreshFromModel(); };
    peakToggle_.onClick = [this] { model_.setPeakNormalised(peakToggle_.getToggleState()); refreshFromModel(); };
    const auto act = [this](const std::function<void()>& action) { if (action) action(); refreshFromModel(); };
    exportFirTextButton_.onClick = [this, act] { act(binding_.actions.exportFirText); };
    exportFirWavButton_.onClick = [this, act] { act(binding_.actions.exportFirWav); };
    exportListButton_.onClick = [this, act] { act(binding_.actions.exportList); };

    autoEqButton_.getProperties().set(az::ui::hintProperty, "one shot: place, then solve the gains");
    suggestButton_.getProperties().set(az::ui::hintProperty, "top 3 one-step candidates");
    peakToggle_.getProperties().set(az::ui::hintProperty, "scale the taps so the largest is 0 dBFS; the text header states the trim");

    filterPanel_.onAccept = [this](std::size_t i) { model_.acceptChip(i); refreshFromModel(); };
    filterPanel_.onDecline = [this](std::size_t i) { model_.declineChip(i); refreshFromModel(); };
    filterPanel_.onApplied = [this](std::size_t i, bool applied) { model_.setApplied(i, applied); refreshFromModel(); };

    readout_.setJustificationType(juce::Justification::topLeft);
    readout_.setFont(az::ui::monoFont(az::ui::readoutFontSize));
    for (juce::Component* c : std::initializer_list<juce::Component*>{
             &measurementCombo_, &targetCombo_, &bandsCombo_, &firPhaseCombo_, &firTapsCombo_, &autoEqButton_,
             &suggestButton_, &clearButton_, &undeclineButton_, &peakToggle_, &exportFirTextButton_,
             &exportFirWavButton_, &exportListButton_, &readout_, &filterPanel_}) {
        addAndMakeVisible(*c);
    }
    refreshFromModel();
    startTimerHz(10);  // library edits and controller status arrive from outside this pane
}

EqPaneView::~EqPaneView() { stopTimer(); }

void EqPaneView::setLibrary(const rta::trace::TraceLibrary* library) {
    library_ = library;
    if (library_ != nullptr) model_.syncLibrary(*library_);
    rebuildPickers();
    refreshFromModel();
}

void EqPaneView::timerCallback() {
    const std::uint64_t revision = library_ != nullptr ? library_->revision() : 0;
    if (shouldRepaint(gate_, 0, revision)) {
        if (library_ != nullptr) model_.syncLibrary(*library_);
        rebuildPickers();
    }
    if (model_.revision() != lastModelRevision_) refreshFromModel();
}

void EqPaneView::rebuildPickers() {
    measurementIds_.clear();
    targetIds_.clear();
    if (library_ != nullptr) {
        measurementIds_ = rta::measure::EqPaneModel::measurementCandidateIds(*library_);
        targetIds_ = rta::measure::EqPaneModel::targetCandidateIds(*library_);
    }
    const auto fill = [this](juce::ComboBox& combo, const std::vector<std::string>& ids, bool withFlat,
                             const rta::measure::EqTracePick& pick) {
        combo.clear(juce::dontSendNotification);
        // The target combo's first item is FLAT (D3's default); traces follow.
        // The measurement combo has no such item and starts "not asked" (D2).
        const int firstTraceItem = withFlat ? kFlatItemId + 1 : 1;
        int selected = withFlat && pick.id.empty() ? kFlatItemId : 0;
        if (withFlat) combo.addItem("FLAT (0 dB + auto-offset)", kFlatItemId);
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const auto* entry = library_ != nullptr ? library_->entry(ids[i]) : nullptr;
            const int itemId = firstTraceItem + static_cast<int>(i);
            combo.addItem(entry != nullptr ? juce::String(entry->name) : juce::String(ids[i]), itemId);
            if (ids[i] == pick.id) selected = itemId;
        }
        if (!pick.id.empty() && selected == 0) {  // the model keeps its own copy; the label says so
            combo.addItem(juce::String(pick.name) + " (removed)", kRemovedItemId);
            selected = kRemovedItemId;
        }
        combo.setSelectedId(selected, juce::dontSendNotification);
    };
    fill(measurementCombo_, measurementIds_, false, model_.measurement());
    fill(targetCombo_, targetIds_, true, model_.target());
}

void EqPaneView::measurementChanged() {
    const int id = measurementCombo_.getSelectedId();
    if (library_ != nullptr && id >= 1 && static_cast<std::size_t>(id) <= measurementIds_.size()) {
        model_.selectMeasurement(*library_, measurementIds_[static_cast<std::size_t>(id) - 1]);
    }
    rebuildPickers();  // a refused pick reverts to what the model still holds
    refreshFromModel();
}

void EqPaneView::targetChanged() {
    const int id = targetCombo_.getSelectedId();
    if (library_ != nullptr && id == kFlatItemId) {
        model_.selectTarget(*library_, "");
    } else if (library_ != nullptr && id > kFlatItemId && static_cast<std::size_t>(id) - 2 < targetIds_.size()) {
        model_.selectTarget(*library_, targetIds_[static_cast<std::size_t>(id) - 2]);
    }
    rebuildPickers();
    refreshFromModel();
}

void EqPaneView::refreshFromModel() {
    lastModelRevision_ = model_.revision();
    const bool measured = model_.hasMeasurement();

    bandsCombo_.setSelectedId(model_.session().config().maxFilters, juce::dontSendNotification);
    firPhaseCombo_.setSelectedId(
        !model_.firPhase() ? 0 : *model_.firPhase() == rta::dsp::FirPhase::Linear ? 1 : 2, juce::dontSendNotification);
    firTapsCombo_.clear(juce::dontSendNotification);
    int tapsItem = 0;
    for (std::size_t i = 0; i < rta::measure::kEqFirTapChoices.size(); ++i) {
        const std::size_t taps = rta::measure::kEqFirTapChoices[i];
        firTapsCombo_.addItem(rta::measure::eqFirTapsLabel(taps, measured ? model_.sampleRate() : 48000.0),
                              static_cast<int>(i) + 1);
        if (model_.firTaps() && *model_.firTaps() == taps) tapsItem = static_cast<int>(i) + 1;
    }
    firTapsCombo_.setSelectedId(tapsItem, juce::dontSendNotification);
    peakToggle_.setToggleState(model_.normalization() == rta::firexport::Normalization::Peak0dBFS,
                               juce::dontSendNotification);

    autoEqButton_.setEnabled(measured);
    suggestButton_.setEnabled(measured);
    clearButton_.setEnabled(!model_.session().committed().empty());
    exportFirTextButton_.setEnabled(model_.canExportFir());
    exportFirWavButton_.setEnabled(model_.canExportFir());
    exportListButton_.setEnabled(model_.canExportList());
    filterPanel_.refresh(model_);

    hz_.clear();
    ghostDb_.clear();
    targetLineDb_.clear();
    correctionDb_.clear();
    juce::String text = juce::String(model_.status());
    if (measured) {
        const auto hz = model_.session().hz();
        hz_.assign(hz.begin(), hz.end());
        ghostDb_ = model_.session().ghostDb();
        targetLineDb_ = model_.targetLineDb();
        correctionDb_ = model_.correctionDb();
        const juce::String targetName =
            model_.target().id.empty() ? juce::String("FLAT") : juce::String(model_.target().name);
        text += "\n" + juce::String(model_.trustReadout()) + " -- " + juce::String(model_.gateReadout());
        text += "\nTARGET " + targetName + " + c (c = " + juce::String(model_.session().levelOffsetDb(), 1) + " dB)";
        text += "\n" + juce::String(model_.limitsReadout());
    }
    readout_.setText(text, juce::dontSendNotification);
    repaint();
}

juce::String EqPaneView::refusalMessage() const {
    if (measurementIds_.empty()) {
        return "NO MEASUREMENT TO EQUALISE -- STORE on TRANSFER with a reference, then pick it here";
    }
    return "CHOOSE A MEASUREMENT -- a stored TRANSFER trace, carrying coherence";
}

void EqPaneView::paint(juce::Graphics& g) {
    g.fillAll(az::ui::background);
    lastPaintDrewChart_ = false;
    if (!model_.hasMeasurement()) {
        g.setColour(emptyStateText);
        g.setFont(az::ui::legendFont(az::ui::captionFontSize, true, az::ui::trackingCaption));
        g.drawFittedText(refusalMessage(), chartArea_.reduced(az::ui::gap * 2), juce::Justification::centred, 3);
        return;
    }
    EqChartData data;
    data.hz = hz_;
    data.measuredDb = model_.measuredDb();
    data.ghostDb = ghostDb_;
    data.targetLineDb = targetLineDb_;
    data.correctionDb = correctionDb_;
    data.trusted = model_.session().trusted();
    data.excluded = model_.session().excluded();
    data.sampleRate = model_.sampleRate();
    paintEqChart(g, chartArea_, data);
    lastPaintDrewChart_ = true;
}

void EqPaneView::resized() {
    auto area = getLocalBounds().reduced(az::ui::gap);
    auto controls = area.removeFromLeft(std::min(kControlsWidth, area.getWidth() / 2));
    area.removeFromLeft(az::ui::gap);
    chartArea_ = area;

    const auto row = [&controls](int height) {
        auto r = controls.removeFromTop(height);
        controls.removeFromTop(4);
        return r;
    };
    const auto halves = [](juce::Rectangle<int> r, juce::Component& left, juce::Component& right) {
        const int w = (r.getWidth() - 4) / 2;
        left.setBounds(r.removeFromLeft(w));
        r.removeFromLeft(4);
        right.setBounds(r);
    };
    measurementCombo_.setBounds(row(az::ui::fieldHeight));
    targetCombo_.setBounds(row(az::ui::fieldHeight));
    bandsCombo_.setBounds(row(az::ui::fieldHeight));
    halves(row(kButtonHeight), autoEqButton_, suggestButton_);

    // The bottom stack is fixed; the chips and list take what is left.
    auto bottom = controls.removeFromBottom(kReadoutHeight);
    readout_.setBounds(bottom);
    controls.removeFromBottom(4);
    const auto stack = [&controls](int height) {
        auto r = controls.removeFromBottom(height);
        controls.removeFromBottom(4);
        return r;
    };
    exportListButton_.setBounds(stack(kButtonHeight));
    halves(stack(kButtonHeight), exportFirTextButton_, exportFirWavButton_);
    peakToggle_.setBounds(stack(az::ui::fieldHeight));
    halves(stack(az::ui::fieldHeight), firPhaseCombo_, firTapsCombo_);
    halves(stack(az::ui::fieldHeight), clearButton_, undeclineButton_);
    filterPanel_.setBounds(controls);
}

}  // namespace rta::view
