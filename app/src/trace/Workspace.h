// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Decision 6 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
#pragma once

#include <cmath>
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

/// Clamp to `kMaxPanes` and normalise the weights. An empty input yields exactly
/// one default pane: a session saved before workspaces existed and a brand-new
/// one are the same case, and both must open.
///
/// **A non-positive weight stays zero.** It is not redistributed, not floored,
/// not turned into an equal share. `az::ui::splitVertically` -- the only
/// consumer of these numbers -- already decided what a non-positive weight
/// means: that child gets zero height, and equal shares happen only when NOBODY
/// expressed a preference. Any other rule here would silently reopen a pane the
/// user collapsed, and the two functions would disagree about the same number.
///
/// So: positive weights are scaled to sum to 1 among themselves; non-positive
/// ones are set to exactly 0; and if there are no positive weights at all,
/// every pane gets `1/n`, which is the same fallback `splitVertically` makes
/// for the same reason.
///
/// **The result must not depend on the scale of the input.** `{1, 1, 0}` and
/// `{2, 2, 0}` express the identical preference and must produce identical
/// shares -- an earlier draft normalised the whole set jointly after replacing
/// zeros with `1/n`, which gave the collapsed pane 0.143 in the first case and
/// 0.077 in the second. Two inputs meaning the same thing produced different
/// layouts, which is the property `weights are normalised on read` exists to
/// forbid and only tested for the all-positive case.
///
/// A non-finite weight (`inf`, `nan`) counts as non-positive. `std::from_chars`
/// accepts both from a hand-edited file, and either one turns joint
/// normalisation into `NaN` shares -- a layout with no geometry at all.
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

    // "Positive and finite" is the only weight that expresses a real
    // preference. inf/nan reach here from a hand-edited file via
    // std::from_chars, which parses both -- treat them the same as zero or a
    // negative: not a usable opinion about this pane's share.
    auto isUsable = [](float w) { return w > 0.0f && std::isfinite(w); };

    double positiveSum = 0.0;
    for (const auto& p : panes) {
        if (isUsable(p.weight)) positiveSum += p.weight;
    }

    if (positiveSum <= 0.0) {
        // Nobody expressed a preference at all (every weight zero, negative,
        // or non-finite): the only case where an equal share is the right
        // fallback, matching splitVertically's own fallback for the same
        // input.
        const float equalShare = 1.0f / static_cast<float>(panes.size());
        for (auto& p : panes) p.weight = equalShare;
        return panes;
    }

    // Scale positive weights to sum to 1 AMONG THEMSELVES -- not among the
    // full set including the zeroed panes -- so {1,1,0} and {2,2,0} both
    // land on {0.5, 0.5, 0.0} regardless of the input's absolute scale. A
    // non-positive/non-finite weight becomes exactly 0: the pane the user
    // collapsed stays collapsed, it does not silently reopen.
    for (auto& p : panes) {
        p.weight = isUsable(p.weight)
                       ? static_cast<float>(static_cast<double>(p.weight) / positiveSum)
                       : 0.0f;
    }
    return panes;
}

}  // namespace rta::trace
