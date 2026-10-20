// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/AlignReadout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace rta::measure {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kRadiansToDegrees = 180.0 / kPi;

// UTF-8, escaped. Each is followed by a space or the end of its literal,
// never by a hex digit: "\xb0a" would parse as the single escape \xb0a.
constexpr const char* kDegree = "\xc2\xb0";
constexpr const char* kPlusMinus = "\xc2\xb1";
constexpr const char* kMiddleDot = "\xc2\xb7";

std::string format(const char* pattern, double a) {
    char buffer[160];
    std::snprintf(buffer, sizeof buffer, pattern, a);
    return buffer;
}

double toMilliseconds(double seconds) { return seconds * 1000.0; }

double toDegrees(double radians) { return radians * kRadiansToDegrees; }

std::string signedDegrees(double radians) { return format("%+.0f", toDegrees(radians)) + kDegree; }

}  // namespace

const char* sourceName(Source source) noexcept { return source == Source::Main ? "MAIN" : "SUB"; }

std::string topologyName(Topology topology) {
    return std::string(topology.family == CrossoverFamily::LinkwitzRiley ? "LR-" : "BW-") +
           std::to_string(topology.order);
}

CycleResolution resolveCycle(const Verdict& verdict, std::optional<std::ptrdiff_t> cycleSamples,
                             double sampleRate) {
    CycleResolution result;
    result.tauSeconds = verdict.tauSeconds;
    if (!cycleSamples.has_value() || !(sampleRate > 0.0) || verdict.candidateTausSeconds.empty()) {
        return result;
    }
    const double target = static_cast<double>(*cycleSamples) / sampleRate;
    double best = verdict.candidateTausSeconds.front();
    for (const double candidate : verdict.candidateTausSeconds) {
        if (std::abs(candidate - target) < std::abs(best - target)) best = candidate;
    }
    result.tauSeconds = best;
    result.resolvedByInteger = true;
    return result;
}

std::string delayInstruction(double tauSeconds, Source highPass, double sampleRate) {
    const long samples = std::lround(std::abs(tauSeconds) * sampleRate);
    if (samples == 0) return "no delay needed between MAIN and SUB";
    // tau > 0: the low-pass side arrives later, so the high-pass side is the
    // earlier source and is delayed. tau < 0: the other way round.
    const Source other = highPass == Source::Main ? Source::Sub : Source::Main;
    const Source delayed = tauSeconds > 0.0 ? highPass : other;
    char buffer[160];
    std::snprintf(buffer, sizeof buffer, "delay %s by %.2f ms (%ld samples)", sourceName(delayed),
                  toMilliseconds(std::abs(tauSeconds)), samples);
    return buffer;
}

std::string inversionSentence(const Verdict& verdict) {
    if (verdict.expectedAmbiguous) return "";
    if (std::abs(verdict.deltaRadians) <= kPi / 2.0) return "";
    return "the measured phase sits nearer the line for the OPPOSITE polarity to the one you named "
           "-- a statement about the measurement, not a setting change: check whether one output "
           "is inverted";
}

std::vector<std::string> verdictLines(const AlignReadoutInput& input) {
    std::vector<std::string> lines;
    if (input.verdict == nullptr) return lines;
    const Verdict& v = *input.verdict;

    std::string crossover = format("crossover %.0f Hz", v.crossoverHz);
    if (v.crossingsHz.size() > 1) {
        crossover += " " + std::string(kMiddleDot) + " " + std::to_string(v.crossingsHz.size()) +
                     " crossings seen (a seed picks one)";
    }
    lines.push_back(std::move(crossover));

    if (!input.cycleSamples.has_value()) {
        std::string candidates;
        for (const double tau : v.candidateTausSeconds) {
            if (!candidates.empty()) candidates += ", ";
            candidates += format("%+.2f", toMilliseconds(tau));
        }
        lines.push_back(format("fit tau* %+.2f ms", toMilliseconds(v.tauSeconds)) +
                        (candidates.empty() ? "" : "; candidates (ms): " + candidates) +
                        " -- type the coarse offset to choose");
        if (input.seedDisagreement.has_value() && input.sampleRate > 0.0) {
            const SeedDisagreement& s = *input.seedDisagreement;
            const auto ms = [&](std::ptrdiff_t samples) {
                return 1000.0 * static_cast<double>(samples) / input.sampleRate;
            };
            char text[200];
            std::snprintf(text, sizeof text,
                          "seed and measured crossing disagree (%.0f Hz vs %.0f Hz); the IRs did not choose",
                          s.seedHz, s.crossingHz);
            lines.push_back(text);
            std::snprintf(text, sizeof text,
                          "IRs suggest %+.2f ms with the lag at the seed, %+.2f ms at the crossing; type the offset",
                          ms(s.hintAtSeedSamples), ms(s.hintAtCrossingSamples));
            lines.push_back(text);
        } else if (input.irHintSamples.has_value() && input.sampleRate > 0.0) {
            lines.push_back(format("IRs suggest about %+.2f ms but fit no candidate clearly",
                                   1000.0 * static_cast<double>(*input.irHintSamples) / input.sampleRate) +
                            "; type the offset");
        }
    } else {
        const CycleResolution resolved = resolveCycle(v, input.cycleSamples, input.sampleRate);
        std::string line = delayInstruction(resolved.tauSeconds, input.highPass, input.sampleRate);
        if (resolved.resolvedByInteger && std::abs(resolved.tauSeconds - v.tauSeconds) > 1.0e-9) {
            line += " " + std::string(kMiddleDot) + format(" fit tau* alone was %+.2f ms; ", toMilliseconds(v.tauSeconds)) +
                    (input.cycleFromIr ? "the bound IRs chose another candidate" : "your offset chose another candidate");
        }
        lines.push_back(std::move(line));
    }

    std::string fit = format("R %.2f", v.agreement) + " " + kMiddleDot + " fit window " + kPlusMinus +
                      format("%.0f", rta::dsp::BandFitOptions{}.octavesEachSide) + " oct, R bar " + format("%.1f", input.matchedPairBar) +
                      ": unmeasured defaults";
    lines.push_back(std::move(fit));
    if (!v.matchedPair) {
        lines.push_back("R is below the bar: this band is not one delay plus one constant -- not a "
                        "matched pair");
    }

    const std::string topology = topologyName(input.topology);
    if (v.expectedAmbiguous) {
        lines.push_back("delta vs " + topology + " NOT INVERTED " + signedDegrees(v.deltaRadians) + " " +
                        kMiddleDot + " vs INVERTED " + signedDegrees(v.alternativeDeltaRadians));
    } else {
        lines.push_back("delta vs " + topology + (input.inversion == ProcessorInversion::Yes ? " INVERTED " : " NOT INVERTED ") +
                        signedDegrees(v.deltaRadians));
    }
    const std::string inversion = inversionSentence(v);
    if (!inversion.empty()) lines.push_back(inversion);
    return lines;
}

}  // namespace rta::measure
