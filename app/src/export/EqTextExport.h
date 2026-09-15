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

/// One row of the file: the filter, and whether it is ALREADY IN the signal
/// path the session's measurement came through.
///
/// The flag is not decoration. `rta::measure::CommittedFilter` carries the
/// same bit, and dropping it here would make the exported list unsafe to load
/// back into the very DSP that produced the measurement: an applied filter
/// re-applied lands its correction twice (a -7.1 dB cut becomes -14.2 dB on
/// an 8 dB bump). Deliberately a separate type from `CommittedFilter` so
/// neither layer has to include the other's header -- the caller does the
/// two-line copy, which is what a UI does anyway.
struct ExportedFilter {
    rta::eq::FilterSpec spec{};
    bool applied = false;
};

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

/// The word that marks a row as already in the rig. A whole word, not a `1`,
/// so a person reading the file in a text editor at a show can see what it
/// means without a legend.
inline constexpr std::string_view kAppliedToken = "applied";

/// One filter per line: `type fc q gain [applied]`, at the project's own
/// readout precision (CLAUDE.md "Reading out numbers") -- frequency a WHOLE
/// number of hertz, dB one decimal, Q two. The written precision is
/// deliberately the round-trip precision: a format that printed more digits
/// than the readouts show would promise a precision no operator ever saw or
/// typed.
///
/// The fifth column is OPTIONAL on read (see parseFilterList) and written
/// only for applied rows, so a file stays as short as it can be and a list
/// written before this column existed still imports.
[[nodiscard]] inline std::string renderFilterList(const std::vector<ExportedFilter>& filters,
                                                  double sampleRate) {
    char line[160];
    std::string out;
    std::snprintf(line, sizeof line, "# rta-eq filter list v1\n# sample_rate_hz=%.0f\n",
                  sampleRate);
    out += line;
    out += "# type fc_hz q gain_db [applied]\n";
    out += "# 'applied' = already in the signal path this measurement came\n";
    out += "# through. Do NOT load such a row back into that same processor.\n";
    for (const auto& filter : filters) {
        std::snprintf(line, sizeof line, "%s %.0f %.2f %.1f%s%s\n",
                      typeName(filter.spec.type).data(), filter.spec.fcHz, filter.spec.q,
                      filter.spec.gainDb, filter.applied ? " " : "",
                      filter.applied ? kAppliedToken.data() : "");
        out += line;
    }
    return out;
}

/// Convenience for a set nothing has been dialled into the rig from yet --
/// every row exports as not-applied.
[[nodiscard]] inline std::string renderFilterList(const std::vector<rta::eq::FilterSpec>& specs,
                                                  double sampleRate) {
    std::vector<ExportedFilter> filters;
    filters.reserve(specs.size());
    for (const auto& spec : specs) filters.push_back(ExportedFilter{ spec, false });
    return renderFilterList(filters, sampleRate);
}

/// The inverse. `#` comments and blank lines are skipped -- SessionCodec's
/// own line convention, reused rather than reinvented. A malformed line is
/// skipped too: a hand-edited file with one bad row still imports the rows
/// that are good, which is what an operator in front of a rig needs.
///
/// A row with no fifth column reads as NOT applied, which is both the
/// backward-compatible reading and the safe one: treating an unknown row as
/// already-in-the-rig would silently drop a correction the operator asked for,
/// whereas treating it as not-yet-applied surfaces as a filter they can see.
[[nodiscard]] inline std::vector<ExportedFilter> parseFilterList(std::string_view text) {
    std::vector<ExportedFilter> filters;
    std::istringstream stream{ std::string(text) };
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream fields{ line };
        std::string name;
        ExportedFilter filter;
        if (!(fields >> name >> filter.spec.fcHz >> filter.spec.q >> filter.spec.gainDb)) {
            continue;
        }
        filter.spec.type = typeFromName(name);
        std::string flag;
        if (fields >> flag) filter.applied = (flag == kAppliedToken);
        filters.push_back(filter);
    }
    return filters;
}

}  // namespace rta::eqexport
