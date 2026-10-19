// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SWEEP lane task T5 (docs/plans/2026-09-29-sweep-ir-lane-plan.md): the SWEEP
// outcome -> Trace seam and the in-memory IR store. JUCE-free.
#include <catch2/catch_test_macros.hpp>

#include "SweepIrFixture.h"
#include "measure/SweepIrPipeline.h"
#include "trace/CaptureConverter.h"
#include "trace/IrStore.h"

#include <cmath>
#include <memory>
#include <string>

using rta::measure::SweepIr;
using rta::measure::SweepIrOutcome;
using rta::measure::SweepIrRequest;
using rta::measure::SweepIrStatus;
using rta::trace::Field;
using rta::trace::IrStore;
using rta::trace::traceFromSweepIr;

namespace {

// 1 s sweeps keep three pipeline runs cheap; the N rule depends on fs and on
// f_lo only, and f_lo is the same ~80 Hz for any duration (two-octave fade-in
// above a 20 Hz start).
SweepIrOutcome runPureDelay(double fs, std::size_t delay) {
    SweepIrRequest request;
    request.sweep = rta::measure::shippedSweepConfig(fs);
    request.sweep.durationSec = 1.0;
    request.tailSeconds = 0.6;
    const auto required = rta::measure::requiredTailSamples(request);
    return rta::measure::analyseSweepCapture(
        sweep_fixture::delayCapture(request.sweep, 3000, required, delay), request);
}

// The spec's N, computed here from the sentence in the plan (decision 4), not
// by calling the shipped function: nextPow2(ceil(0.5*fs) + round(2*fs/f_lo)).
std::size_t specWindowSize(double fs, double fLo) {
    const auto need = static_cast<std::size_t>(std::ceil(0.5 * fs)) +
                      static_cast<std::size_t>(std::llround(2.0 * fs / fLo));
    std::size_t n = 4;
    while (n < need) n <<= 1;
    return n;
}

}  // namespace

TEST_CASE("the window size N follows the plan's rule at 44.1, 48 and 96 kHz", "[sweep_ir_trace]") {
    struct Row { double fs; std::size_t expected; };
    for (const Row row : {Row{44100.0, 32768}, Row{48000.0, 32768}, Row{96000.0, 65536}}) {
        INFO("fs = " << row.fs);
        auto config = rta::measure::shippedSweepConfig(row.fs);
        config.durationSec = 1.0;
        const rta::gen::Sweep sweep(config);
        // The literals are the plan's table; the spec formula must agree with them...
        CHECK(specWindowSize(row.fs, sweep.validBandLowHz()) == row.expected);
        // ...and so must the shipped function and a real run through the pipeline.
        CHECK(rta::measure::sweepIrWindowSize(row.fs, sweep.validBandLowHz()) == row.expected);
        const auto outcome = runPureDelay(row.fs, 37);
        INFO("status message: " << outcome.message);
        REQUIRE(outcome.status == SweepIrStatus::Ok);
        CHECK(outcome.fftSize == row.expected);
        CHECK(outcome.bins.size() == row.expected / 2 + 1);
    }
}

TEST_CASE("a SWEEP trace has phase, no coherence, zero applied delay and T0's roles",
          "[sweep_ir_trace]") {
    rta::platform::ChannelConfig channels;
    REQUIRE(channels.setRole(0, rta::platform::ChannelRole::Measurement));
    REQUIRE(channels.setRole(1, rta::platform::ChannelRole::Reference));

    const auto outcome = runPureDelay(48000.0, 37);
    REQUIRE(outcome.status == SweepIrStatus::Ok);
    const auto trace = traceFromSweepIr(outcome, "SYNTHETIC",
                                        rta::trace::channelRolesSummary(channels));
    REQUIRE(trace.has_value());

    CHECK(trace->has(Field::Magnitude));
    CHECK(trace->has(Field::Phase));
    // Absent, never zero-filled (Trace.h:17-19): a zero coherence reads as
    // "measured, and uncorrelated", which a single sweep never established.
    CHECK_FALSE(trace->has(Field::Coherence));
    CHECK(trace->field(Field::Coherence).empty());
    CHECK(trace->meta().appliedDelaySamples == 0);
    CHECK(trace->meta().channelRoles == "ch0=Measurement,ch1=Reference");
    CHECK(trace->meta().fftSize == 32768);
    CHECK(trace->meta().sampleRate == 48000.0);
    CHECK(trace->pointCount() == 32768 / 2 + 1);
    CHECK(trace->meta().deviceName == "SYNTHETIC");
}

TEST_CASE("a pure delay of 37 multiplies the trace spectrum by exp(-i*w*37)", "[sweep_ir_trace]") {
    // Closed form: delaying the measurement by D multiplies every bin by
    // exp(-i w D), w = 2*pi*k/N. Comparing against the SAME pipeline run with
    // D = 0 cancels the loop pulse's own (tiny) phase, so what is left is the
    // delay and nothing else -- including the lead-in rotation, which both
    // runs apply identically. The trace stores atan2 phase, so the expectation
    // is wrapped.
    constexpr double kPi = 3.14159265358979323846;
    const auto zero = runPureDelay(48000.0, 0);
    const auto delayed = runPureDelay(48000.0, 37);
    REQUIRE(zero.status == SweepIrStatus::Ok);
    REQUIRE(delayed.status == SweepIrStatus::Ok);
    const auto traceZero = traceFromSweepIr(zero, "d", "r");
    const auto traceDelayed = traceFromSweepIr(delayed, "d", "r");
    REQUIRE(traceZero.has_value());
    REQUIRE(traceDelayed.has_value());
    const auto phaseZero = traceZero->field(Field::Phase);
    const auto phaseDelayed = traceDelayed->field(Field::Phase);
    for (const double hz : {500.0, 1000.0, 5000.0}) {
        const std::size_t bin = static_cast<std::size_t>(hz / traceDelayed->binHz());
        const double expected = -2.0 * kPi * static_cast<double>(bin) / 32768.0 * 37.0;
        double diff = std::fmod(static_cast<double>(phaseDelayed[bin]) -
                                    static_cast<double>(phaseZero[bin]) - expected,
                                2.0 * kPi);
        if (diff > kPi) diff -= 2.0 * kPi;
        if (diff < -kPi) diff += 2.0 * kPi;
        INFO("hz = " << hz << ", residual rad = " << diff);
        // Single-precision transforms (memory/float32-fft-precision.md) put the
        // phase floor near 1e-5 rad here; 1e-3 rad (0.06 degrees) is 100x that.
        CHECK(std::fabs(diff) < 1e-3);
    }
}

TEST_CASE("a refused outcome yields no trace, even when its bins are fully populated",
          "[sweep_ir_trace]") {
    SweepIrOutcome refused;
    refused.status = SweepIrStatus::TailTooShort;
    CHECK_FALSE(traceFromSweepIr(refused, "d", "r").has_value());

    // The status check is the guard, not the empty bins: an otherwise complete
    // outcome that was refused must not become a trace.
    auto populated = runPureDelay(48000.0, 37);
    REQUIRE(populated.status == SweepIrStatus::Ok);
    populated.status = SweepIrStatus::ArrivalOutOfWindow;
    CHECK_FALSE(traceFromSweepIr(populated, "d", "r").has_value());
}

TEST_CASE("an outcome whose bins disagree with its fftSize yields no trace, not a crash",
          "[sweep_ir_trace]") {
    auto outcome = runPureDelay(48000.0, 37);
    REQUIRE(outcome.status == SweepIrStatus::Ok);
    outcome.fftSize = 1000;  // Trace::make demands fftSize/2 + 1 bins
    CHECK_FALSE(traceFromSweepIr(outcome, "d", "r").has_value());
}

TEST_CASE("the trace metadata states what a sweep is: rectangular, unaveraged, one estimate",
          "[sweep_ir_trace]") {
    const auto outcome = runPureDelay(48000.0, 37);
    REQUIRE(outcome.status == SweepIrStatus::Ok);
    const auto first = traceFromSweepIr(outcome, "d", "r");
    const auto second = traceFromSweepIr(outcome, "d", "r");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->meta().window == "Rectangular");
    CHECK(first->meta().averagingType == "None");
    CHECK(first->meta().averagingDepth == 0);
    CHECK(first->meta().effectiveAverages == 1.0);
    // TraceLibrary::add refuses a duplicate id outright, so two presses must
    // never share one; and a stamp of 0 would sort every SWEEP before 1970.
    CHECK_FALSE(first->meta().id.empty());
    CHECK(first->meta().id != second->meta().id);
    CHECK(first->meta().capturedAtUnixMs > 0);

    // Magnitude is 20 log10 |bin| of the outcome's own bins, in dB.
    for (const std::size_t k : {std::size_t{100}, std::size_t{1000}, std::size_t{5000}}) {
        const double expected = 20.0 * std::log10(static_cast<double>(std::abs(outcome.bins[k])));
        CHECK(std::fabs(static_cast<double>(first->field(Field::Magnitude)[k]) - expected) < 1e-3);
    }
}

TEST_CASE("the ninth IrStore::add evicts the first, FIFO", "[sweep_ir_trace]") {
    IrStore store;
    auto ir = std::make_shared<const SweepIr>();
    for (int i = 0; i < 8; ++i) store.add("t" + std::to_string(i), ir);
    REQUIRE(store.size() == 8);
    for (int i = 0; i < 8; ++i) CHECK(store.containsForTest("t" + std::to_string(i)));

    store.add("t8", ir);
    CHECK(store.size() == 8);
    CHECK_FALSE(store.containsForTest("t0"));
    for (int i = 1; i <= 8; ++i) CHECK(store.containsForTest("t" + std::to_string(i)));

    store.add("t9", ir);
    CHECK_FALSE(store.containsForTest("t1"));
    CHECK(store.containsForTest("t2"));
}

TEST_CASE("re-adding an id replaces it in place and evicts nothing", "[sweep_ir_trace]") {
    IrStore store;
    auto ir = std::make_shared<const SweepIr>();
    for (int i = 0; i < 8; ++i) store.add("t" + std::to_string(i), ir);
    store.add("t3", ir);
    CHECK(store.size() == 8);
    CHECK(store.containsForTest("t0"));
}

TEST_CASE("re-adding an id swaps the stored IR, releasing the old one", "[sweep_ir_trace]") {
    // No lookup exists to read the entry back (ALIGN's arrives with H5), so the
    // swap is observed through ownership: the store held `first`, and after the
    // replace it holds `second` and no longer `first`.
    IrStore store;
    auto first = std::make_shared<const SweepIr>();
    auto second = std::make_shared<const SweepIr>();
    store.add("t0", first);
    CHECK(first.use_count() == 2);
    store.add("t0", second);
    CHECK(first.use_count() == 1);
    CHECK(second.use_count() == 2);
    CHECK(store.size() == 1);
}
