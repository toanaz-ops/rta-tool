// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view.
#include "view/WorkspaceView.h"

#include <az_ui/az_ui.h>

#include <utility>

namespace rta::view {

WorkspaceView::WorkspaceView(std::vector<rta::trace::PaneSpec> panes, PaneFactory factory) {
    auto normalised = rta::trace::normalisePanes(std::move(panes));
    children_.reserve(normalised.size());
    weights_.reserve(normalised.size());

    for (auto& spec : normalised) {
        // resolvePaneView() already performs the fallback-and-report record
        // decision 6 asks for: an unrecognised `view` string still resolves
        // to `PaneView::Rta`, never to "build nothing" -- a dropped pane
        // here would shift every child after it out from under the caller's
        // own indexing.
        //
        // `resolution.fellBack` (and `.requested`) are read no further than
        // this line, on purpose -- NOT an oversight of PaneResolution's own
        // "the caller REPORTS this" contract. The pane is built either way,
        // which is the load-bearing half; the REPORTING half has no seam to
        // land in today, because nothing in this tree yet loads a saved
        // workspace (`MainComponent` only ever hands this constructor one
        // hardcoded `rta` `PaneSpec`, which never falls back). The seam that
        // owes this a report is whatever future code path decodes a session
        // file's `[pane]` sections and builds a `WorkspaceView` from them --
        // the same layer `SessionCodec`'s own "refuse what you do not
        // understand" / "never refuse a session over a layout word" split
        // already reasons about (docs/dsp/2026-08-29-display-layer-l5c.md
        // decision 6). Until that loader exists, plumbing `fellBack` any
        // further than here would be a report with no reader.
        const auto resolution = resolvePaneView(spec.view);
        auto child = factory(resolution.view);
        addAndMakeVisible(*child);
        weights_.push_back(spec.weight);
        children_.push_back(std::move(child));
    }
}

WorkspaceView::~WorkspaceView() = default;

void WorkspaceView::setLibrary(const rta::trace::TraceLibrary* library) {
    library_ = library;
    for (auto& child : children_) {
        if (auto* consumer = dynamic_cast<LibraryConsumer*>(child.get())) {
            consumer->setLibrary(library_);
        }
    }
}

void WorkspaceView::resized() {
    const auto rects = az::ui::splitVertically(getLocalBounds(), weights_, az::ui::gap);
    // splitVertically always returns exactly weights_.size() rectangles
    // (its own contract), which is exactly children_.size() by construction
    // above -- no bounds check needed on the zip below.
    for (std::size_t i = 0; i < children_.size(); ++i) {
        children_[i]->setBounds(rects[i]);
    }
}

}  // namespace rta::view
