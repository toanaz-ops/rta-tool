// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// RT60-UI lane (docs/plans/2026-10-20-rt60-ui-lane-plan.md), decision 3: the
// band set the RT60 table answers for. Octave bands, 63 Hz to 8 kHz, on the
// base-ten grid of IEC 61260-1:2014 that `rta::dsp::OctaveBands` already
// implements -- the SAME definition the FFT banding path uses, so the decay
// path and the RTA plot cannot disagree about where a band is.
//
// Third-octave bands (wave B, plan T11) are the same grid at fraction 3, 50 Hz
// to 10 kHz: a band is in or out by its CENTRE, so the lowest is the 50.1 Hz
// centre and the highest the 10000 Hz one. They triple the rows and bring
// routine B*T refusals in the low bands (a 50 Hz third is 11.6 Hz wide, so a
// decay under 0.52 s cannot clear the bar of 6), which is why they sit behind a
// toggle and the octave table stayed the default. The 63 Hz octave and the 63
// and 80 Hz thirds are listed even though the shipped sweep cannot answer them
// (its trusted band starts at 80 Hz, and a band is trusted only when its LOWER
// EDGE is inside): the table says so in the row's note, where an absent row
// would read as "the app forgot".
#pragma once

#include "rta/dsp/OctaveBands.h"

#include <cmath>
#include <cstddef>
#include <vector>

namespace rta::measure {

struct Rt60BandEdges {
    int nominalHz = 0;     ///< the label an operator reads: 63, 125 ... 8000
    double centreHz = 0.0; ///< exact mid-band frequency (63.096, 125.89 ...)
    double lowHz = 0.0;
    double highHz = 0.0;
};

/// The label for an exact octave centre: the nearest preferred octave centre of
/// IEC 61260-1 (31.5 is written 32 because the table shows whole hertz and
/// is below the table's range anyway). Nearest on a log axis, so 125.89 is 125
/// and never 126 -- the exact centre is not what anyone reads off a rack.
[[nodiscard]] inline int rt60NominalOctaveHz(double centreHz) {
    constexpr int kPreferred[] = {16, 32, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    int best = kPreferred[0];
    double bestDistance = std::fabs(std::log(centreHz / kPreferred[0]));
    for (const int candidate : kPreferred) {
        const double distance = std::fabs(std::log(centreHz / candidate));
        if (distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

/// Which grid the table is on. Octave is the default (plan decision 3).
enum class Rt60BandSet { Octave, Third };

/// The label for an exact third-octave centre: the nearest preferred third-octave
/// centre of IEC 61260-1 on a log axis, 50 ... 10000 (the series repeats by
/// decades, so one decade's table is scaled). 158.49 reads 160, 251.19 reads 250.
[[nodiscard]] inline int rt60NominalThirdHz(double centreHz) {
    constexpr double kDecade[] = {100, 125, 160, 200, 250, 315, 400, 500, 630, 800};
    const double decade = std::pow(10.0, std::floor(std::log10(centreHz / 100.0)));
    double best = 0.0;
    double bestDistance = 1e300;
    for (const double scale : {decade, decade * 10.0}) {
        for (const double candidate : kDecade) {
            const double value = candidate * scale;
            const double distance = std::fabs(std::log(centreHz / value));
            if (distance < bestDistance) {
                best = value;
                bestDistance = distance;
            }
        }
    }
    return static_cast<int>(std::lround(best));
}

/// 63, 125, 250, 500, 1000, 2000, 4000, 8000 -- lowest first, so a table row's
/// index is its band's rank.
[[nodiscard]] inline std::vector<Rt60BandEdges> rt60OctaveBands() {
    // Centres within [60, 9000]: a band is in or out by its CENTRE, so 63.1 and
    // 7943 are in and 31.6 and 15849 are out, with no band straddling a limit.
    const rta::dsp::OctaveBands grid(1, 60.0, 9000.0);
    std::vector<Rt60BandEdges> out;
    out.reserve(grid.size());
    for (const auto& band : grid.bands()) {
        out.push_back({rt60NominalOctaveHz(band.centre), band.centre, band.lower, band.upper});
    }
    return out;
}

/// 50, 63, 80, 100 ... 10000: 24 bands, lowest first. Edges are the base-ten
/// third-octave edges (centre / and * 10^(3/60)), computed, never typed.
[[nodiscard]] inline std::vector<Rt60BandEdges> rt60ThirdOctaveBands() {
    // Centres within [48, 10500]: 50.1 Hz is in, 39.8 Hz is out; 10000 is in,
    // 12589 is out.
    const rta::dsp::OctaveBands grid(3, 48.0, 10500.0);
    std::vector<Rt60BandEdges> out;
    out.reserve(grid.size());
    for (const auto& band : grid.bands()) {
        out.push_back({rt60NominalThirdHz(band.centre), band.centre, band.lower, band.upper});
    }
    return out;
}

[[nodiscard]] inline std::vector<Rt60BandEdges> rt60Bands(Rt60BandSet set) {
    return set == Rt60BandSet::Third ? rt60ThirdOctaveBands() : rt60OctaveBands();
}

}  // namespace rta::measure
