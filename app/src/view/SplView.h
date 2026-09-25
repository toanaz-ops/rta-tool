// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view.
// Lane L6a task W2-D (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md §4, §11, SPL-R11).
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "measure/SnapshotSource.h"

namespace rta::view {

/// The live SPL pane. Wave 2 wires no history ring into `Snapshot` yet --
/// `SplHistory` (`view/SplStrip.h`'s own subject) is fed nowhere upstream of
/// this class in this lane -- so this view reads `Snapshot::spl`'s single
/// LATEST reading and prints it as text; the scrolling strip picture is
/// `SplPreview`'s job, over canned data, until a later wave publishes a real
/// history for this class to draw from.
///
/// No `LibraryConsumer`: an SPL pane has nothing to do with the stored-trace
/// library `RtaView`/`TransferView` read from.
class SplView final : public juce::Component, private juce::Timer {
public:
    explicit SplView(const rta::measure::SnapshotSource& source);
    ~SplView() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    const rta::measure::SnapshotSource* source_;
    std::uint64_t lastSequence_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplView)
};

}  // namespace rta::view
