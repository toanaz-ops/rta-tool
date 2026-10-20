// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
//
// ALIGN-UI lane, task T3 (docs/plans/2026-10-20-align-ui-lane-plan.md,
// decisions 6 and 7): what the verdict SAYS. Pure text over a `Verdict`, so
// each sentence a loudspeaker technician acts on is proven in ctest, with no
// pane. The strings are UTF-8 with the non-ASCII characters (degree sign,
// plus-minus, middle dot) written as escaped bytes -- H4 shipped a mojibake
// separator from a typed one.
//
// Nothing here derives an answer the operator was asked for. In particular
// the inversion sentence (decision 7) is a statement about which of two lines
// the fitted intercept sits nearer, not a recommendation to change a setting:
// record Sec.2's last paragraph says a gap is "drift, a level change or a
// wrong setting", never "the topology was actually something else".
#pragma once

#include "measure/AlignVerdict.h"
#include "measure/CrossoverTopology.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rta::measure {

/// "MAIN" or "SUB".
[[nodiscard]] const char* sourceName(Source source) noexcept;

/// "LR-4", "BW-2": the same spelling the topology picker shows.
[[nodiscard]] std::string topologyName(Topology topology);

/// The integer resolves the delay ambiguity (decision 6): the fit returns
/// several competing delays and tau* is only the one with the highest
/// agreement. The candidate nearest `cycleSamples / fs` is the delay the
/// operator's coarse offset points at. With no integer, tau* stands and
/// `resolvedByInteger` is false.
struct CycleResolution {
    double tauSeconds = 0.0;
    bool resolvedByInteger = false;
};
[[nodiscard]] CycleResolution resolveCycle(const Verdict& verdict,
                                           std::optional<std::ptrdiff_t> cycleSamples,
                                           double sampleRate);

/// "delay MAIN by 2.00 ms (96 samples)". Positive tau means the low-pass side
/// arrives LATER, so the high-pass side is the earlier source and is the one
/// delayed; negative tau delays the other side. Zero says nothing is needed.
[[nodiscard]] std::string delayInstruction(double tauSeconds, Source highPass, double sampleRate);

/// Decision 7: non-empty only when |delta| > 90 degrees, i.e. the intercept is
/// nearer the line an inverted output would draw. Empty for an ambiguous
/// question (c), where the readout shows both deltas instead.
[[nodiscard]] std::string inversionSentence(const Verdict& verdict);

struct AlignReadoutInput {
    const Verdict* verdict = nullptr;
    Source highPass = Source::Main;
    Topology topology{};
    ProcessorInversion inversion = ProcessorInversion::No;
    double sampleRate = 48000.0;
    std::optional<std::ptrdiff_t> cycleSamples;
    /// The bar the wizard was configured with, so the readout prints the one in force.
    double matchedPairBar = 0.5;
};

/// The verdict as lines of text, in reading order: crossover, the delay to
/// dial in (or the candidates still to choose between), R against its bar,
/// delta against the named topology, the inversion sentence if any.
[[nodiscard]] std::vector<std::string> verdictLines(const AlignReadoutInput& input);

}  // namespace rta::measure
