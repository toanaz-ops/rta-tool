// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-EQ UI wave A, task T2 (docs/plans/2026-09-29-eq-ui-lane-plan.md): the
// STATE MACHINE of EqPaneModel -- what each operator action changes, what it
// says, and what it leaves alone. test_eq_pane_model.cpp proves the numbers a
// stored Trace becomes; this file proves the behaviour around them. It exists
// because `tools/diffmut.py` deleted 60 of the lines the pane model adds and
// nothing in the JUCE-free suite noticed (the JUCE pane tests do, but only in
// an ON build). Every expected string is the one the code is documented to
// produce; every count is derived from the fixture, never read off the model.
#include "EqTraceFixture.h"

#include "measure/EqFirDesign.h"
#include "measure/EqPaneModel.h"
#include "measure/EqTraceGrid.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using eqfixture::kFs;
using eqfixture::makeBumpTrace;
using eqfixture::makeEqTrace;
using rta::measure::EqPaneModel;
using rta::trace::TraceLibrary;

namespace {

constexpr int kFft64 = 64;
constexpr std::size_t kBins64 = 33;

struct Rig {
    TraceLibrary library;
    EqPaneModel model;
    std::string bump;  ///< an 8 dB bump with phase: AUTO EQ places something
    std::string flat;  ///< 0 dB everywhere, coherence 0.95, no phase: nothing to correct
    Rig() {
        bump = library.add(makeBumpTrace("bump", 2048, true), "Bump", "g");
        flat = library.add(makeEqTrace("flat", kFft64, kFs, std::vector<float>(kBins64, 0.0f),
                                       std::vector<float>(kBins64, 0.95f)),
                           "Flat", "g");
    }
    /// Whether `op` advanced the revision (the view redraws on it).
    template <typename Op>
    bool moves(Op op) {
        const auto before = model.revision();
        op();
        return model.revision() > before;
    }
};

}  // namespace

TEST_CASE("EqPaneModel: every state change advances the revision; a no-op does not", "[eq_pane_model_state]") {
    Rig r;
    auto& m = r.model;
    CHECK(r.moves([&] { CHECK_FALSE(m.selectMeasurement(r.library, "nope")); }));  // a refusal changes the status
    CHECK(r.moves([&] { CHECK(m.selectMeasurement(r.library, r.bump)); }));
    CHECK(r.moves([&] { CHECK_FALSE(m.selectTarget(r.library, "nope")); }));
    CHECK(r.moves([&] { CHECK(m.selectTarget(r.library, r.flat)); }));
    CHECK(r.moves([&] { CHECK(m.selectTarget(r.library, "")); }));
    CHECK(r.moves([&] { m.setMaxFilters(3); }));
    CHECK(r.moves([&] { m.suggest(); }));
    REQUIRE_FALSE(m.chips().empty());
    CHECK(r.moves([&] { m.declineChip(0); }));
    CHECK(r.moves([&] { m.clearDeclined(); }));
    CHECK(r.moves([&] { m.suggest(); }));
    REQUIRE_FALSE(m.chips().empty());
    CHECK(r.moves([&] { m.acceptChip(0); }));
    CHECK(r.moves([&] { m.setApplied(0, true); }));
    CHECK(r.moves([&] { m.clearFilters(); }));
    CHECK(r.moves([&] { m.runAutoEq(); }));
    CHECK(r.moves([&] { m.setFirPhase(rta::dsp::FirPhase::Linear); }));
    CHECK(r.moves([&] { m.setFirTaps(4095); }));
    CHECK(r.moves([&] { m.setPeakNormalised(true); }));
    CHECK(r.moves([&] { m.setStatus("x"); }));

    // No-ops: a chip index past the list, and a library that has not changed.
    const auto before = m.revision();
    m.acceptChip(99);
    m.declineChip(99);
    m.syncLibrary(r.library);
    CHECK(m.revision() == before);

    // Library edits the pane must redraw for: a rename, and a pick that leaves.
    REQUIRE(r.library.rename(r.bump, "Renamed"));
    CHECK(r.moves([&] { m.syncLibrary(r.library); }));
    CHECK(m.measurement().name == "Renamed");
    REQUIRE(r.library.setVisible(r.bump, false));
    CHECK(r.moves([&] { m.syncLibrary(r.library); }));
    CHECK(m.measurement().removed);
    REQUIRE(r.library.setVisible(r.bump, true));
    CHECK(r.moves([&] { m.syncLibrary(r.library); }));
    CHECK_FALSE(m.measurement().removed);
}

TEST_CASE("EqPaneModel: the readout says what happened, in the words the pane shows", "[eq_pane_model_state]") {
    Rig r;
    auto& m = r.model;
    CHECK(m.status().find("STORE on TRANSFER with a reference") != std::string::npos);

    m.runAutoEq();
    CHECK(m.status() == "AUTO EQ: pick a measurement first");
    m.suggest();
    CHECK(m.status() == "SUGGEST: pick a measurement first");
    CHECK(m.chips().empty());

    CHECK_FALSE(m.selectMeasurement(r.library, "nope"));
    CHECK(m.status() == "EQ: that trace is not in the library");
    CHECK_FALSE(m.selectTarget(r.library, "nope"));
    CHECK(m.status() == "EQ: that target trace is not usable");

    REQUIRE(m.selectMeasurement(r.library, r.flat));
    CHECK(m.status() == "EQ: measurement \"Flat\" -- press AUTO EQ or SUGGEST");
    m.runAutoEq();  // flat against a flat target: |working residual| never reaches 0.2 dB
    CHECK(m.status() == "AUTO EQ: nothing to correct");
    m.suggest();
    CHECK(m.status() == "SUGGEST: nothing to correct");
    CHECK(m.chips().empty());

    REQUIRE(m.selectMeasurement(r.library, r.bump));
    m.runAutoEq();
    REQUIRE_FALSE(m.session().committed().empty());
    CHECK(m.status() == "AUTO EQ: " + std::to_string(m.session().committed().size()) + " filter(s) -- see the ghost curve");
    m.suggest();
    REQUIRE_FALSE(m.chips().empty());
    CHECK(m.status() == "SUGGEST: " + std::to_string(m.chips().size()) + " candidate(s)");
}

TEST_CASE("EqPaneModel: a trace it cannot use is refused by name, and picks nothing", "[eq_pane_model_state]") {
    TraceLibrary library;
    std::vector<float> nan(kBins64, 0.0f);
    nan[5] = std::numeric_limits<float>::quiet_NaN();
    const auto bad = library.add(makeEqTrace("nan", kFft64, kFs, nan, std::vector<float>(kBins64, 0.95f)), "NaN", "g");
    const auto noRate = library.add(makeEqTrace("norate", kFft64, 0.0, std::vector<float>(kBins64, 0.0f),
                                                std::vector<float>(kBins64, 0.95f)),
                                    "NoRate", "g");
    EqPaneModel m;
    for (const auto& id : { bad, noRate }) {
        CHECK_FALSE(m.selectMeasurement(library, id));
        CHECK(m.status() == "EQ: this trace has no usable frequency axis or non-finite values");
        CHECK_FALSE(m.hasMeasurement());
        CHECK(m.measurement().id.empty());
    }
    CHECK_FALSE(m.selectTarget(library, bad));  // a NaN target is refused too
    CHECK(m.target().id.empty());
}

TEST_CASE("EqPaneModel: picking a measurement resets what belonged to the last one", "[eq_pane_model_state]") {
    Rig r;
    auto& m = r.model;
    REQUIRE(m.selectMeasurement(r.library, r.bump));
    CHECK_FALSE(m.hHalfGridForTest().empty());
    CHECK(m.measurement().id == r.bump);
    CHECK(m.measurement().name == "Bump");
    m.suggest();
    REQUIRE_FALSE(m.chips().empty());

    REQUIRE(m.selectMeasurement(r.library, r.flat));  // no phase
    CHECK(m.hHalfGridForTest().empty());               // the old H is gone, not merely shadowed
    CHECK(m.chips().empty());                           // chips were ranked against the old measurement
    CHECK(m.measurement().id == r.flat);

    // A pick that left the library reads "removed" until it is picked again.
    REQUIRE(r.library.setVisible(r.flat, false));
    m.syncLibrary(r.library);
    REQUIRE(m.measurement().removed);
    REQUIRE(r.library.setVisible(r.flat, true));
    REQUIRE(m.selectMeasurement(r.library, r.flat));
    CHECK_FALSE(m.measurement().removed);
}

TEST_CASE("EqPaneModel: the target survives a refused pick, resets to FLAT, and waits for a measurement",
          "[eq_pane_model_state]") {
    Rig r;
    auto& m = r.model;
    std::vector<float> shaped(kBins64);
    for (std::size_t k = 0; k < kBins64; ++k) shaped[k] = 0.2f * static_cast<float>(k) - 2.0f;
    const auto shape = r.library.add(makeEqTrace("shape", kFft64, kFs, shaped), "Shape", "g");
    const auto flat64 = r.library.add(makeEqTrace("m64", kFft64, kFs, std::vector<float>(kBins64, 0.0f),
                                                  std::vector<float>(kBins64, 0.95f)),
                                      "M64", "g");

    // Picked BEFORE any measurement: stored, and applied once one arrives.
    REQUIRE(m.selectTarget(r.library, shape));
    CHECK(m.target().id == shape);
    CHECK(m.target().name == "Shape");
    CHECK_FALSE(m.hasMeasurement());
    REQUIRE(m.selectMeasurement(r.library, flat64));
    REQUIRE(m.targetDbForTest().size() == kBins64);
    CHECK(std::equal(shaped.begin(), shaped.end(), m.targetDbForTest().begin()));

    // Chips are ranked against the target: changing it drops them.
    REQUIRE(m.selectMeasurement(r.library, r.bump));
    m.suggest();
    REQUIRE_FALSE(m.chips().empty());
    REQUIRE(m.selectTarget(r.library, shape));
    CHECK(m.chips().empty());
    REQUIRE(m.selectMeasurement(r.library, flat64));  // the shaped target must follow to this grid
    CHECK(std::equal(shaped.begin(), shaped.end(), m.targetDbForTest().begin()));

    // A refused pick changes nothing about the target already chosen.
    CHECK_FALSE(m.selectTarget(r.library, "nope"));
    CHECK(m.target().id == shape);
    CHECK(std::equal(shaped.begin(), shaped.end(), m.targetDbForTest().begin()));

    // FLAT clears the pick AND the stored curve: re-picking the measurement
    // must not bring the old target back.
    REQUIRE(m.selectTarget(r.library, ""));
    CHECK(m.target().id.empty());
    CHECK(m.target().name.empty());
    REQUIRE(m.selectMeasurement(r.library, flat64));
    CHECK(std::all_of(m.targetDbForTest().begin(), m.targetDbForTest().end(), [](float v) { return v == 0.0f; }));
}

TEST_CASE("EqPaneModel: ACCEPT commits the chip's own spec; out-of-range indices do nothing",
          "[eq_pane_model_state]") {
    Rig r;
    auto& m = r.model;
    REQUIRE(m.selectMeasurement(r.library, r.bump));
    m.suggest();
    REQUIRE_FALSE(m.chips().empty());

    m.acceptChip(m.chips().size());  // one past the end
    m.declineChip(m.chips().size());
    CHECK(m.session().committed().empty());
    CHECK(std::all_of(m.session().excluded().begin(), m.session().excluded().end(),
                      [](std::uint8_t e) { return e == 0; }));

    const auto top = m.chips().front().spec;
    m.acceptChip(0);
    REQUIRE(m.session().committed().size() == 1);
    const auto& landed = m.session().committed().front();
    CHECK(landed.spec.type == top.type);
    CHECK(landed.spec.fcHz == top.fcHz);
    CHECK(landed.spec.q == top.q);
    CHECK(landed.spec.gainDb == top.gainDb);
    CHECK_FALSE(landed.applied);
}

TEST_CASE("EqPaneModel: DECLINE excludes the chip's band and commits nothing", "[eq_pane_model_state]") {
    Rig r;
    auto& m = r.model;
    REQUIRE(m.selectMeasurement(r.library, r.bump));
    m.suggest();
    REQUIRE_FALSE(m.chips().empty());
    const double declined = m.chips().front().spec.fcHz;
    m.declineChip(0);

    // The bin nearest the declined centre is now excluded.
    const auto hz = m.session().hz();
    std::size_t nearest = 0;
    for (std::size_t k = 1; k < hz.size(); ++k) {
        if (std::abs(hz[k] - declined) < std::abs(hz[nearest] - declined)) nearest = k;
    }
    CHECK(m.session().excluded()[nearest] != 0);
    for (const auto& chip : m.chips()) CHECK(chip.spec.fcHz != declined);  // re-ranked without it
    CHECK(m.session().committed().empty());
}

TEST_CASE("EqPaneModel: marking, clearing and un-declining drop the chips ranked before them", "[eq_pane_model_state]") {
    Rig r;
    auto& m = r.model;
    REQUIRE(m.selectMeasurement(r.library, r.bump));
    const auto rerank = [&] {
        m.suggest();
        REQUIRE_FALSE(m.chips().empty());
    };
    rerank();
    m.acceptChip(0);
    REQUIRE(m.session().committed().size() == 1);

    // An applied filter leaves the ghost and the residual (CommittedFilter),
    // so the bump is un-corrected again and there is something to suggest.
    m.setApplied(0, true);
    CHECK(m.session().committed().front().applied);
    rerank();
    m.setApplied(0, false);
    CHECK_FALSE(m.session().committed().front().applied);
    CHECK(m.chips().empty());

    rerank();
    m.clearDeclined();
    CHECK(m.chips().empty());

    rerank();
    m.clearFilters();
    CHECK(m.chips().empty());
    CHECK(m.session().committed().empty());
    CHECK_FALSE(m.canExportList());
}

TEST_CASE("EqPaneModel: readouts and export gates", "[eq_pane_model_state]") {
    Rig r;
    auto& m = r.model;
    CHECK(m.trustReadout().empty());
    CHECK(m.gateReadout().empty());
    CHECK(m.limitsReadout() == "boost <= +6.0 dB   Q <= 10 boost / 20 cut");

    REQUIRE(m.selectMeasurement(r.library, r.flat));
    CHECK(m.trustReadout() == "33 of 33 bins trusted (coherence >= 0.70)");
    CHECK(m.gateReadout().find("G24 gate off") != std::string::npos);

    REQUIRE(m.selectMeasurement(r.library, r.bump));
    m.runAutoEq();
    REQUIRE_FALSE(m.session().committed().empty());
    CHECK(m.canExportList());
    CHECK(m.normalization() == rta::firexport::Normalization::AsDesigned);

    // canExportFir is the conjunction of four: a measurement, a phase, a
    // length, and something not yet applied.
    CHECK_FALSE(m.canExportFir());
    m.setFirPhase(rta::dsp::FirPhase::Minimum);
    CHECK(m.firPhase() == rta::dsp::FirPhase::Minimum);
    CHECK_FALSE(m.canExportFir());
    m.setFirTaps(1023);
    CHECK(m.firTaps() == 1023);
    CHECK(m.canExportFir());
    m.setPeakNormalised(true);
    CHECK(m.normalization() == rta::firexport::Normalization::Peak0dBFS);
    m.setFirPhase(std::nullopt);
    CHECK_FALSE(m.canExportFir());
    m.setFirPhase(rta::dsp::FirPhase::Linear);
    for (std::size_t i = 0; i < m.session().committed().size(); ++i) m.setApplied(i, true);
    CHECK_FALSE(m.canExportFir());  // nothing left to realise
    CHECK(m.canExportList());       // but the list still records them
}

TEST_CASE("EqPaneModel: the FIR lengths offered are odd, designable, and labelled with fs/N and (N-1)/(2fs)",
          "[eq_pane_model_state]") {
    // Odd because FirDesign realises an even length at twice the gain; each
    // must fit its own design grid (taps <= M/2, FirDesign.h).
    for (const std::size_t taps : rta::measure::kEqFirTapChoices) {
        CAPTURE(taps);
        CHECK(taps % 2 == 1);
        CHECK(taps <= rta::measure::eqFirGridSize(taps) / 2);
    }
    CHECK(rta::measure::kEqFirTapChoices.front() < rta::measure::kEqFirTapChoices.back());
    // 48000/4095 = 11.72 -> "12 Hz"; 4094/96000 s = 42.65 ms -> "42.6 ms" (one decimal).
    CHECK(rta::measure::eqFirTapsLabel(4095, 48000.0) == "4095 taps  12 Hz  42.6 ms");
    // 44100/1023 = 43.1 -> "43 Hz"; 1022/88200 s = 11.59 ms -> "11.6 ms".
    CHECK(rta::measure::eqFirTapsLabel(1023, 44100.0) == "1023 taps  43 Hz  11.6 ms");
}

TEST_CASE("EqTraceGrid: a target with no usable bin is FLAT, and bins below its first read that bin's value",
          "[eq_pane_model_state]") {
    const std::vector<float> dst{ 0.0f, 750.0f, 1500.0f, 3000.0f };

    // No positive-frequency bin at all (DC only): nothing to interpolate, so
    // zeros -- not a throw from interpolateFirTargetDb's empty target.
    const std::vector<float> dcHz{ 0.0f };
    const std::vector<float> dcDb{ 5.0f };
    const auto flat = rta::measure::resampleTargetDb(dcHz, dcDb, dst);
    REQUIRE(flat.size() == dst.size());
    CHECK(std::all_of(flat.begin(), flat.end(), [](float v) { return v == 0.0f; }));

    // Source bins at 1500 and 3000 Hz plus a DC bin. Destination 0 and 750 Hz
    // lie below the first positive bin: clamped to its value, finite. (Were DC
    // kept in the curve, 750 Hz would interpolate from log10(0) = -inf.)
    const std::vector<float> srcHz{ 0.0f, 1500.0f, 3000.0f };
    const std::vector<float> srcDb{ 99.0f, -4.0f, -10.0f };
    const auto out = rta::measure::resampleTargetDb(srcHz, srcDb, dst);
    REQUIRE(out.size() == dst.size());
    CHECK(out[0] == -4.0f);
    CHECK(out[1] == -4.0f);
    CHECK(out[2] == Catch::Approx(-4.0).margin(1e-6));
    CHECK(out[3] == Catch::Approx(-10.0).margin(1e-6));

    CHECK(rta::measure::allFinite(std::vector<float>{}));
    CHECK_FALSE(rta::measure::allFinite(std::vector<float>{ 1.0f, std::numeric_limits<float>::infinity() }));
}

TEST_CASE("EqPaneModel: AUTO EQ drops stale chips; a target that left the library reads removed",
          "[eq_pane_model_state]") {
    Rig r;
    auto& m = r.model;
    REQUIRE(m.selectMeasurement(r.library, r.bump));
    m.suggest();
    REQUIRE_FALSE(m.chips().empty());
    m.runAutoEq();
    CHECK(m.chips().empty());  // ranked before the set the run replaced

    REQUIRE(m.selectTarget(r.library, r.flat));
    CHECK_FALSE(m.target().removed);
    REQUIRE(r.library.setVisible(r.flat, false));
    m.syncLibrary(r.library);
    CHECK(m.target().removed);
    CHECK(m.target().name == "Flat");  // the label survives, the numbers were copied
}
