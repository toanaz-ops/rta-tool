// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of tools/. See snapshot_routing.h.
#include "snapshot_routing.h"

#include "SnapshotRender.h"

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "rta/platform/ChannelConfig.h"

#include <string>
#include <vector>

namespace rta::tools
{

// Tall enough that the OUTER rail (device panel + matrix + role table) fits
// without scrolling, so the picture shows the routing matrix's own inner
// scroll -- at 800 px the rail's outer viewport hides the matrix below the fold.
constexpr int kHeight = 1500;

void renderRoutingSpecimen (const juce::File& outDir, int& failures)
{
    MainComponent component;

    std::vector<std::string> names;
    for (int ch = 0; ch < 16; ++ch)
        names.push_back ("Dante In " + std::to_string (ch + 1));
    MainComponentTestAccess::applyChannelNamesForTest (component, names);

    // A route that lives past row 8: transfer function 3, channel 11
    // (row 12) measured against channel 0, plus one ordinary pair on
    // channels 1/2 so the top of the list is not blank.
    using rta::platform::ChannelRole;
    auto& config = MainComponentTestAccess::channelConfigForTest (component);
    config.setRole (0, ChannelRole::Reference);
    config.setTransferFunction (0, 3);
    config.setRole (11, ChannelRole::Measurement);
    config.setTransferFunction (11, 3);
    config.setRole (1, ChannelRole::Reference);
    config.setRole (2, ChannelRole::Measurement);

    component.setSize (1280, kHeight);
    component.resized();
    MainComponentTestAccess::routingMatrixWidgetForTest (component).refreshFromConfig();

    // Scroll the matrix so its last rows are on screen (rows 9-16 -- the ones
    // no version before H2 could show). The accessor is const because tests
    // only read; scrolling is a view-state poke on a test-only render.
    auto& viewport = const_cast<juce::Viewport&> (
        MainComponentTestAccess::routingMatrixWidgetForTest (component).viewportForTest());
    viewport.setViewPositionProportionately (0.0, 1.0);

    if (! renderComponent (component, outDir, "main-live-routing16.png", 1280, kHeight))
        ++failures;
}

} // namespace rta::tools
