// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for the FIR text writer -- docs/plans/2026-09-07-L7-fir-impl-plan.md
// task F4. Written from docs/dsp/2026-09-06-l7-fir-export.md Sec.5.

#include "export/FirExport.h"
#include "trace/SessionCodecDetail.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace rta::firexport;
using namespace rta::dsp;
using rta::trace::detail::stripTrailingCr;

namespace {

/// Parses the `# key=value` header lines into a map, and the coefficient
/// lines (after `# --- coefficients follow ---`) into a vector -- an
/// independent parse of renderFirText's own output, not a read of an
/// internal value, so these tests check the FORMAT, not just the function
/// that produced it.
struct ParsedText {
    std::unordered_map<std::string, std::string> header;
    std::vector<double> coefficients;
};

ParsedText parseFirText(const std::string& text) {
    ParsedText out;
    std::istringstream stream(text);
    std::string line;
    bool inCoefficients = false;
    while (std::getline(stream, line)) {
        line = std::string(stripTrailingCr(line));
        if (line.empty()) continue;
        if (line == "# --- coefficients follow ---") {
            inCoefficients = true;
            continue;
        }
        if (inCoefficients) {
            out.coefficients.push_back(std::stod(line));
            continue;
        }
        if (line.size() < 2 || line[0] != '#' || line[1] != ' ') {
            throw std::runtime_error("header line missing '# ' prefix: " + line);
        }
        const std::string kv = line.substr(2);
        const auto eq = kv.find('=');
        if (eq == std::string::npos) throw std::runtime_error("header line missing '=': " + line);
        out.header[kv.substr(0, eq)] = kv.substr(eq + 1);
    }
    return out;
}

FirResult makeLinearResult() {
    FirResult r;
    r.taps = { 0.1f, 0.5f, 1.0f, 0.5f, 0.1f };
    r.sampleRate = 48000.0;
    r.phase = FirPhase::Linear;
    r.method = FirMethod::FrequencySampling;
    r.window = WindowType::Hann;
    r.groupDelaySamples = 2;
    r.peakGainDb = 3.5;
    r.coefficientPeak = 1.0;
    r.designFftSize = 64;
    return r;
}

FirResult makeMinimumResult() {
    FirResult r = makeLinearResult();
    r.phase = FirPhase::Minimum;
    r.groupDelaySamples = 0;
    r.truncationLossDb = -137.5;
    return r;
}

}  // namespace

TEST_CASE("Every documented header key is present and parses back", "[fir_text]") {
    // T1 (first). record Sec.5's full key list for a linear-phase result.
    const auto result = makeLinearResult();
    const std::string text = renderFirText(result, Normalization::AsDesigned);
    const auto parsed = parseFirText(text);

    for (const char* key : { "sample_rate_hz", "taps", "phase", "method", "window",
                             "group_delay_samples", "normalization", "applied_trim_db",
                             "peak_gain_db", "coefficient_peak", "generator", "generated_utc" }) {
        CAPTURE(key);
        REQUIRE(parsed.header.count(key) == 1);
    }

    CHECK(std::stod(parsed.header.at("sample_rate_hz")) == result.sampleRate);
    CHECK(parsed.header.at("phase") == "linear");
    CHECK(parsed.header.at("method") == "frequency_sampling");
    CHECK(parsed.header.at("window") == "Hann");
    CHECK(std::stoull(parsed.header.at("group_delay_samples")) == result.groupDelaySamples);
    CHECK(parsed.header.at("normalization") == "as_designed");
    CHECK(std::abs(std::stod(parsed.header.at("peak_gain_db")) - result.peakGainDb) <= 1e-9);
    CHECK(std::abs(std::stod(parsed.header.at("coefficient_peak")) - result.coefficientPeak) <= 1e-9);

    // Minimum-phase-only key absent here; present for a minimum-phase result.
    CHECK(parsed.header.count("truncation_loss_db") == 0);
    const auto minText = renderFirText(makeMinimumResult(), Normalization::AsDesigned);
    const auto minParsed = parseFirText(minText);
    REQUIRE(minParsed.header.count("truncation_loss_db") == 1);
    CHECK(minParsed.header.at("phase") == "minimum");
}

TEST_CASE("sample_rate_hz in the file equals FirResult::sampleRate", "[fir_text]") {
    // T2.
    const auto result = makeLinearResult();
    const auto parsed = parseFirText(renderFirText(result, Normalization::AsDesigned));
    CHECK(std::stod(parsed.header.at("sample_rate_hz")) == result.sampleRate);
}

TEST_CASE("Coefficient count matches taps, and each round-trips within 1e-6", "[fir_text]") {
    // T3. Text is %.9g.
    const auto result = makeLinearResult();
    const auto parsed = parseFirText(renderFirText(result, Normalization::AsDesigned));

    REQUIRE(parsed.coefficients.size() == result.taps.size());
    for (std::size_t i = 0; i < result.taps.size(); ++i) {
        const double expected = static_cast<double>(result.taps[i]);
        const double residual = std::abs(parsed.coefficients[i] - expected);
        CAPTURE(i, parsed.coefficients[i], expected, residual);
        CHECK(residual <= 1e-6);
    }
}

TEST_CASE("A bare (headerless) request is refused", "[fir_text]") {
    // T4 (record Sec.5, Sec.10): the header is not optional.
    CHECK_THROWS_AS(renderFirText(makeLinearResult(), Normalization::AsDesigned, /*bare=*/true),
                    std::invalid_argument);
}

TEST_CASE("Peak0dBFS scales taps so the peak is exactly 1.0 and states the trim", "[fir_text]") {
    // T5.
    auto result = makeLinearResult();
    result.coefficientPeak = 1.0;   // matches the taps above (max|tap| == 1.0)

    const auto parsed = parseFirText(renderFirText(result, Normalization::Peak0dBFS));
    CHECK(parsed.header.at("normalization") == "peak_0dbfs");

    double peak = 0.0;
    for (double c : parsed.coefficients) peak = std::max(peak, std::abs(c));
    CHECK(std::abs(peak - 1.0) <= 1e-6);

    const double appliedTrimDb = std::stod(parsed.header.at("applied_trim_db"));
    const double expectedTrimDb = -20.0 * std::log10(result.coefficientPeak);
    CHECK(std::abs(appliedTrimDb - expectedTrimDb) <= 1e-6);

    // AsDesigned: taps unchanged, trim reported as 0.
    const auto asDesigned = parseFirText(renderFirText(result, Normalization::AsDesigned));
    CHECK(std::stod(asDesigned.header.at("applied_trim_db")) == 0.0);
    for (std::size_t i = 0; i < result.taps.size(); ++i) {
        CHECK(std::abs(asDesigned.coefficients[i] - static_cast<double>(result.taps[i])) <= 1e-6);
    }
}
