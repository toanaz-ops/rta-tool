// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
//
// The two asked-question lists the XOVER pane and the ALIGN pane both offer:
// the crossover topology and wizard question (c), "has the processor already
// inverted one output?". Moved out of CrossoverPaneView.cpp (ALIGN-UI lane,
// T5) so the topology is asked in two places from ONE list -- two copies of a
// seven-row table are two chances for the panes to disagree about what "LR-4"
// means.
#pragma once

#include "measure/CrossoverTopology.h"

#include <array>

namespace rta::view {

/// One entry in the topology picker: a real `rta::measure::Topology` paired
/// with the label the combo box shows. A FINITE, HAND-PICKED list --
/// `expectedOffset` (CrossoverTopology.h) is arithmetically valid at any
/// order, but a combo box needs a finite set, and these seven are what Rane
/// Note 160 (LR-2/4/8) and ordinary practice (BW-1..4) actually ship. Adding an
/// order some rig actually uses is a one-line addition here, never a reason to
/// invent a default.
struct TopologyChoice {
    rta::measure::Topology topology;
    const char* label;
};

inline constexpr std::array<TopologyChoice, 7> kTopologyChoices{ {
    { { rta::measure::CrossoverFamily::LinkwitzRiley, 2 }, "LR-2" },
    { { rta::measure::CrossoverFamily::LinkwitzRiley, 4 }, "LR-4" },
    { { rta::measure::CrossoverFamily::LinkwitzRiley, 8 }, "LR-8" },
    { { rta::measure::CrossoverFamily::Butterworth, 1 }, "BW-1" },
    { { rta::measure::CrossoverFamily::Butterworth, 2 }, "BW-2" },
    { { rta::measure::CrossoverFamily::Butterworth, 3 }, "BW-3" },
    { { rta::measure::CrossoverFamily::Butterworth, 4 }, "BW-4" },
} };

/// One entry in the inversion picker: wizard question (c) with all three
/// answers a real operator can give, never just YES/NO. `Unknown` is not a
/// refusal to answer: it is answered, and it means "draw both candidate lines"
/// (Sec.13.3 via `CrossoverSurface::targetAmbiguous()`).
struct InversionChoice {
    rta::measure::ProcessorInversion inversion;
    const char* label;
};

inline constexpr std::array<InversionChoice, 3> kInversionChoices{ {
    { rta::measure::ProcessorInversion::No, "NOT INVERTED" },
    { rta::measure::ProcessorInversion::Yes, "INVERTED" },
    { rta::measure::ProcessorInversion::Unknown, "UNKNOWN -- SHOW BOTH" },
} };

}  // namespace rta::view
