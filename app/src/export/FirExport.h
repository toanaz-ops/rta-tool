// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest (task F4). FIR taps go through
// rta::dsp::designFir (core/) -- this module only renders/writes them.
#pragma once

#include "rta/dsp/FirDesign.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

namespace rta::firexport {

/// `as_designed`: the coefficients realise the correction at the gain it was
/// designed at (record Sec.5's default). `peak_0dbfs`: taps are scaled so
/// max|tap| == 1.0, and the header states the trim that undoes it -- a
/// STATED choice with its number attached, never REW's silent
/// peak-normalise checkbox (record Sec.5's "argument against the obvious").
enum class Normalization { AsDesigned, Peak0dBFS };

/// The scale factor and its dB equivalent a normalization choice applies to
/// a copy of the designed taps -- shared arithmetic between the text writer
/// (F4) and the WAV writer (F5, app/src/export/FirWavWriter.cpp), so the two
/// formats cannot silently disagree on what "Peak0dBFS" means.
struct NormalizationScale {
    double scale = 1.0;          ///< multiply every tap by this
    double appliedTrimDb = 0.0;  ///< == 20*log10(scale); reported in both writers' headers
};

/// Peak0dBFS: scale = 1/coefficientPeak (so max|tap*scale| == 1.0), trim
/// == -20*log10(coefficientPeak) -- a boost (peak > 1) needs a NEGATIVE trim
/// to reach unity, which falls out of the sign automatically.
/// AsDesigned, or a non-positive coefficientPeak (nothing to scale against):
/// identity.
[[nodiscard]] inline NormalizationScale computeNormalizationScale(double coefficientPeak,
                                                                   Normalization normalization) {
    if (normalization != Normalization::Peak0dBFS || coefficientPeak <= 0.0) {
        return NormalizationScale{};
    }
    const double scale = 1.0 / coefficientPeak;
    return NormalizationScale{ scale, 20.0 * std::log10(scale) };
}

/// `<name>_<fs>Hz_<N>taps_<lin|min>` (no extension) -- record Sec.5's
/// filename contract, shared by the text (F4) and WAV (F5) writers so the
/// convention exists in exactly one place. `sampleRate` renders as a whole
/// number of Hz (project convention, docs/HANDOFF's "reading out numbers").
[[nodiscard]] std::string firFilenameStem(std::string_view name, double sampleRate,
                                          std::size_t taps, rta::dsp::FirPhase phase);

/// Renders the full text-file CONTENT (no filesystem access -- writing to
/// disk is the caller's job, kept separate so this stays unit-testable):
/// a `#`-prefixed key=value header, SessionCodec's own line convention
/// (app/src/trace/SessionCodecDetail.h, reused verbatim), ending
/// `# --- coefficients follow ---`, then one coefficient per line at
/// `%.9g` (record Sec.5). `normalization` is applied to a COPY of
/// `result.taps`; `result` itself is never mutated.
///
/// @throws std::invalid_argument if `bare` is true: the header is not
///         optional (record Sec.5, Sec.10) -- this is what a `--bare`
///         request meets, not a way to actually get headerless output.
[[nodiscard]] std::string renderFirText(const rta::dsp::FirResult& result,
                                        Normalization normalization, bool bare = false,
                                        std::string_view generator = "rtatool");

}  // namespace rta::firexport
