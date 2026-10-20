// SPDX-License-Identifier: AGPL-3.0-or-later
//
// RT60-UI lane wave B (docs/plans/2026-10-20-rt60-ui-lane-plan.md), tasks T9
// (ensemble), T10 (the EDC the plot draws) and T11 (third-octave bands): "Option
// B", SweepIr objects built from closed-form decays (Rt60Fixture.h). JUCE-free.
//
// Bounds are the plan's DISCRIMINATION bounds (half the gap between the truth and
// the named mutant's answer), never tighter; accuracy is core's job. Every figure
// is printed beside its bound ("RT60-W lines").
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Rt60Fixture.h"
#include "measure/Rt60Analysis.h"
#include "measure/Rt60Bands.h"
#include "measure/Rt60EnsembleReadout.h"
#include "measure/SweepIrPipeline.h"
#include "rta/dsp/OctaveBands.h"
#include "rta/gen/Sweep.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

using namespace rta::measure;
namespace fx = rt60_fixture;

namespace {

constexpr const char* kTag = "[rt60_wave_b]";

double shippedL() { return rta::gen::Sweep(shippedSweepConfig(48000.0)).lengthConstantL(); }

fx::IrSpec roomSpec(double t60, std::uint32_t seed) {
    fx::IrSpec spec;
    spec.harmonicSpacingL = shippedL();
    spec.components = fx::flatRoom(t60, seed);
    spec.floorSeed = seed + 5;
    return spec;
}

const Rt60Band* bandAt(const Rt60Result& r, int nominalHz) {
    for (const auto& b : r.bands) {
        if (b.nominalHz == nominalHz) return &b;
    }
    return nullptr;
}

const Rt60EnsembleBand* ensembleBandAt(const Rt60EnsembleResult& r, int nominalHz) {
    for (const auto& b : r.bands) {
        if (b.nominalHz == nominalHz) return &b;
    }
    return nullptr;
}

}  // namespace

// --- T11: the band table -------------------------------------------------------

TEST_CASE("third-octave table: 50 Hz to 10 kHz, IEC 61260-1 labels, base-ten edges one third wide", kTag) {
    const auto bands = rt60ThirdOctaveBands();
    const std::vector<int> labels = {50,  63,  80,  100,  125,  160,  200,  250,  315,  400,  500,  630,
                                     800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000};
    REQUIRE(bands.size() == labels.size());
    for (std::size_t i = 0; i < bands.size(); ++i) {
        INFO("band " << i << " centre " << bands[i].centreHz);
        CHECK(bands[i].nominalHz == labels[i]);
        // Closed form: a third-octave band spans a factor 10^(3/30) = 10^0.1 and its
        // centre is the geometric mean of its edges.
        CHECK(bands[i].highHz / bands[i].lowHz == Catch::Approx(std::pow(10.0, 0.1)).epsilon(1e-12));
        CHECK(std::sqrt(bands[i].lowHz * bands[i].highHz) == Catch::Approx(bands[i].centreHz).epsilon(1e-12));
    }
    // 1000 Hz is the reference of the base-ten grid, exactly.
    CHECK(bands[13].centreHz == Catch::Approx(1000.0).epsilon(1e-12));
    // The set selector picks the table: octave stays 8 rows.
    CHECK(rt60Bands(Rt60BandSet::Octave).size() == 8);
    CHECK(rt60Bands(Rt60BandSet::Third).size() == 24);
}

TEST_CASE("third-octave bands under the shipped sweep: 50, 63 and 80 Hz refused by the 80 Hz edge, 100 Hz up answered", kTag) {
    const auto ir = fx::buildIr(roomSpec(0.80, 1000));  // trusted band 80 Hz .. 19.7 kHz, as shipped
    const Rt60Result r = analyseRt60(&ir, Rt60BandSet::Third);
    REQUIRE(r.analysed());
    REQUIRE(r.bands.size() == 24);
    CHECK(r.bandSet == Rt60BandSet::Third);
    std::printf("RT60-W thirds wall %.1f ms (24 bands, %zu samples)\n", r.wallMs, r.analysedSamples);
    for (const auto& b : r.bands) {
        std::printf("RT60-W third %5d Hz  edges %7.1f-%7.1f  T30 %s%.3f  T20 %s%.3f  B*T %6.1f  truth 0.80 bound +-0.40\n",
                    b.nominalHz, b.lowHz, b.highHz, b.t30.has() ? "" : "(refused) ", b.t30.seconds,
                    b.t20.has() ? "" : "(refused) ", b.t20.seconds, b.bandwidthTime);
    }
    // A band is trusted only when its LOWER edge is inside: the 80 Hz third spans
    // 70.8-89.1 Hz, so it is refused along with 50 and 63.
    for (const int hz : {50, 63, 80}) {
        const auto* b = bandAt(r, hz);
        REQUIRE(b != nullptr);
        CHECK(b->refusal == Rt60Refusal::OutsideTrustedBand);
        CHECK(rt60BandNote(*b, r).find("(80 Hz)") != std::string::npos);
    }
    for (const auto& b : r.bands) {
        if (b.nominalHz < 100) continue;
        INFO("third " << b.nominalHz << " Hz T30 " << b.t30.seconds << " T20 " << b.t20.seconds);
        REQUIRE(b.t30.has());
        REQUIRE(b.t20.has());
        CHECK(std::fabs(b.t30.seconds - 0.80) < 0.40);
        CHECK(std::fabs(b.t20.seconds - 0.80) < 0.40);
    }
}

TEST_CASE("a third-octave band too narrow for a 0.4 s decay refuses on B*T, 40 Hz and 50 Hz included", kTag) {
    // B*T < 6 refuses (core's bar). B = 0.2316 * centre for a third; T = 0.4 s:
    // 40 Hz -> 3.7, 50 Hz -> 4.6. The table starts at 50 Hz, so the 40 Hz third is
    // reached through the explicit-band entry point.
    fx::IrSpec spec;
    spec.harmonicSpacingL = shippedL();
    spec.trustedLowHz = 0.0;  // not stated: nothing clamped, so only B*T decides
    spec.trustedHighHz = 0.0;
    spec.components = fx::flatRoom(0.40);
    const rta::dsp::OctaveBands grid(3, 39.0, 41.0);
    REQUIRE(grid.size() == 1);
    const std::vector<Rt60BandEdges> forty = {
        {40, grid[0].centre, grid[0].lower, grid[0].upper}};
    // The 40 Hz third lies inside the 63 Hz octave component's skirt only partly;
    // give it its own decay so it has something to refuse about.
    spec.components.push_back({grid[0].lower, grid[0].upper, 0.40, 77});
    const auto ir = fx::buildIr(spec);
    const Rt60Result low = analyseRt60Bands(&ir, forty, Rt60BandSet::Third);
    REQUIRE(low.analysed());
    REQUIRE(low.bands.size() == 1);
    std::printf("RT60-W 40 Hz third at T 0.4: B*T %.2f (bar 6) refusal %d\n", low.bands[0].bandwidthTime,
                static_cast<int>(low.bands[0].refusal));
    CHECK(low.bands[0].refusal == Rt60Refusal::BandwidthTimeTooSmall);
    CHECK(rt60BandNote(low.bands[0], low).find("band too narrow for a decay this short") != std::string::npos);

    const Rt60Result table = analyseRt60(&ir, Rt60BandSet::Third);
    REQUIRE(table.analysed());
    const auto* fifty = bandAt(table, 50);
    REQUIRE(fifty != nullptr);
    std::printf("RT60-W 50 Hz third at T 0.4: B*T %.2f (bar 6)\n", fifty->bandwidthTime);
    CHECK(fifty->refusal == Rt60Refusal::BandwidthTimeTooSmall);
    const auto* wide = bandAt(table, 500);  // B = 116 Hz, B*T = 46: plenty
    REQUIRE(wide != nullptr);
    CHECK(wide->t30.has());
}

// --- T10: the curve the plot draws ---------------------------------------------

TEST_CASE("the plotted curve is the integrated, truncated decay: starts at 0 dB, falls monotonically, fits restate T", kTag) {
    const auto ir = fx::buildIr(roomSpec(0.80, 1000));
    const Rt60Result r = analyseRt60(&ir);
    REQUIRE(r.analysed());
    const auto* band = bandAt(r, 1000);
    REQUIRE(band != nullptr);
    REQUIRE(band->t30.has());
    const auto& edc = band->edc;
    REQUIRE(edc.valid());
    REQUIRE(edc.db.size() >= 100);
    CHECK(edc.db.size() <= 1024);
    CHECK(edc.stepSeconds > 0.0);
    // An energy decay curve is a backward integral of a non-negative quantity, so
    // it starts at 0 dB and never rises. An unintegrated energy-time curve starts
    // wherever the first sample is, and rises and falls with the noise.
    CHECK(edc.db.front() == Catch::Approx(0.0).margin(0.01));
    for (std::size_t i = 1; i < edc.db.size(); ++i) {
        INFO("point " << i);
        REQUIRE(edc.db[i] <= edc.db[i - 1] + 1e-3f);
    }
    // The curve falls past the T30 span's end, or core would not have fitted it.
    CHECK(edc.db.back() <= -35.0f);

    // The drawn fits restate core's own figures: slope -60/T, spans at the first
    // crossings of the named levels.
    for (const auto& [span, figure] :
         {std::pair{edc.t30, band->t30}, std::pair{edc.t20, band->t20}, std::pair{edc.edt, band->edt}}) {
        REQUIRE(span.valid);
        CHECK(-60.0 / span.slopeDbPerSec == Catch::Approx(figure.seconds).epsilon(1e-9));
        CHECK(span.endSec > span.startSec);
        const auto at = [&](double sec) {
            return edc.db[std::min(edc.db.size() - 1, static_cast<std::size_t>(sec / edc.stepSeconds))];
        };
        // Within one decimation step of the level (the plotted curve is thinned).
        const double slackDb = std::fabs(span.slopeDbPerSec) * edc.stepSeconds + 0.05;
        CHECK(std::fabs(at(span.startSec) - span.upperDb) <= slackDb + 0.5);
        CHECK(std::fabs(at(span.endSec) - span.lowerDb) <= slackDb + 0.5);
    }
    CHECK(edc.t30.upperDb == -5.0);
    CHECK(edc.t30.lowerDb == -35.0);
    CHECK(edc.t20.lowerDb == -25.0);
    CHECK(edc.edt.upperDb == 0.0);
    CHECK(edc.edt.lowerDb == -10.0);
    // A refused band carries no curve.
    const auto* refused = bandAt(r, 63);
    REQUIRE(refused != nullptr);
    CHECK_FALSE(refused->edc.valid());
}

// --- T9: the ensemble ----------------------------------------------------------

TEST_CASE("an ensemble of three captures: median across them, spread shown, same IR three times has none", kTag) {
    const auto a = fx::buildIr(roomSpec(0.80, 1000));
    const auto b = fx::buildIr(roomSpec(0.80, 2000));
    const auto c = fx::buildIr(roomSpec(0.80, 3000));
    const std::vector<const SweepIr*> three = {&a, &b, &c};
    const Rt60EnsembleResult ens = analyseRt60Ensemble(three);
    REQUIRE(ens.analysed());
    CHECK(ens.captureCount == 3);
    std::printf("RT60-W ensemble 3 octave wall %.1f ms\n", ens.wallMs);

    const Rt60Result ra = analyseRt60(&a);
    const Rt60Result rb = analyseRt60(&b);
    const Rt60Result rc = analyseRt60(&c);
    bool anySpread = false;
    for (const auto& band : ens.bands) {
        if (band.refusal == Rt60Refusal::OutsideTrustedBand) continue;
        const auto* x = bandAt(ra, band.nominalHz);
        const auto* y = bandAt(rb, band.nominalHz);
        const auto* z = bandAt(rc, band.nominalHz);
        REQUIRE(x != nullptr);
        REQUIRE(band.t30.has());
        // The identity: the ensemble's median is the middle of the three singles'
        // T30s, which this test reads from three independent analyses.
        std::vector<double> singles = {x->t30.seconds, y->t30.seconds, z->t30.seconds};
        std::sort(singles.begin(), singles.end());
        std::printf("RT60-W ens %5d Hz  median %.3f (singles %.3f %.3f %.3f)  IQR %.1f %%  truth 0.80 bound +-0.40\n",
                    band.nominalHz, band.t30.seconds, singles[0], singles[1], singles[2], band.t30.spreadPercent);
        CHECK(band.t30.seconds == Catch::Approx(singles[1]).epsilon(1e-12));
        CHECK(std::fabs(band.t30.seconds - 0.80) < 0.40);
        CHECK(band.t30.captures == 3);
        CHECK(band.t30.spreadIsMeaningful());
        anySpread = anySpread || band.t30.spreadPercent > 0.0;
        // Shown in the cell once meaningful.
        CHECK(rt60SpreadCell(band.t30).find("IQR ") != std::string::npos);
        CHECK(rt60SpreadCell(band.t30).find("%") != std::string::npos);
    }
    CHECK(anySpread);  // three different captures disagree by something

    // The same IR counted three times reports no spread at all: core's stated
    // contract, which is why the controller never lets an id in twice.
    const std::vector<const SweepIr*> same = {&a, &a, &a};
    const Rt60EnsembleResult cheat = analyseRt60Ensemble(same);
    REQUIRE(cheat.analysed());
    for (const auto& band : cheat.bands) {
        if (band.refusal == Rt60Refusal::OutsideTrustedBand) continue;
        CHECK(band.t30.spreadPercent == 0.0);
    }
    const auto* real1k = ensembleBandAt(ens, 1000);
    REQUIRE(real1k != nullptr);
    CHECK(real1k->t30.spreadPercent > 0.0);  // the discrimination: a real ensemble is not zero here
}

TEST_CASE("an ensemble of two shows the median and no spread; the header says why", kTag) {
    const auto a = fx::buildIr(roomSpec(0.80, 1000));
    const auto b = fx::buildIr(roomSpec(0.80, 2000));
    const std::vector<const SweepIr*> two = {&a, &b};
    const Rt60EnsembleResult ens = analyseRt60Ensemble(two);
    REQUIRE(ens.analysed());
    const Rt60Result ra = analyseRt60(&a);
    const Rt60Result rb = analyseRt60(&b);
    for (const auto& band : ens.bands) {
        if (band.refusal == Rt60Refusal::OutsideTrustedBand) continue;
        REQUIRE(band.t30.has());
        CHECK(band.t30.captures == 2);
        CHECK_FALSE(band.t30.spreadIsMeaningful());
        CHECK(band.t30.spreadPercent == 0.0);
        // Two points: core's median is their mean.
        const double mean = 0.5 * (bandAt(ra, band.nominalHz)->t30.seconds + bandAt(rb, band.nominalHz)->t30.seconds);
        CHECK(band.t30.seconds == Catch::Approx(mean).epsilon(1e-12));
        CHECK(rt60SpreadCell(band.t30).find("IQR") == std::string::npos);
        CHECK(rt60SpreadCell(band.t30).find(" s") != std::string::npos);
    }
    const auto header = rt60EnsembleHeaderLines(ens, {"first", "second"});
    REQUIRE(header.size() == 2);
    CHECK(header[0] == "ENSEMBLE 2 captures: first, second");
    CHECK(header[1].find("IQR needs 3 captures") != std::string::npos);
    CHECK(header[1].find(" \xc2\xb7 ") != std::string::npos);  // the separator is a real middle dot
    const auto lines = rt60EnsembleTableLines(ens);
    REQUIRE(lines.size() == 9);
    CHECK(lines[0].find("captures") != std::string::npos);
    CHECK(lines[1].find("below this sweep's trusted band (80 Hz)") != std::string::npos);
    CHECK(lines[3].find("2 of 2") != std::string::npos);

    const auto c = fx::buildIr(roomSpec(0.80, 3000));
    const std::vector<const SweepIr*> three = {&a, &b, &c};
    const auto header3 = rt60EnsembleHeaderLines(analyseRt60Ensemble(three), {"x", "y", "z"});
    CHECK(header3[1].find("IQR in percent of the median") != std::string::npos);
    CHECK(header3[1].find("IQR needs 3") == std::string::npos);
}

TEST_CASE("an ensemble refuses what it cannot average: no IR, different sample rates, no tail", kTag) {
    const auto a = fx::buildIr(roomSpec(0.80, 1000));
    auto other = fx::buildIr(roomSpec(0.80, 2000));
    other.sampleRate = 44100.0;
    const std::vector<const SweepIr*> mixed = {&a, &other};
    const auto rate = analyseRt60Ensemble(mixed);
    CHECK(rate.refusal == Rt60Refusal::NoIr);
    CHECK(rate.message.find("different sample rates") != std::string::npos);
    CHECK(rt60EnsembleRefusalText(rate).find("different sample rates") != std::string::npos);

    const std::vector<const SweepIr*> withNull = {&a, nullptr};
    const auto none = analyseRt60Ensemble(withNull);
    CHECK(none.refusal == Rt60Refusal::NoIr);
    CHECK(rt60EnsembleRefusalText(none).find("no IR in memory") != std::string::npos);

    auto noTail = fx::buildIr(roomSpec(0.80, 3000));
    noTail.tailSamples = 0;
    const std::vector<const SweepIr*> tail = {&a, &noTail};
    CHECK(analyseRt60Ensemble(tail).refusal == Rt60Refusal::NoTail);
    CHECK(analyseRt60Ensemble({}).refusal == Rt60Refusal::NoIr);
}

TEST_CASE("an ensemble answers only where EVERY capture established the band", kTag) {
    auto a = fx::buildIr(roomSpec(0.80, 1000));  // trusted from 80 Hz
    auto b = fx::buildIr(roomSpec(0.80, 2000));
    b.trustedLowHz = 100.0;  // a sweep that started higher: the 125 Hz octave's edge, 89.1 Hz, is outside
    const std::vector<const SweepIr*> both = {&a, &b};
    const auto ens = analyseRt60Ensemble(both);
    REQUIRE(ens.analysed());
    CHECK(ens.trustedLowHz == 100.0);  // the tighter of the two, not the looser
    const auto* low = ensembleBandAt(ens, 125);
    REQUIRE(low != nullptr);
    CHECK(low->refusal == Rt60Refusal::OutsideTrustedBand);
    CHECK(rt60EnsembleRow(*low, ens).note.find("(100 Hz)") != std::string::npos);
    const auto* mid = ensembleBandAt(ens, 1000);
    REQUIRE(mid != nullptr);
    CHECK(mid->t30.has());
}

TEST_CASE("an ensemble band too narrow for the decay says so with B*T, as the single table does", kTag) {
    fx::IrSpec spec;
    spec.harmonicSpacingL = shippedL();
    spec.trustedLowHz = 0.0;
    spec.trustedHighHz = 0.0;
    spec.components = fx::flatRoom(0.40, 1000);
    const auto a = fx::buildIr(spec);
    spec.components = fx::flatRoom(0.40, 2000);
    spec.floorSeed = 123;
    const auto b = fx::buildIr(spec);
    const std::vector<const SweepIr*> two = {&a, &b};
    const auto ens = analyseRt60Ensemble(two, Rt60BandSet::Third);
    REQUIRE(ens.analysed());
    const auto* fifty = ensembleBandAt(ens, 50);
    REQUIRE(fifty != nullptr);
    CHECK(fifty->t30.refusal == Rt60Refusal::BandwidthTimeTooSmall);
    CHECK(fifty->bandwidthTime > 0.0);  // a 11.6 Hz band at a decay under 0.52 s: below the bar of 6
    CHECK(fifty->bandwidthTime < 6.0);
    CHECK(rt60EnsembleRow(*fifty, ens).note.find("band too narrow for a decay this short") != std::string::npos);
    CHECK(rt60EnsembleRow(*fifty, ens).t30 == "--");
}

TEST_CASE("wall time of the heavy analyses, printed for decision 11", kTag) {
    const auto a = fx::buildIr(roomSpec(0.80, 1000));
    std::vector<SweepIr> irs;
    for (std::uint32_t i = 0; i < 8; ++i) irs.push_back(fx::buildIr(roomSpec(0.80, 1000 + 1000 * i)));
    std::vector<const SweepIr*> eight;
    for (const auto& ir : irs) eight.push_back(&ir);
    const double octave = analyseRt60(&a).wallMs;
    const double third = analyseRt60(&a, Rt60BandSet::Third).wallMs;
    const double ens8 = analyseRt60Ensemble(eight).wallMs;
    const double ens8Third = analyseRt60Ensemble(eight, Rt60BandSet::Third).wallMs;
    std::printf("RT60-W wall: 1 IR octave %.1f ms, 1 IR third %.1f ms, 8-IR ensemble octave %.1f ms, third %.1f ms"
                " (message-thread line: 100 ms)\n",
                octave, third, ens8, ens8Third);
    std::fflush(stdout);
    // Finite and positive; the claim is the printed figures, not a budget.
    CHECK(octave > 0.0);
    CHECK(ens8Third > 0.0);
}
