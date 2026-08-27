// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace rta::dsp {

/// The octave frequency ratio G.
///
/// IEC 61260-1:2014 designs to the **base-ten** system, G = 10^(3/10) =
/// 1.995262..., and that is the default here. G = 2 exactly is the base-two
/// system: the standard acknowledges it but records that the probability of a
/// base-two filter conforming falls as the mid-band frequency moves away from
/// the 1 kHz reference.
///
/// The difference is not academic and it is not visible. For 1/3 octave the
/// base-two centre is 21.9 cents low at 25 Hz, 1.4 cents low at 800 Hz and 15.1
/// cents high at 12.5 kHz -- a systematic offset from every conforming
/// instrument, on a plot that looks entirely normal. Base-two is offered only so
/// a measurement can be compared against older equipment that used it.
enum class OctaveBase {
    BaseTen,  ///< G = 10^(3/10). IEC 61260-1:2014. The default.
    BaseTwo,  ///< G = 2. Legacy compatibility only.
};

/// Fractional-octave band definitions per IEC 61260-1:2014.
///
/// Band `x` of a 1/b-octave set has mid-band frequency `1000 * G^(x/b)` and
/// edges at `centre * G^(+-1/(2b))`. Bands tile without gap or overlap: one
/// band's upper edge is exactly its neighbour's lower edge.
///
/// This type holds only the frequency layout. It knows nothing about FFT bins,
/// filters or sample rates, so the same definitions serve the FFT banding path
/// and the IEC filter bank -- which is the point, because those two must agree
/// about where a band *is* even when they disagree about what is in it.
class OctaveBands {
public:
    struct Band {
        int    index;   ///< x, such that centre = 1000 * G^(x/fraction)
        double centre;  ///< exact mid-band frequency, hertz
        double lower;   ///< lower band-edge frequency, hertz
        double upper;   ///< upper band-edge frequency, hertz
    };

    /// Every band whose **centre** lies within [lowestHz, highestHz]. Selecting
    /// on the centre rather than on the edges is what makes the set stable: a
    /// band is either in or out, and a band at exactly the limit is in.
    ///
    /// @param fraction  bands per octave: 1, 3, 6, 12, 24, 48. Must be positive.
    OctaveBands(int fraction, double lowestHz, double highestHz,
                OctaveBase base = OctaveBase::BaseTen);

    [[nodiscard]] std::size_t size() const noexcept { return bands_.size(); }
    [[nodiscard]] bool empty() const noexcept { return bands_.empty(); }
    [[nodiscard]] std::span<const Band> bands() const noexcept { return bands_; }
    [[nodiscard]] const Band& operator[](std::size_t i) const { return bands_[i]; }

    [[nodiscard]] int fraction() const noexcept { return fraction_; }
    [[nodiscard]] OctaveBase base() const noexcept { return base_; }

    /// G itself, for callers that need to reason about band widths.
    [[nodiscard]] double ratio() const noexcept;

private:
    int                 fraction_;
    OctaveBase          base_;
    std::vector<Band>   bands_;
};

}  // namespace rta::dsp
