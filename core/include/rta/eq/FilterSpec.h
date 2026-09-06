// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

namespace rta::eq {

/// No all-pass: EQ record Sec.3 gives the placement solve a zero column for
/// any filter type that cannot move magnitude, and an all-pass never does.
enum class FilterType { Peaking, LowShelf, HighShelf };

/// One RBJ cookbook filter, before it is turned into coefficients for a
/// specific sample rate -- the shared vocabulary L7-EQ, L7-ALIGN and L7-FIR
/// all place, allocate and export around (EQ Sec.7).
struct FilterSpec {
    FilterType type = FilterType::Peaking;
    double fcHz = 1000.0;
    double q = 1.0;
    double gainDb = 0.0;
};

}  // namespace rta::eq
