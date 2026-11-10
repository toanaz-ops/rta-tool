// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE.
#pragma once

namespace rta::trace {

// Library group names. One place, so a picker that filters on a group and the
// code that stores into it cannot drift apart by a typo. The RT60, ALIGN and IR
// pickers need an impulse response behind the trace, so they list `kSweepGroup`
// only; a gated response (`kGatedGroup`) and a WAV transfer (`kWavGroup`) have
// none (docs/dsp/2026-11-10-g22-offline-wav-transfer.md D6).
inline constexpr const char* kSweepGroup = "SWEEP";
inline constexpr const char* kGatedGroup = "GATED";
inline constexpr const char* kWavGroup = "WAV";

}  // namespace rta::trace
