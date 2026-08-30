// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#include "rta/ir/Decay.h"

#include "DecayLundeby.h"

#include "rta/dsp/Biquad.h"
#include "rta/dsp/ButterworthDesign.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rta::ir {

using detail::leastSquares;
using detail::powerToDb;

namespace {

/// Order of the band-pass, as the number of second-order sections.
///
/// Four sections is an 8th-order band-pass, matching what every open reference
/// consulted uses (`python-acoustics` builds `order=8`). Named here rather than
/// left as a literal because the filter's own decay scales with it, and the
/// record's B*T figures were measured at this order -- a future change to it
/// must re-measure them rather than assume they carry over.
constexpr int kSections = 4;

/// First index at or below `level`, searching a monotone-ish decreasing curve.
[[nodiscard]] bool firstAtOrBelow(std::span<const float> db, double level,
                                  std::size_t& index) {
    for (std::size_t i = 0; i < db.size(); ++i) {
        if (static_cast<double>(db[i]) <= level) { index = i; return true; }
    }
    return false;
}

[[nodiscard]] DecayTime fitOne(const EnergyDecayCurve& curve, double upperDb,
                               double lowerDb) {
    DecayTime out;
    if (!curve.has()) { out.refusal = curve.refusal; return out; }

    std::size_t i0 = 0, i1 = 0;
    if (!firstAtOrBelow(curve.db, upperDb, i0) ||
        !firstAtOrBelow(curve.db, lowerDb, i1) || i1 <= i0 + 8) {
        out.refusal = DecayRefusal::RangeTooSmall;
        return out;
    }

    std::vector<double> x, y;
    x.reserve(i1 - i0);
    y.reserve(i1 - i0);
    for (std::size_t i = i0; i < i1; ++i) {
        x.push_back(static_cast<double>(i) / curve.sampleRate);
        y.push_back(static_cast<double>(curve.db[i]));
    }

    double slope = 0.0, intercept = 0.0;
    if (!leastSquares(x, y, slope, intercept) || slope >= 0.0) {
        out.refusal = DecayRefusal::NoDecayFound;
        return out;
    }

    // -60/slope, NOT the x2 / x3 / x6 multiplier the references apply. A slope
    // in dB per second already converts to a 60 dB time; multiplying as well
    // double-counts, and lands on a plausible number while doing it.
    out.seconds = -60.0 / slope;
    out.refusal = DecayRefusal::None;
    return out;
}

}  // namespace

std::vector<float> bandFilterZeroPhase(std::span<const float> response,
                                       double lowHz, double highHz,
                                       double sampleRate) {
    if (!(sampleRate > 0.0)) {
        throw std::invalid_argument("bandFilterZeroPhase: sampleRate must be positive");
    }
    if (!(lowHz > 0.0) || !(highHz > lowHz) || !(highHz < 0.5 * sampleRate)) {
        throw std::invalid_argument("bandFilterZeroPhase: need 0 < lowHz < highHz < fs/2");
    }
    if (response.empty()) return {};

    const auto design = dsp::ButterworthDesign::bandPass(lowHz, highHz, sampleRate,
                                                         kSections);

    // Forward, then the same cascade over the reversed result. The filter's own
    // phase cancels, which is the property the decay analysis needs; it also
    // makes the operation non-causal, which is why it lives here and not in the
    // streaming bank.
    std::vector<float> forward(response.size());
    dsp::BiquadCascade cascade(design.sections);
    cascade.process(response, forward);

    std::vector<float> reversed(forward.rbegin(), forward.rend());
    std::vector<float> back(reversed.size());
    cascade.reset();
    cascade.process(reversed, back);

    std::reverse(back.begin(), back.end());
    return back;
}

EnergyDecayCurve energyDecayCurve(std::span<const float> bandResponse,
                                  std::size_t originIndex, double sampleRate,
                                  double bandwidthHz, const DecayConfig& config) {
    EnergyDecayCurve out;
    out.sampleRate = sampleRate;

    if (!(sampleRate > 0.0) || bandResponse.empty() ||
        originIndex >= bandResponse.size()) {
        out.refusal = DecayRefusal::NoDecayFound;
        return out;
    }

    const auto tail = bandResponse.subspan(originIndex);
    std::vector<double> squared(tail.size());
    for (std::size_t i = 0; i < tail.size(); ++i) {
        const double v = static_cast<double>(tail[i]);
        squared[i] = v * v;
    }

    detail::Crossing crossing;
    if (!detail::findCrossing(squared, sampleRate, config, crossing)) {
        out.refusal = DecayRefusal::NoNoiseEstimate;
        return out;
    }

    // Backward integration over the causal region only, stopping at the
    // crossing. Everything past it is noise the integral would otherwise
    // accumulate into the plateau -- which is what makes an untruncated reading
    // depend on when the operator stopped recording.
    const std::size_t cut = crossing.index;
    std::vector<double> integral(cut, 0.0);
    double running = 0.0;
    for (std::size_t i = cut; i-- > 0;) {
        running += squared[i];
        integral[i] = running;
    }
    if (integral.empty() || integral[0] <= 0.0) {
        out.refusal = DecayRefusal::NoDecayFound;
        return out;
    }

    // Lundeby's correction for the energy from the crossing to infinity, under
    // the assumption that the late decay continues as a SINGLE exponential.
    // If power decays as p(t) = p_i exp(-t/tau), the remaining energy is exactly
    // p_i * tau, and tau in samples is 10 / (slope_dB_per_sample * ln 10).
    //
    // The assumption is the load-bearing part, not the algebra: a coupled space
    // decays at two rates and this term then underestimates. It is reported on
    // the result so a caller can see how much of the answer rests on it.
    const double tauSamples =
        10.0 / (crossing.slopeDbPerSample * std::log(10.0));
    out.truncationCorrection = squared[cut - 1] * tauSamples;
    for (auto& v : integral) v += out.truncationCorrection;

    out.db.resize(cut);
    const double ref = integral[0];
    for (std::size_t i = 0; i < cut; ++i) {
        out.db[i] = static_cast<float>(powerToDb(integral[i] / ref));
    }
    out.crossingIndex = cut;
    out.refusal = DecayRefusal::None;

    // The B*T gate, applied LAST, against the decay this function will actually
    // report -- not against Lundeby's internal working slope.
    //
    // An earlier version gated on that working slope, which is fitted over the
    // top 20 dB of the SMOOTHED SQUARED response. On a 40 Hz third-octave it
    // returned 0.963 s where the integrated curve gives 0.607 s and the truth
    // is 0.400 s: a 2.4x error, in the direction that opens the gate, in
    // exactly the narrow bands the gate exists to close. The two numbers are
    // both "the decay", and gating on the one nobody reads let a band through
    // while the reported figure said something else.
    //
    // Gate on the number you ship. T30 preferred; T20 when the range does not
    // reach 35 dB, because a curve that cannot answer T30 can still be too
    // narrow to trust and must still be refusable.
    const auto t30 = fitOne(out, -5.0, -35.0);
    const auto t20 = fitOne(out, -5.0, -25.0);
    if (t30.has()) {
        out.lateDecaySec = t30.seconds;
    } else if (t20.has()) {
        out.lateDecaySec = t20.seconds;
    }

    if (bandwidthHz > 0.0 && out.lateDecaySec > 0.0 &&
        bandwidthHz * out.lateDecaySec < config.minBandwidthTime) {
        out.refusal = DecayRefusal::BandwidthTimeTooSmall;
        out.db.clear();
    }
    return out;
}

DecayTimes decayTimes(const EnergyDecayCurve& curve) {
    DecayTimes out;
    out.edt = fitOne(curve, 0.0, -10.0);
    out.t20 = fitOne(curve, -5.0, -25.0);
    out.t30 = fitOne(curve, -5.0, -35.0);
    return out;
}

Clarity clarity(std::span<const float> bandResponse, std::size_t originIndex,
                double sampleRate, const EnergyDecayCurve& curve) {
    Clarity out;
    const auto refuseAll = [&out](DecayRefusal why) {
        out.c50Db.refusal = why;
        out.c80Db.refusal = why;
        out.d50.refusal = why;
    };

    if (!(sampleRate > 0.0) || bandResponse.empty() ||
        originIndex >= bandResponse.size()) {
        return out;
    }
    if (!curve.has()) {
        // Without a crossing there is no honest end to the late integral, and
        // an integral that runs to the end of the buffer makes C50 a function
        // of how long the recorder ran. Inherit the curve's reason rather than
        // inventing one -- the caller is owed the same diagnosis.
        refuseAll(curve.refusal);
        return out;
    }

    // Both integrals stop at the crossing. Everything past it is noise, and it
    // would all land in the LATE half, so leaving it in biases clarity in one
    // direction only.
    const auto tail = bandResponse.subspan(originIndex);
    const std::size_t cut = std::min(curve.crossingIndex, tail.size());
    const auto at50 = static_cast<std::size_t>(0.050 * sampleRate);
    const auto at80 = static_cast<std::size_t>(0.080 * sampleRate);

    // Refuse rather than treat a missing late half as silence: a capture that
    // ends at 60 ms would otherwise report enormous clarity, which is the
    // reading an operator is least equipped to disbelieve.
    if (cut <= at50) {
        refuseAll(DecayRefusal::RangeTooSmall);
        return out;
    }

    // Energy that zero-phase filtering placed BEFORE the arrival counts as
    // EARLY, and this is measured rather than assumed.
    //
    // `filtfilt` is symmetric about an impulse, so it puts half the direct
    // sound's band energy before t = 0 at every band. An earlier version
    // discarded it as filter leakage; that reading was backwards. It is the
    // direct sound's own energy, displaced by a filter that conserves it, and
    // dropping it is what creates a filter-dependent loss.
    //
    // Both conventions were measured against the C50 of the UNFILTERED impulse
    // response, which has no smearing to argue about. Median of 8 seeds, error
    // in dB against that truth:
    //
    //                     exclude   include
    //     1/3-oct 40 Hz    -3.06     -0.33
    //     1/3-oct 63 Hz    -3.46     -2.98
    //     1/3-oct 125 Hz   +0.62     +1.00
    //     octave 1000 Hz   -0.14     -0.14
    //
    // Include wins decisively where the difference is large, ties where the
    // band is wide, and loses by 0.4 dB in one middle cell. The residual the
    // two share at 125 Hz is band-limiting itself changing C50 -- a property of
    // the measurement, not of this choice.
    double early50 = 0.0, early80 = 0.0, total = 0.0;
    for (std::size_t i = 0; i < originIndex; ++i) {
        const double v = static_cast<double>(bandResponse[i]);
        const double e = v * v;
        total += e;
        early50 += e;
        early80 += e;
    }
    for (std::size_t i = 0; i < cut; ++i) {
        const double e = static_cast<double>(tail[i]) * static_cast<double>(tail[i]);
        total += e;
        if (i < at50) early50 += e;
        if (i < at80) early80 += e;
    }
    if (!(total > 0.0)) {
        refuseAll(DecayRefusal::NoDecayFound);
        return out;
    }

    // The energy Lundeby's correction accounts for lies entirely past the
    // crossing, hence entirely in the late half of every ratio. Adding it to
    // the total and not to the early terms is what puts it there.
    total += curve.truncationCorrection;

    // Each figure stands or falls on its own late half. A response usable to
    // 70 ms answers C50 and cannot answer C80, and the 50 ms answer is not made
    // worse by the 80 ms one being unavailable.
    const double late50 = total - early50;
    if (late50 > 0.0) {
        out.c50Db.value = powerToDb(early50 / late50);
        out.c50Db.refusal = DecayRefusal::None;
        out.d50.value = early50 / total;
        out.d50.refusal = DecayRefusal::None;
    } else {
        out.c50Db.refusal = DecayRefusal::RangeTooSmall;
        out.d50.refusal = DecayRefusal::RangeTooSmall;
    }

    const double late80 = total - early80;
    if (cut > at80 && late80 > 0.0) {
        out.c80Db.value = powerToDb(early80 / late80);
        out.c80Db.refusal = DecayRefusal::None;
    } else {
        out.c80Db.refusal = DecayRefusal::RangeTooSmall;
    }
    return out;
}

}  // namespace rta::ir
