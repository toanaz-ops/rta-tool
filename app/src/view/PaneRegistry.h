// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Decision 6.
#pragma once

#include "trace/Workspace.h"

#include <string>

namespace rta::view {

/// The pane type vocabulary. Lives in app/, never in az_ui -- `rta` and
/// `transfer` are measurement vocabulary, and the module rule keeps that out
/// of the design system.
enum class PaneView { Rta, Transfer };

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
    out.fellBack = true;   // falls back to Rta, and says so
    return out;
}

}  // namespace rta::view
