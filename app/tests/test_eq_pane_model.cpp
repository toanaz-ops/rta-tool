// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-EQ UI task T2 (docs/plans/2026-09-29-eq-ui-lane-plan.md): how a stored
// Trace becomes an EqSession measurement. JUCE-free. Every expected number is
// a closed form: bin k of a 64-point, 48 kHz trace is at k*750 Hz; a
// magnitude of 20*log10(0.5) dB at phase pi/2 radians is the complex number
// 0.5i; a -3 dB/octave tilt is -3*log2(f/f0) at any frequency.
#include "EqTraceFixture.h"

#include "measure/EqPaneModel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

using eqfixture::kFs;
using eqfixture::makeEqTrace;
using rta::measure::EqPaneModel;
using rta::trace::TraceLibrary;

namespace {

constexpr int kFft64 = 64;
constexpr std::size_t kBins64 = 33;  // pointCountFor(64)

std::string addTrace(TraceLibrary& library, rta::trace::Trace trace, const std::string& name) {
    return library.add(std::move(trace), name, "default");
}

/// 33 bins at 48 kHz, flat magnitude, coherence 0.69 on bins 0-16 and 0.70 on
/// bins 17-32, phase pi/2 rad everywhere.
rta::trace::Trace gatedTrace(const std::string& id, float magnitudeDb = 0.0f) {
    std::vector<float> coherence(kBins64, 0.69f);
    for (std::size_t k = 17; k < kBins64; ++k) coherence[k] = 0.70f;
    return makeEqTrace(id, kFft64, kFs, std::vector<float>(kBins64, magnitudeDb), coherence,
                       std::vector<float>(kBins64, static_cast<float>(std::numbers::pi / 2.0)));
}

}  // namespace

TEST_CASE("EqPaneModel: a 64-point 48 kHz trace has 33 bins at exactly k*750 Hz", "[eq_pane_model]") {
    TraceLibrary library;
    const auto id = addTrace(library, gatedTrace("m1"), "M1");
    EqPaneModel model;
    REQUIRE(model.selectMeasurement(library, id));

    const auto hz = model.session().hz();
    REQUIRE(hz.size() == 33);
    bool exact = true;
    for (std::size_t k = 0; k < hz.size(); ++k) {
        // 48000/64 = 750, a float-exact bin width: the product is bitwise.
        exact = exact && (hz[k] == static_cast<float>(static_cast<double>(k) * 750.0));
    }
    CHECK(exact);
    CHECK(model.sampleRate() == kFs);
}

TEST_CASE("EqPaneModel: the trust floor is >=, so 0.70 is trusted and 0.69 is not", "[eq_pane_model]") {
    TraceLibrary library;
    const auto id = addTrace(library, gatedTrace("m1"), "M1");
    EqPaneModel model;
    REQUIRE(model.selectMeasurement(library, id));

    const auto trusted = model.session().trusted();
    REQUIRE(trusted.size() == 33);
    CHECK(std::count(trusted.begin(), trusted.end(), std::uint8_t{ 1 }) == 16);
    CHECK(trusted[16] == 0);  // 0.69
    CHECK(trusted[17] == 1);  // 0.70, the floor itself
    CHECK(model.trustReadout().find("16 of 33 bins trusted") != std::string::npos);
}

TEST_CASE("EqPaneModel: stored phase is RADIANS -- m = 20*log10(0.5), phi = pi/2 is H = 0.5i",
          "[eq_pane_model]") {
    TraceLibrary library;
    const float half = static_cast<float>(20.0 * std::log10(0.5));
    const auto id = addTrace(library, gatedTrace("m1", half), "M1");
    EqPaneModel model;
    REQUIRE(model.selectMeasurement(library, id));

    const auto h = model.hHalfGrid();
    REQUIRE(h.size() == 33);
    for (std::size_t k = 0; k < h.size(); ++k) {
        CHECK(std::abs(h[k]) == Catch::Approx(0.5).margin(1e-6));
        // Read as degrees, pi/2 would be 0.0274 rad and this would fail.
        CHECK(std::arg(h[k]) == Catch::Approx(std::numbers::pi / 2.0).margin(1e-6));
    }
    CHECK(model.gateReadout() == "G24 gate on");
}

TEST_CASE("EqPaneModel: a trace with no phase has an empty H and says the G24 gate is off",
          "[eq_pane_model]") {
    TraceLibrary library;
    const auto id = addTrace(
        library,
        makeEqTrace("nophase", kFft64, kFs, std::vector<float>(kBins64, 0.0f),
                    std::vector<float>(kBins64, 0.95f)),
        "NoPhase");
    EqPaneModel model;
    REQUIRE(model.selectMeasurement(library, id));
    CHECK(model.hHalfGrid().empty());
    CHECK(model.gateReadout().find("G24 gate off") != std::string::npos);
}

TEST_CASE("EqPaneModel: a trace with no coherence is a target, never a measurement", "[eq_pane_model]") {
    TraceLibrary library;
    const auto rtaId = addTrace(library, makeEqTrace("rta", kFft64, kFs, std::vector<float>(kBins64, -20.0f)),
                                "RTA @ 12:00:00");
    const auto tfId = addTrace(library, gatedTrace("tf"), "TRANSFER @ 12:00:01");

    const auto measurements = EqPaneModel::measurementCandidateIds(library);
    REQUIRE(measurements.size() == 1);
    CHECK(measurements.front() == tfId);

    const auto targets = EqPaneModel::targetCandidateIds(library);
    CHECK(targets == std::vector<std::string>{ rtaId, tfId });

    EqPaneModel model;
    CHECK_FALSE(model.selectMeasurement(library, rtaId));
    CHECK(model.status().find("no coherence") != std::string::npos);
    CHECK_FALSE(model.hasMeasurement());

    SECTION("a hidden trace is offered as neither") {
        REQUIRE(library.setVisible(tfId, false));
        CHECK(EqPaneModel::measurementCandidateIds(library).empty());
        CHECK(EqPaneModel::targetCandidateIds(library) == std::vector<std::string>{ rtaId });
    }
}

TEST_CASE("EqPaneModel: FLAT is the default target; a same-grid target is copied bitwise",
          "[eq_pane_model]") {
    TraceLibrary library;
    const auto mid = addTrace(library, gatedTrace("m1"), "M1");
    std::vector<float> shaped(kBins64);
    for (std::size_t k = 0; k < kBins64; ++k) shaped[k] = 0.1f * static_cast<float>(k) - 1.7f;
    const auto tid = addTrace(library, makeEqTrace("t1", kFft64, kFs, shaped), "Target");

    EqPaneModel model;
    REQUIRE(model.selectMeasurement(library, mid));
    REQUIRE(model.targetDb().size() == kBins64);
    CHECK(std::all_of(model.targetDb().begin(), model.targetDb().end(), [](float v) { return v == 0.0f; }));

    REQUIRE(model.selectTarget(library, tid));
    REQUIRE(model.targetDb().size() == kBins64);
    CHECK(std::equal(shaped.begin(), shaped.end(), model.targetDb().begin()));  // bitwise

    REQUIRE(model.selectTarget(library, ""));  // back to FLAT
    CHECK(std::all_of(model.targetDb().begin(), model.targetDb().end(), [](float v) { return v == 0.0f; }));
}

TEST_CASE("EqPaneModel: a -3 dB/oct tilt on another grid resamples to -3*log2(f/f0)", "[eq_pane_model]") {
    // Target on a 128-point grid (bins at k*375 Hz), measurement on 64 points
    // (k*750 Hz). Linear in log10(f) and in dB is exact for a straight tilt,
    // so the only slack is the float storage of the source.
    constexpr int kFft128 = 128;
    constexpr double kF0 = 1000.0;
    std::vector<float> tilt(65);
    for (std::size_t k = 1; k < tilt.size(); ++k) {
        tilt[k] = static_cast<float>(-3.0 * std::log2(static_cast<double>(k) * 375.0 / kF0));
    }
    tilt[0] = tilt[1];  // DC has no place on a log axis; the resampler skips it

    TraceLibrary library;
    const auto mid = addTrace(library, gatedTrace("m1"), "M1");
    const auto tid = addTrace(library, makeEqTrace("tilt", kFft128, kFs, tilt), "Tilt");

    EqPaneModel model;
    REQUIRE(model.selectMeasurement(library, mid));
    REQUIRE(model.selectTarget(library, tid));

    const auto target = model.targetDb();
    REQUIRE(target.size() == kBins64);
    for (std::size_t k = 1; k < kBins64; ++k) {  // 750 Hz .. Nyquist, inside the source's range
        const double expected = -3.0 * std::log2(static_cast<double>(k) * 750.0 / kF0);
        CHECK(static_cast<double>(target[k]) == Catch::Approx(expected).margin(1e-4));
    }
}

TEST_CASE("EqPaneModel: a picked trace that leaves the library is labelled, not lost",
          "[eq_pane_model]") {
    TraceLibrary library;
    const auto id = addTrace(library, gatedTrace("m1"), "M1");
    EqPaneModel model;
    REQUIRE(model.selectMeasurement(library, id));
    REQUIRE(model.measurement().name == "M1");
    CHECK_FALSE(model.measurement().removed);

    REQUIRE(library.setVisible(id, false));
    model.syncLibrary(library);
    CHECK(model.measurement().removed);  // the view appends "(removed)"
    CHECK(model.hasMeasurement());       // the model kept its own copy
    CHECK(model.session().hz().size() == 33);
}

TEST_CASE("EqPaneModel: AUTO EQ on an 8 dB bump commits and the ghost lands nearer the target",
          "[eq_pane_model]") {
    TraceLibrary library;
    const auto id = addTrace(library, eqfixture::makeBumpTrace("bump"), "Bump");
    EqPaneModel model;
    REQUIRE(model.selectMeasurement(library, id));
    model.runAutoEq();
    REQUIRE_FALSE(model.session().committed().empty());
    CHECK(model.status().find("AUTO EQ:") != std::string::npos);

    // |ghost - target| < |measured - target| at the bump (bin nearest 1 kHz =
    // 1000/23.4375 = 42.67 -> bin 43). The identity is EqSession's own
    // (test_eq_session.cpp), reached here through the stored-trace path.
    const std::size_t bump = 43;
    const auto ghost = model.session().ghostDb();
    const double measured = static_cast<double>(model.measuredDb()[bump]);
    CHECK(std::abs(ghost[bump] - static_cast<double>(model.targetDb()[bump])) <
          std::abs(measured - static_cast<double>(model.targetDb()[bump])));

    // The correction strip is exactly ghost - measured.
    const auto correction = model.correctionDb();
    CHECK(correction[bump] == Catch::Approx(ghost[bump] - measured).margin(1e-12));
    // ...and the target line sits at target + c (T0), with c the session's own.
    CHECK(model.targetLineDb()[bump] ==
          Catch::Approx(static_cast<double>(model.targetDb()[bump]) + model.session().levelOffsetDb())
              .margin(1e-12));
}

TEST_CASE("EqPaneModel: G24 excessPhase cost at fftSize 4096 is measured, not assumed",
          "[eq_pane_model]") {
    // Plan risk 2: excessPhase runs 128x oversampled, so a 4096-point trace
    // asks for a 524288-point transform on whatever thread calls AUTO EQ.
    // A -8 dB dip needs a BOOST, and only boosts reach the G24 gate.
    // The timing is REPORTED (WARN), not asserted: a wall-clock bound is a
    // flaky gate; the PR body carries the number and the decision it drives.
    std::vector<float> magnitude(2049, 0.0f);
    std::vector<float> coherence(2049, 0.95f);
    coherence[0] = 0.0f;
    const auto dip = eqfixture::bumpDb(eqfixture::linearHalfGrid(2049), 1000.0, -8.0, 0.5);
    for (std::size_t k = 0; k < magnitude.size(); ++k) magnitude[k] = dip[k];

    TraceLibrary library;
    const auto id = addTrace(
        library, makeEqTrace("g24", 4096, kFs, magnitude, coherence, std::vector<float>(2049, 0.0f)), "G24");
    EqPaneModel model;
    REQUIRE(model.selectMeasurement(library, id));
    REQUIRE_FALSE(model.hHalfGrid().empty());

    const auto t0 = std::chrono::steady_clock::now();
    model.runAutoEq();
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    WARN("G24 gate, AUTO EQ at fftSize 4096: " << ms << " ms");
    CHECK(model.status().find("AUTO EQ:") != std::string::npos);
}
