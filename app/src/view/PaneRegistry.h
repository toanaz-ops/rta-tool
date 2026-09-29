// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Decision 6.
#pragma once

#include "trace/Workspace.h"

#include <string>

namespace rta::trace {
class TraceLibrary;
}

namespace rta::view {

/// The pane type vocabulary. Lives in app/, never in az_ui -- `rta`,
/// `transfer` and `spl` are measurement vocabulary, and the module rule keeps
/// that out of the design system.
///
/// `Spl` added by lane L6a task W2-D (record docs/dsp/
/// 2026-09-16-spl-pro-l6a.md §11, SPL-R11). No `kSchemaVersion` bump
/// (`SessionCodec.h:30` stays 3): an older build reading a session that names
/// `"spl"` already falls back to `Rta` and REPORTS it through `fellBack`,
/// which is this type's whole contract below.
///
/// `Xover` added by the ALIGN-R8 reversal (owner, 2026-09-26; amendment in
/// docs/dsp/2026-09-06-l7-alignment-wizard.md): the G18 crossover surface
/// becomes a live pane. Same no-version-bump shape as `Spl` -- an older
/// build reading `"xover"` falls back to `Rta` and reports it.
///
/// `Eq` added by the L7-EQ UI lane (docs/plans/2026-09-29-eq-ui-lane-plan.md
/// T4): the auto-EQ pane, fifth selector button. Same no-version-bump shape --
/// an older build reading `"eq"` falls back to `Rta` and reports it. EQ state
/// itself (picks, committed filters) is NOT in the session file (plan risk 4);
/// only the pane choice round-trips.
enum class PaneView { Rta, Transfer, Spl, Xover, Eq };

struct PaneResolution {
    PaneView view = PaneView::Rta;
    /// True when `requested` was not recognised. The caller REPORTS this; it
    /// does not refuse the session over it.
    bool fellBack = false;
    std::string requested;
};

[[nodiscard]] inline PaneResolution resolvePaneView(const std::string& name) {
    PaneResolution out;
    out.requested = name;
    if (name == "rta") return out;
    if (name == "transfer") { out.view = PaneView::Transfer; return out; }
    if (name == "spl") { out.view = PaneView::Spl; return out; }
    if (name == "xover") { out.view = PaneView::Xover; return out; }
    if (name == "eq") { out.view = PaneView::Eq; return out; }
    out.fellBack = true;   // falls back to Rta, and says so
    return out;
}

/// The inverse of resolvePaneView -- what a session Save should write for a
/// live `PaneView`, so the vocabulary a saved `[pane] view=` line uses is
/// always one resolvePaneView already recognises, never a second,
/// hand-written spelling that could drift out of sync with it.
[[nodiscard]] inline std::string paneViewName(PaneView view) {
    if (view == PaneView::Transfer) return "transfer";
    if (view == PaneView::Spl) return "spl";
    // PR #45 fix round 3 (verifier, PR #43 reconciliation checklist item 2):
    // this arm was missing -- an XOVER session save silently fell through to
    // "rta" and lost the pane choice on the very next Open. See
    // test_pane_view_name_round_trip.cpp for the guard that would have
    // caught it: a round trip over EVERY `PaneView` enumerator.
    if (view == PaneView::Xover) return "xover";
    if (view == PaneView::Eq) return "eq";
    return "rta";
}

/// The one capability `WorkspaceView` needs from a pane it did not build
/// itself: "can this be pointed at a trace library". `WorkspaceView`'s
/// factory (`view/WorkspaceView.h`) returns a plain `juce::Component`
/// precisely so that file never includes `RtaView.h` or `TransferView.h` --
/// doing so would make it a second composition root, wiring a dependency
/// (which pane class needs an `AnalysisThread`, which needs a library) that
/// is `MainComponent`'s job alone. This interface is the seam:
/// `WorkspaceView::setLibrary` `dynamic_cast`s each child to
/// `LibraryConsumer*` and forwards only to the ones that answer -- "forwards
/// to every child that ACCEPTS one" (task brief), not every child. A pane
/// type that never implements this (a future spectrograph, say, which reads
/// no library at all) is not an error, it is a pane with nothing to draw
/// from one.
///
/// Lives here rather than in its own file so a task that adds it does not
/// also have to add a new entry to `measure_has_no_framework_deps`'s file
/// list (trap 1): this header is JUCE-free already and on that list, and the
/// interface below needs nothing this header does not already forward-
/// declare.
class LibraryConsumer {
public:
    virtual ~LibraryConsumer() = default;

    /// Same nullable contract every implementer states on its own
    /// `setLibrary` (RtaView.h, TransferView.h): null means "draw nothing
    /// from a library", and is the state a freshly constructed pane starts
    /// in.
    virtual void setLibrary(const rta::trace::TraceLibrary* library) = 0;
};

}  // namespace rta::view
