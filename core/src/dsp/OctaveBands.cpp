// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/OctaveBands.h"

#include <cmath>
#include <stdexcept>

namespace rta::dsp {

namespace {

/// The reference frequency all band centres are defined against.
/// IEC 61260-1:2014 fixes this at 1 kHz, which is why the base-ten and base-two
/// systems agree there and diverge in both directions away from it.
constexpr double kReferenceHz = 1000.0;

double ratioFor(const OctaveBase base) noexcept {
    return base == OctaveBase::BaseTen ? std::pow(10.0, 3.0 / 10.0) : 2.0;
}

/// Band selection rounds outward by this much before taking a ceiling or floor.
///
/// The caller's limits are usually themselves computed -- a display range, a
/// Nyquist frequency -- so a band whose centre is mathematically exactly at the
/// limit can land a few ulp on the wrong side of it and silently vanish. A band
/// appearing or disappearing depending on rounding noise is worse than either
/// consistent answer.
constexpr double kSelectionEpsilon = 1.0e-9;

}  // namespace

OctaveBands::OctaveBands(const int fraction, const double lowestHz, const double highestHz,
                         const OctaveBase base)
    : fraction_(fraction), base_(base) {
    if (fraction <= 0) {
        throw std::invalid_argument("OctaveBands fraction must be positive");
    }
    if (!(lowestHz > 0.0) || !(highestHz > lowestHz)) {
        throw std::invalid_argument("OctaveBands needs 0 < lowestHz < highestHz");
    }

    const double g = ratioFor(base);
    const double b = static_cast<double>(fraction);
    const double lnG = std::log(g);

    // centre(x) = 1000 * G^(x/b), so the index of a given frequency is
    // x = b * ln(f/1000) / ln(G). Bands are selected on their CENTRE: a band is
    // either in the set or out of it, with no partial membership at the edges.
    const double lowIndex = b * std::log(lowestHz / kReferenceHz) / lnG;
    const double highIndex = b * std::log(highestHz / kReferenceHz) / lnG;

    const int firstIndex = static_cast<int>(std::ceil(lowIndex - kSelectionEpsilon));
    const int lastIndex = static_cast<int>(std::floor(highIndex + kSelectionEpsilon));

    if (lastIndex < firstIndex) {
        return;  // the range falls between two band centres
    }

    // Half a band, as a ratio. The edges sit at centre / half and centre * half,
    // which puts the centre at the GEOMETRIC mean of its edges -- the arithmetic
    // mean would drift, because bands are equal ratios and not equal widths.
    const double half = std::pow(g, 1.0 / (2.0 * b));

    bands_.reserve(static_cast<std::size_t>(lastIndex - firstIndex + 1));
    for (int x = firstIndex; x <= lastIndex; ++x) {
        const double centre = kReferenceHz * std::pow(g, static_cast<double>(x) / b);
        bands_.push_back({ x, centre, centre / half, centre * half });
    }
}

double OctaveBands::ratio() const noexcept { return ratioFor(base_); }

}  // namespace rta::dsp
