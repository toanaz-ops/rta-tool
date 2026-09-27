// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: enforced by the
// measure_has_no_framework_deps ctest (app/tests/CMakeLists.txt names this
// file explicitly, the same GLOBS escape hatch Trace.h/TraceLibrary.h
// already use). station-3 STORE plan, task T2/T3/T4
// (docs/plans/2026-09-27-store-lane-plan.md; docs/research/
// 2026-09-27-store-trace.md, Part A3): the one seam that turns a live
// `Snapshot` into a storable `Trace`. Nothing here reads a clock for
// identity (T3) and nothing here knows about JUCE, a button, or a pane
// selector widget -- only the pane ENUM the caller says it is looking at.
#pragma once

#include "measure/Analyser.h"
#include "measure/Snapshot.h"
#include "trace/Trace.h"
#include "view/PaneRegistry.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rta::trace {

/// The one radians<->degrees crossing `AnalyserPublish.cpp` makes, run in
/// reverse: that file multiplies by `180.0/std::numbers::pi` to go
/// radians->degrees building a `Snapshot`; this is the same literal, the
/// same direction it always was, just the other end of the round trip
/// (research A3.1 -- reusing the constant rather than re-deriving
/// `pi/180.0` independently is what keeps the two crossings from drifting
/// apart, dual-FFT-conventions memory).
[[nodiscard]] std::vector<float> degToRadPhase(std::span<const float> phaseDeg);

/// A monotonic identity for a freshly-frozen capture -- never a clock
/// reading. `TraceLibrary::add` refuses a duplicate id outright (T4's own
/// caller depends on that never happening for two distinct STORE presses),
/// and two presses landing inside the same publish interval, or two calls
/// from an automated/scripted caller with no wall-clock resolution to lean
/// on, must still resolve to two different strings (research A3.4).
[[nodiscard]] std::string nextCaptureId();

/// Freezes `snapshot` into a `Trace`, branching on which pane is currently
/// showing (owner decision 2026-09-27, "one global STORE button"):
///
/// - `PaneView::Rta`: a magnitude-only trace built from `snapshot.spectrumDb`
///   -- never a phase field (a single-channel capture has none to invent).
/// - `PaneView::Transfer`: the FIXED-FFT `snapshot.transfer` block (owner
///   decision 1 -- MTW is what the pane may be SHOWING, but the fixed-FFT
///   block is what this freezes; the caller's readout is what has to say
///   so). Refuses (`std::nullopt`) when no reference was fed, checked
///   independently of whether `transfer` itself happens to be populated --
///   an inconsistent snapshot must still refuse on the flag alone.
/// - Any other pane (`Spl`, `Xover`): refuses. Neither is a producer of a
///   per-bin curve (research C2); the caller is expected to have already
///   disabled the button for these, this is the defensive second line.
///
/// `config` supplies what only the analyser that PRODUCED this snapshot
/// knows (window, averaging) -- see `AnalysisThread::config()`.
/// `deviceName`/`channelRoles` are plain strings the caller builds from
/// `AudioIo`/`ChannelConfig` state this file must not depend on (research
/// A3.3: neither lives on `Snapshot`).
///
/// `CaptureMeta::calibrationOffsetDb`/`calibrationUnit` are left at their
/// defaults (0.0f / `DbFs`) on every path -- owner decision C3: RTA/TRANSFER
/// are dBFS-native, and inventing a general calibration offset for them is
/// exactly the silent unit-mismatch `Trace.h`'s own comment warns against.
[[nodiscard]] std::optional<Trace> traceFromSnapshot(const rta::measure::Snapshot& snapshot,
                                                      rta::view::PaneView pane,
                                                      const rta::measure::Analyser::Config& config,
                                                      std::string deviceName,
                                                      std::string channelRoles);

}  // namespace rta::trace
