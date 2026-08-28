// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Uses JUCE (it owns a juce::Image), so it
// is deliberately NOT in the measure_has_no_framework_deps file list -- the
// JUCE-free half of the gate is view/RepaintGate.h. See
// docs/specs/2026-08-28-trace-library-and-session.md §4.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "view/PlotGeometry.h"

#include <cstdint>

namespace rta::trace {
class TraceLibrary;
}

namespace rta::view {

/// Every visible stored trace, drawn once into a cached image and blitted
/// after that.
///
/// The point is the live frame's cost. Stored traces carry no snapshot
/// sequence, so re-stroking them on every published frame would make the plot
/// O(N) in the number of recalled captures -- and N is realistically dozens
/// after an afternoon of tuning. Nothing about them changes between library
/// edits, so they are rendered when `TraceLibrary::revision()` moves or the
/// plot geometry changes, and are a single image composite the rest of the
/// time. That is what keeps `RtaView`'s per-frame work O(1) in trace count.
///
/// The LIVE trace is not drawn here on purpose: it changes every frame, so
/// caching it would defeat the whole arrangement. `RtaView` strokes it over
/// this image.
class StoredTraceLayer {
public:
    /// Composites the cached image over `g`, rebuilding it first if anything
    /// the image depends on moved since the last call.
    void draw(juce::Graphics& g, const rta::trace::TraceLibrary& library,
              const PlotGeometry& geometry);

    /// Drops the cached image. NOT what makes the cache honest -- the key
    /// below does that, with no call required from anybody. This only returns
    /// the memory promptly when a view is repointed away from a library it
    /// will not draw again.
    void forget() noexcept;

private:
    void rebuild(const rta::trace::TraceLibrary& library, const PlotGeometry& geometry);

    juce::Image image_;

    /// The cache key: EVERY input the rasterised image depends on.
    ///
    /// `cachedLibrary_` is part of it because a revision alone cannot tell two
    /// libraries apart -- two of them very plausibly both sit at revision 0,
    /// and repointing a view between them would otherwise blit the first
    /// one's picture. Keying on identity means a swap invalidates by
    /// construction, whoever performs it and from wherever, so no call site
    /// has to remember a rule.
    ///
    /// It doubles as the "never built yet" state: `draw` takes the library by
    /// REFERENCE, so the address it compares can never be null, and a null
    /// here can only mean no rebuild has happened. That is why there is no
    /// separate `primed` flag -- and why a library with nothing visible, which
    /// caches an EMPTY image on purpose, is still recognised as cached rather
    /// than rebuilt on every frame.
    const rta::trace::TraceLibrary* cachedLibrary_ = nullptr;
    PlotGeometry cachedGeometry_{};
    std::uint64_t cachedRevision_ = 0;

    /// Where the cached image's (0,0) sits in component coordinates. Derived
    /// from the geometry at rebuild time and reused at blit time so the image
    /// lands back on exactly the pixels it was rasterised for.
    int originX_ = 0;
    int originY_ = 0;
};

}  // namespace rta::view
