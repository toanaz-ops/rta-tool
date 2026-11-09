// SPDX-License-Identifier: AGPL-3.0-or-later
// H9 / L4c task A2: IrGateModel, the IR pane's numbers and words with no window.
// The gate maths is proven in core/tests/test_ir_gate.cpp; this file proves what
// the app does WITH it: the readout, the ETC, the stored trace.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "measure/ExcessPhaseJob.h"
#include "measure/IrGateModel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <memory>
#include <span>
#include <string>
#include <thread>

using Catch::Approx;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using rta::measure::IrGateMarkers;
using rta::measure::SweepIr;

namespace {

constexpr double kFs = 48000.0;
constexpr std::size_t kOrigin = 1000;
constexpr long kArrival = 240;  // 5 ms after t = 0

/// A direct delta at origin + arrival, and an echo `echoSamples` later.
SweepIr spikes(double echoGain = 0.0, long echoSamples = 0) {
    SweepIr ir;
    ir.samples.assign(6000, 0.0f);
    ir.originIndex = kOrigin;
    ir.arrivalSamples = kArrival;
    ir.sampleRate = kFs;
    ir.samples[kOrigin + static_cast<std::size_t>(kArrival)] = 1.0f;
    if (echoGain != 0.0) ir.samples[kOrigin + static_cast<std::size_t>(kArrival + echoSamples)] = static_cast<float>(echoGain);
    return ir;
}

}  // namespace

TEST_CASE("The default gate reads out its markers and its lowest valid frequency", "[ir][gate][model]") {
    const auto analysis = rta::measure::analyseIrGate(spikes(), IrGateMarkers{});
    REQUIRE(analysis.ok);
    // 1 / (11 ms - 1.375 ms) = 103.9 Hz, printed rounded UP to whole hertz.
    CHECK(analysis.readout == "GATE -1.0..10.0 ms \xc2\xb7 valid above 104 Hz");
    CHECK(analysis.windowText == "gate -1.0..10.0 ms, valid above 104 Hz");
    CHECK(analysis.fMinHz == Approx(1.0 / (0.011 - 0.5 * 0.00275)).epsilon(1e-9));
    CHECK(analysis.bins.size() == analysis.transformSize / 2 + 1);
    CHECK(analysis.transformSize == 16384);
    // A lone delta gated whole is flat 0 dB.
    CHECK(std::abs(analysis.bins[200]) == Approx(1.0).margin(1e-5));
}

TEST_CASE("A refused gate says so, with the number, and has no spectrum", "[ir][gate][model]") {
    const auto analysis = rta::measure::analyseIrGate(spikes(), IrGateMarkers{-1.0, 500.0});
    CHECK_FALSE(analysis.ok);
    CHECK(analysis.bins.empty());
    CHECK_THAT(analysis.readout, StartsWith("GATE refused -- "));
    CHECK_THAT(analysis.readout, ContainsSubstring("500.0"));
    CHECK_THAT(analysis.refusal, ContainsSubstring("500.0"));
    CHECK_FALSE(rta::measure::gatedTrace(spikes(), analysis, "capture-test", "dev", "roles").has_value());
}

TEST_CASE("The ETC is 0 dB at the arrival and silent where the IR is", "[ir][gate][model]") {
    const auto etc = rta::measure::buildEtc(spikes(0.5, 480));  // echo at +10 ms
    REQUIRE_FALSE(etc.db.empty());
    CHECK(etc.startMs == Approx(-5.0).margin(0.03));
    CHECK(etc.endMs() == Approx(50.0).margin(0.03));
    const auto at = [&](double ms) {
        return static_cast<std::size_t>(std::llround((ms - etc.startMs) / etc.stepMs));
    };
    CHECK(etc.db[at(0.0)] == Approx(0.0).margin(1e-4));
    CHECK(etc.db[at(10.0)] == Approx(20.0 * std::log10(0.5)).margin(1e-3));  // -6.02 dB
    CHECK(etc.db[at(3.0)] <= -100.0f);
    // The peak-hold spreads a spike by half its window either side and no further.
    CHECK(etc.db[at(0.0 + 0.1)] == Approx(0.0).margin(1e-4));
    CHECK(etc.db[at(0.0 + 0.3)] <= -100.0f);
}

TEST_CASE("The stored trace is the gated response, labelled as such", "[ir][gate][model]") {
    const auto ir = spikes();
    const auto analysis = rta::measure::analyseIrGate(ir, IrGateMarkers{});
    const auto trace = rta::measure::gatedTrace(ir, analysis, "capture-test", "Interface X", "ch0=Measurement");
    REQUIRE(trace.has_value());
    const auto& meta = trace->meta();
    CHECK(meta.id == "capture-test");
    CHECK(meta.window == "gate -1.0..10.0 ms, valid above 104 Hz");
    CHECK(meta.fftSize == 16384);
    CHECK(meta.sampleRate == Approx(kFs));
    CHECK(meta.deviceName == "Interface X");
    CHECK(meta.channelRoles == "ch0=Measurement");
    // The arrival has been taken out of the phase, so XOVER must be told.
    CHECK(meta.appliedDelaySamples == kArrival);
    CHECK(trace->pointCount() == 8193);
    CHECK(trace->has(rta::trace::Field::Phase));
    CHECK_FALSE(trace->has(rta::trace::Field::Coherence));  // never a made-up one
    CHECK(trace->field(rta::trace::Field::Magnitude)[300] == Approx(0.0).margin(1e-4));
    CHECK(std::abs(trace->field(rta::trace::Field::Phase)[300]) <= 1e-4);
}

TEST_CASE("Valid-above text rounds up to whole hertz", "[ir][gate][model]") {
    CHECK(rta::measure::validAboveText(103.896) == "valid above 104 Hz");
    CHECK(rta::measure::validAboveText(104.0) == "valid above 104 Hz");
    CHECK(rta::measure::validAboveText(104.2) == "valid above 105 Hz");
}

// ---- H10: the G24 min / excess-phase decomposition of the gated response --------
//
// Every fixture is a response built on the gate's own 16384-point grid
// (8193 bins, bin k at k*fs/16384) and trusted over the WHOLE band: fMin = 0,
// trustedHigh = Nyquist. No fill and no band edge, so the only departures from
// the closed form are the kernel's own (log-frequency interpolation between
// bins, the float32 spectrum) and not an edge effect that would need a looser
// bound than the closed form deserves.

namespace {

constexpr std::size_t kGridBins = 8193;
constexpr std::size_t kGridN = 16384;
constexpr double kPi = 3.14159265358979323846;

/// H_k = (1 + a*e^{-j w_k d}) * e^{-j w_k extraDelay}.
std::vector<std::complex<float>> combBins(double a, double d, double extraDelay = 0.0) {
    std::vector<std::complex<float>> bins(kGridBins);
    for (std::size_t k = 0; k < kGridBins; ++k) {
        const double theta = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(kGridN);
        const std::complex<double> h = (1.0 + a * std::polar(1.0, -theta * d)) * std::polar(1.0, -theta * extraDelay);
        bins[k] = {static_cast<float>(h.real()), static_cast<float>(h.imag())};
    }
    return bins;
}

double wrapDeg(double deg) {
    while (deg > 180.0) deg -= 360.0;
    while (deg <= -180.0) deg += 360.0;
    return deg;
}

double maxAbsWrappedDiff(const std::vector<float>& got, const std::vector<double>& want) {
    double worst = 0.0;
    for (std::size_t k = 0; k < got.size(); ++k) worst = std::max(worst, std::abs(wrapDeg(got[k] - want[k])));
    return worst;
}

}  // namespace

TEST_CASE("A pure delay has no excess phase once its delay is removed", "[ir][gate][model][excess]") {
    constexpr double kDelay = 96.0;  // samples, 2 ms at 48 kHz
    const auto r = rta::measure::excessPhaseFor(combBins(0.0, 0.0, kDelay), kFs, 0.0, kFs / 2.0);
    REQUIRE(r.valid);
    // tau0 is the mean group delay of H/H_min = H (flat |H|, so H_min = 1). The kernel's
    // group delay is a one-bin central difference of H (ExcessPhase.cpp, smoothing = 1),
    // whose answer on a pure delay is exactly N*sin(2*pi*D/N)/(2*pi*fs) -- NOT D/fs: the
    // sine's relative shortfall 2.3e-4 is 0.022 sample at D = 96. The same value comes
    // from every bin (edge bins use a one-sided difference of the same form), so the
    // mean is that closed form. Bound: the float32 spectrum rounds each phase by
    // <= 6e-8 rad, moving a one-bin difference by <= 2*6e-8/(2*pi*2.93 Hz) = 6.5e-9 s;
    // 2e-8 s leaves 3x for the mean.
    const double tauExact = static_cast<double>(kGridN) * std::sin(2.0 * kPi * kDelay / static_cast<double>(kGridN)) /
                            (2.0 * kPi * kFs);
    CHECK(r.broadbandDelayMs / 1000.0 == Approx(tauExact).margin(2.0e-8));
    // Then phi_x = arg(H e^{+jw tau0}) = w*(tau0 - D/fs), a straight line reaching 3.9 deg
    // at Nyquist: it IS the stencil's bias, not noise, and the kernel returns exactly it.
    // Float error: 6.5e-9 s * 1.5e5 rad/s = 1e-3 rad = 0.056 deg. Bound 0.1 deg.
    std::vector<double> want(kGridBins);
    for (std::size_t k = 0; k < kGridBins; ++k) {
        const double w = 2.0 * kPi * static_cast<double>(k) * kFs / static_cast<double>(kGridN);
        want[k] = w * (tauExact - kDelay / kFs) * 180.0 / kPi;
    }
    CHECK(maxAbsWrappedDiff(r.excessDeg, want) < 0.1);
    // The displayed group delay is tau_x - tau0, on the same reference as the phase. tau_x of a
    // pure delay is tauExact in every interior bin and tau0 is its mean, so the difference is 0
    // (float32 bound as for tau0: 2e-8 s = 2e-5 ms).
    CHECK(r.excessGroupDelayMs[4000] == Approx(0.0).margin(2.0e-5));
    // The measured phase is unwrapped from DC, so it is the straight line -360*k*D/N degrees
    // (-16875 deg at k = 8000), not a sawtooth. Float32 bins: 6e-8 rad each, so 0.1 deg.
    for (const std::size_t k : {std::size_t{100}, std::size_t{3000}, std::size_t{8000}}) {
        CHECK(r.measuredDeg[k] == Approx(-360.0 * static_cast<double>(k) * kDelay / static_cast<double>(kGridN)).margin(0.1));
    }
    // The minimum-phase part of a flat magnitude is zero phase.
    for (const float v : r.minimumDeg) REQUIRE(std::abs(v) < 0.1f);
    CHECK(r.tau0Text() == "tau0 2.00 ms");
}

TEST_CASE("A direct sound plus a weaker echo is minimum phase", "[ir][gate][model][excess]") {
    // 1 + 0.5 e^{-jwd}: its zero sits at |z| = 0.5 < 1, inside the unit circle, so
    // H already IS its own minimum-phase version and the excess phase is 0 (and
    // tau0 = 0: a minimum-phase response owns no pure delay).
    const auto r = rta::measure::excessPhaseFor(combBins(0.5, 48.0), kFs, 0.0, kFs / 2.0);
    REQUIRE(r.valid);
    CHECK(std::abs(r.broadbandDelayMs) < 1.0e-3);  // < 0.05 sample
    CHECK(maxAbsWrappedDiff(r.excessDeg, std::vector<double>(kGridBins, 0.0)) < 0.5);
    // Its measured phase IS the minimum phase.
    double worst = 0.0;
    for (std::size_t k = 0; k < kGridBins; ++k) {
        worst = std::max<double>(worst, std::abs(r.measuredDeg[k] - r.minimumDeg[k]));
    }
    CHECK(worst < 0.5);
}

TEST_CASE("An echo stronger than the direct sound leaves the closed-form excess phase", "[ir][gate][model][excess]") {
    // H = 1 + a e^{-jwd}, a = 2 > 1: the zero is OUTSIDE the unit circle. With
    // |H| = a|1 + e^{-jwd}/a| the minimum-phase partner is H_min = a + e^{-jwd}, and
    //   H/H_min = e^{-jwd} (1 + e^{jwd}/a) / (1 + e^{-jwd}/a)
    // an all-pass whose phase is -wd + 2*atan2(sin(wd)/a, 1 + cos(wd)/a). The second
    // term has winding number 0, so its group delay averages to 0 over whole periods
    // and tau0 ~ d/fs. d = 48 gives 16384/48 = 341.33 bins per period, 24 periods
    // over bins 0..8191, so the mean over k is exactly 0 up to the one extra bin
    // k = 8192: tau0 - d/fs = gd_ap(0)/8193 = -d*2/(a+1)/fs/8193 = -8.1e-8 s, and
    // the kernel's phi_x carries w*(tau0 - d/fs) on top of the all-pass term.
    constexpr double kA = 2.0;
    constexpr double kD = 48.0;
    const auto r = rta::measure::excessPhaseFor(combBins(kA, kD), kFs, 0.0, kFs / 2.0);
    REQUIRE(r.valid);
    const double tau0 = r.broadbandDelayMs / 1000.0;
    const double tauResidual = tau0 - kD / kFs;
    // The 8.1e-8 s edge term, plus the one-bin central difference's own shortfall:
    // each bin reports -sin(delta)/dw rather than -delta/dw, off by <= delta^3/6/dw
    // with |delta| <= theta1*(1 + 2/(a-1)) = 0.055 rad (theta1 = 2*pi*d/16384), i.e.
    // 1.5e-6 s at worst. Bound 2e-6 s.
    CHECK(std::abs(tauResidual) < 2.0e-6);

    std::vector<double> want(kGridBins);
    for (std::size_t k = 0; k < kGridBins; ++k) {
        const double theta = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(kGridN);
        const double allpass = 2.0 * std::atan2(std::sin(theta * kD) / kA, 1.0 + std::cos(theta * kD) / kA);
        const double w = 2.0 * kPi * static_cast<double>(k) * kFs / static_cast<double>(kGridN);
        want[k] = (allpass + w * tauResidual) * 180.0 / kPi;
    }
    // Bound: the kernel's log-frequency interpolation of ln|H| between bins
    // (curvature a*dtheta^2/8 with dtheta = 2*pi*d/16384 = 0.0184 rad, i.e. 2e-5
    // of ln|H| per bin) and float32 rounding give well under 0.5 deg.
    CHECK(maxAbsWrappedDiff(r.excessDeg, want) < 0.5);
    // The implementation's own swing equals the closed form's (and the closed form's is large,
    // so this cannot pass on a flat curve): peaks agree within the same 0.5 deg bound.
    double swingWant = 0.0, swingGot = 0.0;
    for (std::size_t k = 0; k < kGridBins; ++k) {
        swingWant = std::max(swingWant, std::abs(want[k]));
        swingGot = std::max<double>(swingGot, std::abs(r.excessDeg[k]));
    }
    CHECK(swingGot == Approx(swingWant).margin(0.5));
    CHECK(swingGot > 60.0);
    // measured = minimum + excess - w*tau0 (the kernel's own identity), unwrapped.
    for (const std::size_t k : {std::size_t{100}, std::size_t{3000}, std::size_t{8000}}) {
        const double w = 2.0 * kPi * static_cast<double>(k) * kFs / static_cast<double>(kGridN);
        const double lhs = wrapDeg(r.measuredDeg[k] - r.minimumDeg[k] - r.excessDeg[k] + w * tau0 * 180.0 / kPi);
        CHECK(std::abs(lhs) < 0.01);
    }
}

TEST_CASE("Too few trusted bins is a refusal with a sentence, never an empty curve", "[ir][gate][model][excess]") {
    const auto bins = combBins(0.5, 48.0);
    // Lower edge above the upper edge: nothing is trusted.
    const auto none = rta::measure::excessPhaseFor(bins, kFs, 5000.0, 100.0);
    CHECK_FALSE(none.valid);
    CHECK(none.refusal == "too few trusted bins for excess phase");
    CHECK(none.excessDeg.empty());
    CHECK(none.tau0Text().empty());
    // The band edges are inclusive and honoured: bins sit 2.93 Hz apart, so [5000, 5002] holds
    // ONE bin (5001.7 Hz, k = 1707; k = 1706 is 4998.8) and the kernel's two-bin minimum
    // refuses it, while [4990, 5002] holds two. The same at the top edge: k = 35 is 102.5 Hz.
    CHECK_FALSE(rta::measure::excessPhaseFor(bins, kFs, 5000.0, 5002.0).valid);
    CHECK(rta::measure::excessPhaseFor(bins, kFs, 4990.0, 5002.0).valid);
    CHECK_FALSE(rta::measure::excessPhaseFor(bins, kFs, 100.0, 103.0).valid);
    CHECK(rta::measure::excessPhaseFor(bins, kFs, 99.0, 103.0).valid);
    // A hand-built IR that states no trusted ceiling (0) trusts nothing: no silent Nyquist.
    CHECK_FALSE(rta::measure::excessPhaseFor(bins, kFs, 100.0, 0.0).valid);
    // A size that is not 2^k + 1 is the kernel's contract broken: refused, not thrown, and the
    // sentence names that, not a trust problem. The same for a non-positive sample rate.
    const auto badSize = rta::measure::excessPhaseFor(std::span(bins).first(1000), kFs, 0.0, kFs / 2.0);
    CHECK_FALSE(badSize.valid);
    CHECK_THAT(badSize.refusal, ContainsSubstring("2^k + 1"));
    CHECK_FALSE(rta::measure::excessPhaseFor(bins, 0.0, 0.0, kFs / 2.0).valid);
    CHECK_THAT(rta::measure::excessPhaseFor(bins, 0.0, 0.0, 1.0).refusal, ContainsSubstring("sample rate"));
}

// ---- H10: the worker's newest-wins rule --------------------------------------------

namespace {

// A tiny valid grid (33 bins, 2^5 + 1): the kernel is quick on it, so the test is about
// WHICH submission answers, not how long one takes.
rta::measure::ExcessPhaseJob::Input tinyJob(double trustedHighHz) {
    rta::measure::ExcessPhaseJob::Input in;
    in.bins.assign(33, std::complex<float>(1.0f, 0.0f));
    in.sampleRate = kFs;
    in.fMinHz = 0.0;
    in.trustedHighHz = trustedHighHz;
    return in;
}

std::shared_ptr<const rta::measure::IrExcessPhase> pollUntil(rta::measure::ExcessPhaseJob& job, int millis) {
    for (int waited = 0; waited < millis; waited += 5) {
        if (auto r = job.poll()) return r;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return nullptr;
}

}  // namespace

TEST_CASE("The worker answers the newest submission and never a superseded one", "[ir][gate][model][excess][job]") {
    rta::measure::ExcessPhaseJob job;
    // A trusts everything (valid); B trusts nothing (refused). Submitted back to back, so
    // B queues behind A: only B's answer may ever come out.
    job.submit(tinyJob(kFs / 2.0));
    job.submit(tinyJob(0.0));
    const auto answer = pollUntil(job, 10000);
    REQUIRE(answer != nullptr);
    CHECK_FALSE(answer->valid);
    CHECK(job.startedCountForTest() == 2);
    CHECK(pollUntil(job, 200) == nullptr);  // and nothing further arrives

    // invalidate() makes the running job's answer stale: it never surfaces.
    job.submit(tinyJob(kFs / 2.0));
    job.invalidate();
    CHECK(pollUntil(job, 500) == nullptr);
    // ... and the worker is still usable afterwards.
    job.submit(tinyJob(kFs / 2.0));
    const auto again = pollUntil(job, 10000);
    REQUIRE(again != nullptr);
    CHECK(again->valid);
}
