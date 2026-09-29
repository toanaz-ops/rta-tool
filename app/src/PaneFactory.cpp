// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See PaneFactory.h.
#include "PaneFactory.h"

#include "view/CrossoverPaneView.h"
#include "view/PaneRegistry.h"
#include "view/RtaView.h"
#include "view/SplView.h"
#include "view/TransferView.h"

rta::view::WorkspaceView::PaneFactory makePaneFactory(rta::measure::SnapshotSource& source,
                                                      rta::view::EqPaneBinding eq) {
    return [&source, eq](rta::view::PaneView view) -> std::unique_ptr<juce::Component> {
        if (view == rta::view::PaneView::Transfer) {
            return std::make_unique<rta::view::TransferView>(source);
        }
        if (view == rta::view::PaneView::Spl) {
            return std::make_unique<rta::view::SplView>(source);
        }
        if (view == rta::view::PaneView::Xover) {
            // No `source` needed: G18's whole subject is two already-captured
            // library traces, never a running analyser (CrossoverSurface.h's
            // "THERE IS NO OBJECTIVE HERE"). Its own setLibrary is what
            // WorkspaceView::setLibrary reaches through LibraryConsumer.
            return std::make_unique<rta::view::CrossoverPaneView>();
        }
        if (view == rta::view::PaneView::Eq) {
            // Model and actions come from MainComponentEq, not from `source`:
            // the pane equalises an already-stored trace, like XOVER.
            return std::make_unique<rta::view::EqPaneView>(eq);
        }
        return std::make_unique<rta::view::RtaView>(source);
    };
}
