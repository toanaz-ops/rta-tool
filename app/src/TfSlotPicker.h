// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. No JUCE: plain ints, so a test can drive it.
#pragma once

#include "measure/RoutingPlan.h"

#include <algorithm>

namespace rta::app {

/// Combo-box ids are 1-based because id 0 means "nothing selected" (a cleared
/// combo fires onChange with getSelectedId() == 0). The slot is id - 1, clamped
/// to 0..kMaxTransferFunctions-1 so a cleared combo gives slot 0 rather than -1
/// and the routing matrix never keeps a stale or out-of-range slot (H8 review L-8).
[[nodiscard]] constexpr int tfSlotForPickerId(int pickerId) noexcept {
    return std::clamp(pickerId - 1, 0, rta::measure::kMaxTransferFunctions - 1);
}

}  // namespace rta::app
