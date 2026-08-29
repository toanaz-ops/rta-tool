// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Decision 6 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
#pragma once

#include <string>
#include <vector>

namespace rta::trace {

/// Vertical-only, 1..3 panes, one workspace per session.
///
/// The cap is a judgement, matching Open Sound Meter's: below roughly a third
/// of a 760 px window a dB pane stops resolving the 0.1 dB the readout rules
/// promise. Friture's uncapped docking grid needs a dock framework az_ui does
/// not have and buys freedom this use case -- one screen, read from six feet,
/// seconds at a time -- never asks for.
inline constexpr int kMaxPanes = 3;

inline constexpr const char* kDefaultPaneView = "rta";

/// `view` is a VERBATIM string, never an enum, at this layer. SessionCodec must
/// not validate it: mapping a name to a pane type -- and falling back when the
/// name is unknown -- belongs to view/PaneRegistry.h, so the codec keeps its
/// "refuse what you do not understand" rule for measurements while the view
/// keeps its "never refuse a session over a layout word" rule for layouts.
struct PaneSpec {
    std::string view = kDefaultPaneView;
    float weight = 1.0f;
};

/// Clamp to `kMaxPanes`, drop non-positive weights to equal shares, normalise
/// the rest to sum to 1. An empty input yields exactly one default pane: a
/// session saved before workspaces existed and a brand-new one are the same
/// case, and both must open.
[[nodiscard]] inline std::vector<PaneSpec> normalisePanes(std::vector<PaneSpec> panes) {
    // A session saved before workspaces existed and a brand-new session both
    // decode to zero panes -- they are the same case and must both open to
    // exactly one pane, not to nothing.
    if (panes.empty()) {
        return {PaneSpec{}};
    }

    if (panes.size() > static_cast<std::size_t>(kMaxPanes)) {
        panes.resize(static_cast<std::size_t>(kMaxPanes));
    }

    // A non-positive weight (zero, negative, or the whole set at once from a
    // hand-edited file) expresses no usable preference. Privileging pane 0
    // for no stated reason would be inventing one, so every such pane is
    // given the same starting point any of them would get if nobody had an
    // opinion at all: an equal share of the total.
    const float equalShare = 1.0f / static_cast<float>(panes.size());
    for (auto& p : panes) {
        if (!(p.weight > 0.0f)) p.weight = equalShare;
    }

    double sum = 0.0;
    for (const auto& p : panes) sum += p.weight;
    for (auto& p : panes) {
        p.weight = static_cast<float>(static_cast<double>(p.weight) / sum);
    }
    return panes;
}

}  // namespace rta::trace
