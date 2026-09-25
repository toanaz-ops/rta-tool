// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Split out of test_spl_session.cpp (PR #29 round-3 fix pass step 6,
// 400-line hard cap). PR #17 verifier defect 1: the metric cap, and the
// hole it used to open above 16.
//
// OFF-build, same reasoning as test_spl_session.cpp itself.
#include <catch2/catch_test_macros.hpp>

#include "measure/SplSession.h"

#include "rta/dsp/Weighting.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using rta::measure::SplConfig;
using rta::measure::SplSession;

namespace {

constexpr double kFs = 48000.0;

/// `count` metrics, the LAST of which is C-weighted and every earlier one
/// A-weighted -- so a cap that drops the tail, or a fill that gives up and
/// lets the publish fall back, shows up as the C metric reading A numbers.
SplConfig manyMetrics(std::size_t count) {
    SplConfig config;
    config.blockSeconds = 0.1;
    for (std::size_t i = 0; i < count; ++i) {
        const bool last = (i + 1 == count);
        config.metrics.push_back(rta::measure::SplMetricSpec{
            last ? "LCeq_last" : ("LAeq_" + std::to_string(i)),
            last ? rta::dsp::WeightingType::C : rta::dsp::WeightingType::A,
            rta::meter::TimeWeighting::Fast, 4});
    }
    return config;
}

/// A 100 Hz sine, where A and C weighting differ by about 19 dB -- far more
/// than any tolerance question can blur.
std::vector<float> lowSine(std::size_t count) {
    std::vector<float> x(count);
    for (std::size_t n = 0; n < count; ++n) {
        const double t = static_cast<double>(n) / kFs;
        x[n] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * 100.0 * t));
    }
    return x;
}

}  // namespace

TEST_CASE("the metric list is capped at construction, and the refusal is COUNTED",
          "[splsession]") {
    // DEFECT 1 FROM THE PR #17 VERIFIER. `SplConfig::metrics` was an unbounded
    // vector validated nowhere, while the publish path's per-metric window
    // storage is a fixed array of kMaxMetrics. At 17 metrics
    // `fillMetricWindows` returned 0 -- all-or-nothing -- so `metricWindows`
    // arrived EMPTY and every metric fell back to the first chain: the
    // C-weighted metric was published A-weighted numbers under a C label,
    // 18.8 dB wrong. That is e35f121's defect re-opened one index above the
    // array bound.
    //
    // Fixed three ways at once, and each one closes it alone:
    //   (a) the config is TRUNCATED to kMaxMetrics here, so the publish path
    //       cannot be handed more metrics than it has storage for;
    //   (b) fillMetricWindows fills the FIRST N instead of giving up; and
    //   (c) buildSplBlockView no longer falls back when metricWindows is
    //       non-empty but short -- a missing entry is ABSENCE.
    // Three layers because the failure was silent and 18.8 dB wide.
    SplSession session;
    const int channels[] = {0};

    SECTION("exactly at the cap: everything is accepted") {
        session.start(manyMetrics(SplConfig::kMaxMetrics), kFs, channels);
        REQUIRE(session.config() != nullptr);
        CHECK(session.config()->metrics.size() == SplConfig::kMaxMetrics);
        CHECK(session.refusedMetrics() == 0);
    }

    SECTION("one over the cap: TRUNCATED, and the count is published") {
        // TRUNCATION, not refusal of the whole session, and the choice is
        // deliberate: a misconfiguration that silenced SPL logging outright
        // would lose a show's evidence, which is worse than logging the first
        // sixteen. It is only acceptable BECAUSE the count is reported --
        // `refusedMetrics()` here, and `SplBlockView::refusedMetrics` in the
        // published snapshot, so nothing is dropped silently.
        session.start(manyMetrics(SplConfig::kMaxMetrics + 1), kFs, channels);
        REQUIRE(session.config() != nullptr);
        CHECK(session.config()->metrics.size() == SplConfig::kMaxMetrics);
        CHECK(session.refusedMetrics() == 1);
        // The dropped one is the LAST, and it is the C-weighted one -- so the
        // chain for C is not built either, and no surviving metric can be
        // handed C numbers or a C label by accident.
        for (const auto& spec : session.config()->metrics) {
            CHECK(spec.weighting == rta::dsp::WeightingType::A);
            CHECK(spec.id != "LCeq_last");
        }
    }

    SECTION("far over the cap") {
        session.start(manyMetrics(64), kFs, channels);
        REQUIRE(session.config() != nullptr);
        CHECK(session.config()->metrics.size() == SplConfig::kMaxMetrics);
        CHECK(session.refusedMetrics() == 64 - SplConfig::kMaxMetrics);
    }
}

TEST_CASE("fillMetricWindows fills the first N and says how many, never all-or-nothing",
          "[splsession]") {
    // The all-or-nothing return was the mechanism: a 0 meant "no per-metric
    // windows", which the publish path read as "single weighting, use the
    // shared window".
    SplSession session;
    const int channels[] = {0};
    session.start(manyMetrics(SplConfig::kMaxMetrics), kFs, channels);
    REQUIRE(session.config()->metrics.size() == SplConfig::kMaxMetrics);

    const auto hop = lowSine(4800);
    for (int i = 0; i < 6; ++i) session.feedHop(0, hop);

    SECTION("storage exactly the right size") {
        std::array<std::span<const rta::meter::Block>, SplConfig::kMaxMetrics> windows{};
        CHECK(session.fillMetricWindows(0, windows) == SplConfig::kMaxMetrics);
        // The last metric is C-weighted and must get the C chain, which is a
        // DIFFERENT buffer from the A chain the other fifteen share.
        const auto aWindow = session.window(0, rta::dsp::WeightingType::A);
        const auto cWindow = session.window(0, rta::dsp::WeightingType::C);
        REQUIRE_FALSE(aWindow.empty());
        REQUIRE_FALSE(cWindow.empty());
        CHECK(aWindow.data() != cWindow.data());
        CHECK(windows[0].data() == aWindow.data());
        CHECK(windows[SplConfig::kMaxMetrics - 1].data() == cWindow.data());
    }

    SECTION("storage SHORTER than the metric list fills what it can and reports it") {
        std::array<std::span<const rta::meter::Block>, 4> tooSmall{};
        CHECK(session.fillMetricWindows(0, tooSmall) == 4);
        const auto aWindow = session.window(0, rta::dsp::WeightingType::A);
        for (const auto& w : tooSmall) CHECK(w.data() == aWindow.data());
    }

    SECTION("a channel nothing logs fills nothing") {
        std::array<std::span<const rta::meter::Block>, SplConfig::kMaxMetrics> windows{};
        CHECK(session.fillMetricWindows(7, windows) == SplConfig::kMaxMetrics);
        // Filled, but with EMPTY spans: the count says how many rows were
        // written, not that any of them has data.
        for (const auto& w : windows) CHECK(w.empty());
    }
}
