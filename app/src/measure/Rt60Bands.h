// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// RT60-UI lane (docs/plans/2026-10-20-rt60-ui-lane-plan.md), decision 3: the
// band set the RT60 table answers for. Octave bands, 63 Hz to 8 kHz, on the
// base-ten grid of IEC 61260-1:2014 that `rta::dsp::OctaveBands` already
// implements -- the SAME definition the FFT banding path uses, so the decay
// path and the RTA plot cannot disagree about where a band is.
//
// Third-octave bands are deliberately not here (wave B): they double the rows
// and bring routine B*T refusals in the low bands before the octave table is
// proven. The 63 Hz band is listed even though the shipped sweep cannot answer
// it (its trusted band starts at 80 Hz): the table says so in the row's note,
// where an absent row would read as "the app forgot".
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

}  // namespace rta::measure
