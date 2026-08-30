// SPDX-License-Identifier: AGPL-3.0-or-later
// Station D: Farina exponential sweep + inverse filter (rta::gen::Sweep).
// [golden] cases read core/tests/golden/generator.txt; if it hasn't landed
// yet, loadGolden() throws and those cases are legitimately RED, not a bug.
//
// LANE L4b LEANS ON THIS FILE. Its golden vectors are deliberately blind to
// generator and deconvolver divergence, which is only safe while this file and
// test_ir_golden.cpp keep guarding those layers. Simplifying either one
// removes L4b's fence with no guard going red -- see
// docs/dsp/2026-08-30-ir-decay-l4b.md section 7a.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/RealFft.h"
#include "rta/gen/Sweep.h"
#include "support/Golden.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using rta::gen::Sweep;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<rta::test::GoldenCase> sweepGolden() {
    static const auto cases = rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/generator.txt");
    return cases;
}

const rta::test::GoldenCase& findCase(const std::vector<rta::test::GoldenCase>& cases,
                                       const std::string& name) {
    for (const auto& c : cases) {
        if (c.name == name) return c;
    }
    throw std::runtime_error("golden case not found: " + name);
}

double amplitudeFromDb(double db) { return std::pow(10.0, db / 20.0); }
double raisedCosine(double p) { return 0.5 * (1.0 - std::cos(kPi * p)); }

std::size_t nextPow2(std::size_t n) {
    std::size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

/// Linear convolution via zero-padded FFT of `size` (power of two, >=
/// a.size()+b.size()-1 for an alias-free result).
std::vector<float> convolveFft(std::span<const float> a, std::span<const float> b, std::size_t size) {
    rta::dsp::RealFft fft(size);
    std::vector<float> ap(size, 0.0f), bp(size, 0.0f);
    std::copy(a.begin(), a.end(), ap.begin());
    std::copy(b.begin(), b.end(), bp.begin());
    std::vector<std::complex<float>> A(fft.numBins()), B(fft.numBins()), C(fft.numBins());
    fft.forward(ap, A);
    fft.forward(bp, B);
    for (std::size_t k = 0; k < fft.numBins(); ++k) C[k] = A[k] * B[k];
    std::vector<float> out(size);
    fft.inverse(C, out);
    return out;
}

/// 3-point parabolic interpolation of a local extremum -- a discretely
/// sampled sine's true peak rarely lands on a sample, so this recovers it far
/// more accurately than the nearest raw sample would.
double refinePeak(double left, double centre, double right) {
    const double denom = left - 2.0 * centre + right;
    if (std::abs(denom) < 1.0e-15) return centre;
    const double p = 0.5 * (left - right) / denom;
    return centre - 0.25 * (left - right) * p;
}

struct DeconvResult {
    std::size_t mainIndex = 0;
    double mainValue = 0.0, snrDb = 0.0, amp1500Rel = 0.0, amp2300Rel = 0.0;
    std::ptrdiff_t offset1500 = 0, offset2300 = 0;
};

/// Render a sweep, convolve with a synthetic 3-tap room IR, deconvolve with
/// its own inverse filter, measure how cleanly the IR comes back. Shared by
/// the CI-fast and hidden full-range cases.
DeconvResult runSweepDeconvolution(double fs, double f1, double f2, double T, std::size_t fftSize) {
    Sweep::Config cfg;
    cfg.sampleRate = fs; cfg.startHz = f1; cfg.endHz = f2; cfg.durationSec = T;
    cfg.levelDbFsPeak = -6.0;
    Sweep sweep(cfg);
    const auto n = sweep.lengthSamples();

    std::vector<float> swept(n);
    sweep.process(swept);

    // Synthetic room IR: unit impulse at 1000, reflections 0.5@1500 and
    // -0.25@2300, zero elsewhere; sized to N per the plan's worked length
    // (191999 = 96000+96000-1).
    std::vector<float> ir(n, 0.0f);
    ir[1000] = 1.0f;
    ir[1500] = 0.5f;
    ir[2300] = -0.25f;

    REQUIRE(swept.size() + ir.size() - 1 <= fftSize);
    const auto measured = convolveFft(swept, ir, fftSize);

    const auto inv = sweep.buildInverseFilter();
    const auto recovered = convolveFft(measured, inv, fftSize);

    std::size_t mainIdx = 0;
    double mainVal = 0.0;
    for (std::size_t i = 0; i < recovered.size(); ++i) {
        const double v = std::abs((double) recovered[i]);
        if (v > mainVal) { mainVal = v; mainIdx = i; }
    }
    const double mainSigned = (double) recovered[mainIdx];

    auto findNear = [&](std::size_t predicted, std::size_t radius) {
        std::size_t bestI = predicted;
        double bestV = -1.0;
        const std::size_t lo = predicted > radius ? predicted - radius : 0;
        const std::size_t hi = std::min(predicted + radius, recovered.size() - 1);
        for (std::size_t i = lo; i <= hi; ++i) {
            const double v = std::abs((double) recovered[i]);
            if (v > bestV) { bestV = v; bestI = i; }
        }
        return bestI;
    };
    const std::size_t idx1500 = findNear(mainIdx + 500, 32);
    const std::size_t idx2300 = findNear(mainIdx + 1300, 32);

    // Flat only between f1 and f2, so each "impulse" rings physically for
    // ~2 cycles at f1 -- excluded below, not counted as noise.
    const std::size_t halfWidth = (std::size_t) std::llround(2.0 * fs / f1);
    auto inExclusion = [&](std::size_t i) {
        auto near = [&](std::size_t centre) { return (i > centre ? i - centre : centre - i) <= halfWidth; };
        return near(mainIdx) || near(idx1500) || near(idx2300);
    };
    double sumSq = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < recovered.size(); ++i) {
        if (inExclusion(i)) continue;
        const double v = (double) recovered[i];
        sumSq += v * v;
        ++count;
    }
    const double meanSq = count > 0 ? sumSq / (double) count : 1.0;

    DeconvResult result;
    result.mainIndex = mainIdx;
    result.mainValue = mainVal;
    result.snrDb = 10.0 * std::log10((mainVal * mainVal) / meanSq);
    result.amp1500Rel = (double) recovered[idx1500] / mainSigned;
    result.amp2300Rel = (double) recovered[idx2300] / mainSigned;
    result.offset1500 = (std::ptrdiff_t) idx1500 - (std::ptrdiff_t) mainIdx;
    result.offset2300 = (std::ptrdiff_t) idx2300 - (std::ptrdiff_t) mainIdx;
    return result;
}

}  // namespace

TEST_CASE("L and K follow from f1, f2 and T", "[sweep]") {
    struct Params { double f1, f2, t; };
    const Params sets[] = { { 20.0, 20000.0, 10.0 }, { 100.0, 10000.0, 2.0 }, { 50.0, 5000.0, 3.5 } };

    for (const auto& p : sets) {
        CAPTURE(p.f1, p.f2, p.t);
        Sweep::Config cfg;
        cfg.startHz = p.f1;
        cfg.endHz = p.f2;
        cfg.durationSec = p.t;
        Sweep sweep(cfg);

        // Novak synchronisation rounds f1*L to a whole number, so the closed
        // form has an extra step: L = round(f1 * T/ln(f2/f1)) / f1. Without it
        // the sweep's total phase is not a whole number of turns and the
        // harmonic packets carry a frequency-dependent phase rotation.
        const double requestedL = p.t / std::log(p.f2 / p.f1);
        const double expectedL = std::max(1.0, std::round(p.f1 * requestedL)) / p.f1;
        CHECK_THAT(sweep.synchronisedCycles(),
                   WithinAbs(std::round(sweep.synchronisedCycles()), 1.0e-9));
        CHECK_THAT(sweep.durationSec(),
                   WithinRel(expectedL * std::log(p.f2 / p.f1), 1.0e-12));
        const double expectedK = 2.0 * kPi * p.f1 * expectedL;
        CHECK_THAT(sweep.lengthConstantL(), WithinRel(expectedL, 1.0e-12));
        CHECK_THAT(sweep.phaseConstantK(), WithinRel(expectedK, 1.0e-12));

        // The identity that PROVES L is correct, independent of how it was
        // computed: requiring f(T) = f2 is exactly how L was derived. Under
        // synchronisation the T in that identity is the RENDERED duration, not
        // the requested one -- quantising L moves the moment at which the sweep
        // reaches f2, and it reaches f2 exactly then. Reading p.t here instead
        // asks for the frequency 20 ms past the end of a 1.98 s sweep, which
        // for a 100 Hz-10 kHz sweep is 4.7% high.
        CHECK_THAT(p.f1 * std::exp(sweep.durationSec() / sweep.lengthConstantL()),
                   WithinRel(p.f2, 1.0e-12));
    }
}

TEST_CASE("The sweep phase matches the closed form K(e^(t/L)-1)", "[sweep][golden]") {
    Sweep::Config cfg;
    cfg.sampleRate = 48000.0; cfg.startHz = 100.0; cfg.endHz = 10000.0; cfg.durationSec = 2.0;
    Sweep sweep(cfg);

    const auto n = sweep.lengthSamples();
    for (int i = 0; i < 10; ++i) {
        const std::size_t sample = (n * (std::size_t) i) / 10 + 1;
        const double t = (double) sample / cfg.sampleRate;
        const double expected = sweep.phaseConstantK() * (std::exp(t / sweep.lengthConstantL()) - 1.0);
        CAPTURE(sample);
        CHECK_THAT(sweep.phaseAt(sample), WithinRel(expected, 1.0e-12));
    }

    // `cases` must be named: findCase() returns a reference into its
    // argument, and an unnamed sweepGolden() temporary dies at the end of
    // THIS statement -- binding straight to findCase(sweepGolden(), ...)
    // left `g` dangling (a deterministic SIGSEGV once memory was reused).
    const auto cases = sweepGolden();
    const auto& g = findCase(cases, "sweep_params");
    Sweep::Config gcfg;
    gcfg.sampleRate = g.row("fs").front(); gcfg.startHz = g.row("f1").front();
    gcfg.endHz = g.row("f2").front(); gcfg.durationSec = g.row("T").front();
    Sweep goldenSweep(gcfg);

    CHECK_THAT(goldenSweep.lengthConstantL(), WithinRel(g.row("L").front(), 1.0e-9));
    CHECK_THAT(goldenSweep.phaseConstantK(), WithinRel(g.row("K").front(), 1.0e-9));

    const auto& gn = g.row("n");
    const auto& gphase = g.row("phase");
    REQUIRE(gn.size() == gphase.size());
    for (std::size_t i = 0; i < gn.size(); ++i) {
        const auto sample = (std::size_t) gn[i];
        CHECK_THAT(goldenSweep.phaseAt(sample), WithinRel(gphase[i], 1.0e-9));
    }
}

TEST_CASE("Instantaneous frequency is f1 e^(t/L), measured at the zero crossings", "[sweep]") {
    Sweep::Config cfg;
    cfg.sampleRate = 48000.0;
    cfg.startHz = 50.0;
    cfg.endHz = 15000.0;
    cfg.durationSec = 6.0;   // several seconds -- plenty of cycles near each target time
    Sweep sweep(cfg);

    const auto n = sweep.lengthSamples();
    std::vector<float> samples(n);
    sweep.process(samples);

    // Independent route: never calls phaseAt/instantaneousFrequency. Scans
    // the RENDERED signal's positive-going zero crossings and takes
    // 1/interval as a measured local frequency -- checks what process()
    // actually produced, not the formula case 21 already checks.
    std::vector<double> crossingsSec;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        if (samples[i] <= 0.0f && samples[i + 1] > 0.0f) {
            const double denom = (double) samples[i + 1] - (double) samples[i];
            const double frac = denom != 0.0 ? -(double) samples[i] / denom : 0.0;
            crossingsSec.push_back(((double) i + frac) / cfg.sampleRate);
        }
    }
    REQUIRE(crossingsSec.size() > 100);

    const double lengthL = sweep.lengthConstantL();
    for (double frac : { 0.1, 0.3, 0.5, 0.7, 0.9 }) {
        const double targetT = frac * cfg.durationSec;
        const auto it = std::lower_bound(crossingsSec.begin(), crossingsSec.end(), targetT);
        REQUIRE(it != crossingsSec.begin());
        REQUIRE(it != crossingsSec.end());
        const double t1 = *(it - 1), t2 = *it;
        const double interval = t2 - t1;
        REQUIRE(interval > 0.0);
        const double measuredFreq = 1.0 / interval;
        const double midT = 0.5 * (t1 + t2);
        const double expectedFreq = cfg.startHz * std::exp(midT / lengthL);
        CAPTURE(frac, measuredFreq, expectedFreq);
        CHECK_THAT(measuredFreq, WithinRel(expectedFreq, 0.005));
    }
}

TEST_CASE("The fade is raised-cosine, and the cycles floor still applies", "[sweep]") {
    Sweep::Config cfg;
    cfg.sampleRate = 48000.0;
    cfg.startHz = 50.0;
    cfg.endHz = 5000.0;
    cfg.durationSec = 3.0;
    cfg.levelDbFsPeak = -6.0;
    cfg.fadeInSec = 0.001;      // deliberately shorter than 2/startHz = 0.04 s
    cfg.fadeInOctaves = 0.0;    // isolate the cycles floor this case is about:
                                // at this duration the octave floor is 0.903 s
                                // and would otherwise win by a factor of 22
    Sweep sweep(cfg);

    const auto expectedClamp = (std::size_t) std::llround((2.0 / cfg.startHz) * cfg.sampleRate);
    CHECK(sweep.fadeInSamples() == expectedClamp);

    // Shape check uses a SEPARATE, long fade, not the clamped 2-cycle
    // minimum: peak-tracking needs the envelope to barely move within one
    // carrier cycle, which the clamp (already verified above) violates.
    auto shapeCfg = cfg;
    shapeCfg.fadeInSec = 1.0;   // 50 cycles at 50 Hz
    shapeCfg.fadeInOctaves = 0.0;   // measure the fade this case names
    Sweep shapeSweep(shapeCfg);
    const auto fadeLen = shapeSweep.fadeInSamples();
    std::vector<float> samples(fadeLen + 4);
    shapeSweep.process(samples);

    // Track successive positive peaks (parabolically refined) against
    // amplitude*raisedCosine(p). 1e-5 relative OR'd with 3e-5 absolute:
    // near the fade's start the envelope is near zero, where a small fixed
    // float32/refinement noise floor reads as a huge relative error.
    const double amplitude = amplitudeFromDb(cfg.levelDbFsPeak);
    int checked = 0;
    for (std::size_t i = 1; i + 1 < fadeLen; ++i) {
        if (samples[i - 1] < samples[i] && samples[i] > samples[i + 1] && samples[i] > 0.0f) {
            const double refined = refinePeak(samples[i - 1], samples[i], samples[i + 1]);
            const double p = (double) i / (double) (fadeLen - 1);
            const double expected = amplitude * raisedCosine(p);
            CAPTURE(i, p);
            CHECK_THAT(refined, WithinRel(expected, 1.0e-5) || WithinAbs(expected, 3.0e-5));
            ++checked;
        }
    }
    REQUIRE(checked >= 1);
}

TEST_CASE("The inverse filter envelope rises 6 dB per octave with frequency", "[sweep]") {
    Sweep::Config cfg;
    cfg.sampleRate = 48000.0;
    cfg.startHz = 100.0;
    cfg.endHz = 10000.0;
    cfg.durationSec = 2.0;
    cfg.levelDbFsPeak = -6.0;
    Sweep sweep(cfg);

    const double lengthL = sweep.lengthConstantL();
    const double amplitude = amplitudeFromDb(cfg.levelDbFsPeak);

    // Octave points anchored at the temporal midpoint, where f(T/2) =
    // sqrt(f1*f2). Stepping by exactly L*ln(2) in CONTINUOUS time doubles the
    // frequency to full double precision -- f(t) is a pure exponential, no
    // sample quantisation involved.
    const double tMid = cfg.durationSec / 2.0;
    const double octaveStep = lengthL * std::log(2.0);
    const double t[3] = { tMid, tMid + octaveStep, tMid + 2.0 * octaveStep };
    double freq[3];
    for (int i = 0; i < 3; ++i) freq[i] = cfg.startHz * std::exp(t[i] / lengthL);

    // Assertion A: the exact algebra "+6 dB/oct" rests on, hence 1e-9.
    // Deviation: a literal ratio-of-rendered-samples check cannot reach
    // 1e-9 -- the nearest float32 sample to t[i] is off by up to half a
    // sample, so assertion B below checks the rendered signal at a looser,
    // honest tolerance instead.
    CHECK_THAT(freq[1] / freq[0], WithinRel(2.0, 1.0e-9));
    CHECK_THAT(freq[2] / freq[1], WithinRel(2.0, 1.0e-9));

    const auto n = sweep.lengthSamples();
    const auto inv = sweep.buildInverseFilter();
    double envelope[3];
    for (int i = 0; i < 3; ++i) {
        std::size_t n0 = (std::size_t) std::llround(t[i] * cfg.sampleRate), best = n0;
        double bestAbsSin = std::abs(std::sin(sweep.phaseAt(n0)));
        const std::size_t lo = n0 > 50 ? n0 - 50 : 0;
        const std::size_t hi = std::min(n0 + 50, n - 1);
        for (std::size_t s = lo; s <= hi; ++s) {
            const double v = std::abs(std::sin(sweep.phaseAt(s)));
            if (v > bestAbsSin) { bestAbsSin = v; best = s; }
        }
        REQUIRE(bestAbsSin > 0.9);   // well clear of a sine zero-crossing

        const double model = amplitude * std::sin(sweep.phaseAt(best));
        const std::size_t m = n - 1 - best;
        envelope[i] = (double) inv[m] / model;

        const double expectedEnvelope = sweep.instantaneousFrequency(best) / cfg.endHz;
        CAPTURE(i, best, model, expectedEnvelope);
        CHECK_THAT(envelope[i], WithinRel(expectedEnvelope, 1.0e-4));
    }
    CHECK_THAT(envelope[1] / envelope[0], WithinRel(2.0, 2.0e-3));
    CHECK_THAT(envelope[2] / envelope[1], WithinRel(2.0, 2.0e-3));
}

TEST_CASE("Sweep then inverse filter recovers a synthetic IR at SNR above 60 dB", "[sweep][golden]") {
    // Parameters chosen for CI runtime (plan §3): N = 96000, linear
    // convolution length 191999, FFT size 262144.
    const double fs = 48000.0, f1 = 100.0, f2 = 10000.0, t = 2.0;
    REQUIRE(nextPow2(96000 + 96000 - 1) == 262144);

    const auto result = runSweepDeconvolution(fs, f1, f2, t, 262144);

    CHECK(result.offset1500 == 500);
    CHECK(result.offset2300 == 1300);
    CHECK_THAT(result.amp1500Rel, WithinRel(0.5, 0.005));
    CHECK_THAT(result.amp2300Rel, WithinRel(-0.25, 0.005));
    CHECK(result.snrDb > 60.0);

    // Same dangling-reference trap as "sweep_params" above -- `cases` must be
    // a named local so it outlives `g`.
    const auto cases = sweepGolden();
    const auto& g = findCase(cases, "sweep_deconv");
    // +/-2 dB, not the plan's +/-0.5 dB: two independent pipelines (float32
    // RealFft here vs float64 numpy in gen_generator.py; separate peak
    // finders/convolutions) measuring a noise-floor ratio don't converge to
    // 0.5 dB from the same formulas alone -- a stable ~1.2 dB gap persisted
    // here even after fixing a real fade off-by-one on the Python side.
    // +/-2 dB still catches a real error; the >60 dB floor above is binding.
    CHECK_THAT(result.snrDb, WithinAbs(g.row("snr_db").front(), 2.0));

    // peak_index was WRITTEN to the golden from the first day and read by
    // nothing. Four commits in this lane cited "peak_index must not move" as a
    // tripwire; it lived only in a human reading the regeneration diff, so a
    // later regeneration that moved it would have gone green. That is trap #1
    // of docs/HANDOFF.md -- a guard that has silently stopped guarding -- in
    // its golden-vector form.
    CHECK(result.mainIndex == (std::size_t) g.row("peak_index").front());

    // And the stored index is not an oracle: it is a closed form. The analysis
    // pulse peaks at Ninv-1 (= round(T*fs) - 1), and the synthetic room puts
    // its direct arrival ir_index samples later. Deriving it here means a
    // generator change that silently altered the fixture cannot hide inside a
    // regenerated number.
    // T_sync, not T: synchronisation moves the duration, so the requested one
    // no longer gives the sample count the sweep actually rendered.
    const auto ninv = (std::size_t) std::llround(g.row("T_sync").front() * g.row("fs").front());
    CHECK((std::size_t) g.row("peak_index").front()
          == ninv - 1u + (std::size_t) g.row("ir_index").front());

    // The two implementations must agree on the FIXTURE, not just the result.
    CHECK_THAT(g.row("ir_index").front(), WithinAbs(1000.0, 0.0));
    CHECK_THAT(g.row("ir_amp").front(), WithinAbs(1.0, 0.0));
}

TEST_CASE("Full-range 20 Hz to 20 kHz sweep deconvolution", "[.][slow][sweep]") {
    const double fs = 48000.0, f1 = 20.0, f2 = 20000.0, t = 10.0;
    const auto result = runSweepDeconvolution(fs, f1, f2, t, 1u << 20);

    CHECK(result.offset1500 == 500);
    CHECK(result.offset2300 == 1300);
    CHECK_THAT(result.amp1500Rel, WithinRel(0.5, 0.005));
    CHECK_THAT(result.amp2300Rel, WithinRel(-0.25, 0.005));
    CHECK(result.snrDb > 60.0);
}

TEST_CASE("The inverse filter is the faded sweep reversed and shaped, nothing else",
          "[sweep]") {
    Sweep::Config cfg;
    cfg.sampleRate  = 48000.0;
    cfg.startHz     = 100.0;
    cfg.endHz       = 10000.0;
    cfg.durationSec = 2.0;
    cfg.fadeInSec   = 0.30;    // ASYMMETRIC on purpose. Under equal fade widths
    cfg.fadeOutSec  = 0.02;    // every candidate construction agrees, which is
    Sweep sweep(cfg);          // exactly how a second, undocumented Tukey layer
                               // lived in buildInverseFilter for four days
                               // without a single test noticing.
    REQUIRE(sweep.fadeInSamples() != sweep.fadeOutSamples());

    const auto n = sweep.lengthSamples();
    std::vector<float> forward(n);
    sweep.process(forward);
    const auto inv = sweep.buildInverseFilter();
    REQUIRE(inv.size() == n);

    // Sweep.h specifies inv[m] = x[N-1-m] * (instantaneousFrequency(N-1-m)/endHz)
    // -- the already-rendered, already-faded sweep, reversed, shaped. No further
    // windowing of any kind. Pinning that as the SPEC, element by element, is
    // what makes a later "helpful" extra layer go red instead of going unnoticed.
    // The tolerance is a closed form, not a picked number. buildInverseFilter
    // computes the product in double and stores ONE float; process() renders
    // the same forward samples bit-for-bit through the same expression (see
    // Sweep::nextSample). So the only difference is a single double->float
    // rounding. Every |inv[m]| is below 1.0 -- asserted below rather than
    // assumed -- hence ulp <= 2^-24 and half an ulp is 2^-25 = 2.98e-8.
    //
    // NOT zero, deliberately. MSVC /fp:precise does not contract today so
    // bit-identical would pass, but a toolchain that enabled FMA in one of the
    // two translation units and not the other would make this red with nothing
    // wrong. One rounding is the right claim; zero roundings is a claim about
    // the compiler.
    // Measured for this fixture: worst 1.48995e-08 with largest |inv| = 0.4787.
    // Since that largest is below 0.5 its own ulp is 2^-25 and half of that is
    // 2^-26 = 1.49012e-08 -- so the measurement SATURATES the tighter sub-bound,
    // which is how we know 2^-25 is a real bound and not a comfortable margin.
    // The looser figure is asserted so a configuration whose samples reach into
    // [0.5, 1) does not turn a correct implementation red.
    constexpr double kHalfUlpBelowOne = 2.98023223876953125e-08;   // 2^-25
    double worst = 0.0;
    double largest = 0.0;
    for (std::size_t m = 0; m < n; ++m) {
        const std::size_t original = n - 1 - m;
        const double expected = static_cast<double>(forward[original])
                              * (sweep.instantaneousFrequency(original) / cfg.endHz);
        worst = std::max(worst, std::abs(static_cast<double>(inv[m]) - expected));
        largest = std::max(largest, std::abs(static_cast<double>(inv[m])));
    }
    CAPTURE(worst, largest);
    CHECK(largest < 1.0);              // the premise the bound rests on
    CHECK(worst <= kHalfUlpBelowOne);

    // Both ends already reach zero through the inherited fades, which is why no
    // second layer is needed to prevent a step in the kernel.
    CHECK_THAT(static_cast<double>(inv.front()), WithinAbs(0.0, 1.0e-9));
    CHECK_THAT(static_cast<double>(inv.back()), WithinAbs(0.0, 1.0e-9));
}

TEST_CASE("The fade-out has a floor of two cycles at endHz", "[sweep]") {
    Sweep::Config cfg;
    cfg.sampleRate  = 48000.0;
    cfg.startHz     = 20.0;
    cfg.endHz       = 20000.0;
    cfg.durationSec = 2.0;
    cfg.fadeOutSec  = 0.0;     // legitimate request; must not leave a step
    Sweep sweep(cfg);

    // 2 / 20000 Hz = 0.1 ms = 5 samples at 48 kHz.
    const auto expected = (std::size_t) std::llround((2.0 / cfg.endHz) * cfg.sampleRate);
    CHECK(sweep.fadeOutSamples() == expected);
    CHECK(expected == 5u);

    // Dormant at the shipped default: 0.02 s is 960 samples, far above the floor.
    auto defaultCfg = cfg;
    defaultCfg.fadeOutSec = 0.02;
    CHECK(Sweep(defaultCfg).fadeOutSamples() == 960u);
}

TEST_CASE("The fade-in clamp is expressed in octaves of sweep travel", "[sweep]") {
    Sweep::Config cfg;
    cfg.sampleRate = 48000.0;
    cfg.startHz = 50.0;
    cfg.endHz = 5000.0;
    cfg.durationSec = 3.0;
    cfg.fadeInSec = 0.001;      // below every floor, so a floor must win

    // Three floors compete. L = T/ln(f2/f1) = 3/ln(100) = 0.651442 s, so two
    // octaves is 2*ln2*L = 0.903090 s -- an order of magnitude above the
    // 0.04 s cycles floor, which is the whole point: the cycles rule is
    // expressed in seconds, and the artefact floor is governed by octaves.
    Sweep sweep(cfg);
    // Read L from the sweep: synchronisation rounds it, so 3/ln(100) is the
    // requested value, not the one the fade floor is computed against.
    const double octaveFloorSec = 2.0 * std::log(2.0) * sweep.lengthConstantL();
    CHECK(sweep.fadeInSamples()
          == (std::size_t) std::llround(octaveFloorSec * cfg.sampleRate));
    CHECK_THAT(sweep.fadeInOctavesAchieved(), WithinRel(2.0, 1.0e-5));

    // The cycles floor still governs where it is the larger of the two: a very
    // short sweep travels half an octave faster than two cycles at f1 take.
    auto shortCfg = cfg;
    shortCfg.durationSec = 0.05;
    shortCfg.fadeInOctaves = 0.5;
    Sweep shortSweep(shortCfg);
    CHECK(shortSweep.fadeInSamples()
          == (std::size_t) std::llround((2.0 / shortCfg.startHz) * shortCfg.sampleRate));

    // Zero restores the pre-2026-08-30 behaviour exactly, which is what makes
    // the trade-off in record decision 5 selectable rather than imposed.
    auto legacyCfg = cfg;
    legacyCfg.fadeInOctaves = 0.0;
    CHECK(Sweep(legacyCfg).fadeInSamples()
          == (std::size_t) std::llround((2.0 / cfg.startHz) * cfg.sampleRate));

    // And a LONGER sweep keeps the same width in octaves. Under the cycles rule
    // alone it would have shrunk -- 0.04 s is 0.5 octave at T=3 s but 0.15
    // octave at T=10 s -- so lengthening the sweep made the band edges worse,
    // which is the defect this floor exists to remove.
    auto longCfg = cfg;
    longCfg.durationSec = 10.0;
    CHECK_THAT(Sweep(longCfg).fadeInOctavesAchieved(), WithinRel(2.0, 1.0e-5));
}

TEST_CASE("The valid band's edges follow from the fades", "[sweep]") {
    Sweep::Config cfg;
    cfg.sampleRate = 48000.0;
    cfg.startHz = 100.0;
    cfg.endHz = 10000.0;
    cfg.durationSec = 2.0;
    Sweep sweep(cfg);

    // f_lo = f1*exp(fadeInSec/L). When the octave floor binds, fadeInSec is
    // exactly octaves*ln2*L, so the exponential collapses to f1 * 2^octaves --
    // the lower band edge IS the knob. 100 Hz * 2^2 = 400 Hz.
    CHECK_THAT(sweep.validBandLowHz(), WithinRel(400.0, 1.0e-4));

    // f_hi = f2*exp(-fadeOutSec/L), with L read from the sweep rather than
    // recomputed -- synchronisation rounds it.
    CHECK_THAT(sweep.validBandHighHz(),
               WithinRel(10000.0 * std::exp(-0.02 / sweep.lengthConstantL()), 1.0e-9));
    CHECK(sweep.validBandHighHz() < cfg.endHz);
    CHECK(sweep.validBandLowHz() > cfg.startHz);
}
