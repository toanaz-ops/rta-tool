// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
//
// L7-DELAY task A/B (docs/plans/2026-09-07-L7-delay-impl-plan.md; decision
// record docs/dsp/2026-09-06-l7-auto-delay.md sec.3, sec.4). `suggestDelay`
// is a POLICY layer over `findDelayPhat`'s primitive (record sec.0): which of
// several correlation peaks is the answer, a trust figure with a derivation,
// and a named refusal instead of a silent zero. It shares the spectral half
// with `findDelayPhat` (PhatCorrelation.h, DEL-R1) rather than re-running a
// second correlator.
#pragma once

#include "rta/dsp/DelayFinder.h"

#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace rta::dsp {

/// One ranked local maximum of the one-shot correlation, up to
/// `DelayPolicy::maxCandidates` of these accompany `DelaySuggestion::best`.
struct DelayCandidate {
    std::ptrdiff_t delaySamples = 0;  ///< integer lag inside the window
    double subSample = 0.0;           ///< parabolic refinement, |.| <= 0.5
    double height = 0.0;              ///< SIGNED normalised |r| at this peak
    bool inverted = false;            ///< height < 0
};

/// A refusal is named, never a silently returned zero (record sec.3, sec.4).
/// `WindowEmpty`: nothing plausible turned up inside the operator-stated
/// `[minLag, maxLag]` window -- a stronger refusal than `BelowFloor`, because
/// the operator gave positive information ("look between X and Y") that this
/// verdict says was not honoured by anything in the data. `BelowFloor`: a
/// candidate WAS found (including over the default, unrestricted window),
/// but its trust does not clear `DelayPolicy::acceptMultiple` times the
/// derived null floor.
enum class DelayVerdict { Accepted, BelowFloor, WindowEmpty };

struct DelaySuggestion {
    DelayEstimate best{};  ///< reused verbatim; see DelayFinder.h

    /// `peak / f_band`, in [0, 1] by construction (record sec.4): a
    /// triangle-inequality bound on a sum of unit phasors, so the value
    /// cannot run away regardless of input. 0.0 until a candidate is found.
    double trust = 0.0;

    /// `sqrt(ln m / M_in)` (record sec.4): the derived expected maximum of
    /// the correlation under the null hypothesis of uncorrelated channels,
    /// on the SAME normalised scale as `trust` (the derivation's own
    /// "normalised" step divides by `f_band`, exactly matching `trust`'s own
    /// division -- see DelayPolicy.cpp). Always computed, even on a refusal:
    /// it is what makes the refusal explicable rather than a bare "no".
    double nullFloor = 0.0;

    /// `|second| / |best|`, in [0, 1] (record sec.3). DISPLAY-ONLY -- no code
    /// path in this file gates on it; see A5 in the impl plan for why.
    double ambiguity = 0.0;

    /// Ranked by |height| descending, length <= `DelayPolicy::maxCandidates`.
    std::vector<DelayCandidate> candidates;

    DelayVerdict verdict = DelayVerdict::WindowEmpty;
};

struct DelayPolicy {
    /// The plausibility window, in samples. Full linear range by default
    /// (record sec.3): "100 m" is a fact about a venue the caller can state;
    /// "0.5 of the peak" is not, and is not offered as a knob here.
    std::ptrdiff_t minLag = std::numeric_limits<std::ptrdiff_t>::min();
    std::ptrdiff_t maxLag = std::numeric_limits<std::ptrdiff_t>::max();

    int maxCandidates = 4;      ///< K (record sec.3)
    double acceptMultiple = 4.0;  ///< c; accept iff trust >= c*nullFloor (record sec.4)
};

/// One-shot delay suggestion: PHAT correlation over two aligned spans (the
/// same primitive `findDelayPhat` uses -- see PhatCorrelation.h, DEL-R1),
/// picking the argmax inside `policy`'s window, ranking up to
/// `policy.maxCandidates` local maxima, and naming a refusal rather than
/// returning an untrustworthy number silently.
///
/// @throws std::invalid_argument under the same conditions as
///         `findDelayPhat` (empty spans, mismatched lengths, maxHz < minHz).
[[nodiscard]] DelaySuggestion suggestDelay(std::span<const float> reference,
                                            std::span<const float> measurement,
                                            const PhatOptions& options, const DelayPolicy& policy);

}  // namespace rta::dsp
