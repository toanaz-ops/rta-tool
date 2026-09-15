// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
//
// L7-EQ task E (decision record docs/dsp/2026-09-06-l7-auto-eq.md sec.7:
// "Export of the FilterSpec list as text is app/"). Content only -- writing
// to disk is the caller's job, exactly as FirExport.h splits it, so the
// format stays unit-testable with no filesystem.
#pragma once

#include "rta/eq/FilterSpec.h"

#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rta::eqexport {

/// The wire names. Lower case and one word so a line splits on whitespace
/// alone, and so a human retyping a filter list by hand cannot get the
/// capitalisation wrong.
[[nodiscard]] inline std::string_view typeName(rta::eq::FilterType type) {
    switch (type) {
        case rta::eq::FilterType::LowShelf: return "lowshelf";
        case rta::eq::FilterType::HighShelf: return "highshelf";
        case rta::eq::FilterType::Peaking: break;
    }
    return "peaking";
}

/// Unknown text reads back as Peaking: the export's own vocabulary is closed
/// (FilterSpec has three members), so a foreign word is a typo in a
/// hand-edited file, and the least surprising recovery is the type every
/// allocator pass actually places.
[[nodiscard]] inline rta::eq::FilterType typeFromName(std::string_view name) {
    if (name == "lowshelf") return rta::eq::FilterType::LowShelf;
    if (name == "highshelf") return rta::eq::FilterType::HighShelf;
    return rta::eq::FilterType::Peaking;
}

/// One filter per line: `type fc q gain`, at the project's own readout
/// precision (CLAUDE.md "Reading out numbers") -- frequency a WHOLE number of
/// hertz, dB one decimal, Q two. The written precision is deliberately the
/// round-trip precision: a format that printed more digits than the readouts
/// show would promise a precision no operator ever saw or typed.
[[nodiscard]] inline std::string renderFilterList(const std::vector<rta::eq::FilterSpec>& specs,
                                                  double sampleRate) {
    char line[128];
    std::string out;
    std::snprintf(line, sizeof line, "# rta-eq filter list v1\n# sample_rate_hz=%.0f\n",
                  sampleRate);
    out += line;
    out += "# type fc_hz q gain_db\n";
    for (const auto& spec : specs) {
        std::snprintf(line, sizeof line, "%s %.0f %.2f %.1f\n", typeName(spec.type).data(),
                      spec.fcHz, spec.q, spec.gainDb);
        out += line;
    }
    return out;
}

/// The inverse. `#` comments and blank lines are skipped -- SessionCodec's
/// own line convention, reused rather than reinvented. A malformed line is
/// skipped too: a hand-edited file with one bad row still imports the rows
/// that are good, which is what an operator in front of a rig needs.
[[nodiscard]] inline std::vector<rta::eq::FilterSpec> parseFilterList(std::string_view text) {
    std::vector<rta::eq::FilterSpec> specs;
    std::istringstream stream{ std::string(text) };
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream fields{ line };
        std::string name;
        rta::eq::FilterSpec spec;
        if (!(fields >> name >> spec.fcHz >> spec.q >> spec.gainDb)) continue;
        spec.type = typeFromName(name);
        specs.push_back(spec);
    }
    return specs;
}

}  // namespace rta::eqexport
