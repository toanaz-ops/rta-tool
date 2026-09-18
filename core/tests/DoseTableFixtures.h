// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- core test support. No JUCE, no Qt, no audio-device API.
#pragma once

// THE REGULATORS' OWN TABLES, TRANSCRIBED AS DATA.
//
// Provenance, because a number without it is a claim and not evidence:
//
//   NIOSH Table 1-1 and Table 1-2
//     DHHS (NIOSH) Publication No. 98-126, "Criteria for a Recommended
//     Standard: Occupational Noise Exposure, Revised Criteria 1998".
//     Table 1-1 "Combinations of noise exposure levels and durations that no
//     worker exposure shall equal or exceed" -- printed page 2, PDF page 20.
//     Table 1-2 "Daily noise dose as an 8-hr TWA" -- printed page 3, PDF
//     page 21, with its printed footnote "*TWA = 10 x Log(D/100) + 85".
//     Read 2026-09-18 from
//     web.archive.org/web/2020/https://www.cdc.gov/niosh/docs/98-126/pdfs/98-126.pdf
//     -- 126 pages, BORN DIGITAL (/Author NIOSH, /Creator Adobe InDesign CC
//     2014, /Producer Adobe PDF Library 11.0), so this is the document's own
//     text layer and not an image extraction. Every cdc.gov path for the PDF
//     now returns 404; the DOI 10.26616/NIOSHPUB98126 redirects to a landing
//     page with no text.
//     Ten rows cross-check against the independently verified spot values in
//     docs/research/2026-09-16-l6a-spl-pro-station1-research.md section A4.2
//     (80, 85, 95, 99, 100, 120, 124, 127, 129 and the 130-140 row); all ten
//     agree.
//
//   OSHA Table G-16a
//     29 CFR 1910.95 Appendix A ("Noise Exposure Computation", mandatory),
//     "Reference duration, T (hour)" against "A-weighted sound level, L".
//     51 rows, 1 dB steps, 80 through 130. Read 2026-09-18 from
//     law.cornell.edu/cfr/text/29/1910.95 -- osha.gov returned HTTP 403 and
//     ecfr.gov bot-blocked. Three rows cross-check against the research
//     file's own section A4.1 values (80 -> 32, 81 -> 27.9, 130 -> 0.031); all
//     three agree. Footnote formula: T = 8 / 2^((L - 90)/5).
//
// The durations are stored EXACTLY AS PRINTED, including the printing's own
// inconsistent significant figures (G-16a prints 32 and 16 and 8 with no
// decimal, 27.9 and 7.0 and 3.0 with one, and 0.125 with three while its
// neighbours 0.14 and 0.11 have two). That is load-bearing: the acceptance
// bound for each row is one unit in THAT ROW'S last printed place, so
// normalising the strings would change the bound.

#include <array>
#include <cstddef>
#include <cstdlib>
#include <string_view>

namespace rta::testing {

/// The printed en dash in a Table 1-1 Hours / Minutes / Seconds cell: the unit
/// is unused, which is zero of it and not "unknown".
inline constexpr int kDash = -1;

struct Table11Row {
    int levelDb;
    int hours;
    int minutes;
    int seconds;
};

/// NIOSH 98-126 Table 1-1, the 50 single-level rows. The 51st printed row is
/// "130-140 -> < 1 sec", which is a range and an inequality rather than a
/// (level, duration) pair, so it is not in this array; test_dose_tables.cpp
/// asserts it separately.
inline constexpr std::array<Table11Row, 50> kNioshTable11{{
    {80, 25, 24, kDash},   {81, 20, 10, kDash},   {82, 16, kDash, kDash},
    {83, 12, 42, kDash},   {84, 10, 5, kDash},    {85, 8, kDash, kDash},
    {86, 6, 21, kDash},    {87, 5, 2, kDash},     {88, 4, kDash, kDash},
    {89, 3, 10, kDash},    {90, 2, 31, kDash},    {91, 2, kDash, kDash},
    {92, 1, 35, kDash},    {93, 1, 16, kDash},    {94, 1, kDash, kDash},
    {95, kDash, 47, 37},   {96, kDash, 37, 48},   {97, kDash, 30, kDash},
    {98, kDash, 23, 49},   {99, kDash, 18, 59},   {100, kDash, 15, kDash},
    {101, kDash, 11, 54},  {102, kDash, 9, 27},   {103, kDash, 7, 30},
    {104, kDash, 5, 57},   {105, kDash, 4, 43},   {106, kDash, 3, 45},
    {107, kDash, 2, 59},   {108, kDash, 2, 22},   {109, kDash, 1, 53},
    {110, kDash, 1, 29},   {111, kDash, 1, 11},   {112, kDash, kDash, 56},
    {113, kDash, kDash, 45}, {114, kDash, kDash, 35}, {115, kDash, kDash, 28},
    {116, kDash, kDash, 22}, {117, kDash, kDash, 18}, {118, kDash, kDash, 14},
    {119, kDash, kDash, 11}, {120, kDash, kDash, 9}, {121, kDash, kDash, 7},
    {122, kDash, kDash, 6},  {123, kDash, kDash, 4}, {124, kDash, kDash, 3},
    {125, kDash, kDash, 3},  {126, kDash, kDash, 2}, {127, kDash, kDash, 1},
    {128, kDash, kDash, 1},  {129, kDash, kDash, 1},
}};

/// The duration a Table 1-1 row PRINTS, in seconds. An en dash is zero of that
/// unit.
[[nodiscard]] inline double table11PrintedSeconds(const Table11Row& row) noexcept {
    const double h = row.hours == kDash ? 0.0 : static_cast<double>(row.hours);
    const double m = row.minutes == kDash ? 0.0 : static_cast<double>(row.minutes);
    const double s = row.seconds == kDash ? 0.0 : static_cast<double>(row.seconds);
    return h * 3600.0 + m * 60.0 + s;
}

/// That row's own printed resolution, in seconds: **one minute where an Hours
/// cell carries a number, one second otherwise.** The document has NO footnote
/// and nowhere says the values are rounded, so the rule is read off the
/// printing itself -- which is the whole reason the acceptance bound is
/// per-row rather than one constant.
///
/// WHY THIS KEY AND NOT THE SEconds CELL, because the two differ on two rows
/// and PR #20's verifier was right to ask. Table 1-1 prints in two FORMATS:
/// hours-and-minutes above 1 h, minutes-and-seconds below it. An en dash means
/// "zero of this unit", and on a minutes-and-seconds row that is a true
/// statement to the second -- rows 97 (`- 30 -`) and 100 (`- 15 -`) are
/// EXACTLY 1800 s and 900 s, asserted by `test_dose_tables.cpp` D2b3. On an
/// hours-and-minutes row it is not: 80 dBA prints `25 24 -` where the exact
/// value carries 54.3 seconds. So the seconds column is informative on one
/// format and not on the other, and the format is what the Hours cell tells
/// you.
///
/// It also matters, once: the record's own prose says "the row's smallest
/// printed unit", which read literally gives r = 60 s at 100 dBA and a bound
/// of 6.6667 % -- contradicting the record's OWN printed 0.1111 % for that row
/// and making D2e's rejection of `q = 10` at 100 dBA impossible. The plan's
/// phrasing ("1 min where an Hours cell is printed and 1 s where the row is
/// printed in seconds") is unambiguous, agrees with both documents' four
/// quoted bounds, and is what ships. Record amendment A7.
[[nodiscard]] inline double table11ResolutionSeconds(const Table11Row& row) noexcept {
    return row.hours == kDash ? 1.0 : 60.0;
}

/// The smallest unit the row actually PRINTS -- a different question, and the
/// one D2d needs: it asks which unit the value was rounded TO, not how far the
/// printed value may sit from the exact one.
[[nodiscard]] inline double table11PrintedUnitSeconds(const Table11Row& row) noexcept {
    return row.seconds == kDash ? 60.0 : 1.0;
}

struct G16aRow {
    int levelDb;
    std::string_view printedHours;  ///< exactly as the CFR prints it
};

/// 29 CFR 1910.95 App. A Table G-16a, all 51 rows.
inline constexpr std::array<G16aRow, 51> kOshaTableG16a{{
    {80, "32"},    {81, "27.9"},  {82, "24.3"},  {83, "21.1"},  {84, "18.4"},
    {85, "16"},    {86, "13.9"},  {87, "12.1"},  {88, "10.6"},  {89, "9.2"},
    {90, "8"},     {91, "7.0"},   {92, "6.1"},   {93, "5.3"},   {94, "4.6"},
    {95, "4"},     {96, "3.5"},   {97, "3.0"},   {98, "2.6"},   {99, "2.3"},
    {100, "2"},    {101, "1.7"},  {102, "1.5"},  {103, "1.3"},  {104, "1.1"},
    {105, "1"},    {106, "0.87"}, {107, "0.76"}, {108, "0.66"}, {109, "0.57"},
    {110, "0.5"},  {111, "0.44"}, {112, "0.38"}, {113, "0.33"}, {114, "0.29"},
    {115, "0.25"}, {116, "0.22"}, {117, "0.19"}, {118, "0.16"}, {119, "0.14"},
    {120, "0.125"}, {121, "0.11"}, {122, "0.095"}, {123, "0.082"}, {124, "0.072"},
    {125, "0.063"}, {126, "0.054"}, {127, "0.047"}, {128, "0.041"}, {129, "0.036"},
    {130, "0.031"},
}};

/// The printed duration, parsed from the SAME string the resolution is read
/// from. One transcription, so the value and its resolution cannot disagree.
[[nodiscard]] inline double g16aPrintedHours(const G16aRow& row) {
    return std::strtod(std::string(row.printedHours).c_str(), nullptr);
}

/// One unit in that row's last printed decimal place: 1 h at `80 -> 32`,
/// 0.1 h at `81 -> 27.9`, 0.001 h at `120 -> 0.125` and `130 -> 0.031`.
[[nodiscard]] inline double g16aResolutionHours(const G16aRow& row) noexcept {
    const std::size_t dot = row.printedHours.find('.');
    if (dot == std::string_view::npos) return 1.0;
    const std::size_t decimals = row.printedHours.size() - dot - 1;
    double r = 1.0;
    for (std::size_t i = 0; i < decimals; ++i) r *= 0.1;
    return r;
}

struct Table12Row {
    long long dosePercent;
    double printedTwaDb;
};

/// NIOSH 98-126 Table 1-2, all 121 rows, printed page 3.
inline constexpr std::array<Table12Row, 121> kNioshTable12{{
    {20, 78.0},      {30, 79.8},      {40, 81.0},      {50, 82.0},
    {60, 82.8},      {70, 83.5},      {80, 84.0},      {90, 84.5},
    {100, 85.0},     {110, 85.4},     {120, 85.8},     {130, 86.1},
    {140, 86.5},     {150, 86.8},     {170, 87.3},     {200, 88.0},
    {250, 89.0},     {300, 89.8},     {350, 90.4},     {400, 91.0},
    {450, 91.5},     {500, 92.0},     {550, 92.4},     {600, 92.8},
    {650, 93.1},     {700, 93.5},     {750, 93.8},     {800, 94.0},
    {900, 94.5},     {1000, 95.0},    {1050, 95.2},    {1100, 95.4},
    {1150, 95.6},    {1200, 95.8},    {1300, 96.1},    {1400, 96.5},
    {1500, 96.8},    {1600, 97.0},    {1700, 97.3},    {1800, 97.6},
    {1900, 97.8},    {2000, 98.0},    {2500, 99.0},    {3000, 99.8},
    {3500, 100.4},   {4000, 101.0},   {4500, 101.5},   {5000, 102.0},
    {6000, 102.8},   {7000, 103.5},   {8000, 104.0},   {9000, 104.5},
    {10000, 105.0},  {12000, 105.8},  {14000, 106.5},  {16000, 107.0},
    {18000, 107.6},  {20000, 108.0},  {25000, 109.0},  {30000, 109.8},
    {35000, 110.4},  {40000, 111.0},  {45000, 111.5},  {50000, 102.0},
    {60000, 112.8},  {70000, 113.5},  {80000, 114.0},  {90000, 114.5},
    {100000, 115.0}, {110000, 115.4}, {120000, 115.8}, {130000, 116.1},
    {140000, 116.5}, {150000, 116.8}, {175000, 117.4}, {200000, 118.0},
    {225000, 118.5}, {250000, 119.0}, {275000, 119.4}, {300000, 119.8},
    {350000, 120.4}, {400000, 121.0}, {450000, 121.5}, {500000, 122.0},
    {600000, 122.8}, {700000, 123.5}, {800000, 124.0}, {900000, 124.5},
    {1000000, 125.0},  {1100000, 125.4},  {1200000, 125.8},  {1300000, 126.1},
    {1400000, 126.5},  {1600000, 127.0},  {1800000, 127.6},  {2000000, 128.0},
    {2200000, 128.4},  {2400000, 128.8},  {2600000, 129.1},  {2800000, 129.5},
    {3000000, 129.8},  {3500000, 130.4},  {4000000, 131.0},  {4500000, 131.5},
    {5000000, 132.0},  {6000000, 132.8},  {7000000, 133.5},  {8000000, 134.0},
    {9000000, 134.5},  {10000000, 135.0}, {12000000, 135.8}, {14000000, 136.5},
    {16000000, 137.0}, {18000000, 137.6}, {20000000, 138.0}, {22000000, 138.4},
    {24000000, 138.8}, {26000000, 139.0}, {28000000, 139.5}, {30000000, 139.8},
    {32500000, 140.1},
}};

}  // namespace rta::testing
