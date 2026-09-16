// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// L7-ALIGN task D (docs/plans/2026-09-15-L7-align-impl-plan.md; decision
// record docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.3, Sec.9).
//
// WHY THIS IS IN app/ AND NOT core/. core takes spans and returns numbers.
// "Which crossover the designer built" is not a number any measurement
// produces -- it is an answer the OPERATOR gives, and this file is the lookup
// over that answer. A builder who moves `enum class CrossoverFamily` into
// core/ has moved the ruling's boundary; the test test_crossover_topology.cpp
// scans core/ for exactly that.
#pragma once

namespace rta::measure {

/// The two families this lane ships. Bessel is deferred on purpose: its HP/LP
/// offset is NOT a constant in f, so it is not a row in this table -- adding a
/// family is a research pass, adding an ORDER is arithmetic (record Sec.3,
/// Rane Note 147).
enum class CrossoverFamily { LinkwitzRiley, Butterworth };

/// Wizard question (c): has the processor already inverted one output?
///
/// It must be ASKED. 180 degrees of wiring and 180 degrees of topology are the
/// same thing to a measurement, so no measurement can separate them -- and
/// "maximise the measured sum" is that derivation wearing a different hat
/// (record Sec.6, owner ruling 2026-08-30).
enum class ProcessorInversion { Yes, No, Unknown };

struct Topology {
    CrossoverFamily family = CrossoverFamily::LinkwitzRiley;
    int order = 4;
};

struct ExpectedOffset {
    /// The line the G18 surface draws, in radians, wrapped to (-pi, pi].
    double radians = 0.0;

    /// True only for ProcessorInversion::Unknown, where there are TWO candidate
    /// lines and the operator picks (record Sec.13.3). It is not a third value
    /// and nothing here chooses between them.
    bool ambiguous = false;
    double alternativeRadians = 0.0;
};

/// Record Sec.3's whole table as ONE closed form, evaluated.
///
/// A Butterworth low-pass of order N is L(s) = 1/D_N(s) and the matching
/// high-pass is H(s) = s^N/D_N(s), so H/L = s^N and on s = j*omega
///
///     arg H - arg L = N * 90 degrees,   at EVERY frequency, not just at fc.
///
/// A Linkwitz-Riley of order N is the Butterworth of order N/2 cascaded with
/// itself (Linkwitz 1976; Rane Note 160), so the same relation holds with the
/// same N -- which is why `family` does not appear in the arithmetic at all.
/// What the family changes is the DESIGNED SUM (0 dB for LR, +3.01 dB for
/// BW4), and that is a mark on a plot, not this function's job.
///
/// SETTLED by docs/research/2026-09-15-l7-align-order4-probe.md Sec.8 (PR #3,
/// merged): the identity holds exactly under this repo's convention, analog
/// and digital, orders 1-8, at every frequency, so this table ships UNCHANGED.
/// L4a's "wrong sign at order 4" is attributed there to L4a decision 6b's
/// UN-WHITENED correlation-peak-sign rule applied across two different
/// passbands -- an estimator this repo does not ship -- and not to any
/// convention. An even-order sign discrepancy is evidence about the SYSTEM,
/// never about a convention, and no topology sign may be read off ANY
/// correlation peak, whitened or not.
///
/// The offsets are arg(H_HP-side) - arg(H_LP-side), HP relative to LP
/// (ALIGN-R14). 0 and 180 are symmetric under swapping which is which; +-90 is
/// not, which is why the wizard asks which source is the high-pass side.
[[nodiscard]] ExpectedOffset expectedOffset(Topology topology, ProcessorInversion inversion);

}  // namespace rta::measure
