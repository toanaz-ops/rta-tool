// SPDX-License-Identifier: AGPL-3.0-or-later
//
// station-3 STORE plan tasks T2/T3/T4 (docs/plans/2026-09-27-store-lane-plan.md;
// docs/research/2026-09-27-store-trace.md Part A3). JUCE-free -- CaptureConverter.h
// touches no framework, so this test proves the Snapshot->Trace conversion the
// same way test_virtual_trace.cpp/test_trace.cpp beside it prove Trace itself,
// on all three CI operating systems.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "trace/CaptureConverter.h"

#include <numbers>
#include <set>

using Catch::Matchers::WithinAbs;
using rta::measure::Analyser;
using rta::measure::Snapshot;
using rta::measure::TransferBlock;
using rta::trace::degToRadPhase;
using rta::trace::nextCaptureId;
using rta::trace::pointCountFor;
using rta::trace::traceFromSnapshot;
using rta::view::PaneView;

namespace {

constexpr float kEps = 1e-4f;

Snapshot makeSnapshot(double sampleRate, std::size_t fftSize) {
    Snapshot snapshot;
    snapshot.sampleRate = sampleRate;
    snapshot.fftSize = fftSize;
    snapshot.spectrumDb.assign(pointCountFor(static_cast<int>(fftSize)), -20.0f);
    return snapshot;
}

}  // namespace

// --- T2: degToRadPhase, a closed-form identity ------------------------------

TEST_CASE("degToRadPhase matches the exact degrees->radians identity", "[capture_converter]") {
    const std::vector<float> deg{0.0f, 90.0f, -90.0f, 180.0f};
    const auto rad = degToRadPhase(deg);
    REQUIRE(rad.size() == deg.size());
    CHECK_THAT(static_cast<double>(rad[0]), WithinAbs(0.0, kEps));
    CHECK_THAT(static_cast<double>(rad[1]), WithinAbs(std::numbers::pi / 2.0, kEps));
    CHECK_THAT(static_cast<double>(rad[2]), WithinAbs(-std::numbers::pi / 2.0, kEps));
    CHECK_THAT(static_cast<double>(rad[3]), WithinAbs(std::numbers::pi, kEps));
}

TEST_CASE("degToRadPhase round-trips against AnalyserPublish's own rad->deg conversion",
         "[capture_converter]") {
    // The one existing rad->deg helper (AnalyserPublish.cpp's inline loop) is
    // reproduced here as the round-trip partner -- no second constant, the
    // same `180.0/std::numbers::pi` literal that file uses, just applied in
    // the OTHER direction to close the loop.
    for (const float degIn : {-179.9f, -90.0f, 0.0f, 90.0f, 179.9f}) {
        const auto rad = degToRadPhase(std::span<const float>(&degIn, 1));
        REQUIRE(rad.size() == 1);
        const float degOut = rad[0] * static_cast<float>(180.0 / std::numbers::pi);
        CHECK_THAT(static_cast<double>(degOut), WithinAbs(static_cast<double>(degIn), 1e-3));
    }
}

// --- T3: nextCaptureId, fake-clock-proof by construction --------------------

TEST_CASE("nextCaptureId returns 1000 distinct ids with no clock involved", "[capture_converter]") {
    std::set<std::string> ids;
    for (int i = 0; i < 1000; ++i) {
        ids.insert(nextCaptureId());
    }
    CHECK(ids.size() == 1000);
}

TEST_CASE("nextCaptureId called twice back-to-back never collides", "[capture_converter]") {
    // The mutant T3 names (std::chrono millisecond timestamps) collides on
    // exactly this: two calls with no time advancing between them.
    const auto a = nextCaptureId();
    const auto b = nextCaptureId();
    CHECK(a != b);
}

// --- T4: Snapshot/pane -> Trace conversion ----------------------------------

TEST_CASE("traceFromSnapshot on the RTA pane freezes spectrumDb, magnitude-only",
         "[capture_converter]") {
    const auto snapshot = makeSnapshot(48000.0, 2048);
    const auto trace = traceFromSnapshot(snapshot, PaneView::Rta, Analyser::Config{}, "dev", "roles");
    REQUIRE(trace.has_value());
    CHECK(trace->pointCount() == snapshot.spectrumDb.size());
    for (std::size_t i = 0; i < snapshot.spectrumDb.size(); ++i) {
        CHECK(trace->field(rta::trace::Field::Magnitude)[i] == snapshot.spectrumDb[i]);
    }
    CHECK_FALSE(trace->has(rta::trace::Field::Phase));
}

TEST_CASE("traceFromSnapshot on TRANSFER freezes the fixed-FFT block, magnitude+phase+coherence",
         "[capture_converter]") {
    auto snapshot = makeSnapshot(48000.0, 2048);
    TransferBlock tf;
    tf.magnitudeDb.assign(pointCountFor(2048), -6.0f);
    tf.phaseDeg.assign(pointCountFor(2048), 45.0f);
    tf.coherence = std::vector<float>(pointCountFor(2048), 0.9f);
    tf.effectiveAverages = 12.5;
    tf.appliedDelaySamples = 37;
    snapshot.transfer = tf;
    snapshot.hasReference = true;

    const auto trace = traceFromSnapshot(snapshot, PaneView::Transfer, Analyser::Config{}, "dev", "roles");
    REQUIRE(trace.has_value());
    REQUIRE(trace->has(rta::trace::Field::Phase));
    REQUIRE(trace->has(rta::trace::Field::Coherence));

    const auto expectedPhase = degToRadPhase(tf.phaseDeg);
    for (std::size_t i = 0; i < tf.magnitudeDb.size(); ++i) {
        CHECK(trace->field(rta::trace::Field::Magnitude)[i] == tf.magnitudeDb[i]);
        CHECK_THAT(static_cast<double>(trace->field(rta::trace::Field::Phase)[i]),
                  WithinAbs(static_cast<double>(expectedPhase[i]), kEps));
        CHECK(trace->field(rta::trace::Field::Coherence)[i] == (*tf.coherence)[i]);
    }
    CHECK(trace->meta().appliedDelaySamples == 37);
}

TEST_CASE("traceFromSnapshot on TRANSFER with absent coherence stays absent, never zero-filled",
         "[capture_converter]") {
    auto snapshot = makeSnapshot(48000.0, 2048);
    TransferBlock tf;
    tf.magnitudeDb.assign(pointCountFor(2048), -6.0f);
    tf.phaseDeg.assign(pointCountFor(2048), 0.0f);
    // tf.coherence left absent -- below the effective-average gate.
    snapshot.transfer = tf;
    snapshot.hasReference = true;

    const auto trace = traceFromSnapshot(snapshot, PaneView::Transfer, Analyser::Config{}, "dev", "roles");
    REQUIRE(trace.has_value());
    CHECK_FALSE(trace->has(rta::trace::Field::Coherence));
}

TEST_CASE("traceFromSnapshot on TRANSFER refuses when hasReference is false, even if transfer is populated",
         "[capture_converter]") {
    // Deliberately inconsistent fixture, independent of whether `transfer`
    // itself happens to hold data -- this is the shape that must catch the
    // mutant naming this row: dropping the `hasReference` check while
    // leaving a `transfer.has_value()` check in place would still wrongly
    // build a Trace here, because `transfer` DOES have a value.
    auto snapshot = makeSnapshot(48000.0, 2048);
    TransferBlock tf;
    tf.magnitudeDb.assign(pointCountFor(2048), -6.0f);
    tf.phaseDeg.assign(pointCountFor(2048), 0.0f);
    snapshot.transfer = tf;
    snapshot.hasReference = false;

    const auto trace = traceFromSnapshot(snapshot, PaneView::Transfer, Analyser::Config{}, "dev", "roles");
    CHECK_FALSE(trace.has_value());
}

TEST_CASE("traceFromSnapshot on TRANSFER refuses when transfer is absent", "[capture_converter]") {
    auto snapshot = makeSnapshot(48000.0, 2048);
    snapshot.hasReference = true;
    // snapshot.transfer left as std::nullopt.

    const auto trace = traceFromSnapshot(snapshot, PaneView::Transfer, Analyser::Config{}, "dev", "roles");
    CHECK_FALSE(trace.has_value());
}

TEST_CASE("traceFromSnapshot on a non-producer pane always refuses", "[capture_converter]") {
    const auto snapshot = makeSnapshot(48000.0, 2048);
    CHECK_FALSE(traceFromSnapshot(snapshot, PaneView::Spl, Analyser::Config{}, "dev", "roles").has_value());
    CHECK_FALSE(traceFromSnapshot(snapshot, PaneView::Xover, Analyser::Config{}, "dev", "roles").has_value());
}
