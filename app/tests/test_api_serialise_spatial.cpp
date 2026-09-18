// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L-API Task E (record docs/dsp/2026-09-16-remote-api.md sec.6; sec.11
// items 3, 4; sec.15 R4, R10, R13). E1-E8 only.
//
// The three blocks that carry a state a well-meaning implementer drops --
// per-band coherenceAvailable, a string-valued absence enum, a string-valued
// membership enum -- plus the union endpoint R13 defines.

#include <catch2/catch_test_macros.hpp>

#include "ApiFixture.h"

#include "api/ApiPolicy.h"
#include "api/ApiSerialise.h"
#include "api/ApiSettings.h"

#include "rta/dsp/MtwLayout.h"
#include "rta/dsp/SpatialAverage.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using rta::api::ApiSettings;
using rta::api::clampPoints;
using rta::api::Request;
using rta::api::serialiseAverage;
using rta::api::serialiseMtw;
using rta::api::serialisePositions;
using rta::api::serialiseSnapshot;
using rta::api::test::makeApiFixture;
using rta::api::test::makeEmptyApiFixture;

namespace {

[[nodiscard]] Request allPoints() {
    Request request;
    request.points = clampPoints(0, ApiSettings{});
    return request;
}

[[nodiscard]] std::size_t countOf(const std::string& haystack, std::string_view needle) {
    std::size_t count = 0;
    for (std::size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size())) {
        ++count;
    }
    return count;
}

}  // namespace

TEST_CASE("E1 per-band coherenceAvailable survives, and the zeros travel with it",
          "[api][spatial]") {
    // MtwBandDescriptor's own comment: an index whose band has not passed its
    // gate holds 0.0f and "must not be read as a measured zero". A client
    // that ignores this draws a bottom band reading zero coherence for five
    // and a half seconds and reports a fault that does not exist.
    //
    // makeSyntheticMtw() states a COMPLETED measurement -- its comment says
    // coherenceAvailable is true in every band -- so the filling bottom band
    // is built here by hand rather than waited for from a fixture that cannot
    // produce it.
    auto snapshot = makeApiFixture();
    REQUIRE(snapshot.mtw.has_value());
    REQUIRE_FALSE(snapshot.mtw->bands.empty());

    auto& bottom = snapshot.mtw->bands.front();
    bottom.coherenceAvailable = false;
    for (std::size_t i = 0; i < bottom.pointCount; ++i) {
        snapshot.mtw->coherence[bottom.firstIndex + i] = 0.0f;
    }

    const std::string body = serialiseMtw(snapshot, allPoints());
    CHECK(countOf(body, "\"coherenceAvailable\":false") == 1u);
    CHECK(countOf(body, "\"coherenceAvailable\":true") == snapshot.mtw->bands.size() - 1u);
    // The zeros are still there. Dropping them would renumber every index
    // after the bottom band, which is worse than the confusion the flag
    // exists to prevent.
    CHECK(body.find("\"coherence\":[0,0") != std::string::npos);
}

TEST_CASE("E2 /mtw's axis is the other kind, and all eight descriptor fields cross",
          "[api][spatial]") {
    const auto snapshot = makeApiFixture();
    const std::string body = serialiseMtw(snapshot, allPoints());
    CHECK(body.find("\"kind\":\"explicit\"") != std::string::npos);
    // Unlike /transfer, an explicit axis SENDS its frequency vector: the MTW
    // engine's bins are not i*sampleRate/fftSize and a client cannot rebuild
    // them from parameters.
    CHECK(body.find("\"frequencyHz\":[") != std::string::npos);

    // E8's data-driven half: eight fields, none optional. A ninth added later
    // without a test is visible here as a list that no longer matches the
    // struct.
    static constexpr std::array<std::string_view, 8> kFields{
        "firstIndex",  "pointCount",         "fftSize", "windowSeconds",
        "integrationSeconds", "effectiveAverages", "seamHz", "coherenceAvailable"};
    REQUIRE(snapshot.mtw.has_value());
    for (const auto field : kFields) {
        INFO("band descriptor field: " << field);
        CHECK(countOf(body, std::string("\"") + std::string(field) + "\":")
              >= snapshot.mtw->bands.size());
    }
}

TEST_CASE("E3 absence is an ARRAY OF STRINGS, never an integer", "[api][spatial]") {
    // OSM flattens enums to their integer value with no name, so a
    // renumbering between versions silently changes meaning with nothing on
    // the wire to reveal it -- a measured failure mode in a shipping product.
    // memory/a-placeholder-for-an-absent-result-erases-its-state.md is about
    // this exact field being rewritten from NoWeight/2 to NoContributor/0.
    auto snapshot = makeApiFixture();
    REQUIRE(snapshot.average.has_value());
    REQUIRE(snapshot.average->absence.size() > 3u);
    snapshot.average->absence[0] = rta::dsp::SpatialAbsence::Present;
    snapshot.average->absence[1] = rta::dsp::SpatialAbsence::NoContributor;
    snapshot.average->absence[2] = rta::dsp::SpatialAbsence::NoWeight;

    const std::string body = serialiseAverage(snapshot, allPoints());
    CHECK(body.find("\"absence\":[\"present\",\"noContributor\",\"noWeight\"")
          != std::string::npos);
    CHECK(body.find("\"absence\":[0") == std::string::npos);
    CHECK(body.find("\"absence\":[2") == std::string::npos);
}

TEST_CASE("E4 membership is a string too", "[api][spatial]") {
    auto snapshot = makeApiFixture();
    REQUIRE(snapshot.positions.size() >= 3u);
    snapshot.positions[0].membership = rta::measure::Membership::Member;
    snapshot.positions[1].membership = rta::measure::Membership::ExcludedDifferentReference;
    snapshot.positions[2].membership = rta::measure::Membership::ExcludedOverCapacity;

    const std::string body = serialisePositions(snapshot);
    CHECK(body.find("\"membership\":\"member\"") != std::string::npos);
    CHECK(body.find("\"membership\":\"excludedDifferentReference\"") != std::string::npos);
    CHECK(body.find("\"membership\":\"excludedOverCapacity\"") != std::string::npos);
    CHECK(body.find("\"membership\":0") == std::string::npos);
}

TEST_CASE("E5 the two quantities keep their code names", "[api][spatial]") {
    // L6b sec.2 and sec.4 exist because phaseAgreement and weightedCoherence
    // are routinely mistaken for a coherence estimate. Neither is one, and a
    // wire format that renamed either to something friendlier would undo that
    // record. This is the mutation whose GREEN version would be the most
    // plausible-looking code in the lane.
    const auto snapshot = makeApiFixture();
    const std::string body = serialiseAverage(snapshot, allPoints());
    CHECK(body.find("\"phaseAgreement\":[") != std::string::npos);
    CHECK(body.find("\"weightedCoherence\":[") != std::string::npos);
    CHECK(body.find("\"coherence\":") == std::string::npos);
}

TEST_CASE("E6 /positions carries no per-bin array", "[api][spatial]") {
    // L6b sec.6 fixed that publish cost must be O(1) in N. An API that
    // re-expanded a per-bin array per position would reintroduce exactly the
    // churn that decision refused.
    const auto snapshot = makeApiFixture();
    const std::string body = serialisePositions(snapshot);
    // One array in the whole body: the positions array itself. Any per-bin
    // curve would be a second one, whatever it was named.
    CHECK(countOf(body, "[") == 1u);
    // N position objects plus the one enclosing body object.
    CHECK(countOf(body, "{") == snapshot.positions.size() + 1u);
    CHECK(countOf(body, "\"levelDb\":") == snapshot.positions.size());
}

TEST_CASE("E7 /snapshot is a union, and absence is absence", "[api][spatial]") {
    const auto full = makeApiFixture();
    const std::string body = serialiseSnapshot(full, allPoints());
    for (const auto* name : {"\"transfer\":", "\"mtw\":", "\"bands\":", "\"spectrum\":",
                             "\"average\":", "\"positions\":"}) {
        INFO("block: " << name);
        CHECK(body.find(name) != std::string::npos);
    }
    CHECK(body.find("\"schemaVersion\":1") != std::string::npos);
    CHECK(body.find("\"available\":[") != std::string::npos);
    // Record sec.15 R10: soloTransfer is not on the wire in v1 and never
    // appears in `available`. Named rather than forgotten.
    CHECK(body.find("soloTransfer") == std::string::npos);

    const auto empty = makeEmptyApiFixture();
    const std::string bare = serialiseSnapshot(empty, allPoints());
    CHECK(bare.find("\"mtw\":") == std::string::npos);
    CHECK(bare.find("\"transfer\":") == std::string::npos);
    CHECK(bare.find("\"average\":") == std::string::npos);
    CHECK(bare.find("\"positions\":") == std::string::npos);
    // The blocks that are never optional still serve.
    CHECK(bare.find("\"bands\":[") != std::string::npos);
}

TEST_CASE("E8 dropping any one band-descriptor field is visible", "[api][spatial]") {
    // The counting half of E2, stated as its own case so the mutation row it
    // backs has somewhere to point. The field list is written once, above,
    // and a ninth field added to MtwBandDescriptor without an entry there is
    // what this pair is for.
    const auto snapshot = makeApiFixture();
    REQUIRE(snapshot.mtw.has_value());
    const std::string body = serialiseMtw(snapshot, allPoints());
    const auto bands = snapshot.mtw->bands.size();
    CHECK(countOf(body, "\"firstIndex\":") == bands);
    CHECK(countOf(body, "\"seamHz\":") == bands);
    CHECK(countOf(body, "\"integrationSeconds\":") == bands);
    CHECK(countOf(body, "\"windowSeconds\":") == bands);
}
