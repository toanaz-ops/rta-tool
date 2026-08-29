// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Decision 6 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "trace/Workspace.h"
#include "view/PaneRegistry.h"

#include <functional>
#include <memory>
#include <vector>

namespace rta::view {

/// The 1..3 pane vertical stack (record decision 6): one child per
/// `rta::trace::PaneSpec`, built through a factory the COMPOSITION ROOT
/// supplies.
///
/// This class knows the pane VOCABULARY (`PaneView`, from `PaneRegistry.h`)
/// but not the pane CLASSES: `MainComponent`, not this file, is what knows
/// an `rta::view::RtaView` needs an `AnalysisThread` reference to construct,
/// and this file including `RtaView.h`/`TransferView.h` to build one itself
/// would make it a second composition root, wiring dependencies it has no
/// business knowing about. The factory is handed the resolved `PaneView` and
/// returns a plain `juce::Component`; `WorkspaceView` never asks what
/// concrete type came back.
///
/// `setLibrary` reaches every child through `LibraryConsumer`
/// (`view/PaneRegistry.h`) for the same reason: a `dynamic_cast` needs only
/// that interface's definition, never the concrete pane header. "Forwards to
/// every child that accepts one" (task brief) -- a pane type that never
/// implements `LibraryConsumer` (a future spectrograph, say) is left alone,
/// not treated as an error.
class WorkspaceView final : public juce::Component {
public:
    using PaneFactory = std::function<std::unique_ptr<juce::Component>(PaneView)>;

    /// `panes` is normalised (`trace::normalisePanes`) before anything is
    /// built, so an empty vector -- a session saved before workspaces
    /// existed, or a brand-new one -- yields exactly one default `rta` pane,
    /// the same fallback every other reader of a `PaneSpec` list gets.
    /// `factory` is consumed synchronously, once per pane, inside this
    /// constructor; it is not stored, because nothing in this task rebuilds
    /// the pane set after construction.
    WorkspaceView(std::vector<rta::trace::PaneSpec> panes, PaneFactory factory);
    ~WorkspaceView() override;

    /// Forwarded to every child that implements `LibraryConsumer`; a child
    /// that does not is left alone. Nullable, the same contract every
    /// `LibraryConsumer` implementer states on its own `setLibrary`
    /// (RtaView.h, TransferView.h).
    void setLibrary(const rta::trace::TraceLibrary* library);

    /// Splits `getLocalBounds()` in proportion to each pane's normalised
    /// weight (`az::ui::splitVertically`, `az::ui::gap` between panes) and
    /// assigns the result to the children in pane order -- the same order
    /// `addAndMakeVisible` was called in, so pane index and child index
    /// (`getChildComponent`) always agree.
    void resized() override;

private:
    /// Owns the children; `addAndMakeVisible` registers them in the
    /// component hierarchy but takes no ownership of its own, so this vector
    /// is what keeps each pane alive for the life of the workspace.
    std::vector<std::unique_ptr<juce::Component>> children_;

    /// Parallel to `children_` by index -- pane i's normalised weight is
    /// `weights_[i]`, fed to `az::ui::splitVertically` on every `resized()`.
    std::vector<float> weights_;

    const rta::trace::TraceLibrary* library_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WorkspaceView)
};

}  // namespace rta::view
