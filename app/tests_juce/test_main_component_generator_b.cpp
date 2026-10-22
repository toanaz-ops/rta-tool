// SPDX-License-Identifier: AGPL-3.0-or-later
// H7 tasks T6/T7 (docs/plans/2026-10-21-h7-generator-output-plan.md, wave B1):
// signal, frequency and output choice on the masthead generator, against a REAL
// MainComponent in SYNTHETIC mode. What the generator plays is read off the real
// analysis chain -- the RTA spectrum's peak bin, the TRANSFER magnitude of the
// loopback (out 1 = 4-sample delay + 40 Hz-16 kHz band-pass, out 2 = LR4 100 Hz
// + 100 samples) -- not off the controller's own settings. The 2 Hz timer is
// not pumped offscreen; `tickUntil` drives it by hand, bounded.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

namespace {

using Access = MainComponentTestAccess;
using rta::measure::GeneratorSignal;
using rta::measure::GeneratorState;
constexpr const char* kTag = "[main_component_generator_b]";

template <typename Predicate>
bool tickUntil(MainComponent& c, Predicate&& done, int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        Access::timerTickForTest(c);
        if (done()) return true;
        juce::Thread::sleep(5);
    }
    Access::timerTickForTest(c);
    return done();
}

/// Lets the analyser's exponential averaging (tau 0.5 s) converge: 8 time constants leave
/// under 0.04 % of the old level, i.e. well under the 0.1 dB the cases below ask for.
void pumpFor(MainComponent& c, int ms) {
    static_cast<void>(tickUntil(c, [] { return false; }, ms));
}

MainComponentGenerator& genOf(MainComponent& c) { return Access::generatorForTest(c); }

void startSynthetic(MainComponent& c) {
    c.setSyntheticMode(true);
    Access::waitForResyncForTest(c);
}

bool settled(MainComponent& c) { return genOf(c).runnerForTest().state() == GeneratorState::Playing; }

struct Peak {
    double hz = 0.0;
    double db = -1000.0;
    double binHz = 0.0;
    double aboveMedianDb = 0.0;
};

/// The RTA spectrum's strongest bin from the latest published snapshot.
std::optional<Peak> spectrumPeak(MainComponent& c) {
    const auto snapshot = Access::analysisThread(c).latest();
    if (!snapshot || snapshot->spectrumDb.size() < 8 || snapshot->fftSize == 0) return std::nullopt;
    const auto& s = snapshot->spectrumDb;
    const auto top = std::max_element(s.begin() + 1, s.end());  // skip DC
    std::vector<float> sorted(s.begin() + 1, s.end());
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    Peak peak;
    peak.binHz = snapshot->sampleRate / static_cast<double>(snapshot->fftSize);
    peak.hz = static_cast<double>(top - s.begin()) * peak.binHz;
    peak.db = *top;
    peak.aboveMedianDb = *top - sorted[sorted.size() / 2];
    return peak;
}

/// TRANSFER magnitude (dB) at the bin nearest `hz`, or nullopt with no transfer block yet.
std::optional<double> transferDbAt(MainComponent& c, double hz) {
    const auto snapshot = Access::analysisThread(c).latest();
    if (!snapshot || !snapshot->transfer.has_value() || snapshot->fftSize == 0) return std::nullopt;
    const double binHz = snapshot->sampleRate / static_cast<double>(snapshot->fftSize);
    const auto k = static_cast<std::size_t>(std::llround(hz / binHz));
    const auto& mag = snapshot->transfer->magnitudeDb;
    return k < mag.size() ? std::optional<double>(mag[k]) : std::nullopt;
}

}  // namespace

TEST_CASE("generator B: SINE 1000 Hz peaks within one bin of 1000 Hz, and six - steps lower it 6.0 dB", kTag) {
    MainComponent c;
    startSynthetic(c);
    genOf(c).signalSelected(GeneratorSignal::Sine);
    genOf(c).frequencySelected(1000);
    CHECK(genOf(c).frequencyEnabledForTest());
    genOf(c).genClicked();
    REQUIRE(genOf(c).runnerForTest().state() == GeneratorState::Playing);
    CHECK(genOf(c).readoutForTest() == "GEN: playing sine 1000 Hz -30.0 dBFS peak on out 1");

    // A tone stands well clear of the SYNTHETIC noise floor (-50 dBFS RMS); then let the
    // averaging converge so the baseline is the tone's level, not a point on its way up.
    Peak first;
    REQUIRE(tickUntil(c, [&] {
        const auto p = spectrumPeak(c);
        if (p && p->aboveMedianDb > 25.0 && std::abs(p->hz - 1000.0) <= p->binHz) first = *p;
        return first.aboveMedianDb > 25.0;
    }, 20000));
    pumpFor(c, 4000);
    const auto settledPeak = spectrumPeak(c);
    REQUIRE(settledPeak.has_value());
    const Peak before = *settledPeak;
    CAPTURE(before.hz, before.db, before.binHz);
    CHECK(std::abs(before.hz - 1000.0) <= before.binHz);

    // Six steps of 1.0 dB: the tone drops 6.0 dB (10^(6/20) = 1.995; 6.02 dB). Wait
    // for the analyser's own averaging to converge from above, then read it.
    for (int i = 0; i < 6; ++i) genOf(c).stepClicked(-1);
    REQUIRE(tickUntil(c, [&] { return settled(c); }, 3000));
    CHECK(genOf(c).runnerForTest().settings().levelDb == -36.0);
    pumpFor(c, 4000);
    const auto after = spectrumPeak(c);
    REQUIRE(after.has_value());
    const double drop = before.db - after->db;
    CAPTURE(after->hz, after->db, drop);
    CHECK(std::abs(after->hz - before.hz) <= 1e-9);  // the same bin: the frequency did not move
    CHECK(std::abs(drop - 6.0) <= 0.1);
}

TEST_CASE("generator B: the frequency choice moves the peak; it is ignored for noise", kTag) {
    MainComponent c;
    startSynthetic(c);
    genOf(c).signalSelected(GeneratorSignal::Sine);
    CHECK(genOf(c).frequencyComboCountForTest() == 31);
    genOf(c).frequencySelected(2000);
    genOf(c).genClicked();
    REQUIRE(genOf(c).runnerForTest().state() == GeneratorState::Playing);
    REQUIRE(tickUntil(c, [&] {
        const auto p = spectrumPeak(c);
        return p && p->aboveMedianDb > 25.0 && std::abs(p->hz - 2000.0) <= p->binHz;
    }, 20000));

    genOf(c).signalSelected(GeneratorSignal::Pink);
    CHECK_FALSE(genOf(c).frequencyEnabledForTest());  // a frequency means nothing to noise
    REQUIRE(tickUntil(c, [&] { return settled(c); }, 3000));
    CHECK(genOf(c).readoutForTest() == "GEN: playing pink -30.0 dBFS RMS on out 1");
}

TEST_CASE("generator B: a signal change while playing is exactly one swap", kTag) {
    MainComponent c;
    startSynthetic(c);
    genOf(c).genClicked();
    REQUIRE(genOf(c).runnerForTest().state() == GeneratorState::Playing);
    genOf(c).signalSelected(GeneratorSignal::White);
    CHECK(genOf(c).runnerForTest().state() == GeneratorState::Swapping);  // disarm, wait, setSource, arm
    CHECK(genOf(c).readoutForTest().contains("white"));
    REQUIRE(tickUntil(c, [&] { return settled(c); }, 3000));
    CHECK(genOf(c).runnerForTest().settings().signal == GeneratorSignal::White);
    CHECK_FALSE(Access::outputSourceQuiescentForTest(c));  // playing again
}

TEST_CASE("generator B: OUT lists one entry per device output and the last tick cannot be removed", kTag) {
    MainComponent c;
    startSynthetic(c);
    auto rows = genOf(c).outputChoices();
    REQUIRE(rows.size() == 2);  // the SYNTHETIC loopback renders two outputs
    CHECK(rows[0].first == "out 1");
    CHECK(rows[0].second);
    CHECK(rows[1].first == "out 2");
    CHECK_FALSE(rows[1].second);

    genOf(c).outputToggled(0);  // would leave nothing ticked
    CHECK(genOf(c).settings().outputs == std::vector<int>{0});
    genOf(c).outputToggled(1);
    CHECK(genOf(c).settings().outputs == std::vector<int>{0, 1});
    genOf(c).outputToggled(0);
    CHECK(genOf(c).settings().outputs == std::vector<int>{1});
    genOf(c).outputToggled(1);  // the last tick stays
    CHECK(genOf(c).settings().outputs == std::vector<int>{1});
    CHECK(genOf(c).readoutForTest().contains("out 2"));
}

TEST_CASE("generator B: outputs are ADDITIVE and change live, with no swap", kTag) {
    MainComponent c;
    startSynthetic(c);
    genOf(c).genClicked();
    REQUIRE(genOf(c).runnerForTest().state() == GeneratorState::Playing);
    CHECK(Access::outputRoleForTest(c, 0) == rta::platform::OutputRole::Routed);
    CHECK(Access::outputRoleForTest(c, 1) != rta::platform::OutputRole::Routed);

    genOf(c).outputToggled(1);  // out 1 + out 2: both routed (soloOutput would keep one)
    CHECK(genOf(c).runnerForTest().state() == GeneratorState::Playing);  // live routing, not a swap
    CHECK(Access::outputRoleForTest(c, 0) == rta::platform::OutputRole::Routed);
    CHECK(Access::outputRoleForTest(c, 1) == rta::platform::OutputRole::Routed);
    CHECK(genOf(c).readoutForTest() == "GEN: playing pink -30.0 dBFS RMS on out 1+2");

    genOf(c).outputToggled(0);  // out 2 only
    CHECK(genOf(c).runnerForTest().state() == GeneratorState::Playing);
    CHECK(Access::outputRoleForTest(c, 0) != rta::platform::OutputRole::Routed);
    CHECK(Access::outputRoleForTest(c, 1) == rta::platform::OutputRole::Routed);
}

TEST_CASE("generator B: out 2 only reaches TRANSFER through the 100 Hz low-pass; out 1 only through the band-pass",
          kTag) {
    MainComponent c;
    startSynthetic(c);
    genOf(c).outputToggled(1);
    genOf(c).outputToggled(0);  // out 2 only, set while stopped
    genOf(c).genClicked();
    REQUIRE(genOf(c).runnerForTest().state() == GeneratorState::Playing);

    // Out 2 is LR4 100 Hz: ~-0.5 dB at 50 Hz (closed form 1/(1+(f/fc)^4) = 0.941),
    // and four octaves-and-a-bit down by 1 kHz (>= 80 dB in theory; the estimate is
    // bounded by the -50 dBFS RMS measurement noise, so the case asks for 20 dB).
    std::optional<double> at50, at1k;
    REQUIRE(tickUntil(c, [&] {
        at50 = transferDbAt(c, 50.0);
        at1k = transferDbAt(c, 1000.0);
        return at50 && at1k && *at50 > -3.0;  // the low-pass has taken over the TRANSFER
    }, 30000));
    pumpFor(c, 3000);  // let the transfer FIFO turn over completely before reading both bins
    at50 = transferDbAt(c, 50.0);
    at1k = transferDbAt(c, 1000.0);
    REQUIRE((at50 && at1k));
    INFO("out 2 only: 50 Hz " << *at50 << " dB, 1 kHz " << *at1k << " dB");
    CHECK(*at1k <= *at50 - 20.0);

    genOf(c).outputToggled(0);  // add out 1
    genOf(c).outputToggled(1);  // drop out 2: out 1 only, live
    REQUIRE(tickUntil(c, [&] {
        at50 = transferDbAt(c, 50.0);
        at1k = transferDbAt(c, 1000.0);
        // Both bins must have turned over: the FIFO still holds out-2 frames for a while, and a
        // 50 Hz bin read mid-transition says nothing about the band-pass (-1.5 dB closed form).
        return at50 && at1k && *at1k > -3.0 && *at50 > -4.0;
    }, 30000));
    pumpFor(c, 3000);
    at50 = transferDbAt(c, 50.0);
    at1k = transferDbAt(c, 1000.0);
    REQUIRE((at50 && at1k));
    INFO("out 1 only: 50 Hz " << *at50 << " dB, 1 kHz " << *at1k << " dB");
    CHECK(std::abs(*at1k - *at50) <= 3.0);  // 40 Hz-16 kHz band-pass: ~-1.5 dB at 50 Hz
}

TEST_CASE("generator B: GEN, - and + are touch targets (44 px) and keep the device panel in view", kTag) {
    MainComponent c;
    c.setSize(1280, 800);
    c.resized();
    auto& g = genOf(c);
    for (const juce::Component* button : std::initializer_list<const juce::Component*>{
             &g.genButtonForTest(), &g.minusButtonForTest(), &g.plusButtonForTest()}) {
        CHECK(button->getHeight() >= az::ui::touchTarget);
        CHECK(button->getWidth() >= az::ui::touchTarget);
        CHECK(button->getBottom() <= az::ui::transportHeight);
    }
    // The legends: a hyphen and a plus, not blanks and not dots.
    CHECK(g.minusButtonForTest().getButtonText() == "-");
    CHECK(g.plusButtonForTest().getButtonText() == "+");
    const auto& viewport = Access::railViewportForTest(c);
    CHECK(viewport.getHeight() >= 260);  // PR #88's minimum for the device panel
}
