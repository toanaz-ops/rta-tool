// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See AlignPaneView.h.
#include "view/AlignPaneView.h"

#include "measure/AlignReadout.h"
#include "trace/VirtualTrace.h"
#include "view/AxisMetrics.h"
#include "view/CrossoverSurfaceRenderer.h"
#include "view/MeasureColours.h"
#include "view/TopologyChoices.h"

#include <az_ui/az_ui.h>

#include <algorithm>
#include <array>
#include <vector>

namespace rta::view {
namespace {

constexpr int kTimerHz = 10;
constexpr int kButtonHeight = 30;
constexpr int kStatusHeight = 20;
constexpr int kBlockerHeight = 20;
constexpr int kReadoutHeight = 112;
constexpr int kSummationHeight = 180;
constexpr int kMaxListedOutputs = 16;

/// Typical crossover frequencies the SEED question offers. "NO SEED" is item
/// 1; the first crossing the gated bins show is then used (record Sec.2).
constexpr std::array<double, 12> kSeedChoicesHz{ 60.0, 80.0, 100.0, 120.0, 150.0, 200.0,
                                                  300.0, 500.0, 1000.0, 2000.0, 3000.0, 5000.0 };

juce::String fromUtf8(const std::string& text) { return juce::String(juce::CharPointer_UTF8(text.c_str())); }

}  // namespace

AlignPaneView::AlignPaneView(AlignPaneBinding binding) : binding_(binding) {
    jassert(binding_.runner != nullptr);

    mainCombo_.setTextWhenNothingSelected("MAIN OUTPUT");
    subCombo_.setTextWhenNothingSelected("SUB OUTPUT");
    highPassCombo_.setTextWhenNothingSelected("WHICH SIDE IS HIGH-PASS? (NOT INFERRED)");
    highPassCombo_.addItem("MAIN IS HIGH-PASS", 1);
    highPassCombo_.addItem("SUB IS HIGH-PASS", 2);
    topologyCombo_.setTextWhenNothingSelected("CHOOSE TOPOLOGY (NOT INFERRED)");
    inversionCombo_.setTextWhenNothingSelected("WAS THE OUTPUT INVERTED? (NOT INFERRED)");
    seedCombo_.setTextWhenNothingSelected("CROSSOVER SEED? (NOT ASKED)");
    for (std::size_t i = 0; i < kTopologyChoices.size(); ++i) {
        topologyCombo_.addItem(kTopologyChoices[i].label, static_cast<int>(i) + 1);
    }
    for (std::size_t i = 0; i < kInversionChoices.size(); ++i) {
        inversionCombo_.addItem(kInversionChoices[i].label, static_cast<int>(i) + 1);
    }
    seedCombo_.addItem("NO SEED (FIRST CROSSING)", 1);
    for (std::size_t i = 0; i < kSeedChoicesHz.size(); ++i) {
        seedCombo_.addItem("SEED " + juce::String(static_cast<int>(kSeedChoicesHz[i])) + " HZ",
                           static_cast<int>(i) + 2);
    }

    for (auto* combo : { &mainCombo_, &subCombo_, &highPassCombo_, &topologyCombo_, &inversionCombo_,
                         &seedCombo_ }) {
        combo->onChange = [this] { answersChanged(); };
        addAndMakeVisible(*combo);
    }

    measureButton_.getProperties().set(az::ui::hintProperty,
                                       "pink noise on the high-pass side only, then the low-pass side only");
    sumButton_.getProperties().set(az::ui::hintProperty,
                                   "both outputs together: set the delay in the rack first, then measure the sum");
    measureButton_.onClick = [this] {
        binding_.runner->press();
        refresh();
    };
    sumButton_.onClick = [this] {
        binding_.runner->pressSum();
        refresh();
    };
    offsetButton_.onClick = [this] {
        binding_.runner->answerCycleMs(offsetEditor_.getText().getDoubleValue());
        refresh();
    };
    offsetEditor_.setTextToShowWhenEmpty("coarse offset, ms", az::ui::faded);
    offsetEditor_.setInputRestrictions(12, "0123456789.-");

    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    blockerLabel_.setJustificationType(juce::Justification::centredLeft);
    blockerLabel_.setColour(juce::Label::textColourId, az::ui::faded);
    readoutLabel_.setJustificationType(juce::Justification::topLeft);
    readoutLabel_.setFont(az::ui::monoFont(az::ui::readoutFontSize));

    for (juce::Component* c : std::initializer_list<juce::Component*>{
             &measureButton_, &sumButton_, &offsetButton_, &offsetEditor_, &statusLabel_, &blockerLabel_,
             &readoutLabel_}) {
        addAndMakeVisible(*c);
    }
    restoreAnswers();
    refresh();
    startTimerHz(kTimerHz);  // device state and run progress arrive from outside this pane
}

AlignPaneView::~AlignPaneView() { stopTimer(); }

void AlignPaneView::setLibrary(const rta::trace::TraceLibrary*) {}

void AlignPaneView::timerCallback() {
    const juce::String blocker = fromUtf8(binding_.runner->blocker().text);
    if (binding_.runner->revision() != lastRevision_ || blocker != lastBlockerText_ ||
        binding_.runner->outputChannels() != lastOutputCount_) {
        refresh();
    }
}

void AlignPaneView::rebuildOutputCombos() {
    const int count = std::min(binding_.runner->outputChannels(), kMaxListedOutputs);
    lastOutputCount_ = binding_.runner->outputChannels();
    const auto& answers = binding_.runner->answers();
    const auto fill = [count](juce::ComboBox& combo, const char* prefix, int selectedChannel) {
        combo.clear(juce::dontSendNotification);
        for (int ch = 0; ch < count; ++ch) {
            combo.addItem(juce::String(prefix) + " OUT " + juce::String(ch + 1), ch + 1);
        }
        combo.setSelectedId(selectedChannel < count ? selectedChannel + 1 : 0, juce::dontSendNotification);
    };
    fill(mainCombo_, "MAIN", answers.mainOutput);
    fill(subCombo_, "SUB", answers.subOutput);
}

void AlignPaneView::restoreAnswers() {
    const auto& a = binding_.runner->answers();
    const auto none = juce::dontSendNotification;
    highPassCombo_.setSelectedId(a.highPass.has_value() ? (*a.highPass == rta::measure::Source::Main ? 1 : 2) : 0, none);
    int topologyId = 0;
    for (std::size_t i = 0; i < kTopologyChoices.size() && a.topology.has_value(); ++i) {
        const auto& t = kTopologyChoices[i].topology;
        if (t.family == a.topology->family && t.order == a.topology->order) topologyId = static_cast<int>(i) + 1;
    }
    topologyCombo_.setSelectedId(topologyId, none);
    int inversionId = 0;
    for (std::size_t i = 0; i < kInversionChoices.size() && a.inversion.has_value(); ++i) {
        if (kInversionChoices[i].inversion == *a.inversion) inversionId = static_cast<int>(i) + 1;
    }
    inversionCombo_.setSelectedId(inversionId, none);
    int seedId = a.seedAnswered ? 1 : 0;
    for (std::size_t i = 0; i < kSeedChoicesHz.size() && a.seedHz.has_value(); ++i) {
        if (kSeedChoicesHz[i] == *a.seedHz) seedId = static_cast<int>(i) + 2;
    }
    seedCombo_.setSelectedId(seedId, none);
}

void AlignPaneView::answersChanged() {
    auto& a = binding_.runner->answers();
    if (mainCombo_.getSelectedId() > 0) a.mainOutput = mainCombo_.getSelectedId() - 1;
    if (subCombo_.getSelectedId() > 0) a.subOutput = subCombo_.getSelectedId() - 1;

    const int side = highPassCombo_.getSelectedId();
    a.highPass = side == 1 ? std::optional<rta::measure::Source>(rta::measure::Source::Main)
                 : side == 2 ? std::optional<rta::measure::Source>(rta::measure::Source::Sub)
                             : std::nullopt;
    const int topology = topologyCombo_.getSelectedId();
    a.topology = (topology >= 1 && static_cast<std::size_t>(topology) <= kTopologyChoices.size())
                     ? std::optional<rta::measure::Topology>(kTopologyChoices[static_cast<std::size_t>(topology) - 1].topology)
                     : std::nullopt;
    const int inversion = inversionCombo_.getSelectedId();
    a.inversion = (inversion >= 1 && static_cast<std::size_t>(inversion) <= kInversionChoices.size())
                      ? std::optional<rta::measure::ProcessorInversion>(
                            kInversionChoices[static_cast<std::size_t>(inversion) - 1].inversion)
                      : std::nullopt;
    const int seed = seedCombo_.getSelectedId();
    a.seedAnswered = seed >= 1;
    a.seedHz = (seed >= 2 && static_cast<std::size_t>(seed) - 2 < kSeedChoicesHz.size())
                   ? std::optional<double>(kSeedChoicesHz[static_cast<std::size_t>(seed) - 2])
                   : std::nullopt;
    refresh();
}

void AlignPaneView::rebuildSurface() {
    ready_ = false;
    surface_ = CrossoverSurface{};
    const auto& runner = *binding_.runner;
    const auto& run = runner.runAnswers();
    const auto& verdict = runner.verdict();
    const auto* mainTrace = runner.mainCapture();
    const auto* subTrace = runner.subCapture();
    if (!verdict.has_value() || mainTrace == nullptr || subTrace == nullptr || !run.complete()) return;

    const bool mainIsHigh = *run.highPass == rta::measure::Source::Main;
    auto highSide = rta::trace::VirtualTrace::fromTrace(mainIsHigh ? *mainTrace : *subTrace);
    auto lowSide = rta::trace::VirtualTrace::fromTrace(mainIsHigh ? *subTrace : *mainTrace);
    if (!highSide.has_value() || !lowSide.has_value()) return;

    surface_.setAskedTopology(*run.topology, *run.inversion);
    surface_.setWindow(PhaseWindow{ verdict->crossoverHz > 0.0 ? verdict->crossoverHz : 1000.0, 1.0 });
    surface_.setSources(std::move(*highSide), std::move(*lowSide));

    // The suggested delay as a pending G11 op on the EARLIER source: positive
    // tau means the low-pass side arrives later, so the high-pass side is
    // delayed (the same sentence AlignReadout prints).
    if (runner.cycleSamples().has_value()) {
        const double tau =
            rta::measure::resolveCycle(*verdict, runner.cycleSamples(), runner.sampleRate()).tauSeconds;
        std::vector<rta::trace::VirtualOp> highChain, lowChain;
        if (tau > 0.0) highChain.push_back(rta::trace::DelayOp{ tau });
        if (tau < 0.0) lowChain.push_back(rta::trace::DelayOp{ -tau });
        surface_.setPendingOps(std::move(highChain), std::move(lowChain));
    }
    if (const auto* sum = runner.measuredSum()) {
        const auto magnitude = sum->field(rta::trace::Field::Magnitude);
        surface_.setMeasuredSum(std::vector<float>(magnitude.begin(), magnitude.end()));
    }
    ready_ = true;
}

void AlignPaneView::refresh() {
    auto& runner = *binding_.runner;
    lastRevision_ = runner.revision();
    if (runner.outputChannels() != lastOutputCount_) rebuildOutputCombos();

    const auto blocker = runner.blocker();
    lastBlockerText_ = fromUtf8(blocker.text);
    const bool busy = runner.busy();
    const auto step = runner.step();
    measureButton_.setEnabled(blocker.kind == rta::measure::AlignBlock::None);
    sumButton_.setEnabled(!busy && (step == rta::measure::WizardStep::Computed ||
                                    step == rta::measure::WizardStep::AskingPolarity));
    const bool askable = !busy && runner.verdict().has_value() &&
                         (step == rta::measure::WizardStep::AskingCycle || step == rta::measure::WizardStep::Computed);
    offsetButton_.setEnabled(askable);
    offsetEditor_.setEnabled(askable);

    statusLabel_.setText(fromUtf8(runner.status()), juce::dontSendNotification);
    blockerLabel_.setText(busy || blocker.kind == rta::measure::AlignBlock::None
                              ? juce::String()
                              : "MEASURE unavailable: " + lastBlockerText_,
                          juce::dontSendNotification);

    const auto& run = runner.runAnswers();
    juce::String readout;
    if (runner.verdict().has_value() && run.complete()) {
        rta::measure::AlignReadoutInput input;
        input.verdict = &*runner.verdict();
        input.highPass = *run.highPass;
        input.topology = *run.topology;
        input.inversion = *run.inversion;
        input.sampleRate = runner.sampleRate();
        input.cycleSamples = runner.cycleSamples();
        input.matchedPairBar = rta::measure::kAlignMatchedPairAgreement;
        std::string joined;
        for (const auto& line : rta::measure::verdictLines(input)) joined += line + "\n";
        readout = fromUtf8(joined);
    }
    readoutLabel_.setText(readout, juce::dontSendNotification);

    rebuildSurface();
    repaint();
}

void AlignPaneView::paint(juce::Graphics& g) {
    g.fillAll(az::ui::background);
    if (!ready_) {
        g.setColour(emptyStateText);
        g.setFont(az::ui::legendFont(az::ui::captionFontSize, true, az::ui::trackingCaption));
        g.drawFittedText("MEASURE TO SEE THE ALIGNMENT SURFACE", chartArea_.reduced(az::ui::gap * 2),
                         juce::Justification::centred, 2);
        lastPaintDrewChart_ = false;
        return;
    }
    paintCrossoverPhase(g, phaseArea_, surface_);
    paintCrossoverSummation(g, summationArea_, surface_);
    lastPaintDrewChart_ = true;
}

void AlignPaneView::resized() {
    auto area = getLocalBounds().reduced(az::ui::gap);
    const auto row = [&area](std::initializer_list<juce::Component*> parts, int height) {
        auto strip = area.removeFromTop(height);
        const int count = static_cast<int>(parts.size());
        const int width = (strip.getWidth() - az::ui::gap * (count - 1)) / count;
        int index = 0;
        for (auto* part : parts) {
            part->setBounds(++index == count ? strip : strip.removeFromLeft(width));
            strip.removeFromLeft(az::ui::gap);
        }
        area.removeFromTop(az::ui::gap);
    };
    row({ &mainCombo_, &subCombo_, &highPassCombo_ }, az::ui::fieldHeight);
    row({ &topologyCombo_, &inversionCombo_, &seedCombo_ }, az::ui::fieldHeight);

    auto buttons = area.removeFromTop(kButtonHeight);
    measureButton_.setBounds(buttons.removeFromLeft(140));
    buttons.removeFromLeft(az::ui::gap);
    sumButton_.setBounds(buttons.removeFromLeft(100));
    buttons.removeFromLeft(az::ui::gap * 3);
    offsetEditor_.setBounds(buttons.removeFromLeft(150));
    buttons.removeFromLeft(az::ui::gap);
    offsetButton_.setBounds(buttons.removeFromLeft(140));
    area.removeFromTop(az::ui::gap);

    statusLabel_.setBounds(area.removeFromTop(kStatusHeight));
    blockerLabel_.setBounds(area.removeFromTop(kBlockerHeight));
    readoutLabel_.setBounds(area.removeFromTop(kReadoutHeight));
    area.removeFromTop(az::ui::gap);

    chartArea_ = area;
    area.removeFromBottom(kFrequencyLabelHeight);
    summationArea_ = area.removeFromBottom(kSummationHeight);
    area.removeFromBottom(az::ui::gap);
    phaseArea_ = area;
}

}  // namespace rta::view
