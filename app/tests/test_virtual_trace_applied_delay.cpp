// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane app/xover-applied-delay (docs/dsp/2026-11-09-xover-applied-delay.md).
//
// Z2: CaptureMeta::appliedDelaySamples was stored and never read, so a TRANSFER
// trace STOREd after APPLY summed with one stored before gets a wrong relative
// delay. VirtualTrace::fromTraceRestoringAppliedDelay brings each trace back to
// the physical timeline; plain fromTrace must NOT, because ALIGN corrects the
// same difference inside crossoverBandFit.
//
// Stored-phase convention (T0, core/tests/test_dualfft_applied_delay_sign.cpp,
// asserted against the real DualFftEngine): H_stored = H_true * e^{+j 2 pi f D/fs}.
// So a physically flat, zero-phase response captured with D applied is stored
// with phase +2 pi f D/fs, which is how the fixtures below build it.
#include "trace/Trace.h"
#include "trace/VirtualTrace.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <string>
#include <vector>

using rta::trace::CaptureMeta;
using rta::trace::Trace;
using rta::trace::VirtualTrace;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr int kFftSize = 256;
constexpr double kBinHz = 48000.0 / kFftSize;  // 187.5

/// A physically flat, 0 dB, zero-phase response captured with `appliedSamples`
/// applied at `sampleRate`. Stored phase = wrap(+2 pi f D / fs), per T0.
Trace flatTraceCapturedWith(int appliedSamples, double sampleRate = 48000.0) {
    CaptureMeta meta;
    meta.id = "t";
    meta.sampleRate = sampleRate;
    meta.fftSize = kFftSize;
    meta.channelRoles = "ref:1 meas:2";
    meta.appliedDelaySamples = appliedSamples;

    const std::size_t n = rta::trace::pointCountFor(kFftSize);
    std::vector<float> phase(n);
    for (std::size_t k = 0; k < n; ++k) {
        const double f = static_cast<double>(k) * sampleRate / kFftSize;
        const double theta = 2.0 * kPi * f * appliedSamples / sampleRate;
        phase[k] = static_cast<float>(std::arg(std::polar(1.0, theta)));
    }
    auto trace = Trace::make(meta, std::vector<float>(n, 0.0f));
    REQUIRE(trace.has_value());
    REQUIRE(trace->setPhase(std::move(phase)));
    return std::move(*trace);
}

// Phase is stored as float, wrapped to [-pi, pi]: the float spacing for |x| in
// [2, 4) is 2^-22 = 2.38e-7, so the worst storage error is 1.2e-7 rad. A unit
// phasor off by an angle e is off by |e| in modulus-of-difference to first
// order, which bounds |sum - 2| for two such phasors where one is exact.
constexpr double kStorageErr = 1.2e-7;
constexpr double kTol = 1.5e-7;  // kStorageErr plus double-precision slack

void requireAllBinsUnity(const VirtualTrace& v) {
    const auto h = v.render().h;
    for (std::size_t k = 0; k < h.size(); ++k) {
        CHECK(std::abs(h[k] - std::complex<double>(1.0, 0.0)) <= kTol);
    }
}

}  // namespace

TEST_CASE("Z2: restoring each trace's own applied delay makes the sum coherent",
          "[virtual_trace][applied_delay]") {
    // N = 16 puts the first null of the UNRECONCILED sum, f = fs/(2N) = 1500 Hz,
    // exactly on bin 8 of the 187.5 Hz grid, where the stored phase of B is pi.
    constexpr int kN = 16;
    const Trace a = flatTraceCapturedWith(0);
    const Trace b = flatTraceCapturedWith(kN);

    const auto va = VirtualTrace::fromTraceRestoringAppliedDelay(a);
    const auto vb = VirtualTrace::fromTraceRestoringAppliedDelay(b);
    REQUIRE(va.has_value());
    REQUIRE(vb.has_value());
    const auto summed = rta::trace::sumOf(*va, *vb);

    for (std::size_t k = 0; k < summed.h.size(); ++k) {
        // Two unit phasors, one exact and one within kStorageErr of unity.
        CHECK(std::abs(summed.h[k] - std::complex<double>(2.0, 0.0)) <= kTol);
        CHECK(std::abs(summed.magnitudeDb[k] - 6.0206f) < 1e-5f);
        CHECK(std::abs(summed.phaseRadians[k]) <= kStorageErr);
    }

    // Discrimination: the same two traces through plain fromTrace are the
    // physical lie this lane exists to remove -- a null at f = fs/(2N).
    const auto pa = VirtualTrace::fromTrace(a);
    const auto pb = VirtualTrace::fromTrace(b);
    REQUIRE(pa.has_value());
    REQUIRE(pb.has_value());
    const auto naive = rta::trace::sumOf(*pa, *pb);
    REQUIRE(std::abs(48000.0 / (2.0 * kN) - 8.0 * kBinHz) < 1e-9);
    CHECK(naive.magnitudeDb[8] < -40.0f);
}

TEST_CASE("Z2: the rotation uses the trace's OWN sample rate", "[virtual_trace][applied_delay]") {
    // 44.1 kHz, D = 16. A restoration using 48000 would leave a residual of
    // 2 pi f 16 (1/44100 - 1/48000), about 0.02 rad at 1 kHz and growing with
    // f, far above kTol, so this cannot pass by luck.
    requireAllBinsUnity(*VirtualTrace::fromTraceRestoringAppliedDelay(flatTraceCapturedWith(16, 44100.0)));
}

TEST_CASE("Z2: a negative applied delay is restored with the opposite rotation",
          "[virtual_trace][applied_delay]") {
    requireAllBinsUnity(*VirtualTrace::fromTraceRestoringAppliedDelay(flatTraceCapturedWith(-16)));
}

TEST_CASE("Z2: with D == 0 the restoring factory is bit-identical to fromTrace",
          "[virtual_trace][applied_delay]") {
    const Trace a = flatTraceCapturedWith(0);
    const auto plain = VirtualTrace::fromTrace(a)->render();
    const auto restoring = VirtualTrace::fromTraceRestoringAppliedDelay(a)->render();
    REQUIRE(plain.h.size() == restoring.h.size());
    for (std::size_t k = 0; k < plain.h.size(); ++k) {
        REQUIRE(plain.h[k] == restoring.h[k]);  // exact: no rotation was run
    }
}

TEST_CASE("Z2/T2: plain fromTrace does not rotate, because ALIGN corrects it itself",
          "[virtual_trace][applied_delay]") {
    // Same stored data, D differs only in the metadata: plain fromTrace must
    // ignore the metadata entirely. If it rotated, AlignmentWizard would
    // correct twice (once here, once via
    // BandFitOptions::appliedDelayDifferenceSamples).
    const Trace withDelay = flatTraceCapturedWith(16);
    CaptureMeta zeroed = withDelay.meta();
    zeroed.appliedDelaySamples = 0;
    const auto magnitude = withDelay.field(rta::trace::Field::Magnitude);
    const auto phase = withDelay.field(rta::trace::Field::Phase);
    auto copy = Trace::make(zeroed, std::vector<float>(magnitude.begin(), magnitude.end()));
    REQUIRE(copy.has_value());
    REQUIRE(copy->setPhase(std::vector<float>(phase.begin(), phase.end())));

    const auto left = VirtualTrace::fromTrace(withDelay)->render();
    const auto right = VirtualTrace::fromTrace(*copy)->render();
    for (std::size_t k = 0; k < left.h.size(); ++k) {
        REQUIRE(left.h[k] == right.h[k]);
    }
}
