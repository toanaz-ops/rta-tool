// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// L7-OUT task D (docs/plans/2026-09-07-L7-out-impl-plan.md; decision record
// docs/dsp/2026-09-06-l7-output-path.md sec.3, sec.9). Policy over the
// OutputEngine PRIMITIVE (record sec.3: "independent per-output gates are
// the primitive; strict solo is app policy") -- a loop over routeOutput,
// exactly like RoutingPlan.h is a pure function over ChannelConfig. No
// app/ code anywhere else may touch a gate or the source directly (sec.9).
#pragma once

#include "rta/platform/ChannelConfig.h"
#include "rta/platform/OutputEngine.h"

namespace rta::measure {

/// Owner ruling (record sec.13.2): a SEQUENCE (auto-step, G20) defaults to
/// `StrictSolo`; a MANUAL toggle defaults to `Additive`. The platform is
/// indifferent between the two (record sec.3) -- this enum is the only place
/// that distinction is ever made.
enum class SoloMode { StrictSolo, Additive };

/// Every output off. The caller's own loop over `routeOutput` -- no new
/// platform state, matching record sec.3's "app/ supplies soloOutput(ch) ...
/// as a loop over routeOutput".
inline void muteAll(rta::platform::OutputEngine& engine) noexcept {
    for (int ch = 0; ch < rta::platform::kMaxChannels; ++ch) {
        engine.routeOutput(ch, false);
    }
}

/// `outputChannel` on, every other output off -- SMPTE ST 202 sec.5.6 / M1
/// *Autosolo*'s model (record sec.3), and what `CaptureSequencer::onStep`
/// binds to for G20.
inline void soloOutput(rta::platform::OutputEngine& engine, int outputChannel) noexcept {
    for (int ch = 0; ch < rta::platform::kMaxChannels; ++ch) {
        engine.routeOutput(ch, ch == outputChannel);
    }
}

/// One manual or sequenced toggle. `StrictSolo` with `on == true` clears
/// every other output first (Smaart's multi-mono model still reachable
/// through repeated `Additive` toggles, per record sec.3); every other
/// combination -- `Additive` either way, or `StrictSolo` turning a channel
/// OFF -- is the plain per-channel `routeOutput` with nothing else touched.
inline void applyToggle(rta::platform::OutputEngine& engine, int outputChannel, bool on,
                         SoloMode mode) noexcept {
    if (mode == SoloMode::StrictSolo && on) {
        soloOutput(engine, outputChannel);
        return;
    }
    engine.routeOutput(outputChannel, on);
}

}  // namespace rta::measure
