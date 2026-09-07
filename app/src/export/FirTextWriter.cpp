// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
#include "export/FirExport.h"
#include "trace/SessionCodecDetail.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <stdexcept>

namespace rta::firexport {

using rta::trace::detail::writeLine;
using rta::trace::detail::writeNumeric;

namespace {

std::string_view phaseToString(rta::dsp::FirPhase phase) {
    return phase == rta::dsp::FirPhase::Linear ? "linear" : "minimum";
}

std::string_view methodToString(rta::dsp::FirMethod method) {
    // Only FrequencySampling is implemented in v1 (designFir() itself
    // throws for LeastSquares), but the vocabulary is record Sec.5's, kept
    // complete here rather than assuming which one a caller passed.
    return method == rta::dsp::FirMethod::FrequencySampling ? "frequency_sampling" : "least_squares";
}

std::string_view normalizationToString(Normalization normalization) {
    return normalization == Normalization::AsDesigned ? "as_designed" : "peak_0dbfs";
}

std::string currentUtcTimestamp() {
    using namespace std::chrono;
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", floor<seconds>(system_clock::now()));
}

// `# key=value` -- SessionCodec's own writeLine/writeNumeric (reused
// verbatim, not reimplemented) produce the "key=value\n" body; prefixing
// "# " before each call is the whole difference from that convention.
template <typename T>
void writeHeaderNumeric(std::string& out, std::string_view key, T value) {
    out += "# ";
    writeNumeric(out, key, value);
}

void writeHeaderLine(std::string& out, std::string_view key, std::string_view value) {
    out += "# ";
    writeLine(out, key, value);
}

}  // namespace

std::string firFilenameStem(std::string_view name, double sampleRate, std::size_t taps,
                            rta::dsp::FirPhase phase) {
    // Sample rate as a whole number of Hz -- project convention (CLAUDE.md
    // "Reading out numbers": a decimal that never carries information costs
    // a character and invites false precision), and filenames are exactly
    // that kind of readout.
    char buf[256];
    const int n = std::snprintf(buf, sizeof(buf), "%s_%dHz_%zutaps_%s", std::string(name).c_str(),
                                static_cast<int>(std::lround(sampleRate)), taps,
                                phase == rta::dsp::FirPhase::Linear ? "lin" : "min");
    return std::string(buf, static_cast<std::size_t>(n));
}

std::string renderFirText(const rta::dsp::FirResult& result, Normalization normalization, bool bare,
                          std::string_view generator) {
    if (bare) {
        // record Sec.5, Sec.10: "recreates the one bug this format exists to
        // prevent" -- a bare request is refused outright, not honoured.
        throw std::invalid_argument("renderFirText: bare (headerless) export is refused");
    }

    // Shared with the WAV writer (FirExport.h) so the two formats cannot
    // silently disagree on what "Peak0dBFS" means.
    const auto [scale, appliedTrimDb] = computeNormalizationScale(result.coefficientPeak, normalization);

    std::string out;
    writeHeaderNumeric(out, "sample_rate_hz", result.sampleRate);
    writeHeaderNumeric(out, "taps", result.taps.size());
    writeHeaderLine(out, "phase", phaseToString(result.phase));
    writeHeaderLine(out, "method", methodToString(result.method));
    writeHeaderLine(out, "window", rta::dsp::toString(result.window));
    if (result.phase == rta::dsp::FirPhase::Linear) {
        writeHeaderNumeric(out, "group_delay_samples", result.groupDelaySamples);
    }
    writeHeaderLine(out, "normalization", normalizationToString(normalization));
    writeHeaderNumeric(out, "applied_trim_db", appliedTrimDb);
    writeHeaderNumeric(out, "peak_gain_db", result.peakGainDb);
    writeHeaderNumeric(out, "coefficient_peak", result.coefficientPeak);
    if (result.phase == rta::dsp::FirPhase::Minimum && result.truncationLossDb.has_value()) {
        writeHeaderNumeric(out, "truncation_loss_db", *result.truncationLossDb);
    }
    writeHeaderLine(out, "generator", generator);
    writeHeaderLine(out, "generated_utc", currentUtcTimestamp());
    out += "# --- coefficients follow ---\n";

    // %.9g, not to_chars: record Sec.5 asks for a compact, human-scannable
    // coefficient file, and float32's ~7 significant decimal digits fit
    // comfortably inside 9 -- the round-trip tolerance a reader checks
    // against (1e-6, plan F4 T3) is far looser than what 9 digits already
    // guarantees.
    char buf[32];
    for (float tap : result.taps) {
        const double value = static_cast<double>(tap) * scale;
        const int n = std::snprintf(buf, sizeof(buf), "%.9g", value);
        out.append(buf, static_cast<std::size_t>(n));
        out += '\n';
    }
    return out;
}

}  // namespace rta::firexport
