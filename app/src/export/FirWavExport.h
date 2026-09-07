// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. JUCE-USING (juce::File, WavAudioFormat)
// on purpose -- unlike FirExport.h, this header is NOT in
// measure_has_no_framework_deps's GLOBS list (task F5). The core FIR
// design math stays framework-free in rta_core; only the file write here
// touches JUCE, the same split app/src/trace/SessionStore.h draws between
// JUCE-free encoding and (elsewhere) filesystem I/O.
#pragma once

#include "export/FirExport.h"
#include "rta/dsp/FirDesign.h"

#include <juce_core/juce_core.h>

namespace rta::firexport {

/// v1 writes 32-bit IEEE float only (record docs/dsp/2026-09-06-l7-fir-export.md
/// Sec.5: "Never an integer WAV: a boost puts |h[n]| > 1, which float
/// carries and int clips"). The enum exists so a caller CAN ask for
/// something else and be refused loudly (plan F5 T2), not so there is a
/// second code path.
enum class WavSampleFormat { Float32, Int16 };

/// Writes mono, 32-bit IEEE float, sample rate = result.sampleRate in the
/// header (record Sec.5). `normalization` is applied the same way
/// renderFirText's is (a copy of result.taps, result itself untouched).
///
/// @throws std::invalid_argument if `format` is not Float32, or if the file
///         cannot be opened for writing.
void writeFirWav(const juce::File& file, const rta::dsp::FirResult& result,
                 Normalization normalization, WavSampleFormat format = WavSampleFormat::Float32);

}  // namespace rta::firexport
