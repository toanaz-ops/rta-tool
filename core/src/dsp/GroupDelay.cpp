// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/GroupDelay.h"

#include <algorithm>
#include <numbers>
#include <stdexcept>

namespace rta::dsp {

namespace {

// tau = -Im(dHdOmega * conj(h)) / |h|^2, with the 0/0 case at |h| == 0
// resolved to 0.0 rather than NaN -- see the header for why a silent zero is
// the safer failure than letting a NaN reach a view that takes min/max over
// the whole trace.
double closedForm(std::complex<double> dHdOmega, std::complex<double> h) noexcept {
    const double magSq = std::norm(h);
    if (magSq <= 0.0) {
        return 0.0;
    }
    return -std::imag(dHdOmega * std::conj(h)) / magSq;
}

}  // namespace

void groupDelaySeconds(std::span<const std::complex<double>> h,
                       double binWidthHz,
                       std::size_t smoothingBins,
                       std::span<double> out) {
    if (smoothingBins == 0) {
        throw std::invalid_argument("groupDelaySeconds smoothingBins must be at least 1");
    }
    if (out.size() != h.size()) {
        throw std::invalid_argument("groupDelaySeconds out span must match h in length");
    }
    if (!(binWidthHz > 0.0)) {
        throw std::invalid_argument("groupDelaySeconds binWidthHz must be positive");
    }

    // Angular width of ONE bin, in rad/s. binWidthHz is already fs/N, so this
    // is 2*pi*fs/N -- the step dOmega takes per bin index.
    const double angularBinWidth = 2.0 * std::numbers::pi * binWidthHz;
    const std::size_t numBins = h.size();
    const std::size_t m = smoothingBins;

    for (std::size_t k = 0; k < numBins; ++k) {
        // The full central window fits: compare h[k+m] against h[k-m]. This
        // is the case the pure-delay test pins in closed form -- see
        // test_group_delay.cpp for why the discrete answer is
        // N*sin(2*pi*m*D/N)/(2*pi*m*fs), not the continuous D/fs.
        if (k >= m && k + m < numBins) {
            const std::complex<double> diff = h[k + m] - h[k - m];
            // Central difference: the two samples are 2*m bins apart, so the
            // step in omega is 2*m*angularBinWidth, not m*angularBinWidth.
            const double dOmega = 2.0 * static_cast<double>(m) * angularBinWidth;
            out[k] = closedForm(diff / dOmega, h[k]);
            continue;
        }

        // Near a spectrum edge, the symmetric window would need bins below
        // DC or above Nyquist that do not exist. Fall back to the widest
        // ONE-SIDED difference that fits, rather than inventing samples --
        // that is the "honest thing to do" the header promises. Prefer
        // whichever side has more room so bin 0 (which has none on the left)
        // still gets the full m-bin forward difference when the spectrum is
        // long enough.
        const std::size_t forward = (k + 1 < numBins) ? std::min(m, numBins - 1 - k) : 0;
        const std::size_t backward = std::min(m, k);

        if (forward >= backward && forward > 0) {
            const std::complex<double> diff = h[k + forward] - h[k];
            const double dOmega = static_cast<double>(forward) * angularBinWidth;
            out[k] = closedForm(diff / dOmega, h[k]);
        } else if (backward > 0) {
            const std::complex<double> diff = h[k] - h[k - backward];
            const double dOmega = static_cast<double>(backward) * angularBinWidth;
            out[k] = closedForm(diff / dOmega, h[k]);
        } else {
            // A single-bin spectrum has no neighbour on either side, so
            // there is nothing to take a difference against.
            out[k] = 0.0;
        }
    }
}

}  // namespace rta::dsp
