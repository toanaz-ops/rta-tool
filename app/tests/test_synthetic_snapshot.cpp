// SPDX-License-Identifier: AGPL-3.0-or-later
//
// makeSyntheticSnapshot is the offline, deterministic path that feeds both
// `rta-view.png` (tools/snapshot.cpp) and CI: no threads, no timing, so a
// given SyntheticSpec must always produce the same Snapshot -- that is the
// precondition for the PNG being reviewable as a byte-for-byte diff.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "measure/SyntheticSnapshot.h"

#include <cmath>
#include <cstddef>

using namespace rta::measure;

namespace {

SyntheticSpec fastSpec() {
    SyntheticSpec spec;
    spec.analysis.sampleRate = 48000.0;
    spec.analysis.fftSize = 4096;
    spec.analysis.hopSize = 1024;
    spec.seconds = 2.0;  // enough frames without slowing the suite
    return spec;
}

}  // namespace

TEST_CASE("The same spec produces the identical snapshot", "[synthetic-snapshot]") {
    const auto spec = fastSpec();
    const auto a = makeSyntheticSnapshot(spec);
    const auto b = makeSyntheticSnapshot(spec);

    REQUIRE(a->bands.size() == b->bands.size());
    for (std::size_t i = 0; i < a->bands.size(); ++i) {
        CAPTURE(i);
        CHECK(a->bands[i].levelDb == b->bands[i].levelDb);
    }
    REQUIRE(a->spectrumDb.size() == b->spectrumDb.size());
    for (std::size_t i = 0; i < a->spectrumDb.size(); ++i) {
        CHECK(a->spectrumDb[i] == b->spectrumDb[i]);
    }
}

TEST_CASE("A different seed produces a different snapshot", "[synthetic-snapshot]") {
    auto spec = fastSpec();
    const auto a = makeSyntheticSnapshot(spec);
    spec.seed ^= 1u;
    const auto b = makeSyntheticSnapshot(spec);

    bool anyDifferent = false;
    for (std::size_t i = 0; i < a->bands.size(); ++i) {
        if (a->bands[i].levelDb != b->bands[i].levelDb) {
            anyDifferent = true;
            break;
        }
    }
    CHECK(anyDifferent);
}

TEST_CASE("The default synthetic spec is a usable picture", "[synthetic-snapshot]") {
    const SyntheticSpec spec;  // every default, including 4.0 s of pink noise
    const auto snap = makeSyntheticSnapshot(spec);

    CHECK(snap->bands.size() >= 20);
    CHECK(snap->peakBandLevelDb >= -60.0f);
    CHECK(snap->peakBandLevelDb <= 0.0f);

    for (const auto& band : snap->bands) {
        CHECK(std::isfinite(band.levelDb));
        if (band.centreHz >= 50.0f && band.centreHz <= 10000.0f) {
            CHECK(band.levelDb > -120.0f);
        }
    }
}
