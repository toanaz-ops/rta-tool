// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rtatool_view_tests. Fixtures and pixel predicates shared
// by test_transfer_view.cpp, split out to keep that file under the project's
// 400-line hard cap. Header-only and included by exactly one translation
// unit, so the anonymous namespace below carries no ODR risk.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <az_ui/az_ui.h>

#include "measure/Snapshot.h"
#include "measure/SnapshotSource.h"
#include "measure/SyntheticSnapshot.h"
#include "view/TransferView.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>

namespace {

using rta::measure::Snapshot;
using rta::measure::SnapshotPtr;
using rta::view::TransferView;

const juce::Colour kBackground = az::ui::background;

/// A snapshot carrying a real, deterministic transfer block -- the fixture
/// most tests in this file render.
SnapshotPtr snapshotWithTransfer(std::size_t fftSize, double sampleRate, int delaySamples) {
    auto snap = std::make_shared<Snapshot>();
    snap->sequence = 1;
    snap->sampleRate = sampleRate;
    snap->fftSize = fftSize;
    snap->transfer = rta::measure::makeSyntheticTransfer(fftSize, sampleRate, delaySamples);
    return snap;
}

/// An RTA-only snapshot -- bands present, no reference channel ever fed, so
/// `transfer` is genuinely absent rather than empty. Matches how `Analyser`
/// itself would leave the field before the first `pushPair`.
SnapshotPtr rtaOnlySnapshot() {
    auto snap = std::make_shared<Snapshot>();
    snap->sequence = 1;
    snap->sampleRate = 48000.0;
    snap->fftSize = 4096;
    return snap;
}

/// True for a pixel that could only be the LIVE trace's sodium-amber ink
/// (accentArgb 0xffff9f1c, R=255 B=28), never the grid's cool grey
/// (borderArgb 0xff2b2f37, R=43 B=55) or the stored trace's cool grey
/// (dimArgb 0xff868d98, R=134 B=152) -- both of the latter have R <= B.
/// True even at the alpha floor (0.25): blended 25% over the near-black
/// background (0x0a0b0d) still lands at roughly R=71 B=17, R-B=54.
bool isLiveTraceish(juce::Colour c) {
    return static_cast<int>(c.getRed()) - static_cast<int>(c.getBlue()) > 30;
}

/// True for a pixel that could plausibly be a STORED trace's cool grey ink
/// (dimArgb 0xff868d98, R<=B, G=141), even faded by a low coherence -- but
/// not the background (G=11) and not the grid's hairline (borderArgb
/// 0xff2b2f37, G=47). G > 60 is comfortably between the grid's 47 and a
/// moderately dim (gamma^2 = 0.3, alpha ~0.475) stored pixel's blended ~73:
/// picked with margin on both sides, not tuned to one fixture.
bool isStoredTraceish(juce::Colour c) {
    return c.getRed() <= c.getBlue() && c.getGreen() > 60;
}

/// Within a small per-channel tolerance of `target`. A 1px-tall extent (a
/// flat trace: min == max, so `fillColumn`'s height is exactly one row) is
/// still drawn through the software renderer's float-coordinate `fillRect`,
/// which shaves a few levels off an otherwise-opaque fill even at an
/// integer-aligned position -- an exact `==` would fail on every such pixel
/// though the fill is visibly, unambiguously the target colour. A taller
/// extent (a real dB range, several pixels high) has interior rows an exact
/// match WOULD catch; a degenerate one has none, so the tolerance is what
/// makes the assertion test the colour rather than the renderer's rounding.
bool closeTo(juce::Colour c, juce::Colour target, int tolerance) {
    return std::abs(static_cast<int>(c.getRed()) - static_cast<int>(target.getRed())) <= tolerance
        && std::abs(static_cast<int>(c.getGreen()) - static_cast<int>(target.getGreen())) <= tolerance
        && std::abs(static_cast<int>(c.getBlue()) - static_cast<int>(target.getBlue())) <= tolerance;
}

bool anyPixelDiffers(const juce::Image& image, juce::Rectangle<int> area, juce::Colour from) {
    for (int y = area.getY(); y < area.getBottom(); ++y) {
        for (int x = area.getX(); x < area.getRight(); ++x) {
            if (image.getPixelAt(x, y) != from) return true;
        }
    }
    return false;
}

bool anyPixelMatches(const juce::Image& image, juce::Rectangle<int> area,
                     const std::function<bool(juce::Colour)>& predicate) {
    for (int y = std::max(0, area.getY()); y < std::min(image.getHeight(), area.getBottom()); ++y) {
        for (int x = std::max(0, area.getX()); x < std::min(image.getWidth(), area.getRight()); ++x) {
            if (predicate(image.getPixelAt(x, y))) return true;
        }
    }
    return false;
}

/// The BRIGHTEST green channel among pixels in `area` matching `predicate`,
/// or 0 if none match. Used to tell an opaque stored-trace pixel (green 141)
/// from a low-coherence one (blended down to roughly 70-95 depending on what
/// was under it) -- `anyPixelMatches` alone proves presence, not dimness,
/// and a coherence-ignoring bug still satisfies "some stored-trace-ish pixel
/// is here".
juce::uint8 brightestMatchingGreen(const juce::Image& image, juce::Rectangle<int> area,
                                   const std::function<bool(juce::Colour)>& predicate) {
    juce::uint8 best = 0;
    for (int y = std::max(0, area.getY()); y < std::min(image.getHeight(), area.getBottom()); ++y) {
        for (int x = std::max(0, area.getX()); x < std::min(image.getWidth(), area.getRight()); ++x) {
            const auto c = image.getPixelAt(x, y);
            if (predicate(c)) best = std::max(best, c.getGreen());
        }
    }
    return best;
}

juce::Image renderView(TransferView& view, int width, int height) {
    juce::Image image(juce::Image::ARGB, width, height, true);
    juce::Graphics g(image);
    view.renderTo(g, juce::Rectangle<int>(0, 0, width, height));
    return image;
}

/// The brightest LIVE-trace-coloured pixel actually drawn in column `x`
/// across `[yTop, yBottom)` -- scanning rather than guessing a row, since a
/// magnitude/coherence curve puts different columns at different dB. Shared
/// by the two "dimmer at low trust" tests (magnitude and ribbon).
juce::uint8 brightestLiveTraceRed(const juce::Image& image, int x, int yTop, int yBottom) {
    juce::uint8 best = 0;
    for (int y = yTop; y < yBottom; ++y) {
        const auto c = image.getPixelAt(x, y);
        if (isLiveTraceish(c)) best = std::max(best, c.getRed());
    }
    return best;
}

}  // namespace
