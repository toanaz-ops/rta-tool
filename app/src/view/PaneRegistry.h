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
enum class PaneView { Rta, Transfer, Spl };

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
    out.fellBack = true;   // falls back to Rta, and says so
    return out;
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
