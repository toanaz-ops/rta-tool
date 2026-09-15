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

#include <cstddef>
#include <cstdio>
#include <optional>
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

/// `nullopt` for anything outside the closed vocabulary. There is deliberately
/// NO fallback type: a word this format cannot represent must not import as
/// some other filter. The fallback that used to live here read an 80 Hz
/// `lowshelf +2 dB` as an 80 Hz PEAKING `+2 dB` whenever a byte-order mark
/// was glued to the word -- which Notepad and PowerShell 5.1's `Out-File` /
/// `Set-Content` add to every file they write, so on Windows it was the
/// default case, not the typo case. A different filter, landed on the rig,
/// that nobody was told about.
[[nodiscard]] inline std::optional<rta::eq::FilterType> typeFromName(std::string_view name) {
    if (name == "peaking") return rta::eq::FilterType::Peaking;
    if (name == "lowshelf") return rta::eq::FilterType::LowShelf;
    if (name == "highshelf") return rta::eq::FilterType::HighShelf;
    return std::nullopt;
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

/// The rows that imported, and the rows that did not. A rejected row the
/// caller cannot see is a row the operator silently loses
/// (memory/a-placeholder-for-an-absent-result-erases-its-state.md), so the
/// refusals are named rather than dropped.
struct FilterListParse {
    std::vector<ExportedFilter> filters;
    /// 1-based line numbers, counting every line of the input including
    /// comments and blanks, so the number matches what an editor shows.
    std::vector<std::size_t> rejectedLines;
};

/// The UTF-8 byte-order mark, which Notepad and PowerShell 5.1 put at the
/// start of every file they write.
inline constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

/// The inverse of renderFilterList. Blank lines and `#` comments are skipped
/// whatever they are indented by -- SessionCodec's own line convention, reused
/// rather than reinvented. Everything else REFUSES rather than guesses: a row
/// that does not parse, whose type word is outside the vocabulary, or whose
/// fifth column is present but unreadable, is REJECTED and its line number
/// reported. A hand-edited file with one bad row still imports the rows that
/// are good, which is what an operator in front of a rig needs, but nothing is
/// quietly reshaped into a different filter or a different applied state.
///
/// A leading UTF-8 BOM is stripped before anything else looks at the text.
/// `'\r'` needs no handling inside a row: it is whitespace to `operator>>`, so
/// CRLF files parse as they stand.
///
/// **The one place a MISSING value is filled in rather than refused** is the
/// fifth column: a row with no flag at all reads as NOT applied. That is not a
/// guess about what someone meant, it is the format's own older four-column
/// shape, and the fill-in is the safe direction -- the filter surfaces as one
/// the operator can see and decide about, rather than being hidden from them
/// as already-handled. A flag that is present but unreadable gets no such
/// benefit of the doubt (see the body).
[[nodiscard]] inline FilterListParse parseFilterList(std::string_view text) {
    if (text.size() >= kUtf8Bom.size() && text.substr(0, kUtf8Bom.size()) == kUtf8Bom) {
        text.remove_prefix(kUtf8Bom.size());
    }

    FilterListParse parse;
    std::istringstream stream{ std::string(text) };
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;

        // Find the first thing that is not whitespace BEFORE deciding what
        // kind of line this is. Testing only `line.front()` made three
        // spaces, a tab, a CR, or a comment a human had lined up by hand look
        // like a malformed row -- and a false rejection is not harmless: a
        // rejected line is an alarm, and an alarm that cries wolf is how the
        // next real refusal gets ignored.
        const std::size_t firstGlyph = line.find_first_not_of(" \t\r\n\v\f");
        if (firstGlyph == std::string::npos) continue;  // blank, however spelled
        if (line[firstGlyph] == '#') continue;          // comment, however indented

        std::istringstream fields{ line };
        std::string name;
        ExportedFilter filter;
        if (!(fields >> name >> filter.spec.fcHz >> filter.spec.q >> filter.spec.gainDb)) {
            parse.rejectedLines.push_back(lineNumber);
            continue;
        }
        const auto type = typeFromName(name);
        if (!type.has_value()) {
            parse.rejectedLines.push_back(lineNumber);
            continue;
        }
        filter.spec.type = *type;

        // The fifth column refuses on the same terms the type word does. A
        // MISSING column still reads as not-applied -- that is a four-column
        // file, the format's own older shape. A column that is PRESENT and
        // unreadable is different: somebody meant something this build cannot
        // interpret, and guessing "not applied" is the dangerous guess. A
        // misspelled `appllied` would import a filter the rig already has,
        // the operator would land it again, and the correction would arrive
        // twice -- the -14.2 dB arithmetic this column exists to prevent.
        // Anything after the flag is refused for the same reason: a token
        // nobody can interpret is not a token to ignore.
        std::string flag;
        if (fields >> flag) {
            std::string trailing;
            if (flag != kAppliedToken || (fields >> trailing)) {
                parse.rejectedLines.push_back(lineNumber);
                continue;
            }
            filter.applied = true;
        }
        parse.filters.push_back(filter);
    }
    return parse;
}

}  // namespace rta::eqexport
