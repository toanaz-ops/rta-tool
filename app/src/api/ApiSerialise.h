// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/api. No JUCE, no Qt, no audio-device API, and
// no server library: the whole wire format is proven in RTA_BUILD_APP=OFF,
// which is the only configuration CI runs (record sec.15 R15).
// See docs/dsp/2026-09-16-remote-api.md sec.6 as amended by sec.15 (R4, R10,
// R11, R12, R13).
#pragma once

#include "api/ApiSettings.h"

#include "measure/Snapshot.h"

#include <string>

namespace rta::api {

/// What survived validation. `points` is normally ALREADY clamped by
/// `clampPoints`, and the serialiser does not re-derive a limit, because a
/// limit computed in two places is a limit that can disagree with itself.
///
/// **The default is the SHIPPED CAP, not zero, and that is a safety
/// property rather than a convenience.** The point cap is a real-time-safety
/// control (record sec.9): an uncapped `?points=` is one of two ways a
/// remote caller can make this program do unbounded work during a show.
/// While the default meant "no limit", that control depended on every caller
/// remembering to call `clampPoints` -- so a handler built without it would
/// have served an unbounded body with nothing red to say so. A default now
/// means the shipped cap, which is the conservative reading: an operator who
/// configured a LARGER cap and forgot to clamp gets 8192 rather than
/// everything. See test_api_serialise.cpp D6b.
struct Request {
    int points = kDefaultMaxPointsPerResponse;
};

/// The eight bodies of record sec.6 as amended by sec.15. Every one carries
/// `schemaVersion` and `sequence`, because a body that is saved to a file or
/// pasted into an issue has left the URL -- and its `/api/v1/` segment --
/// behind.
///
/// `/transfer` serves `Snapshot::transfer`, never `soloTransfer` (sec.15
/// R10). `soloTransfer`, `peakBandLevelDb`, `peakBandCentreHz` and
/// `referenceBands` are decided OUT of v1 by name, so a later reader knows
/// they were decided rather than forgotten.
[[nodiscard]] std::string serialiseStatus(const measure::Snapshot& snapshot,
                                          const ApiSettings& settings);

/// Record sec.15 R13: the UNION of `/status` and every block that is
/// present, keyed by block name, with an absent block's key ABSENT -- the
/// same absence rule `coherence` gets, for the same reason. This is the
/// endpoint the golden file pins in full.
[[nodiscard]] std::string serialiseSnapshot(const measure::Snapshot& snapshot,
                                            const Request& request);

[[nodiscard]] std::string serialiseTransfer(const measure::Snapshot& snapshot,
                                            const Request& request);
[[nodiscard]] std::string serialiseSpectrum(const measure::Snapshot& snapshot,
                                            const Request& request);
[[nodiscard]] std::string serialiseBands(const measure::Snapshot& snapshot);

/// The spatial half, implemented in ApiSerialiseSpatial.cpp. The split is
/// unconditional rather than "if the cap is reached": discovering a 400-line
/// cap mid-task is how a file ends up at 399 lines with two jobs in it.
[[nodiscard]] std::string serialiseMtw(const measure::Snapshot& snapshot,
                                       const Request& request);
[[nodiscard]] std::string serialiseAverage(const measure::Snapshot& snapshot,
                                           const Request& request);
[[nodiscard]] std::string serialisePositions(const measure::Snapshot& snapshot);

namespace detail {

/// The `/status` fields, without the enclosing braces, so `/snapshot` can be
/// their union with the blocks rather than a second spelling of them (sec.15
/// R13). Shared across the two .cpp files; not part of the API surface.
[[nodiscard]] std::string statusFields(const measure::Snapshot& snapshot,
                                       const ApiSettings& settings);

/// The body of a transfer-shaped block -- axis, effectiveAverages,
/// appliedDelaySamples, the three arrays -- without braces, so `/transfer`
/// and `/snapshot`'s `transfer` key are the same bytes by construction.
[[nodiscard]] std::string transferFields(const measure::Snapshot& snapshot,
                                         const measure::TransferBlock& block,
                                         const Request& request);

/// The MTW block's fields, likewise. Declared here rather than duplicated:
/// the union endpoint and the single endpoint must never drift apart.
[[nodiscard]] std::string mtwFields(const measure::MtwBlock& block, const Request& request);
[[nodiscard]] std::string averageFields(const measure::AverageBlock& block,
                                        const Request& request);
[[nodiscard]] std::string positionsArray(const measure::Snapshot& snapshot);
[[nodiscard]] std::string bandsArray(const measure::Snapshot& snapshot);

}  // namespace detail

}  // namespace rta::api
