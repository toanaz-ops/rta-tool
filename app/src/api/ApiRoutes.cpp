// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/api. See ApiRoutes.h for why the endpoint table
// lives outside ApiServer.cpp.

#include "api/ApiRoutes.h"

#include <array>

namespace rta::api {
namespace {

// Eight adapters, because the serialisers deliberately do NOT share a
// signature: `/bands` and `/positions` take no `Request` (their length is the
// band count, not a caller-supplied point count) and `/status` is the only one
// that reads `ApiSettings`. Adapting here rather than widening all eight
// declarations keeps each serialiser's parameter list a statement about what
// it actually depends on.
//
// `[[maybe_unused]]`-free by construction: every parameter these ignore is
// unnamed, which says the same thing to a reader and to the compiler.

std::string status(const measure::Snapshot& s, const Request&, const ApiSettings& settings) {
    return serialiseStatus(s, settings);
}
std::string snapshot(const measure::Snapshot& s, const Request& r, const ApiSettings&) {
    return serialiseSnapshot(s, r);
}
std::string transfer(const measure::Snapshot& s, const Request& r, const ApiSettings&) {
    return serialiseTransfer(s, r);
}
std::string mtw(const measure::Snapshot& s, const Request& r, const ApiSettings&) {
    return serialiseMtw(s, r);
}
std::string bands(const measure::Snapshot& s, const Request&, const ApiSettings&) {
    return serialiseBands(s);
}
std::string spectrum(const measure::Snapshot& s, const Request& r, const ApiSettings&) {
    return serialiseSpectrum(s, r);
}
std::string average(const measure::Snapshot& s, const Request& r, const ApiSettings&) {
    return serialiseAverage(s, r);
}
std::string positions(const measure::Snapshot& s, const Request&, const ApiSettings&) {
    return serialisePositions(s);
}

}  // namespace

std::span<const RouteEntry> apiRoutes() {
    // `/snapshot` is the union of `/status` and every present block (sec.15
    // R13), so it is listed beside them rather than derived from them: a
    // reader looking for "which paths exist" gets one list.
    static const std::array<RouteEntry, 8> kRoutes{{
        {"status", &status},
        {"snapshot", &snapshot},
        {"transfer", &transfer},
        {"mtw", &mtw},
        {"bands", &bands},
        {"spectrum", &spectrum},
        {"average", &average},
        {"positions", &positions},
    }};
    return std::span<const RouteEntry>(kRoutes);
}

}  // namespace rta::api
