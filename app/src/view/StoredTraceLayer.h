// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Uses JUCE (it owns a juce::Image), so it
// is deliberately NOT in the measure_has_no_framework_deps file list -- the
// JUCE-free half of the gate is view/RepaintGate.h. See
// docs/specs/2026-08-28-trace-library-and-session.md §4.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "trace/Trace.h"
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
    /// Which field this instance rasterises. Fixed at construction, NOT part of
    /// the cache key: one instance draws one field for its whole life, so a key
    /// term for it could never differ between two calls. The Bode composite
    /// owns two instances, one per pane.
    explicit StoredTraceLayer(rta::trace::Field field = rta::trace::Field::Magnitude);

    /// Composites the cached image over `g`, rebuilding it first if anything
    /// the image depends on moved since the last call.
    void draw(juce::Graphics& g, const rta::trace::TraceLibrary& library,
              const PlotGeometry& geometry);

    /// Drops the cached image. NOT what makes the cache honest -- the key
    /// below does that, with no call required from anybody. This only returns
    /// the memory promptly when a view is repointed away from a library it
    /// will not draw again.
    void forget() noexcept;

    /// How many times the cached image has actually been rebuilt.
    ///
    /// This is production API added purely so the caching contract can be
    /// TESTED rather than asserted. "The live repaint is O(1) in trace count"
    /// is entirely a claim about how often this number moves, and every other
    /// observable of this class -- the pixels on screen -- is identical
    /// whether the image was reused or re-rasterised from scratch. Without a
    /// counter the claim is unfalsifiable, so a regression that rebuilds every
    /// frame would look exactly like correct code and would only ever surface
    /// as dropped frames at a live show.
    ///
    /// That is the trade: one word of state and one accessor, in exchange for
    /// the one property this class exists for being checkable by a test. It is
    /// NOT dead code -- `app/tests_juce/test_stored_trace_layer.cpp` is its
    /// caller. Deleting it deletes the tests.
    [[nodiscard]] std::uint64_t rebuildCount() const noexcept { return rebuildCount_; }

private:
    void rebuild(const rta::trace::TraceLibrary& library, const PlotGeometry& geometry);

    /// See the constructor's comment: fixed for the instance's whole life,
    /// deliberately absent from the cache key below.
    const rta::trace::Field field_;

    juce::Image image_;

    /// The cache key: EVERY input the rasterised image depends on.
    ///
    /// `cachedGeneration_` is part of it because a revision alone cannot tell
    /// two libraries apart -- two of them very plausibly both sit at revision
    /// 0, and repointing a view between them would otherwise blit the first
    /// one's picture. This used to key on `&library` instead, but a freed
    /// library can be replaced by a new one at the very same address (a
    /// same-sized allocation reusing a freed slot is routine, not exotic), and
    /// a fresh library at revision 0 with unchanged geometry would then
    /// false-hit a stale image. `TraceLibrary::generation()` is drawn from a
    /// process-wide counter that only ever increases, so it cannot repeat the
    /// way an address can.
    ///
    /// `hasCached_` carries the "never built yet" state that the raw pointer
    /// used to carry for free (a reference has no null address to start
    /// unequal to). A library with nothing visible still caches an EMPTY image
    /// on purpose, so this has to be a real flag, not "generation == some
    /// sentinel" -- generation 0 is a legitimate library.
    bool hasCached_ = false;
    std::uint64_t cachedGeneration_ = 0;
    PlotGeometry cachedGeometry_{};
    std::uint64_t cachedRevision_ = 0;

    /// Where the cached image's (0,0) sits in component coordinates. Derived
    /// from the geometry at rebuild time and reused at blit time so the image
    /// lands back on exactly the pixels it was rasterised for.
    int originX_ = 0;
    int originY_ = 0;

    /// Counts calls to `rebuild`, not calls to `draw`. 64 bits so it cannot
    /// wrap inside any session a human will sit through, which keeps a test's
    /// "moved by exactly one" assertion meaningful.
    std::uint64_t rebuildCount_ = 0;
};

}  // namespace rta::view
