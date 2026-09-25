// SPDX-License-Identifier: AGPL-3.0-or-later
//
// PR #29 round-3 fix pass, step 1 (MEDIUM). Record docs/dsp/
// 2026-09-16-spl-pro-l6a.md §5 is explicit: Ln accumulates "the
// time-weighted level... at the detector sampling rate" and the label
// footer says "Fast, 100 ms sampling, linear interpolation". Before this
// fix, `SplChannelState::onBlockClosed` fed `lnHistogram_` from
// `Block::maxFastDb` once per CLOSED BLOCK -- the wrong granularity: a
// signal alternating loud/quiet within one 1 s block always reads the
// block's loud MAX-HELD peak, so every Ln (L1..L95) collapsed to the same
// number.
//
// This is JUCE-free (SplSession/SplChannelState/SplMeter carry no
// framework dependency), so it is proven OFF on all three CI operating
// systems.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "measure/SplChannelState.h"
#include "measure/SplSession.h"

#include "rta/dsp/Weighting.h"
#include "rta/meter/LevelHistogram.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::measure::ChainBlockAtClose;
using rta::measure::SplBlockView;
using rta::measure::SplChannelState;
using rta::measure::SplConfig;
using rta::measure::SplSession;
using rta::meter::Block;

namespace {

constexpr double kFs = 48000.0;
constexpr double kTwoPi = 6.283185307179586;

/// Mirrors `AnalysisThread::feedSpl`'s real per-hop loop, INCLUDING the
/// step this fix round added: `feedLnTicks` runs once per hop, whether or
/// not a block closed on it (record §5: ticks are on the detector's own
/// 100 ms clock, never tied to block closure).
void feedAndDrive(SplSession& session, SplChannelState& state, int channel,
                  std::span<const float> hop) {
    session.feedHop(channel, hop);
    const auto weightings = session.weightings();
    std::vector<std::span<const Block>> closedByChain(weightings.size());
    std::vector<std::span<const Block>> fullWindowByChain(weightings.size());
    std::size_t closedCount = 0;
    for (std::size_t c = 0; c < weightings.size(); ++c) {
        closedByChain[c] = session.newlyClosedBlocks(channel, weightings[c]);
        fullWindowByChain[c] = session.window(channel, weightings[c]);
        closedCount = closedByChain[c].size();
    }
    std::vector<ChainBlockAtClose> atClose(weightings.size());
    for (std::size_t i = 0; i < closedCount; ++i) {
        for (std::size_t c = 0; c < weightings.size(); ++c) {
            atClose[c].weighting = weightings[c];
            atClose[c].block = closedByChain[c][i];
            atClose[c].windowThroughThisBlock =
                rta::measure::windowAtClose(closedByChain[c], fullWindowByChain[c], i);
        }
        state.onBlockClosed(atClose);
    }
    state.feedLnTicks(session.newlyTickedLnLevelsDb(channel, rta::dsp::WeightingType::A));
}

}  // namespace

TEST_CASE("Ln is fed at the 100 ms detector sampling rate, not the block clock",
         "[spl_ln_ticks]") {
    // 1 kHz: the shipped A-weighting curve is NORMALISED to 0 dB there
    // (core/src/dsp/Weighting.cpp: analogGainA() == 1/bracketA(1000)) by
    // construction, so a full-scale sine reads its own mean-square-referenced
    // dBFS through the A chain with no analytic gain/loss to account for.
    constexpr double kFreqHz = 1000.0;
    constexpr double kHighAmp = 1.0;    // 0 dBFS peak
    constexpr double kLowAmp = 0.01;    // -40 dBFS peak
    constexpr double kPhaseSeconds = 2.0;  // long enough for the Fast (tau
                                           // = 125 ms) detector to actually
                                           // settle near each target by the
                                           // end of its own phase -- see the
                                           // closed-form simulation below.
    constexpr double kTau = 0.125;

    SplConfig config;
    config.blockSeconds = 1.0;  // the verifier's own repro parameter
    config.logSpanSeconds = 200.0;

    SplSession session;
    const int channels[] = {0};
    session.start(config, kFs, channels);
    SplChannelState state(config, kFs);

    // +0.5 before truncating: the same round-to-nearest `SplMeter.cpp`'s own
    // `blockSamplesFor`/`lnSamplesPerTickFor` use, so this fixture's tick
    // schedule matches the meter's internal one exactly rather than by
    // floating-point coincidence.
    const auto samplesPerPhase = static_cast<std::size_t>(kPhaseSeconds * kFs + 0.5);
    const auto samplesPerTick = static_cast<std::size_t>(0.1 * kFs + 0.5);
    REQUIRE(samplesPerPhase % samplesPerTick == 0);  // ticks never straddle a phase edge
    constexpr int kCycles = 8;

    // --- Drive the REAL session with REAL, A-weighting-filtered audio -----
    std::vector<float> hop(samplesPerTick);
    std::uint64_t sampleIndex = 0;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        for (int phase = 0; phase < 2; ++phase) {
            const double amp = (phase == 0) ? kHighAmp : kLowAmp;
            for (std::size_t t = 0; t < samplesPerPhase; t += samplesPerTick) {
                for (std::size_t n = 0; n < samplesPerTick; ++n) {
                    const double time = static_cast<double>(sampleIndex) / kFs;
                    hop[n] = static_cast<float>(amp * std::sin(kTwoPi * kFreqHz * time));
                    ++sampleIndex;
                }
                feedAndDrive(session, state, 0, hop);
            }
        }
    }

    SplBlockView view;
    state.fillPublish(view);
    // config.lnPercents default {1,5,10,50,90,95}: index 2 = L10, index 4 = L90.
    REQUIRE(view.lnDb[2].has_value());
    REQUIRE(view.lnDb[4].has_value());

    // --- The independent closed-form reference -----------------------------
    // A second, from-scratch Fast-detector simulation over the SAME tick
    // schedule, computed here rather than typed: the discrete step is EXACT
    // (not an approximation) because samplesPerTick/kFs == 0.1 s exactly at
    // 48 kHz, so the per-tick decay factor is precisely exp(-0.1/tau) --
    // rta::meter::Detector's own alpha = 1 - exp(-1/(fs*tau)) raised to
    // samplesPerTick gives exactly that. The 1 kHz carrier itself is not
    // modelled (the 125 ms detector heavily attenuates a 1 kHz ripple; the
    // filter's own group delay/settling is the ONE thing this simulation
    // does not capture, which is why the comparison below carries a margin
    // for it, not for the detector's own dynamics).
    const double decayPerTick = std::exp(-0.1 / kTau);
    const double meanSquareHigh = kHighAmp * kHighAmp / 2.0;
    const double meanSquareLow = kLowAmp * kLowAmp / 2.0;
    const std::size_t ticksPerPhase = samplesPerPhase / samplesPerTick;

    rta::meter::LevelHistogram expected(config.histogramBaseDb());
    double simMeanSquare = 0.0;  // Detector::reset()'s own default
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        for (int phase = 0; phase < 2; ++phase) {
            const double target = (phase == 0) ? meanSquareHigh : meanSquareLow;
            for (std::size_t k = 0; k < ticksPerPhase; ++k) {
                simMeanSquare = target + (simMeanSquare - target) * decayPerTick;
                expected.add(10.0 * std::log10(simMeanSquare) + config.referenceOffsetDb);
            }
        }
    }
    const auto expectedL10 = expected.percentileDb(10.0);
    const auto expectedL90 = expected.percentileDb(90.0);
    REQUIRE(expectedL10.has_value());
    REQUIRE(expectedL90.has_value());

    // A margin for the ONE thing the closed form above does not model: the
    // A-weighting biquad cascade's own (much faster than 125 ms) transient
    // at the amplitude step, plus the histogram's own half-bin quantisation
    // (0.05 dB, this project's established bound -- test_spl_channel_
    // state.cpp's "c" case).
    constexpr double kFilterTransientMarginDb = 1.0;
    CHECK_THAT(*view.lnDb[2], WithinAbs(*expectedL10, kFilterTransientMarginDb));
    CHECK_THAT(*view.lnDb[4], WithinAbs(*expectedL90, kFilterTransientMarginDb));

    // Distinguishing check the OLD bug could never pass: L10 and L90 must be
    // FAR apart. Under the old `Block::maxFastDb`-per-block feed, every
    // block's max-held Fast reading is the block's own loud half, so EVERY
    // Ln -- L1 through L95 -- would collapse to the same ~0 dBFS number
    // (verifier repro: "every Ln reads ~90.95" at a +90.95 dB offset).
    CHECK(*view.lnDb[2] - *view.lnDb[4] > 20.0);
}
