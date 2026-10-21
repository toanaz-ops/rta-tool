// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane-end LOW (audit prj010-fable-audit-20261021 follow-up): eqChartDbRange
// treated a trusted bin as "drawn" only inside a run of two or more, while
// paintEqChart dropped its trusted mask as soon as ONE bin was trusted. With
// exactly one lone trusted bin the axis took every bin (nothing drawn), but the
// curves were still masked to that single bin -- a run of one strokes nothing,
// so the plot came out blank under an axis sized for data it never drew.
//
// Both now ask eqChartHasDrawnTrusted(). The behavioural half paints through
// the real paintEqChart and counts trace-coloured pixels; a predicate test
// alone would stay green if paintEqChart went back to its own inline test.
#include <catch2/catch_test_macros.hpp>

#include "view/EqChartRenderer.h"

#include <juce_graphics/juce_graphics.h>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace {

constexpr int kWidth = 700;
constexpr int kHeight = 420;
constexpr int kBins = 24;

struct Grid {
    std::vector<float> hz;
    std::vector<float> measured;
    std::vector<double> ghost;
    std::vector<double> targetLine;
    std::vector<double> correction;
    std::vector<std::uint8_t> trusted;

    // 100 Hz .. ~12 kHz, log-spaced; a flat 0 dB measurement, a 20 dB bump at bin 3.
    explicit Grid(std::vector<std::uint8_t> trustedMask) : trusted(std::move(trustedMask)) {
        for (int k = 0; k < kBins; ++k) {
            hz.push_back(100.0f * std::pow(1.25f, static_cast<float>(k)));
            measured.push_back(k == 3 ? 20.0f : 0.0f);
            ghost.push_back(0.0);
            targetLine.push_back(0.0);
            correction.push_back(0.0);
        }
    }

    rta::view::EqChartData data() const {
        rta::view::EqChartData d;
        d.hz = hz;
        d.measuredDb = measured;
        d.ghostDb = ghost;
        d.targetLineDb = targetLine;
        d.correctionDb = correction;
        d.trusted = trusted;
        return d;
    }
};

std::vector<std::uint8_t> maskOf(std::initializer_list<int> trustedBins) {
    std::vector<std::uint8_t> mask(kBins, 0);
    for (const int k : trustedBins) mask[static_cast<std::size_t>(k)] = 1;
    return mask;
}

// Amber (`trace`, az::ui::accent) pixels below the legend row, which is itself
// set in the trace colour and would otherwise dominate the count.
int traceInk(const Grid& grid) {
    juce::Image image(juce::Image::ARGB, kWidth, kHeight, true);
    {
        // Scoped: the software renderer holds the bitmap until it is destroyed.
        juce::Graphics g(image);
        g.fillAll(juce::Colours::black);
        rta::view::paintEqChart(g, image.getBounds(), grid.data());
    }
    int count = 0;
    for (int y = 40; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const auto c = image.getPixelAt(x, y);
            if (c.getRed() > 200 && c.getGreen() > 110 && c.getGreen() < 200 && c.getBlue() < 90) ++count;
        }
    }
    return count;
}

}  // namespace

TEST_CASE("one lone trusted bin: axis and mask agree, the curve is not blanked",
          "[main_component_panes_eq][eq_chart_mask]") {
    const Grid allTrusted(std::vector<std::uint8_t>(kBins, 1));
    const Grid lone(maskOf({10}));
    const Grid pair(maskOf({10, 11}));

    // The predicate itself: a run of one strokes nothing.
    CHECK(rta::view::eqChartHasDrawnTrusted(allTrusted.data()));
    CHECK(rta::view::eqChartHasDrawnTrusted(pair.data()));
    CHECK_FALSE(rta::view::eqChartHasDrawnTrusted(lone.data()));

    // The axis for a lone bin takes every bin (the 20 dB bump is in range).
    CHECK(rta::view::eqChartDbRange(lone.data()).top >= 20.0);

    // The paint: with no drawn trusted run the mask is dropped, so the lone-bin
    // chart carries as much measured ink as the fully trusted one. Before the
    // fix it carried none.
    const int full = traceInk(allTrusted);
    const int loneInk = traceInk(lone);
    INFO("full-trust ink " << full << ", lone-bin ink " << loneInk);
    REQUIRE(full > 200);
    CHECK(loneInk > full * 8 / 10);

    // And a genuine run of two still masks: only that run is judged, so the
    // chart is NOT as inked as the unmasked ones.
    CHECK(traceInk(pair) < full / 2);
}
