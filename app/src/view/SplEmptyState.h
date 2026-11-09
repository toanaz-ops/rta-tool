// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: plain strings, so a test can
// read them. UI test 2026-11-09: with SYNTHETIC on the SPL pane drew nothing
// at all, because a session existed but had no metric to show.
#pragma once

#include <string>
#include <vector>

namespace rta::view {

/// The empty-state text of the SPL pane, one entry per line. `sessionOpen` is
/// true when the analysis side already holds an SPL session (which happens as
/// soon as the input runs) but nothing in it can be shown yet.
[[nodiscard]] inline std::vector<std::string> splEmptyStateLines(bool sessionOpen) {
    std::vector<std::string> lines;
    lines.emplace_back(sessionOpen ? "NO SPL READING YET" : "NO SPL SESSION");
    lines.emplace_back("SPL needs a real measurement input, calibrated with CAL START / CAL END.");
    lines.emplace_back("The SYNTHETIC source alone produces no SPL reading.");
    return lines;
}

}  // namespace rta::view
