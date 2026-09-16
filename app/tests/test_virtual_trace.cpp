// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-ALIGN task G (docs/plans/2026-09-15-L7-align-impl-plan.md; decision
// record docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.5, Sec.10.10).
//
// The ONE dB<->complex conversion point. What is proven here is mostly
// NEGATIVE: a VirtualTrace cannot become a Trace, cannot reach TraceLibrary,
// carries no CaptureMeta, and never names the sum's per-bin trust
// `coherence`. Those four are the reasons this type exists at all -- the
// arithmetic it runs is already locked in core by task A.

#include "CodeLines.h"
#include "trace/Trace.h"
#include "trace/TraceLibrary.h"
#include "trace/VirtualTrace.h"

#include "rta/dsp/TransferEstimator.h"
#include "rta/eq/BiquadDesign.h"
#include "rta/eq/FilterSpec.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <type_traits>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::trace::CaptureMeta;
using rta::trace::Field;
using rta::trace::Trace;
using rta::trace::VirtualTrace;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr int kFftSize = 256;
constexpr double kSampleRate = 48000.0;

/// A source whose magnitude spans the whole displayable range -- the -120 dB
/// floor (TransferSnapshot::kMagnitudeFloorDb) at one end and +20 dB at the
/// other -- and whose phase spans essentially all of (-pi, pi].
///
/// "Essentially": the endpoints are held a hundredth of a radian inside +-pi
/// on purpose. `float(pi)` is LARGER than pi, so a stored phase of exactly pi
/// re-reads through std::arg as -pi + 1e-7 -- a full 2*pi round-trip error
/// that is a property of the (-pi, pi] wrap convention, not of this
/// conversion. Testing the conversion at a value where the convention itself
/// is discontinuous would measure the wrong thing.
Trace makeSourceTrace(std::string id = "src-a") {
    CaptureMeta meta;
    meta.id = std::move(id);
    meta.sampleRate = kSampleRate;
    meta.fftSize = kFftSize;
    meta.channelRoles = "ref:1 meas:2";

    const std::size_t n = rta::trace::pointCountFor(kFftSize);
    std::vector<float> magnitudeDb(n);
    std::vector<float> phase(n);
    std::vector<float> coherence(n);
    for (std::size_t k = 0; k < n; ++k) {
        const double t = static_cast<double>(k) / static_cast<double>(n - 1);
        magnitudeDb[k] = static_cast<float>(-120.0 + 140.0 * t);
        phase[k] = static_cast<float>((-kPi + 0.01) + (2.0 * kPi - 0.02) * t);
        coherence[k] = static_cast<float>(0.25 + 0.5 * t);
    }

    auto trace = Trace::make(std::move(meta), std::move(magnitudeDb));
    REQUIRE(trace.has_value());
    REQUIRE(trace->setPhase(std::move(phase)));
    REQUIRE(trace->setCoherence(std::move(coherence)));
    return std::move(*trace);
}

/// A flat source at `db` with zero phase, for the summation cases.
Trace makeFlatTrace(std::string id, double db) {
    CaptureMeta meta;
    meta.id = std::move(id);
    meta.sampleRate = kSampleRate;
    meta.fftSize = kFftSize;
    meta.channelRoles = "ref:1 meas:2";

    const std::size_t n = rta::trace::pointCountFor(kFftSize);
    std::vector<float> magnitudeDb(n, static_cast<float>(db));
    std::vector<float> phase(n, 0.0f);

    auto trace = Trace::make(std::move(meta), std::move(magnitudeDb));
    REQUIRE(trace.has_value());
    REQUIRE(trace->setPhase(std::move(phase)));
    return std::move(*trace);
}

// --- G3/G4: detection idioms ------------------------------------------
// Each has a POSITIVE control on Trace itself, so a trait that silently
// stopped detecting anything would fail here rather than pass everywhere.

template <typename Library, typename Argument, typename = void>
struct AddIsWellFormed : std::false_type {};

template <typename Library, typename Argument>
struct AddIsWellFormed<Library, Argument,
                       std::void_t<decltype(std::declval<Library&>().add(
                           std::declval<Argument>(), std::string{}, std::string{}))>>
    : std::true_type {};

template <typename T, typename = void>
struct HasMeta : std::false_type {};

template <typename T>
struct HasMeta<T, std::void_t<decltype(std::declval<const T&>().meta())>> : std::true_type {};

}  // namespace

TEST_CASE("G1: an empty chain round-trips dB and phase through complex", "[virtual_trace]") {
    const Trace source = makeSourceTrace();
    const auto virtualTrace = VirtualTrace::fromTrace(source);
    REQUIRE(virtualTrace.has_value());

    const auto rendered = virtualTrace->render();
    REQUIRE(rendered.magnitudeDb.size() == source.pointCount());
    REQUIRE(rendered.phaseRadians.size() == source.pointCount());

    const auto sourceDb = source.field(Field::Magnitude);
    const auto sourcePhase = source.field(Field::Phase);

    double worstDb = 0.0;
    double worstPhase = 0.0;
    double worstDbBeforeQuantisation = 0.0;
    double worstPhaseBeforeQuantisation = 0.0;
    for (std::size_t k = 0; k < source.pointCount(); ++k) {
        worstDb = std::max(worstDb, std::abs(static_cast<double>(rendered.magnitudeDb[k])
                                             - static_cast<double>(sourceDb[k])));
        worstPhase = std::max(worstPhase, std::abs(static_cast<double>(rendered.phaseRadians[k])
                                                   - static_cast<double>(sourcePhase[k])));
        // The SAME residual read before the float cast. The two are reported
        // separately because the float figures below come out at exactly 0 --
        // the double round trip lands inside half a float ULP of the stored
        // value at every bin, so rounding back to float returns the original
        // bit pattern. A residual of exactly zero would be a fixture that
        // cannot fail (memory/a-fixture-can-be-too-well-behaved-to-fail.md) if
        // it were the ONLY number here, so the arithmetic's own residual is
        // measured too and bounded at a figure it has to earn.
        worstDbBeforeQuantisation =
            std::max(worstDbBeforeQuantisation,
                     std::abs(20.0 * std::log10(std::abs(rendered.h[k]))
                              - static_cast<double>(sourceDb[k])));
        worstPhaseBeforeQuantisation =
            std::max(worstPhaseBeforeQuantisation,
                     std::abs(std::arg(rendered.h[k]) - static_cast<double>(sourcePhase[k])));
    }
    // Printed beside the tolerance: a residual nobody can see is a residual
    // nobody can argue with (CLAUDE.md verification standard).
    INFO("worst dB residual " << worstDb << " against 1e-4; worst phase residual " << worstPhase
                              << " against 1e-6");
    INFO("before the float cast: dB " << worstDbBeforeQuantisation << ", phase "
                                      << worstPhaseBeforeQuantisation << ", both against 1e-9");
    CHECK(worstDb <= 1e-4);
    CHECK(worstPhase <= 1e-6);
    CHECK(worstDbBeforeQuantisation <= 1e-9);
    CHECK(worstPhaseBeforeQuantisation <= 1e-9);

    // Including the floor bin itself, which is the one the record singles out.
    CHECK_THAT(static_cast<double>(rendered.magnitudeDb.front()), WithinAbs(-120.0, 1e-4));
}

TEST_CASE("G2: the -120 dB floor is harmless in a sum", "[virtual_trace]") {
    // 10^(-120/20) = 1e-6. Against a unit bin that is 20log10(1 + 1e-6) =
    // 8.6859e-6 dB -- the record's claim, asserted rather than repeated.
    const auto floorTrace = VirtualTrace::fromTrace(makeFlatTrace("floor", -120.0));
    const auto unitTrace = VirtualTrace::fromTrace(makeFlatTrace("unit", 0.0));
    REQUIRE(floorTrace.has_value());
    REQUIRE(unitTrace.has_value());

    const auto floorRender = floorTrace->render();
    CHECK_THAT(std::abs(floorRender.h.front()), WithinAbs(1.0e-6, 1e-12));

    const auto summed = rta::trace::sumOf(*unitTrace, *floorTrace);
    const double closedForm = 20.0 * std::log10(1.0 + 1.0e-6);
    INFO("closed form " << closedForm << " dB");
    CHECK(std::abs(closedForm) <= 8.7e-6);
    CHECK(std::abs(static_cast<double>(summed.magnitudeDb.front())) <= 8.7e-6);
}

TEST_CASE("G3: a VirtualTrace cannot become a Trace and cannot reach the library",
          "[virtual_trace]") {
    // The barrier is the TYPE, not a naming convention (record Sec.5,
    // research D8). TraceLibrary::add takes a Trace by value, so a preview
    // that is not a Trace has no way in.
    static_assert(!std::is_constructible_v<Trace, const VirtualTrace&>,
                  "a VirtualTrace must not be convertible to a Trace");
    static_assert(!std::is_convertible_v<VirtualTrace, Trace>);

    static_assert(AddIsWellFormed<rta::trace::TraceLibrary, Trace>::value,
                  "positive control: add(Trace, name, group) IS well-formed");
    static_assert(!AddIsWellFormed<rta::trace::TraceLibrary, const VirtualTrace&>::value,
                  "library.add(virtualTrace, \"\", \"\") must be ill-formed");
    SUCCEED("compile-time barrier holds");
}

TEST_CASE("G4: a VirtualTrace has no CaptureMeta", "[virtual_trace]") {
    static_assert(HasMeta<Trace>::value, "positive control: a Trace does have meta()");
    static_assert(!HasMeta<VirtualTrace>::value, "a VirtualTrace must have no meta()");

    // And the type carries none by any other spelling either.
    const std::filesystem::path header =
        std::filesystem::path{ RTA_REPO_ROOT } / "app" / "src" / "trace" / "VirtualTrace.h";
    for (const auto& line : rta::test::codeLines(header)) {
        INFO(line);
        CHECK(line.find("capturemeta") == std::string::npos);
    }
}

TEST_CASE("G5: delay, polarity and gain leave the source's trust untouched", "[virtual_trace]") {
    const Trace source = makeSourceTrace();
    auto virtualTrace = VirtualTrace::fromTrace(source);
    REQUIRE(virtualTrace.has_value());

    virtualTrace->setChain({ rta::trace::DelayOp{ 1.7e-3 }, rta::trace::PolarityOp{},
                             rta::trace::GainOp{ 0.5 } });
    const auto rendered = virtualTrace->render();

    const auto sourceCoherence = source.field(Field::Coherence);
    REQUIRE(rendered.coherence.size() == sourceCoherence.size());
    for (std::size_t k = 0; k < sourceCoherence.size(); ++k) {
        // BITWISE: how much a trace is trusted is not something a delay, a
        // polarity flip or a gain trim has any opinion about (record Sec.5).
        CHECK(rendered.coherence[k] == sourceCoherence[k]);
    }

    // A source with no coherence renders none -- not a vector of zeros
    // (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
    const auto noCoherence = VirtualTrace::fromTrace(makeFlatTrace("bare", 0.0));
    REQUIRE(noCoherence.has_value());
    CHECK(noCoherence->render().coherence.empty());
}

TEST_CASE("G6: the sum's trust is not a coherence, in the type and in the header",
          "[virtual_trace]") {
    const Trace a = makeSourceTrace("sum-a");
    const Trace b = makeSourceTrace("sum-b");
    const auto va = VirtualTrace::fromTrace(a);
    const auto vb = VirtualTrace::fromTrace(b);
    REQUIRE(va.has_value());
    REQUIRE(vb.has_value());

    const auto summed = rta::trace::sumOf(*va, *vb);
    REQUIRE(summed.trustPresent);
    REQUIRE(summed.summationTrust.size() == a.pointCount());
    const auto coherenceA = a.field(Field::Coherence);
    const auto coherenceB = b.field(Field::Coherence);
    for (std::size_t k = 0; k < summed.summationTrust.size(); ++k) {
        CHECK(summed.summationTrust[k] == std::min(coherenceA[k], coherenceB[k]));
    }

    // Structural, and scoped to the SUM's declaration block rather than to the
    // whole file: the single-source render passes a coherence through and must
    // go on saying so. Run from the test (RTA_REPO_ROOT) so it executes on all
    // three CI operating systems instead of living in a grep someone has to
    // remember to type.
    const std::filesystem::path header =
        std::filesystem::path{ RTA_REPO_ROOT } / "app" / "src" / "trace" / "VirtualTrace.h";
    const auto lines = rta::test::codeLines(header);

    bool inside = false;
    bool sawBlock = false;
    bool sawTrustField = false;
    std::size_t offenders = 0;
    for (const auto& line : lines) {
        if (line.find("struct virtualsum") != std::string::npos) {
            inside = true;
            sawBlock = true;
            continue;
        }
        if (!inside) continue;
        if (line.find("};") != std::string::npos) {
            inside = false;
            continue;
        }
        if (line.find("summationtrust") != std::string::npos) sawTrustField = true;
        if (line.find("coherence") != std::string::npos) {
            INFO("VirtualSum declares: " << line);
            ++offenders;
        }
    }
    CHECK(sawBlock);
    CHECK(sawTrustField);
    CHECK(offenders == 0u);
}

TEST_CASE("G7: the biquad op goes through one response path, not two", "[virtual_trace]") {
    const Trace source = makeFlatTrace("biquad", 0.0);
    auto plain = VirtualTrace::fromTrace(source);
    auto filtered = VirtualTrace::fromTrace(source);
    REQUIRE(plain.has_value());
    REQUIRE(filtered.has_value());

    const std::vector<rta::eq::FilterSpec> specs{
        { rta::eq::FilterType::Peaking, 1000.0, 2.0, -4.5 },
        { rta::eq::FilterType::LowShelf, 120.0, 0.7, +3.0 },
        { rta::eq::FilterType::HighShelf, 6000.0, 0.7, -2.0 },
    };
    filtered->setChain({ rta::trace::BiquadOp{ specs, kSampleRate } });

    const auto plainRender = plain->render();
    const auto filteredRender = filtered->render();
    const double binWidthHz = kSampleRate / static_cast<double>(kFftSize);

    // Compared on the COMPLEX output rather than on the rendered float dB:
    // both sides share one source, so the delta is exact in double and the
    // 1e-9 measures the response path, not float storage.
    double worst = 0.0;
    for (std::size_t k = 1; k < plainRender.h.size(); ++k) {
        const double hz = static_cast<double>(k) * binWidthHz;
        if (hz >= kSampleRate / 2.0) break;
        double expected = 0.0;
        for (const auto& spec : specs) expected += rta::eq::responseDb(spec, kSampleRate, hz);
        const double measured = 20.0 * std::log10(std::abs(filteredRender.h[k]))
                                - 20.0 * std::log10(std::abs(plainRender.h[k]));
        worst = std::max(worst, std::abs(measured - expected));
    }
    INFO("worst |rendered - sum of responseDb| = " << worst << " dB against 1e-9");
    CHECK(worst <= 1e-9);
}
