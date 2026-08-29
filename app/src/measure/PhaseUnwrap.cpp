// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/PhaseUnwrap.h"

#include <numbers>
#include <stdexcept>

namespace rta::measure {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kPi = std::numbers::pi;

/// Shared implementation for both overloads. `coherence` is null for the
/// ungated one -- every bin is then trusted unconditionally.
void unwrapCore(std::span<const float> wrapped, const std::span<const float>* coherence,
                 std::span<float> out, const UnwrapOptions& options) {
    if (out.size() != wrapped.size()) {
        throw std::invalid_argument("unwrapPhase: out must be the same length as wrapped");
    }
    if (coherence != nullptr && coherence->size() != wrapped.size()) {
        throw std::invalid_argument("unwrapPhase: coherence must be the same length as wrapped");
    }
    if (wrapped.empty()) {
        return;
    }

    // `anchor` is the last TRUSTED raw wrapped sample: the reference the
    // next bin's step is measured against. A gated bin never updates it,
    // which is what stops one bad reading from being read as a genuine
    // 2*pi step by the bin after it -- the comparison simply skips over
    // it, exactly as if it were not there.
    //
    // `anchorSet` starts false and the loop below starts at bin 0, not 1:
    // bin 0 gets NO exemption from the gate. Seeding the anchor from
    // wrapped[0] unconditionally would let a garbage DC reading become the
    // reference every later bin is measured against -- and DC is exactly
    // where coherence tends to be worst in practice (no signal below the
    // measurement's low-frequency limit), so this is the common case, not
    // an edge one. Until a trusted bin is seen, every bin is passed through
    // as its own raw value (offset 0), which is the honest thing to show
    // when nothing trustworthy has anchored the trace yet.
    bool anchorSet = false;
    double anchor = 0.0;
    double offset = 0.0;

    for (std::size_t i = 0; i < wrapped.size(); ++i) {
        const bool gated = coherence != nullptr && options.minimumCoherence > 0.0f
                         // <=, not <: a bin exactly AT the threshold is not
                         // yet trustworthy either -- the threshold is the
                         // floor a bin must clear, not a value it may equal.
                         && (*coherence)[i] <= options.minimumCoherence;
        if (!gated) {
            if (anchorSet) {
                const double delta = static_cast<double>(wrapped[i]) - anchor;
                // A running offset that ACCUMULATES across steps, not a
                // per-bin fmod: a pure delay wraps repeatedly across a
                // whole spectrum, and re-deriving each bin's correction
                // from scratch (fmod of that one step alone) forgets how
                // many multiples of 2*pi the earlier steps already added,
                // so it cannot reproduce a line that has wrapped more than
                // once.
                //
                // Strict `>`/`<` against +/-pi, not >=/<=: wrapped is
                // produced by the (-pi, pi] convention (numpy.unwrap and
                // std::remainder agree), so a delta that lands EXACTLY on
                // +/-pi is already representable without a correction --
                // treating it as a discontinuity would be double-counting
                // the one convention this function is built to match.
                if (delta > kPi) {
                    offset -= kTwoPi;
                } else if (delta < -kPi) {
                    offset += kTwoPi;
                }
            }
            anchor = static_cast<double>(wrapped[i]);
            anchorSet = true;
        }
        out[i] = static_cast<float>(static_cast<double>(wrapped[i]) + offset);
    }
}

}  // namespace

void unwrapPhase(std::span<const float> wrapped, std::span<float> out,
                 const UnwrapOptions& options) {
    unwrapCore(wrapped, nullptr, out, options);
}

void unwrapPhase(std::span<const float> wrapped, std::span<const float> coherence,
                 std::span<float> out, const UnwrapOptions& options) {
    unwrapCore(wrapped, &coherence, out, options);
}

}  // namespace rta::measure
