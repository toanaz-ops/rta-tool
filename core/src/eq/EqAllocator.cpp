// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/eq/EqAllocator.h"

#include "rta/dsp/ExcessPhase.h"
#include "rta/eq/BiquadDesign.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace rta::eq {

namespace {

void validate(const EqInput& input) {
    const std::size_t m = input.hz.size();
    if (input.residualDb.size() != m || input.coherence.size() != m ||
        input.trusted.size() != m || input.excluded.size() != m) {
        throw std::invalid_argument(
            "rankCandidates/autoEq: hz, residualDb, coherence, trusted and excluded "
            "must be the same length");
    }
    if (input.sampleRate <= 0.0) {
        throw std::invalid_argument("rankCandidates/autoEq: sampleRate must be positive");
    }
}

/// Step 1 (EqAllocator.h doc comment): the gamma^2/f-weighted mean of
/// residualDb over every trusted, non-excluded bin -- the "anchor band"
/// taken as the whole trusted region (a labelled simplification).
double autoOffset(const EqInput& input) {
    double num = 0.0, den = 0.0;
    for (std::size_t k = 0; k < input.hz.size(); ++k) {
        if (!input.trusted[k] || input.excluded[k] || !(input.hz[k] > 0.0f)) continue;
        const double w = static_cast<double>(input.coherence[k]) / static_cast<double>(input.hz[k]);
        num += w * static_cast<double>(input.residualDb[k]);
        den += w;
    }
    return (den > 0.0) ? num / den : 0.0;
}

/// Walks from `start` in `direction` (+1 or -1) while `sign*residual` is
/// non-increasing (still descending away from the fStar extremum), stopping
/// at the first bin where it would increase again -- that bin is the
/// flanking, OPPOSITE-SENSE extremum (EqAllocator.h step 2).
std::size_t walkToFlank(std::span<const float> residual, std::size_t start, int direction,
                        double sign) {
    std::size_t idx = start;
    while (true) {
        const long long nextSigned = static_cast<long long>(idx) + direction;
        if (nextSigned < 0 || static_cast<std::size_t>(nextSigned) >= residual.size()) break;
        const auto next = static_cast<std::size_t>(nextSigned);
        if (sign * residual[next] <= sign * residual[idx]) {
            idx = next;
        } else {
            break;
        }
    }
    return idx;
}

/// The bin resolution half-depth crossing (D1's own acceptance: "to the
/// grid's resolution", no sub-bin interpolation) walking from fStar toward
/// `flank`: the first bin whose |residual| has fallen under half the
/// extremum's own magnitude.
double halfCrossingHz(std::span<const float> hz, std::span<const float> residual,
                      std::size_t fStar, std::size_t flank, double sign) {
    const double half = sign * static_cast<double>(residual[fStar]) / 2.0;
    const int direction = (flank < fStar) ? -1 : (flank > fStar ? 1 : 0);
    if (direction == 0) return hz[fStar];
    std::size_t idx = fStar;
    while (idx != flank) {
        const auto next = static_cast<std::size_t>(static_cast<long long>(idx) + direction);
        if (sign * static_cast<double>(residual[next]) < half) return hz[next];
        idx = next;
    }
    return hz[flank];
}

/// Q_max for a boost, tied to the room T60 when present: t_60 = 2.2*Q_p/fc,
/// Q_p = A*Q for a cookbook boost (A = 10^(G/40), record Sec.3) -- solved
/// for Q at t_60 == roomT60Sec using a representative +6 dB boost (A at the
/// gain cap), since the allocator has not solved a gain yet at placement
/// time. Falls back to qMaxBoost with no T60.
double tiedQMaxBoost(const EqInput& input, double fc) {
    if (!input.roomT60Sec.has_value() || !(fc > 0.0)) return input.qMaxBoost;
    const double a = std::pow(10.0, input.gCapDb / 40.0);
    const double q = (*input.roomT60Sec) * fc / (2.2 * a);
    return std::min(q, input.qMaxBoost * 4.0);  // a generous ceiling above the fallback, not unbounded
}

/// excessPhase() over the WHOLE grid, magnitude read directly as abs(H) --
/// no separate magnitude field exists in EqInput (EqAllocator.h's own doc
/// comment on hHalfGrid explains why one is not needed).
dsp::ExcessPhaseResult computeExcessPhase(const EqInput& input) {
    std::vector<float> magnitude(input.hHalfGrid.size());
    for (std::size_t k = 0; k < magnitude.size(); ++k) magnitude[k] = static_cast<float>(std::abs(input.hHalfGrid[k]));
    return dsp::excessPhase(magnitude, input.hHalfGrid, input.trusted, input.sampleRate);
}

struct Placement {
    FilterSpec spec;
    double scoreDb = 0.0;
    DipClassification dip;
};

/// The shared greedy loop (EqAllocator.h steps 1-5): repeatedly takes the
/// largest-|workingResidual| eligible bin, bounds its region, derives Q,
/// gates a boost through G24 when hHalfGrid is available, and blocks a
/// half-bandwidth halo around every accepted placement before the next
/// search -- never re-solving gains between placements (record Sec.3).
std::vector<Placement> greedyPlace(const EqInput& input, std::span<const float> workingResidual,
                                   int maxCount) {
    std::vector<Placement> placements;
    std::vector<bool> blocked(input.hz.size(), false);
    std::optional<dsp::ExcessPhaseResult> xp;
    const bool gateAvailable = !input.hHalfGrid.empty() && input.hHalfGrid.size() == input.hz.size();

    while (static_cast<int>(placements.size()) < maxCount) {
        std::size_t fStar = input.hz.size();
        double bestAbs = -1.0;
        for (std::size_t k = 0; k < input.hz.size(); ++k) {
            if (!input.trusted[k] || input.excluded[k] || blocked[k]) continue;
            // DC and Nyquist are not legal biquad centres (designBiquad's own
            // domain: 0 < fc < fs/2) -- excluded here rather than left to
            // surface as a thrown exception the moment a tied extremum
            // happens to land on one of them.
            if (!(static_cast<double>(input.hz[k]) > 0.0 &&
                  static_cast<double>(input.hz[k]) < input.sampleRate / 2.0)) {
                continue;
            }
            const double v = std::abs(static_cast<double>(workingResidual[k]));
            if (v > bestAbs) {
                bestAbs = v;
                fStar = k;
            }
        }
        // kMinPlacementDb (a labelled judgement, EqAllocator.h's own shape
        // of judgement as kQMin): below this, "the largest remaining bin" is
        // noise-floor or flat-baseline residue, not a feature worth a
        // filter -- without this floor, a perfectly flat remainder (every
        // eligible bin tied at the SAME tiny value, e.g. the auto-offset's
        // own rounding) would still place placements.size() < maxCount
        // filters on nothing, one per iteration, until maxCount is reached.
        constexpr double kMinPlacementDb = 0.2;
        if (fStar >= input.hz.size() || bestAbs < kMinPlacementDb) break;  // no more real extrema

        const double sign = (workingResidual[fStar] < 0.0f) ? -1.0 : 1.0;
        const bool isBoost = sign < 0.0;  // a dip below target needs boosting up
        const std::size_t fL = walkToFlank(workingResidual, fStar, -1, sign);
        const std::size_t fR = walkToFlank(workingResidual, fStar, 1, sign);
        const double f1 = halfCrossingHz(input.hz, workingResidual, fStar, fL, sign);
        const double f2 = halfCrossingHz(input.hz, workingResidual, fStar, fR, sign);
        const double fc = static_cast<double>(input.hz[fStar]);
        const double qMax = isBoost ? tiedQMaxBoost(input, fc) : input.qMaxCut;
        const double q = std::clamp(fc / std::max(f2 - f1, 1e-9), kQMin, qMax);

        DipClassification dip{ DipVerdict::Boostable, 0.0, 0.0, 0.0 };
        if (isBoost && gateAvailable) {
            if (!xp) xp = computeExcessPhase(input);
            dip = classifyDip(*xp, input.hz, fL, fStar, fR, workingResidual, true);
            if (dip.verdict == DipVerdict::NotMinimumPhase) {
                blocked[fStar] = true;  // never place a boost here; try elsewhere
                continue;
            }
        }

        const FilterSpec spec{ FilterType::Peaking, fc, q, 0.0 };
        const double halfBandwidthHz = fc / (2.0 * q);
        for (std::size_t k = 0; k < input.hz.size(); ++k) {
            if (std::abs(static_cast<double>(input.hz[k]) - fc) <= halfBandwidthHz) blocked[k] = true;
        }

        placements.push_back(Placement{ spec, bestAbs, dip });
    }
    return placements;
}

std::vector<float> computeWorkingResidual(const EqInput& input, double c) {
    std::vector<float> working(input.hz.size());
    for (std::size_t k = 0; k < input.hz.size(); ++k) {
        working[k] = static_cast<float>(static_cast<double>(input.residualDb[k]) - c);
    }
    return working;
}

/// SIGN, DERIVED (flag to the record owner alongside EQ-R1..R5): a cascade
/// with response R_i(f) inserted into the signal chain changes the MEASURED
/// level by +R_i(f) dB at every frequency (dB adds under cascading), so
/// EQ-R3's ghost identity ghost_k = m_k + sum_i R_i(f_k) converges to the
/// target only if sum_i R_i(f) approximates -(m_k - t_k - c) =
/// -workingResidual, never workingResidual itself. solveGains's own contract
/// (EqGainSolve.h) fits whatever residualDb it is HANDED, directly, by
/// design (Task C's own golden and closed-form tests -- C1..C4 -- are
/// already built and committed against that literal contract, and stay
/// correct under it); it is EqAllocator's job, as solveGains's documented
/// caller, to hand it the curve that makes THIS caller's own physics work
/// out, i.e. the negation of the residual placement itself reads off
/// (verified end to end by test_eq_allocator_placement.cpp's D1/D4: a dip
/// below target solves to a POSITIVE, boosting gain once this negation is
/// applied, not a negative one).
std::vector<float> negated(std::span<const float> values) {
    std::vector<float> out(values.size());
    for (std::size_t k = 0; k < values.size(); ++k) out[k] = -values[k];
    return out;
}

}  // namespace

std::vector<Candidate> rankCandidates(const EqInput& input, int maxCandidates) {
    validate(input);
    const double c = autoOffset(input);
    const auto working = computeWorkingResidual(input, c);
    const auto placements = greedyPlace(input, working, maxCandidates);

    std::vector<Candidate> candidates;
    candidates.reserve(placements.size());
    for (const auto& p : placements) {
        // A single-filter gain estimate for the chip's own ghost preview
        // (record Sec.2: Suggest's chips need a real predicted response,
        // not a placeholder) -- re-solved jointly the moment a second chip
        // is accepted (app-layer, EQ-R3), never re-gated here.
        EqInput single = input;
        const auto negatedWorking = negated(working);
        single.residualDb = negatedWorking;  // see negated()'s doc comment
        const std::vector<FilterSpec> placedOne{ p.spec };
        const auto solved = solveGains(placedOne, single);
        FilterSpec withGain = p.spec;
        withGain.gainDb = solved.gainsDb.front();
        candidates.push_back(Candidate{ withGain, p.scoreDb, p.dip });
    }
    return candidates;
}

std::vector<FilterSpec> autoEq(const EqInput& input) {
    validate(input);
    const double c = autoOffset(input);
    const auto working = computeWorkingResidual(input, c);
    const auto placements = greedyPlace(input, working, input.maxFilters);
    if (placements.empty()) return {};

    std::vector<FilterSpec> placed;
    placed.reserve(placements.size());
    for (const auto& p : placements) placed.push_back(p.spec);

    EqInput jointInput = input;
    const auto negatedWorking = negated(working);
    jointInput.residualDb = negatedWorking;  // see negated()'s doc comment
    const auto solved = solveGains(placed, jointInput);
    for (std::size_t i = 0; i < placed.size(); ++i) placed[i].gainDb = solved.gainsDb[i];
    return placed;
}

}  // namespace rta::eq
