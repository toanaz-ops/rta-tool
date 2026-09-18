// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/api. The SPATIAL blocks: /mtw, /average and
// /positions. Split from ApiSerialise.cpp unconditionally rather than "if
// the cap is reached" -- discovering the 400-line cap mid-task is how a file
// ends up at 399 lines doing two jobs.
//
// These three carry the states a well-meaning implementer drops: per-band
// coherenceAvailable, and two enums that must be NAMES on the wire and never
// integers. See docs/dsp/2026-09-16-remote-api.md sec.6 and sec.15 R4.

#include "api/ApiSerialise.h"

#include "api/ApiJson.h"

#include "rta/dsp/SpatialAverage.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace rta::api {
namespace {

using json::array;
using json::emittedCount;
using json::number;
using json::stringValue;

[[nodiscard]] std::string key(std::string_view name) {
    return stringValue(name) + ':';
}

[[nodiscard]] std::size_t limitOf(const Request& request) {
    // A non-positive `points` falls back to the SHIPPED CAP, never to
    // SIZE_MAX. "No limit" is not a value this function is allowed to
    // produce: the cap is a real-time-safety control, and a serialiser that
    // treats a malformed Request as permission to emit everything hands a
    // remote caller unbounded work. Absence still cannot truncate a body --
    // it just cannot exceed the cap either.
    return request.points > 0 ? static_cast<std::size_t>(request.points)
                              : static_cast<std::size_t>(kDefaultMaxPointsPerResponse);
}

/// A NAME, never an integer. OSM flattens enums to their integer value with
/// no name (`server.cpp:286-288`), so a renumbering between versions
/// silently changes meaning with nothing on the wire to reveal it -- a
/// measured failure mode in a shipping product, not a hypothetical.
/// memory/a-placeholder-for-an-absent-result-erases-its-state.md is about
/// this exact field being rewritten from NoWeight/2 to NoContributor/0.
[[nodiscard]] std::string_view absenceName(rta::dsp::SpatialAbsence absence) {
    switch (absence) {
        case rta::dsp::SpatialAbsence::Present: return "present";
        case rta::dsp::SpatialAbsence::NoContributor: return "noContributor";
        case rta::dsp::SpatialAbsence::NoWeight: return "noWeight";
    }
    // Unreachable for a valid enumerator, and it emits a name that is
    // obviously not one of the three rather than a plausible default: a
    // placeholder for an absent result erases its state, which is the whole
    // reason this enum travels by name.
    return "unknown";
}

[[nodiscard]] std::string_view membershipName(measure::Membership membership) {
    switch (membership) {
        case measure::Membership::Member: return "member";
        case measure::Membership::ExcludedDifferentReference:
            return "excludedDifferentReference";
        case measure::Membership::ExcludedOverCapacity: return "excludedOverCapacity";
    }
    return "unknown";
}

}  // namespace

namespace detail {

std::string mtwFields(const measure::MtwBlock& block, const Request& request) {
    const std::size_t limit = limitOf(request);
    const std::size_t count = emittedCount(block.magnitudeDb.size(), limit);

    std::string out;
    // The OTHER axis kind. MtwBlock's bins are not i*sampleRate/fftSize, so
    // the frequency vector is SENT rather than derived -- a client cannot
    // rebuild this one from parameters, which is exactly why axis.kind
    // exists and costs one string.
    out += key("axis") + '{' + key("kind") + stringValue("explicit") + ',' +
           key("pointCount") + number(count) + '}' + ',';
    out += key("frequencyHz") + array(std::span<const double>(block.frequencyHz), limit) + ',';
    out += key("magnitudeDb") + array(std::span<const float>(block.magnitudeDb), limit) + ',';
    out += key("phaseDeg") + array(std::span<const float>(block.phaseDeg), limit) + ',';
    // The coherence array travels WHOLE, zeros included, and the per-band
    // flag below says which stretches of it are measured. Dropping the
    // unmeasured entries would renumber every index after them, which is a
    // worse defect than the confusion the flag exists to prevent.
    out += key("coherence") + array(std::span<const float>(block.coherence), limit) + ',';
    out += key("appliedDelaySamples") + number(block.appliedDelaySamples) + ',';
    out += key("bands") + '[';
    bool first = true;
    for (const auto& band : block.bands) {
        if (!first) {
            out += ',';
        }
        first = false;
        out += '{';
        out += key("firstIndex") + number(band.firstIndex) + ',';
        out += key("pointCount") + number(band.pointCount) + ',';
        out += key("fftSize") + number(band.fftSize) + ',';
        out += key("windowSeconds") + number(band.windowSeconds) + ',';
        out += key("integrationSeconds") + number(band.integrationSeconds) + ',';
        out += key("effectiveAverages") + number(band.effectiveAverages) + ',';
        out += key("seamHz") + number(band.seamHz) + ',';
        // The single field most likely to be dropped by a well-meaning
        // implementer, and the record says so. An index whose band has not
        // passed its gate holds 0.0f in `coherence` and must not be read as
        // a measured zero -- a client that ignores this draws a bottom band
        // reading zero coherence for five and a half seconds and reports a
        // fault that does not exist.
        out += key("coherenceAvailable") + (band.coherenceAvailable ? "true" : "false");
        out += '}';
    }
    out += ']';
    return out;
}

std::string averageFields(const measure::AverageBlock& block, const Request& request) {
    const std::size_t limit = limitOf(request);

    std::string out;
    out += key("pointCount") + number(emittedCount(block.magnitudeDb.size(), limit)) + ',';
    out += key("magnitudeDb") + array(std::span<const float>(block.magnitudeDb), limit) + ',';
    out += key("phaseDeg") + array(std::span<const float>(block.phaseDeg), limit) + ',';
    // SPELLED AS THE CODE SPELLS THEM, and neither is a coherence estimate.
    // L6b sec.2 and sec.4 exist because those two quantities are routinely
    // mistaken for one; a wire format that renamed either to something
    // friendlier would undo that record.
    out += key("phaseAgreement") + array(std::span<const float>(block.phaseAgreement), limit) + ',';
    out += key("weightedCoherence") +
           array(std::span<const float>(block.weightedCoherence), limit) + ',';
    out += key("contributors") +
           array(std::span<const std::uint16_t>(block.contributors), limit) + ',';

    // sec.15 R4: a per-bin ARRAY of strings. AverageBlock::absence is a
    // std::vector, one entry per bin, not the single scalar sec.6 read it as.
    out += key("absence") + '[';
    const std::size_t absenceCount = emittedCount(block.absence.size(), limit);
    for (std::size_t i = 0; i < absenceCount; ++i) {
        if (i != 0) {
            out += ',';
        }
        out += stringValue(absenceName(block.absence[i]));
    }
    out += ']';
    return out;
}

std::string positionsArray(const measure::Snapshot& snapshot) {
    // NO per-bin array here, deliberately. L6b sec.6 fixed that publish cost
    // must be O(1) in N; an API that re-expanded a curve per position would
    // reintroduce the churn that decision refused, over a socket.
    std::string out = "[";
    bool first = true;
    for (const auto& position : snapshot.positions) {
        if (!first) {
            out += ',';
        }
        first = false;
        out += '{';
        out += key("tfIndex") + number(position.tfIndex) + ',';
        out += key("name") + stringValue(position.name) + ',';
        out += key("levelDb") + number(position.levelDb) + ',';
        out += key("weightedCoherence") + number(position.weightedCoherence) + ',';
        out += key("effectiveAverages") + number(position.effectiveAverages) + ',';
        out += key("gatePassed") + (position.gatePassed ? "true" : "false") + ',';
        out += key("overloaded") + (position.overloaded ? "true" : "false") + ',';
        out += key("membership") + stringValue(membershipName(position.membership));
        out += '}';
    }
    out += ']';
    return out;
}

}  // namespace detail

std::string serialiseMtw(const measure::Snapshot& snapshot, const Request& request) {
    std::string out = "{";
    out += key("schemaVersion") + "1,";
    out += key("sequence") + number(snapshot.sequence);
    if (snapshot.mtw.has_value()) {
        out += ',' + detail::mtwFields(*snapshot.mtw, request);
    }
    out += '}';
    return out;
}

std::string serialiseAverage(const measure::Snapshot& snapshot, const Request& request) {
    std::string out = "{";
    out += key("schemaVersion") + "1,";
    out += key("sequence") + number(snapshot.sequence);
    if (snapshot.average.has_value()) {
        out += ',' + detail::averageFields(*snapshot.average, request);
    }
    out += '}';
    return out;
}

std::string serialisePositions(const measure::Snapshot& snapshot) {
    std::string out = "{";
    out += key("schemaVersion") + "1,";
    out += key("sequence") + number(snapshot.sequence) + ',';
    out += key("positions") + detail::positionsArray(snapshot);
    out += '}';
    return out;
}

}  // namespace rta::api
