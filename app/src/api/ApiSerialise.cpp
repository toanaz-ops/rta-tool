// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/api. The FIXED-AXIS blocks: /status,
// /transfer, /spectrum, /bands, and the /snapshot union that composes them
// with the spatial half. See ApiSerialiseSpatial.cpp for /mtw, /average and
// /positions, and ApiSerialise.h for why that split is unconditional.

#include "api/ApiSerialise.h"

#include "api/ApiJson.h"

#include <cstddef>
#include <span>
#include <string>

namespace rta::api {
namespace {

using json::array;
using json::emittedCount;
using json::number;
using json::stringValue;

/// `"key":` -- the one piece of punctuation repeated often enough that
/// spelling it by hand is how a stray space ends up in one body and not the
/// other, which a byte-compare golden would then lock in.
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

/// The uniform axis: bin i is at i*sampleRate/fftSize, so NO frequency
/// vector is sent (record sec.6). `pointCount` is MEASURED from the array
/// that will be emitted, never derived as fftSize/2+1 -- sec.15 R11, because
/// the two agree only when the engine filled the whole half-spectrum.
[[nodiscard]] std::string uniformAxis(const measure::Snapshot& snapshot, std::size_t pointCount) {
    std::string out = "{";
    out += key("kind") + stringValue("uniform") + ',';
    out += key("sampleRate") + number(snapshot.sampleRate) + ',';
    out += key("fftSize") + number(snapshot.fftSize) + ',';
    out += key("pointCount") + number(pointCount);
    out += '}';
    return out;
}

}  // namespace

namespace detail {

std::string statusFields(const measure::Snapshot& snapshot, const ApiSettings& settings) {
    (void)settings;   // v1 exposes no setting on the wire; see below
    std::string out;
    out += key("schemaVersion") + "1,";
    out += key("app") + stringValue("RTA Tool") + ',';
    out += key("sequence") + number(snapshot.sequence) + ',';
    out += key("sampleRate") + number(snapshot.sampleRate) + ',';
    out += key("fftSize") + number(snapshot.fftSize) + ',';
    out += key("fraction") + number(snapshot.fraction) + ',';
    out += key("hasReference") + (snapshot.hasReference ? "true" : "false") + ',';
    out += key("framesAnalysed") + number(snapshot.framesAnalysed) + ',';
    out += key("droppedSamples") + number(snapshot.droppedSamples) + ',';
    // The capability list, and it is the honest answer to what this build
    // serves rather than something a client infers from a 404. Six names
    // (sec.15 R12 -- six is the length of THIS list, not the endpoint count,
    // which is eight). "spl" joins it the day the Meters track puts SPL in
    // the Snapshot and not a day earlier: Snapshot carries dBFS only.
    out += key("available") +
           R"(["transfer","mtw","bands","spectrum","average","positions"])";
    return out;
}

std::string transferFields(const measure::Snapshot& snapshot,
                           const measure::TransferBlock& block, const Request& request) {
    const std::size_t limit = limitOf(request);
    const std::size_t count = emittedCount(block.magnitudeDb.size(), limit);

    std::string out;
    out += key("axis") + uniformAxis(snapshot, count) + ',';
    out += key("effectiveAverages") + number(block.effectiveAverages) + ',';
    out += key("appliedDelaySamples") + number(block.appliedDelaySamples) + ',';
    out += key("magnitudeDb") + array(std::span<const float>(block.magnitudeDb), limit) + ',';
    out += key("phaseDeg") + array(std::span<const float>(block.phaseDeg), limit);
    // ABSENT means absent: no key at all, never null, never an array of 1.0
    // and never zeros. A single frame gives coherence identically 1.0 at
    // every frequency, so a broken engine looks perfect -- that gate
    // survives the wire or it was never a gate.
    if (block.coherence.has_value()) {
        out += ',' + key("coherence") + array(std::span<const float>(*block.coherence), limit);
    }
    return out;
}

std::string bandsArray(const measure::Snapshot& snapshot) {
    std::string out = "[";
    bool first = true;
    for (const auto& band : snapshot.bands) {
        if (!first) {
            out += ',';
        }
        first = false;
        out += '{';
        out += key("centreHz") + number(band.centreHz) + ',';
        out += key("lowerHz") + number(band.lowerHz) + ',';
        out += key("upperHz") + number(band.upperHz) + ',';
        out += key("levelDb") + number(band.levelDb) + ',';
        // The difference between a measurement and a band the FFT physically
        // cannot resolve. A client that drops it draws a fault that does not
        // exist, in the one place the engine was honest about.
        out += key("underResolved") + (band.underResolved ? "true" : "false");
        out += '}';
    }
    out += ']';
    return out;
}

}  // namespace detail

std::string serialiseStatus(const measure::Snapshot& snapshot, const ApiSettings& settings) {
    return '{' + detail::statusFields(snapshot, settings) + '}';
}

std::string serialiseTransfer(const measure::Snapshot& snapshot, const Request& request) {
    std::string out = "{";
    out += key("schemaVersion") + "1,";
    out += key("sequence") + number(snapshot.sequence);
    if (snapshot.transfer.has_value()) {
        out += ',' + detail::transferFields(snapshot, *snapshot.transfer, request);
    }
    out += '}';
    return out;
}

std::string serialiseSpectrum(const measure::Snapshot& snapshot, const Request& request) {
    const std::size_t limit = limitOf(request);
    const std::size_t count = emittedCount(snapshot.spectrumDb.size(), limit);

    std::string out = "{";
    out += key("schemaVersion") + "1,";
    out += key("sequence") + number(snapshot.sequence) + ',';
    out += key("axis") + uniformAxis(snapshot, count) + ',';
    out += key("spectrumDb") + array(std::span<const float>(snapshot.spectrumDb), limit);
    out += '}';
    return out;
}

std::string serialiseBands(const measure::Snapshot& snapshot) {
    std::string out = "{";
    out += key("schemaVersion") + "1,";
    out += key("sequence") + number(snapshot.sequence) + ',';
    out += key("fraction") + number(snapshot.fraction) + ',';
    out += key("bands") + detail::bandsArray(snapshot);
    out += '}';
    return out;
}

std::string serialiseSnapshot(const measure::Snapshot& snapshot, const Request& request) {
    // Record sec.15 R13: the union of /status and every block that is
    // PRESENT, keyed by block name, with an absent block's key absent. Not a
    // second spelling of /status -- statusFields is the same bytes, so the
    // two endpoints cannot drift apart.
    std::string out = "{";
    out += detail::statusFields(snapshot, ApiSettings{});
    out += ',' + key("bands") + detail::bandsArray(snapshot);
    out += ',' + key("spectrum") + '{' +
           key("axis") +
           uniformAxis(snapshot, emittedCount(snapshot.spectrumDb.size(), limitOf(request))) +
           ',' + key("spectrumDb") +
           array(std::span<const float>(snapshot.spectrumDb), limitOf(request)) + '}';
    if (snapshot.transfer.has_value()) {
        out += ',' + key("transfer") + '{' +
               detail::transferFields(snapshot, *snapshot.transfer, request) + '}';
    }
    if (snapshot.mtw.has_value()) {
        out += ',' + key("mtw") + '{' + detail::mtwFields(*snapshot.mtw, request) + '}';
    }
    if (snapshot.average.has_value()) {
        out += ',' + key("average") + '{' + detail::averageFields(*snapshot.average, request) + '}';
    }
    // An empty group is an absent key too: `positions` is empty exactly when
    // no group is configured, and an empty array on the wire would claim a
    // group exists with nobody in it.
    if (!snapshot.positions.empty()) {
        out += ',' + key("positions") + detail::positionsArray(snapshot);
    }
    out += '}';
    return out;
}

}  // namespace rta::api
