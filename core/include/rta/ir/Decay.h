// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace rta::ir {

/// Why a decay figure was withheld.
///
/// `None` if and only if a number is present. The pair is asserted equivalent
/// in both directions by a test, so neither can drift into meaning something
/// the other does not -- the same discipline `Polarity::Refusal` uses.
///
/// Every refusal here means "this measurement cannot support the question",
/// never "the code gave up". Decisions and measurements are in
/// `docs/dsp/2026-08-30-ir-decay-l4b.md`.
enum class DecayRefusal {
    None,

    /// The band is too narrow for a decay this short, so what would be reported
    /// is largely the FILTER's decay rather than the room's.
    ///
    /// Measured, oracle truncation, ensemble of 24: at a TRUE B*T of 3.7 no
    /// filtering mode is usable -- +19.6 % forward, +45.8 % zero-phase,
    /// +16.3 % time-reversed on T30. By 5.8 the two viable modes recover to
    /// within a few percent. The literature's floor for the best available
    /// method is about 4 and it is reproduced here rather than assumed.
    ///
    /// Expressed in B*T and NOT as a ratio of filter decay to room decay.
    /// Both sessions on this lane measured that ratio and their tables differed
    /// by exactly 2x on every row -- one designed the band at 8th order, the
    /// other at 16th. A ratio is a property of an implementation's filter
    /// order; `B` comes off the band table and `T` off the room.
    ///
    /// ## The threshold is 6, not 4, and the reason is a defect found by a test
    ///
    /// The gate can only read the decay it MEASURED, and at low B*T that
    /// measurement is inflated by the very filter the gate exists to catch.
    /// Measured, 1/3-octave, zero-phase, ensemble of 12:
    ///
    ///     true B*T 2.78 -> measured 5.31   (inflation x1.91)
    ///     true B*T 3.71 -> measured 5.27   (x1.42)
    ///     true B*T 11.12 -> measured 11.15 (x1.00)
    ///
    /// So a threshold of 4 in measured units admits everything it was written
    /// to refuse. A first version of this gate did exactly that, and the test
    /// that caught it is `"A band too narrow for the decay refuses"`.
    ///
    /// The two populations are NOT cleanly separable in measured units -- the
    /// worst unusable cell reads 5.58 and the best usable one reads 5.48. Six
    /// is the smallest threshold with **zero false accepts** on two grids built
    /// by two sessions independently (25 cells and 30 cells; they agree on the
    /// inflation to within 0.4 %).
    ///
    /// **The price is a RANGE, not one cell.** On the denser grid the whole
    /// band of true B*T from about 4 to 6 reads back as 5.6-5.9 and is refused
    /// -- four usable cells, not one. A dry room measured in the low
    /// third-octaves will meet this refusal ROUTINELY rather than rarely, and
    /// anything reporting these figures to an operator should say so. Erring
    /// toward refusal is still the right direction for a number someone acts
    /// on, but it is not a free choice.
    ///
    /// ## The margin is 0.29 and it SHRINKS as the ensemble grows
    ///
    /// Both calibrating grids used a median of 12 realisations. That is the
    /// same mistake that later voided this lane's EDT figures, where a median
    /// of 24 wandered over 47 percentage points between seed families -- so the
    /// threshold was re-checked rather than trusted. Worst measured B*T among
    /// cells whose TRUE B*T is below 4:
    ///
    ///     n=12, three different seed families:  5.27  5.41  5.58
    ///     n=100:                                5.48  5.58  5.71
    ///
    /// Zero false accepts still, at every ensemble size tried. **But the worst
    /// case RISES with n** -- 5.58 at twelve, 5.71 at a hundred -- because a
    /// maximum can only grow as more of the tail is sampled. The margin to 6.0
    /// is 0.29 and it is not guaranteed to survive a much larger ensemble.
    ///
    /// Do not read this as "6.0 is proven". Read it as: 6.0 holds everywhere it
    /// has been measured, the property being relied on is an extremum rather
    /// than an average, and an extremum is the statistic least protected by a
    /// small sample. If this constant is ever re-derived, start at n >= 100.
    ///
    /// **This constant is filter-order dependent** -- the inflation is the
    /// filter's own decay -- so changing the section count means re-measuring
    /// it, not assuming it carries over. Record decision 4c.
    BandwidthTimeTooSmall,

    /// No decreasing slope could be fitted, so there is no decay to describe.
    NoDecayFound,

    /// The curve never falls to the lower bound the figure needs -- a T30 needs
    /// 35 dB below the start, and a curve that only reaches 22 dB has not
    /// answered the question.
    ///
    /// Refusing beats the widespread alternative of taking the nearest
    /// available sample: `python-acoustics` 0.2.6 does
    /// `sch_db[abs(sch_db - end).argmin()]`, which reports a decay time for a
    /// curve that never decayed that far.
    RangeTooSmall,

    /// Lundeby could not place a crossing between the decay and the noise
    /// floor, so the integral has no honest upper limit.
    ///
    /// Reached when the record holds less than the noise-estimate margin past
    /// the crossing. The margin is 10 dB OF DECAY, converted through the
    /// measured slope -- never a fraction of the record. A buffer-fraction
    /// margin ties the answer to how long the operator recorded for, which is
    /// the exact dependency truncation exists to remove; it would reinstate it
    /// one level down where it is far harder to see. Record decision 3a.
    NoNoiseEstimate,

    /// Zero-phase filtering was asked for without samples before the arrival.
    ///
    /// Not a fussy precondition. A forward-backward filter is non-causal and
    /// pads its input; around a direct sound sitting at index 0 the padding
    /// reflects into a step of twice that sound and the filter rings on it.
    /// Measured, 125 Hz octave band, unit spike: energy 24.70 at index 0
    /// against 0.0033 at 200 ms -- **38.7 dB fabricated, landing exactly where
    /// EDT reads**.
    ///
    /// Trimming an impulse response to begin at the direct sound is the
    /// ordinary thing to do, so this must refuse rather than pad silently.
    /// `Deconvolution` keeps its negative-time region; do not discard it before
    /// filtering. Record decision 5a.
    NoLeadIn,
};

/// One decay figure and, when absent, the reason.
struct DecayTime {
    double seconds = 0.0;
    DecayRefusal refusal = DecayRefusal::NoDecayFound;

    [[nodiscard]] bool has() const noexcept { return refusal == DecayRefusal::None; }
};

/// EDT, T20 and T30 from one curve. Each can refuse independently: a decay with
/// 28 dB of usable range answers T20 and refuses T30, and reporting one figure
/// while silently substituting another is how a 20 dB measurement gets read as
/// a 30 dB one.
struct DecayTimes {
    DecayTime edt;   ///<  0 dB to -10 dB
    DecayTime t20;   ///< -5 dB to -25 dB
    DecayTime t30;   ///< -5 dB to -35 dB
};

/// Backward-integrated energy decay curve, in dB, referenced to its own start.
struct EnergyDecayCurve {
    /// One sample per input sample from the arrival onward. 0 dB at index 0.
    std::vector<float> db;

    double sampleRate = 0.0;

    /// Where the decay was judged to meet the noise floor. Samples beyond it
    /// are excluded from the integral, and the energy they would have carried
    /// is added back by the correction below.
    std::size_t crossingIndex = 0;

    /// Energy added for the region past `crossingIndex`, under Lundeby's
    /// assumption that the late decay continues as a SINGLE exponential.
    ///
    /// Reported rather than hidden because that assumption is the load-bearing
    /// part. A coupled space or a hard rear wall decays at two rates, and this
    /// term then underestimates -- a caller comparing it against the integrated
    /// energy can see how much of the answer rests on it.
    double truncationCorrection = 0.0;

    /// The decay the B*T gate was applied to, in seconds: T30 where the range
    /// allowed it, otherwise T20.
    ///
    /// Reported because a gate nobody can read is a gate nobody can check --
    /// and because reading it is how the defect below was found.
    ///
    /// It is deliberately THE SAME quantity `decayTimes()` returns, not
    /// Lundeby's internal working slope. An earlier version gated on that
    /// working slope, fitted over the top 20 dB of the smoothed squared
    /// response, and on a 40 Hz third-octave it read 0.963 s where the
    /// integrated curve gives 0.607 s and the truth is 0.400 s. Both numbers
    /// are "the decay"; gating on the one nobody reads let a band through while
    /// the reported figure said something else.
    double lateDecaySec = 0.0;

    DecayRefusal refusal = DecayRefusal::NoDecayFound;

    [[nodiscard]] bool has() const noexcept { return refusal == DecayRefusal::None; }
};

/// Clarity and definition, from the same band-filtered response.
///
/// These are ratios of energies inside ONE signal, so they are invariant to its
/// overall scale -- unlike the decay times, they need no dynamic-range gate of
/// their own. They still need the arrival index to be right, because the split
/// is measured from it.
struct DecayValue {
    double value = 0.0;
    DecayRefusal refusal = DecayRefusal::NoDecayFound;

    [[nodiscard]] bool has() const noexcept { return refusal == DecayRefusal::None; }
};

struct Clarity {
    DecayValue c50Db;   ///< 10 log10( E[0,50ms) / E[50ms,cut) )
    DecayValue c80Db;   ///< 10 log10( E[0,80ms) / E[80ms,cut) )
    DecayValue d50;     ///< E[0,50ms) / E[0,cut), in 0..1

    /// Each figure refuses on its own, for the same reason `DecayTimes` does.
    ///
    /// A response whose usable region ends at 70 ms answers C50 and cannot
    /// answer C80: there is no late half left for the 80 ms split. Returning a
    /// single verdict for all three would throw away a C50 that is perfectly
    /// well determined, and a caller that then reported "clarity unavailable"
    /// would be describing the code rather than the room.
    [[nodiscard]] bool any() const noexcept {
        return c50Db.has() || c80Db.has() || d50.has();
    }
};

struct DecayConfig {
    /// Intervals per 10 dB of decay used to size the smoothing window.
    ///
    /// The window is sized from the MEASURED decay rate, never from a fixed
    /// number of milliseconds. A fixed window cannot be right at both ends --
    /// 30 ms is a third of a dry room's decay and a twentieth of a hall's --
    /// and that is the same defect, in a different quantity, that L4a measured
    /// in its fixed 50 ms polarity window, which read a 50-71 Hz subwoofer as
    /// 46.9-18270 Hz.
    double intervalsPer10Db = 5.0;

    /// How far above the noise floor the late-decay regression must stay.
    ///
    /// Fitting into the knee, where decay and noise are comparable, flattens
    /// the slope and reads LONG. The error is one-sided, so it does not average
    /// out across bands and cannot be dismissed as scatter.
    double standoffDb = 10.0;

    /// Span of the late-decay fit. Wide enough that slope error is small,
    /// narrow enough to stay above the knee. 20 dB above a 10 dB standoff needs
    /// 30 dB of usable range -- which is where T30's dynamic-range requirement
    /// comes from, rather than being asserted separately.
    double fitRangeDb = 20.0;

    /// Decay past the crossing before the noise is re-estimated. Decibels, not
    /// a fraction of the record: see `DecayRefusal::NoNoiseEstimate`.
    double noiseMarginDb = 10.0;

    int maxIterations = 30;
    double convergenceSec = 0.01;

    /// Refuse below this bandwidth-time product, computed from the MEASURED
    /// decay. Six, not the literature's four, because the measured decay is
    /// inflated by the filter at exactly the values being gated -- see
    /// `DecayRefusal::BandwidthTimeTooSmall` for the grid and the cost.
    double minBandwidthTime = 6.0;
};

/// Band-pass the response with a zero-phase (forward then backward) filter.
///
/// **Zero-phase, and the choice is measured rather than conventional.** Forward
/// filtering is disqualified: at B*T = 5.8 it reports an EDT of +127.9 %, and
/// at 3.7 of +248.0 %, with the truncation point taken from an oracle so that
/// nothing at all is estimated. That is the signal path, not the estimator.
///
/// Above the B*T floor the remaining two modes split -- zero-phase ahead on
/// T30, time-reversed ahead on EDT -- and zero-phase ships because T30 is the
/// figure an operator acts on. Record decision 4d.
///
/// **A forward-backward filter is non-causal, so this cannot run in the audio
/// callback and cannot be expressed by a streaming bank.** That is why the
/// decay path does not reuse `rta::dsp::FilterBank`; the two share a band table
/// and nothing else.
///
/// `originIndex` must leave room before it -- see `DecayRefusal::NoLeadIn`.
/// Returns an empty vector on any refusal; callers get the reason from the
/// functions below, which check the same preconditions.
///
/// Throws `std::invalid_argument` if the sample rate is not positive or the
/// band is not `0 < lowHz < highHz < sampleRate/2`.
[[nodiscard]] std::vector<float> bandFilterZeroPhase(std::span<const float> response,
                                                     double lowHz, double highHz,
                                                     double sampleRate);

/// Backward-integrated decay curve of an already band-filtered response.
///
/// `bandwidthHz` is the band's width, used only for the B*T gate; pass 0 to
/// skip that gate, which is right for a broadband curve and wrong for a band.
///
/// Integration starts at `originIndex`, never at sample 0: the lead-in exists
/// so the filter has somewhere to put its pre-ring, and integrating across it
/// would fold that pre-ring into the answer.
[[nodiscard]] EnergyDecayCurve energyDecayCurve(std::span<const float> bandResponse,
                                                std::size_t originIndex,
                                                double sampleRate,
                                                double bandwidthHz,
                                                const DecayConfig& config = {});

/// EDT, T20 and T30 by least-squares slope, each extrapolated to 60 dB.
///
/// The x2 / x3 / x6 multiplier applied by most reference implementations is NOT
/// applied and must not be. That factor converts a decay measured over a
/// 30/20/10 dB span into a 60 dB figure; a least-squares slope in dB per second
/// already carries the conversion, so `-60/slope` IS the 60 dB time. Applying
/// both double-counts, and does so into a plausible-looking number.
[[nodiscard]] DecayTimes decayTimes(const EnergyDecayCurve& curve);

/// C50, C80 and D50 from a band-filtered response, over a TRUNCATED late half.
///
/// ## The late integral has the same disease the decay times had
///
/// The late term is an integral from 50 or 80 ms to the end of the record. Left
/// untruncated it grows with every noise-only sample, so C50 reads LOW as a
/// function of how long the operator left the recorder running -- the identical
/// failure that makes an untruncated Schroeder curve depend on the same thing.
/// Clarity does not escape it by being a ratio: the numerator is bounded by the
/// split point and only the denominator grows.
///
/// So the caller passes the crossing point from `energyDecayCurve`, and both
/// integrals stop there. `curve.truncationCorrection` is added to the LATE half
/// only -- the energy past the crossing is by construction all late.
///
/// ## The origin is the arrival, and it is not sample zero
///
/// The split is measured from `originIndex`, so an origin wrong by 5 ms moves
/// 5 ms of energy across the boundary and C50 moves with it. Pass the
/// `Deconvolution::originIndex`, not the start of the buffer.
///
/// Energy that zero-phase filtering placed BEFORE the origin counts as EARLY.
///
/// `filtfilt` is symmetric about an impulse, so it puts half of the direct
/// sound's band energy before t = 0. That is the direct sound's own energy,
/// displaced by a filter that conserves it -- not leakage. An earlier version
/// discarded it on the leakage reading, and discarding it is what creates a
/// filter-dependent loss: measured against the C50 of the UNFILTERED impulse
/// response, excluding costs -3.06 dB at 1/3-octave 40 Hz where including costs
/// -0.33 dB. Both conventions were measured; the numbers are in Decay.cpp.
///
/// Refuses when the response ends before the split point, rather than treating
/// a missing late half as silence, which would report a magnificent room for a
/// capture that simply stopped early.
[[nodiscard]] Clarity clarity(std::span<const float> bandResponse,
                              std::size_t originIndex,
                              double sampleRate,
                              const EnergyDecayCurve& curve);

}  // namespace rta::ir
