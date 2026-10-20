// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/Rt60Analysis.h"

#include "rta/ir/Decay.h"
#include "rta/ir/DecayEnsemble.h"

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

// The plotted curve keeps at most this many points: a pane a few hundred pixels
// wide cannot show more, and a third-octave table carries 24 of them.
constexpr std::size_t kMaxEdcPoints = 1024;

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

// The span core fitted: the same two first-crossings `fitOne` (Decay.cpp) finds,
// the same samples. The line is rebuilt from T (see Rt60FitSpan).
Rt60FitSpan fitSpanOf(const rta::ir::EnergyDecayCurve& curve, const rta::ir::DecayTime& time, double upperDb,
                      double lowerDb) {
    Rt60FitSpan span;
    span.upperDb = upperDb;
    span.lowerDb = lowerDb;
    if (!time.has()) return span;
    const auto crossing = [&curve](double level) {
        for (std::size_t i = 0; i < curve.db.size(); ++i) {
            if (static_cast<double>(curve.db[i]) <= level) return i;
        }
        return curve.db.size();
    };
    const std::size_t i0 = crossing(upperDb);
    const std::size_t i1 = crossing(lowerDb);
    if (i1 <= i0) return span;
    double meanX = 0.0, meanY = 0.0;
    for (std::size_t i = i0; i < i1; ++i) {
        meanX += static_cast<double>(i) / curve.sampleRate;
        meanY += static_cast<double>(curve.db[i]);
    }
    const double n = static_cast<double>(i1 - i0);
    meanX /= n;
    meanY /= n;
    span.slopeDbPerSec = -60.0 / time.seconds;
    span.interceptDb = meanY - span.slopeDbPerSec * meanX;
    for (std::size_t i = i0; i < i1; ++i) {
        const double x = static_cast<double>(i) / curve.sampleRate;
        const double line = span.interceptDb + span.slopeDbPerSec * x;
        span.maxResidualDb = std::max(span.maxResidualDb, std::fabs(static_cast<double>(curve.db[i]) - line));
    }
    span.startSec = static_cast<double>(i0) / curve.sampleRate;
    span.endSec = static_cast<double>(i1) / curve.sampleRate;
    span.valid = true;
    return span;
}

Rt60Edc edcOf(const rta::ir::EnergyDecayCurve& curve, const rta::ir::DecayTimes& times) {
    Rt60Edc edc;
    if (!curve.has() || curve.db.empty()) return edc;
    const std::size_t stride = std::max<std::size_t>(1, (curve.db.size() + kMaxEdcPoints - 1) / kMaxEdcPoints);
    for (std::size_t i = 0; i < curve.db.size(); i += stride) edc.db.push_back(curve.db[i]);
    edc.stepSeconds = static_cast<double>(stride) / curve.sampleRate;
    edc.t30 = fitSpanOf(curve, times.t30, -5.0, -35.0);
    edc.t20 = fitSpanOf(curve, times.t20, -5.0, -25.0);
    edc.edt = fitSpanOf(curve, times.edt, 0.0, -10.0);
    return edc;
}

// Filter, integrate, truncate: one band of one window. Both the single-IR table
// and the ensemble go through here, so the two cannot disagree about a band.
rta::ir::EnergyDecayCurve curveOfBand(const Rt60BandEdges& edges, std::span<const float> window, std::size_t leadIn,
                                      double fs) {
    const auto filtered = rta::ir::bandFilterZeroPhase(window, edges.lowHz, edges.highHz, fs);
    return rta::ir::energyDecayCurve(filtered, leadIn, fs, edges.highHz - edges.lowHz);
}

Rt60Band analyseBand(const Rt60BandEdges& edges, std::span<const float> window, std::size_t leadIn, double fs,
                     double trustedLowHz, double trustedHighHz) {
    Rt60Band band;
    band.nominalHz = edges.nominalHz;
    band.lowHz = edges.lowHz;
    band.highHz = edges.highHz;

    if (!insideTrustedBand(edges, trustedLowHz, trustedHighHz)) {
        band.refusal = Rt60Refusal::OutsideTrustedBand;
        band.t30.refusal = band.t20.refusal = band.edt.refusal = Rt60Refusal::OutsideTrustedBand;
        return band;
    }

    const auto curve = curveOfBand(edges, window, leadIn, fs);
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
    band.edc = edcOf(curve, times);
    return band;
}

// The window of one IR (plan F1, F2): `out`'s capture fields are filled, and on
// false `out.refusal` says why the whole analysis is withheld.
bool cropWindow(const SweepIr& ir, Rt60Result& out, std::span<const float>& window) {
    out.sampleRate = ir.sampleRate;
    out.trustedLowHz = ir.trustedLowHz;
    out.trustedHighHz = ir.trustedHighHz;
    out.arrivalSamples = ir.arrivalSamples;
    out.tailSeconds = static_cast<double>(ir.tailSamples) / ir.sampleRate;
    out.leadInSamples = rt60LeadInSamples(ir.sampleRate, ir.harmonicSpacingL);

    // The window: lead-in before the arrival, then everything the capture
    // really measured, and not one sample of the artefact beyond it (plan F1).
    const long arrival = static_cast<long>(ir.originIndex) + ir.arrivalSamples;
    if (arrival < static_cast<long>(out.leadInSamples)) {
        out.refusal = Rt60Refusal::NoLeadIn;
        return false;
    }
    const std::size_t arrivalIndex = static_cast<std::size_t>(arrival);
    const std::size_t start = arrivalIndex - out.leadInSamples;
    const std::size_t end = std::min(ir.samples.size(), ir.originIndex + ir.tailSamples);
    if (end <= arrivalIndex) {
        out.refusal = Rt60Refusal::NoTail;
        return false;
    }
    out.analysedSamples = end - start;
    window = std::span<const float>(ir.samples.data() + start, end - start);
    return true;
}

bool unusable(const SweepIr* ir) { return ir == nullptr || ir->samples.empty() || !(ir->sampleRate > 0.0); }

double msSince(std::chrono::steady_clock::time_point started) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}

bool cancelled(const std::atomic<bool>* cancel) { return cancel != nullptr && cancel->load(); }

Rt60Spread spreadOf(const rta::ir::DecaySpread& s) {
    Rt60Spread out;
    out.seconds = s.value.seconds;
    out.refusal = mirrored(s.value.refusal);
    out.spreadPercent = s.spreadIsMeaningful() ? s.spreadPercent : 0.0;
    out.captures = s.captures;
    return out;
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

Rt60Result analyseRt60Bands(const SweepIr* ir, std::span<const Rt60BandEdges> bands, Rt60BandSet set,
                            const std::atomic<bool>* cancel) {
    const auto started = std::chrono::steady_clock::now();
    if (unusable(ir)) return refusedWhole(Rt60Refusal::NoIr);
    if (ir->tailSamples == 0) return refusedWhole(Rt60Refusal::NoTail);

    Rt60Result out;
    out.bandSet = set;
    std::span<const float> window;
    if (!cropWindow(*ir, out, window)) return out;

    try {
        for (const auto& edges : bands) {
            if (cancelled(cancel)) break;
            out.bands.push_back(analyseBand(edges, window, out.leadInSamples, ir->sampleRate, ir->trustedLowHz,
                                            ir->trustedHighHz));
        }
    } catch (const std::exception& e) {
        out.bands.clear();
        out.refusal = Rt60Refusal::NoDecayFound;
        out.message = e.what();
    }
    out.wallMs = msSince(started);
    return out;
}

Rt60Result analyseRt60(const SweepIr* ir, Rt60BandSet set, const std::atomic<bool>* cancel) {
    const auto bands = rt60Bands(set);
    return analyseRt60Bands(ir, bands, set, cancel);
}

Rt60EnsembleResult analyseRt60Ensemble(std::span<const SweepIr* const> irs, Rt60BandSet set,
                                       const std::atomic<bool>* cancel) {
    const auto started = std::chrono::steady_clock::now();
    Rt60EnsembleResult out;
    out.bandSet = set;
    out.captureCount = irs.size();
    const auto refuse = [&out](Rt60Refusal why, const char* message = "") {
        out.refusal = why;
        out.message = message;
        return out;
    };
    if (irs.empty()) return refuse(Rt60Refusal::NoIr);

    // Crop every capture first: one unusable IR withholds the ensemble, with that
    // IR's own reason, rather than quietly averaging fewer captures than the
    // operator ticked.
    struct Prepared {
        Rt60Result head;
        std::span<const float> window;
        const SweepIr* ir = nullptr;
    };
    std::vector<Prepared> prepared;
    prepared.reserve(irs.size());
    for (const SweepIr* ir : irs) {
        if (unusable(ir)) return refuse(Rt60Refusal::NoIr);
        if (ir->tailSamples == 0) return refuse(Rt60Refusal::NoTail);
        if (ir->sampleRate != irs.front()->sampleRate) {
            return refuse(Rt60Refusal::NoIr, "the captures were taken at different sample rates");
        }
        Prepared p;
        p.ir = ir;
        if (!cropWindow(*ir, p.head, p.window)) return refuse(p.head.refusal);
        prepared.push_back(std::move(p));
    }

    out.sampleRate = irs.front()->sampleRate;
    out.tailSeconds = prepared.front().head.tailSeconds;
    for (const auto& p : prepared) {
        out.tailSeconds = std::min(out.tailSeconds, p.head.tailSeconds);
        // The tightest trusted band any capture states: a band is answered only
        // where every capture established it. Zero is "not stated" and clamps nothing.
        if (p.head.trustedLowHz > 0.0) out.trustedLowHz = std::max(out.trustedLowHz, p.head.trustedLowHz);
        if (p.head.trustedHighHz > 0.0) {
            out.trustedHighHz = out.trustedHighHz > 0.0 ? std::min(out.trustedHighHz, p.head.trustedHighHz)
                                                        : p.head.trustedHighHz;
        }
    }

    try {
        for (const auto& edges : rt60Bands(set)) {
            if (cancelled(cancel)) break;
            Rt60EnsembleBand band;
            band.nominalHz = edges.nominalHz;
            band.lowHz = edges.lowHz;
            band.highHz = edges.highHz;
            if (!insideTrustedBand(edges, out.trustedLowHz, out.trustedHighHz)) {
                band.refusal = Rt60Refusal::OutsideTrustedBand;
                band.t30.refusal = band.t20.refusal = band.edt.refusal = Rt60Refusal::OutsideTrustedBand;
                out.bands.push_back(band);
                continue;
            }
            std::vector<rta::ir::EnergyDecayCurve> curves;
            curves.reserve(prepared.size());
            bool boundByRefusal = false;
            for (const auto& p : prepared) {
                curves.push_back(curveOfBand(edges, p.window, p.head.leadInSamples, p.ir->sampleRate));
                const double bt = (edges.highHz - edges.lowHz) * curves.back().lateDecaySec;
                const bool refusedOnBt = curves.back().refusal == DecayRefusal::BandwidthTimeTooSmall;
                if (curves.size() == 1 || (refusedOnBt && !boundByRefusal)) band.bandwidthTime = bt;
                boundByRefusal = boundByRefusal || refusedOnBt;
            }
            const auto across = rta::ir::decayTimesAcross(curves);
            band.t30 = spreadOf(across.t30);
            band.t20 = spreadOf(across.t20);
            band.edt = spreadOf(across.edt);
            out.bands.push_back(band);
        }
    } catch (const std::exception& e) {
        out.bands.clear();
        return refuse(Rt60Refusal::NoDecayFound, e.what());
    }
    out.wallMs = msSince(started);
    return out;
}

}  // namespace rta::measure
