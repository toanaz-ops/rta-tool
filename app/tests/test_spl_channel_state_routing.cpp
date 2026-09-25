// SPDX-License-Identifier: AGPL-3.0-or-later
// Split out of test_spl_channel_state.cpp (PR #29 round-3 fix pass step 6,
// 400-line hard cap). Lane L6a task W2-E1, fix round 2026-09-25: an
// independent verifier refuted the first version of SplChannelState.cpp --
// every consumer read the channel's FIRST configured chain regardless of
// which metric it was actually about. See SplChannelState.h.
//
// These three cases (1a/1b/1c) are the ones that need a REAL SplSession
// (real weighting filters, real audio) to prove the routing claim -- the
// rest of the original file's fixtures build Blocks by hand and stayed in
// test_spl_channel_state.cpp.
#include "measure/SplChannelState.h"

#include "measure/SplSession.h"

#include "rta/dsp/Weighting.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::measure::ChainBlockAtClose;
using rta::measure::SplAlarmSpec;
using rta::measure::SplBlockView;
using rta::measure::SplChannelState;
using rta::measure::SplConfig;
using rta::measure::SplMetricSpec;
using rta::measure::SplSession;
using rta::meter::Block;
using rta::meter::DoseSettings;

namespace {

constexpr double kTwoPi = 6.283185307179586;

Block blockAtLevel(std::uint64_t index, std::uint32_t samples, double levelDb) {
    Block b;
    b.blockIndex = index;
    b.blockSamples = samples;
    b.sumSquares = static_cast<double>(samples) * std::pow(10.0, levelDb / 10.0);
    // A constant-level fixture: the block's own Fast max-hold reads the same
    // level as its Leq (both quantities agree for a steady-state signal),
    // so Ln fixtures built on this helper feed a real value into the
    // histogram rather than the default kLevelFloorDb.
    b.maxFastDb = static_cast<float>(levelDb);
    return b;
}

/// Feeds ONE chain's block (default weighting A, the common case in these
/// fixtures), growing `window` first and building the single-entry
/// `ChainBlockAtClose` array `onBlockClosed` now takes.
void feedOneChain(SplChannelState& state, std::vector<Block>& window, const Block& block,
                  rta::dsp::WeightingType weighting = rta::dsp::WeightingType::A) {
    window.push_back(block);
    ChainBlockAtClose atClose;
    atClose.weighting = weighting;
    atClose.block = block;
    atClose.windowThroughThisBlock = window;
    state.onBlockClosed(std::span<const ChainBlockAtClose>(&atClose, 1));
}

/// Drains a real `SplSession` after `feedHop` and folds every chain's own
/// newly-closed block(s) into `state`, mirroring `AnalysisThread::feedSpl`'s
/// own loop (including `rta::measure::windowAtClose`) so these fixtures
/// exercise the SAME reconstruction production code uses, over REAL,
/// weighting-filtered audio.
///
/// PR #29 round-3 fix pass step 7: this helper diverged from `feedSpl` the
/// moment step 1 added `feedLnTicks` there -- `feedSpl` calls it once per
/// hop, unconditionally, AFTER the closed-block loop, whether or not a
/// block closed on this hop. Updated here to keep mirroring production
/// (rather than leaving `runOrderProbe`'s own `Probe::ln50` silently
/// starved of ticks).
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

struct Probe {
    float alarmValueDb = 0.0f;
    std::optional<double> dose0;
    std::optional<double> ln50;
};

/// Reproduces the verifier's own probe: metrics [LCeq, LAeq] (or the reverse,
/// via `aFirst`), an alarm on LAeq, a real low-frequency tone fed through the
/// REAL weighting filters via a real `SplSession`.
Probe runOrderProbe(bool aFirst, double freqHz, double fs, std::uint32_t blockSamples) {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 100.0;
    const SplMetricSpec aMetric{"LAeq", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 1};
    const SplMetricSpec cMetric{"LCeq", rta::dsp::WeightingType::C, rta::meter::TimeWeighting::Fast, 1};
    config.metrics = aFirst ? std::vector<SplMetricSpec>{aMetric, cMetric}
                           : std::vector<SplMetricSpec>{cMetric, aMetric};
    SplAlarmSpec spec;
    spec.metricId = "LAeq";
    spec.limitDb = 1000.0;  // never fires; only valueDb is read
    spec.windowBlocks = 1;
    config.alarms = {spec};

    SplSession session;
    const int channels[] = {0};
    session.start(config, fs, channels);
    SplChannelState state(config, fs);

    std::vector<float> hop(blockSamples);
    for (int block = 0; block < 3; ++block) {  // let the biquad cascade settle
        for (std::uint32_t n = 0; n < blockSamples; ++n) {
            const double t = static_cast<double>(block) + static_cast<double>(n) / fs;
            hop[n] = static_cast<float>(std::sin(kTwoPi * freqHz * t));
        }
        feedAndDrive(session, state, 0, hop);
    }

    SplBlockView view;
    state.fillPublish(view);
    REQUIRE(view.alarms.size() == 1);
    REQUIRE(view.dosePercent[0].has_value());

    Probe p;
    p.alarmValueDb = view.alarms[0].valueDb;
    p.dose0 = view.dosePercent[0];
    p.ln50 = view.lnDb[3];  // config.lnPercents default {1,5,10,50,90,95}: index 3 = 50.0
    return p;
}

}  // namespace

// --- 1: every consumer reads the chain its own definition names -----------

TEST_CASE("1a a low-frequency tone: alarm follows A, matching the shipped filter's own analytic gap",
         "[spl_channel_state]") {
    constexpr double kFs = 48000.0;
    constexpr double kFreqHz = 20.0;  // A rolls off sharply here; C stays near flat
    constexpr std::uint32_t kBlockSamples = 48000;  // blockSeconds = 1.0

    const auto withCFirst = runOrderProbe(/*aFirst=*/false, kFreqHz, kFs, kBlockSamples);
    const auto withAFirst = runOrderProbe(/*aFirst=*/true, kFreqHz, kFs, kBlockSamples);

    // ORDER-INDEPENDENCE: config.metrics listing LAeq first or LCeq first
    // must publish the identical number. Mutant "feed the first chain
    // again" flips this: with LCeq first, the alarm would read LCeq's own
    // (much louder, unattenuated) level instead.
    CHECK_THAT(withCFirst.alarmValueDb, WithinAbs(withAFirst.alarmValueDb, 1e-4));

    // AND it is actually the A-WEIGHTED level, not merely order-stable:
    // derive the expected level from the shipped weighting filter's own
    // ANALYTIC magnitude at 20 Hz -- computed here, never typed. A
    // unit-amplitude sine reads 0 dBFS sine-referenced; mean-square
    // referenced (record §13 Q1's own seam) that is kFullScaleSineOffsetDb
    // below, and a steady-state tone through the filter adds its own
    // analytic gain on top.
    constexpr double kMeanSquareSeamDb = -3.0102999566398120;
    const double expectedALevel =
        kMeanSquareSeamDb + rta::dsp::Weighting::analyticDb(kFreqHz, rta::dsp::WeightingType::A);
    const double gapDb = rta::dsp::Weighting::analyticDb(kFreqHz, rta::dsp::WeightingType::C) -
                         rta::dsp::Weighting::analyticDb(kFreqHz, rta::dsp::WeightingType::A);
    // Sanity on the fixture itself: 20 Hz really does separate A from C
    // sharply, or this test would not be able to tell the bug from a pass.
    REQUIRE(gapDb > 20.0);

    // Generous (digital-filter approximation + block-boundary settling,
    // record 8's own "the design target and closed form" note): still tight
    // enough to fail by a wide margin under the first-chain bug, which would
    // read within a fraction of a dB of LCeq's level, `gapDb` away.
    CHECK_THAT(static_cast<double>(withCFirst.alarmValueDb), WithinAbs(expectedALevel, 1.0));
}

TEST_CASE("1b dose and Ln read the A-weighted chain, never the first configured chain",
         "[spl_channel_state]") {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 100.0;
    config.dose[0] = DoseSettings{85.0, 10.0, 10.0, 80.0};  // T_c=10s, L_c=85, q=10, threshold=80

    SplChannelState state(config, 48000.0);
    std::vector<Block> cWindow, aWindow;

    // C is fed FIRST in the per-hop array (as a config listing LCeq before
    // LAeq would produce) at an absurd level that would blow dose past
    // 100 % many times over if it were ever read; A sits EXACTLY at the
    // criterion, which is the D1a closed form: L = L_c, T = T_c => D = 100 %.
    for (std::uint64_t i = 0; i < 10; ++i) {
        const Block cBlock = blockAtLevel(i, 48000, 130.0);
        const Block aBlock = blockAtLevel(i, 48000, 85.0);
        cWindow.push_back(cBlock);
        aWindow.push_back(aBlock);
        std::array<ChainBlockAtClose, 2> atClose{
            ChainBlockAtClose{rta::dsp::WeightingType::C, cBlock, cWindow},
            ChainBlockAtClose{rta::dsp::WeightingType::A, aBlock, aWindow},
        };
        state.onBlockClosed(atClose);
    }

    SplBlockView view;
    state.fillPublish(view);
    REQUIRE(view.dosePercent[0].has_value());
    // Mutant "feed the first chain again" reads the 130 dB C block instead:
    // D = 100 * 10 * 10^((130-85)/10) / 10 = 3.16e6 %, nowhere near 100 %.
    CHECK_THAT(*view.dosePercent[0], WithinAbs(100.0, 1e-9));
}

TEST_CASE("1c onBlockClosed no longer feeds Ln at all -- feedLnTicks is the only path",
         "[spl_channel_state]") {
    // Fix round 2026-09-25 (PR #29 round-3 fix pass step 1): Ln used to be
    // fed from the A-weighted chain's own closed block (Block::maxFastDb)
    // inside onBlockClosed -- the wrong granularity entirely (record §5:
    // "Fast, 100 ms sampling", not once per block). It now comes
    // EXCLUSIVELY from feedLnTicks (test_spl_ln_ticks.cpp exercises that
    // path through a real SplSession+SplMeter). A caller that closes many
    // blocks but never calls feedLnTicks must see Ln stay ABSENT -- proof
    // the two paths are actually decoupled, not merely that the old wrong
    // value stopped appearing. The "feed maxFastDb again" mutant (reverting
    // this fix) makes Ln PRESENT here, at ~80 dB, failing every CHECK_FALSE
    // below.
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 200.0;

    SplChannelState state(config, 48000.0);
    std::vector<Block> aWindow;
    for (std::uint64_t i = 0; i < 20; ++i) {
        feedOneChain(state, aWindow, blockAtLevel(i, 48000, 80.0));
    }

    SplBlockView view;
    state.fillPublish(view);
    for (const auto& ln : view.lnDb) CHECK_FALSE(ln.has_value());
}
