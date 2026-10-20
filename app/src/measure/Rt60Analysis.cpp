// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/Rt60Analysis.h"

#include "rta/ir/Decay.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <span>

namespace rta::measure {

namespace {

// The correspondence Rt60Analysis.h promises, checked where it can be: a
// reordered or extended DecayRefusal fails the build here, not at a customer.
using rta::ir::DecayRefusal;
static_assert(static_cast<int>(Rt60Refusal::None) == static_cast<int>(DecayRefusal::None));
static_assert(static_cast<int>(Rt60Refusal::BandwidthTimeTooSmall) ==
              static_cast<int>(DecayRefusal::BandwidthTimeTooSmall));
static_assert(static_cast<int>(Rt60Refusal::NoDecayFound) == static_cast<int>(DecayRefusal::NoDecayFound));
static_assert(static_cast<int>(Rt60Refusal::RangeTooSmall) == static_cast<int>(DecayRefusal::RangeTooSmall));
static_assert(static_cast<int>(Rt60Refusal::NoNoiseEstimate) == static_cast<int>(DecayRefusal::NoNoiseEstimate));
static_assert(static_cast<int>(Rt60Refusal::NoLeadIn) == static_cast<int>(DecayRefusal::NoLeadIn));

// Upper bound on the lead-in. An octave band at 63 Hz rings for a few
// milliseconds; 200 ms is two orders of margin and is what the plan names.
constexpr double kMaxLeadInSeconds = 0.2;

Rt60Refusal mirrored(DecayRefusal why) { return static_cast<Rt60Refusal>(static_cast<int>(why)); }

Rt60Figure figureOf(const rta::ir::DecayTime& time) { return {time.seconds, mirrored(time.refusal)}; }

Rt60Result refusedWhole(Rt60Refusal why) {
    Rt60Result out;
    out.refusal = why;
    return out;
}

// Band edges inside what the sweep established. Zero means "not stated", and
// then nothing is clamped (SweepIr's own contract) rather than a full band
// being assumed.
bool insideTrustedBand(const Rt60BandEdges& band, double trustedLowHz, double trustedHighHz) {
    if (trustedLowHz > 0.0 && band.lowHz < trustedLowHz) return false;
    if (trustedHighHz > 0.0 && band.highHz > trustedHighHz) return false;
    return true;
}

Rt60Band analyseBand(const Rt60BandEdges& edges, std::span<const float> window, std::size_t leadIn,
                     double fs, double trustedLowHz, double trustedHighHz) {
    Rt60Band band;
    band.nominalHz = edges.nominalHz;
    band.lowHz = edges.lowHz;
    band.highHz = edges.highHz;

    if (!insideTrustedBand(edges, trustedLowHz, trustedHighHz)) {
        band.refusal = Rt60Refusal::OutsideTrustedBand;
        band.t30.refusal = band.t20.refusal = band.edt.refusal = Rt60Refusal::OutsideTrustedBand;
        return band;
    }

    const auto filtered = rta::ir::bandFilterZeroPhase(window, edges.lowHz, edges.highHz, fs);
    const auto curve =
        rta::ir::energyDecayCurve(filtered, leadIn, fs, edges.highHz - edges.lowHz);
    const auto times = rta::ir::decayTimes(curve);

    band.refusal = mirrored(curve.refusal);
    band.t30 = figureOf(times.t30);
    band.t20 = figureOf(times.t20);
    band.edt = figureOf(times.edt);
    if (curve.has() && !curve.db.empty()) {
        band.hasRange = true;
        band.rangeDb = -static_cast<double>(curve.db.back());
    }
    band.bandwidthTime = (edges.highHz - edges.lowHz) * curve.lateDecaySec;
    band.crossingSec = static_cast<double>(curve.crossingIndex) / fs;
    return band;
}

}  // namespace

std::size_t rt60LeadInSamples(double sampleRate, double harmonicSpacingL) {
    const double capped = kMaxLeadInSeconds * sampleRate;
    if (!(harmonicSpacingL > 0.0)) return static_cast<std::size_t>(std::llround(capped));
    // 0.5 * L * ln 2: half the distance to the 2nd harmonic packet.
    const double packetBound = 0.5 * harmonicSpacingL * std::log(2.0) * sampleRate;
    if (packetBound >= capped) return static_cast<std::size_t>(std::llround(capped));
    return static_cast<std::size_t>(std::floor(packetBound));
}

Rt60Result analyseRt60(const SweepIr* ir) {
    const auto started = std::chrono::steady_clock::now();
    if (ir == nullptr || ir->samples.empty() || !(ir->sampleRate > 0.0)) return refusedWhole(Rt60Refusal::NoIr);
    if (ir->tailSamples == 0) return refusedWhole(Rt60Refusal::NoTail);

    Rt60Result out;
    out.sampleRate = ir->sampleRate;
    out.trustedLowHz = ir->trustedLowHz;
    out.trustedHighHz = ir->trustedHighHz;
    out.arrivalSamples = ir->arrivalSamples;
    out.tailSeconds = static_cast<double>(ir->tailSamples) / ir->sampleRate;
    out.leadInSamples = rt60LeadInSamples(ir->sampleRate, ir->harmonicSpacingL);

    // The window: lead-in before the arrival, then everything the capture
    // really measured, and not one sample of the artefact beyond it (plan F1).
    const long arrival = static_cast<long>(ir->originIndex) + ir->arrivalSamples;
    if (arrival < static_cast<long>(out.leadInSamples)) {
        out.refusal = Rt60Refusal::NoLeadIn;
        return out;
    }
    const std::size_t arrivalIndex = static_cast<std::size_t>(arrival);
    const std::size_t start = arrivalIndex - out.leadInSamples;
    const std::size_t end = std::min(ir->samples.size(), ir->originIndex + ir->tailSamples);
    if (end <= arrivalIndex) {
        out.refusal = Rt60Refusal::NoTail;
        return out;
    }
    out.analysedSamples = end - start;

    try {
        const std::span<const float> window(ir->samples.data() + start, end - start);
        for (const auto& edges : rt60OctaveBands()) {
            out.bands.push_back(analyseBand(edges, window, out.leadInSamples, ir->sampleRate,
                                            ir->trustedLowHz, ir->trustedHighHz));
        }
    } catch (const std::exception& e) {
        out.bands.clear();
        out.refusal = Rt60Refusal::NoDecayFound;
        out.message = e.what();
    }
    out.wallMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return out;
}

}  // namespace rta::measure
