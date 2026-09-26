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

/// The same alternating high/low sine used by the 100-ms-clock case above,
/// but as a single flat buffer -- so it can be fed through `feedHop` in ANY
/// hop size, including one that does not evenly divide the 100 ms tick
/// period, without changing a single sample value. Amplitude switches at
/// exact `samplesPerPhase` sample boundaries, in absolute sample time, same
/// as the loop this factors out of.
std::vector<float> alternatingSignal(double freqHz, double highAmp, double lowAmp,
                                     std::size_t samplesPerPhase, int cycles, double fs) {
    std::vector<float> x(static_cast<std::size_t>(cycles) * 2 * samplesPerPhase);
    std::uint64_t sampleIndex = 0;
    std::size_t pos = 0;
    for (int cycle = 0; cycle < cycles; ++cycle) {
        for (int phase = 0; phase < 2; ++phase) {
            const double amp = (phase == 0) ? highAmp : lowAmp;
            for (std::size_t t = 0; t < samplesPerPhase; ++t) {
                const double time = static_cast<double>(sampleIndex) / fs;
                x[pos++] = static_cast<float>(amp * std::sin(kTwoPi * freqHz * time));
                ++sampleIndex;
            }
        }
    }
    return x;
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

    // --- The tolerance, DERIVED, not typed (round-4 item 6) ----------------
    //
    // (a) The Fast exponential's OWN closed-form settling residual, at the
    // LAST tick of a phase -- the tick the bulk of that phase's histogram
    // mass sits nearest, since kPhaseSeconds/kTau = 16 time constants. By
    // `ticksPerPhase` ticks the recursive state has decayed toward its
    // target by `decayPerTick^ticksPerPhase` of the ORIGINAL high/low gap
    // (the same exact discrete recursion `expected` above already computes,
    // evaluated at its own fixed point). That linear-domain residual is
    // converted to a dB bound via the standard small-signal estimate
    // d(10*log10 x) ~= (10/ln10) * dx/x -- evaluated at the SMALLER of the
    // two targets (meanSquareLow), because the same absolute linear residual
    // reads as a LARGER dB delta there than it would at the bigger target,
    // which is what makes this an upper bound rather than a typical case.
    const double linearGapFraction =
        std::pow(decayPerTick, static_cast<double>(ticksPerPhase));
    const double worstCaseLinearResidual =
        linearGapFraction * std::fabs(meanSquareHigh - meanSquareLow);
    const double settlingResidualDb =
        (10.0 / std::log(10.0)) * worstCaseLinearResidual / meanSquareLow;

    // (b) The histogram's own quantisation: no percentile it reports can sit
    // closer than half a bin to the true continuous value (LevelHistogram.h's
    // own `percentileDb` comment; this project's established bound --
    // test_spl_channel_state.cpp's "c" case). BUT this comparison is between
    // TWO INDEPENDENTLY quantised percentiles -- `view.lnDb` from the real
    // session's own histogram and `expectedL10`/`expectedL90` from this
    // fixture's SEPARATE `LevelHistogram` built from the closed-form
    // simulation above -- so each side can independently sit up to half a bin
    // away from the true continuous value, on OPPOSITE sides of it. The
    // worst-case gap between the two readings is therefore a FULL bin width,
    // not half (LOW follow-up batch, item 2).
    const double histogramQuantizationDb = rta::meter::LevelHistogram::kBinWidthDb;

    const double kFilterTransientMarginDb = settlingResidualDb + histogramQuantizationDb;
    INFO("settlingResidualDb = " << settlingResidualDb
                                  << ", histogramQuantizationDb = " << histogramQuantizationDb
                                  << ", derived margin = " << kFilterTransientMarginDb);
    CHECK_THAT(*view.lnDb[2], WithinAbs(*expectedL10, kFilterTransientMarginDb));
    CHECK_THAT(*view.lnDb[4], WithinAbs(*expectedL90, kFilterTransientMarginDb));

    // Distinguishing check the OLD bug could never pass: L10 and L90 must be
    // FAR apart. Under the old `Block::maxFastDb`-per-block feed, every
    // block's max-held Fast reading is the block's own loud half, so EVERY
    // Ln -- L1 through L95 -- would collapse to the same ~0 dBFS number
    // (verifier repro: "every Ln reads ~90.95" at a +90.95 dB offset).
    CHECK(*view.lnDb[2] - *view.lnDb[4] > 20.0);
}

// --- round-4 item 6: a hop size that does NOT evenly divide the tick -------
// period, so a tick-phase bug (reset-per-hop, an off-by-one tick period, tau
// confused with the sampling period) has somewhere to show up. The fixture
// above uses hop == tick == 4800 samples, so every hop boundary coincides
// with a tick boundary and a phase bug is invisible to it.

TEST_CASE("Ln ticks stay phase-correct when the hop size does not divide the tick period",
         "[spl_ln_ticks]") {
    constexpr double kFreqHz = 1000.0;
    constexpr double kHighAmp = 1.0;
    constexpr double kLowAmp = 0.01;
    constexpr double kPhaseSeconds = 2.0;
    constexpr double kTau = 0.125;
    constexpr int kCycles = 8;
    // 1024 does NOT divide 4800 (the tick period at 48 kHz): 4800 / 1024 is
    // not an integer, so successive hops land at a DIFFERENT phase within
    // the tick period every time, and only a persistent (never per-hop-
    // reset) sample counter tracking the correct 100 ms period can still
    // land every tick on its true 100 ms boundary.
    constexpr std::size_t kHopSamples = 1024;

    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 200.0;

    SplSession session;
    const int channels[] = {0};
    session.start(config, kFs, channels);
    SplChannelState state(config, kFs);

    const auto samplesPerTick = static_cast<std::size_t>(0.1 * kFs + 0.5);
    const auto samplesPerPhase = static_cast<std::size_t>(kPhaseSeconds * kFs + 0.5);
    REQUIRE(samplesPerPhase % samplesPerTick == 0);  // fixture sanity, as above
    REQUIRE(samplesPerPhase % kHopSamples != 0);     // the property this case needs
    const std::size_t ticksPerPhase = samplesPerPhase / samplesPerTick;

    const auto signal =
        alternatingSignal(kFreqHz, kHighAmp, kLowAmp, samplesPerPhase, kCycles, kFs);
    REQUIRE(signal.size() % kHopSamples == 0);  // clean chunking, no partial last hop

    std::uint64_t totalTicks = 0;
    for (std::size_t off = 0; off < signal.size(); off += kHopSamples) {
        const std::span<const float> chunk(signal.data() + off, kHopSamples);
        feedAndDrive(session, state, 0, chunk);
        // Read AFTER feedAndDrive already forwarded this hop's ticks to
        // `state` -- the span itself is untouched until the NEXT push().
        totalTicks += session.newlyTickedLnLevelsDb(0, rta::dsp::WeightingType::A).size();
    }

    // EXACT integer identity: 10 ticks/second at the true 100 ms period,
    // over the fixture's whole duration -- never a tolerance.
    const double totalSeconds =
        static_cast<double>(signal.size()) / kFs;
    REQUIRE_THAT(totalSeconds, WithinAbs(32.0, 1e-9));  // kCycles*2*kPhaseSeconds
    CHECK(totalTicks == static_cast<std::uint64_t>(10.0 * totalSeconds));

    // L10/L90 must still land where the SAME closed form (as the 100-ms-hop
    // case above) predicts, with hops that do not align to tick boundaries.
    SplBlockView view;
    state.fillPublish(view);
    REQUIRE(view.lnDb[2].has_value());
    REQUIRE(view.lnDb[4].has_value());

    const double decayPerTick = std::exp(-0.1 / kTau);
    const double meanSquareHigh = kHighAmp * kHighAmp / 2.0;
    const double meanSquareLow = kLowAmp * kLowAmp / 2.0;

    rta::meter::LevelHistogram expected(config.histogramBaseDb());
    double simMeanSquare = 0.0;
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

    // Same derivation as the 100-ms-hop case above: settling residual at the
    // last tick of a phase, plus a FULL histogram bin -- two INDEPENDENTLY
    // quantised percentiles being compared, each up to half a bin off the
    // true value on either side (LOW follow-up batch, item 2; see that case's
    // own comment for the full argument).
    const double linearGapFraction = std::pow(decayPerTick, static_cast<double>(ticksPerPhase));
    const double worstCaseLinearResidual =
        linearGapFraction * std::fabs(meanSquareHigh - meanSquareLow);
    const double settlingResidualDb =
        (10.0 / std::log(10.0)) * worstCaseLinearResidual / meanSquareLow;
    const double marginDb =
        settlingResidualDb + rta::meter::LevelHistogram::kBinWidthDb;

    CHECK_THAT(*view.lnDb[2], WithinAbs(*expectedL10, marginDb));
    CHECK_THAT(*view.lnDb[4], WithinAbs(*expectedL90, marginDb));
    CHECK(*view.lnDb[2] - *view.lnDb[4] > 20.0);
}
