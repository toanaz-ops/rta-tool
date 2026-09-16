// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/CrossoverTopology.h"

#include <cmath>
#include <numbers>

namespace rta::measure {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kTwoPi = 2.0 * kPi;

/// Wrap to (-pi, pi]. The half-open end matters: record Sec.3's 180-degree
/// rows must read +pi and not -pi, so that comparing a fitted intercept
/// against them is one subtraction and not a special case.
double wrapToPiHalfOpen(double radians) noexcept {
    double wrapped = std::remainder(radians, kTwoPi);
    if (wrapped <= -kPi) wrapped += kTwoPi;
    return wrapped;
}

}  // namespace

ExpectedOffset expectedOffset(Topology topology, ProcessorInversion inversion) {
    // The whole Sec.3 table, evaluated. N * 90 degrees, wrapped:
    //   N = 0 mod 4 ->    0     N = 1 mod 4 ->  +90
    //   N = 2 mod 4 ->  180     N = 3 mod 4 ->  -90
    // `topology.family` is deliberately unread -- an LR-N is the BW-(N/2)
    // cascaded with itself, so it inherits the identity with the SAME N. The
    // family is carried because the operator answered it and because the
    // DESIGNED SUM differs, not because the offset does.
    const double raw = wrapToPiHalfOpen(static_cast<double>(topology.order) * kPi / 2.0);

    // Question (c). An output the processor already inverted adds exactly pi;
    // that is the whole content of the answer, and it is why the question has
    // to be asked rather than measured.
    const double inverted = wrapToPiHalfOpen(raw + kPi);

    ExpectedOffset result;
    switch (inversion) {
        case ProcessorInversion::Yes:
            result.radians = inverted;
            break;
        case ProcessorInversion::No:
            result.radians = raw;
            break;
        case ProcessorInversion::Unknown:
            // TWO lines, not a guess between them. Showing both and letting the
            // measured intercept land on one is the closest this lane comes to
            // the forbidden move, and record Sec.13.3 is where the owner ruled
            // that it stays on the right side of it: the operator reads the
            // plot, the software does not pick.
            result.radians = raw;
            result.alternativeRadians = inverted;
            result.ambiguous = true;
            break;
    }
    return result;
}

}  // namespace rta::measure
