// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. H8 review M-2: `RTA_API_ENABLED=1` with a port
// that fails to bind used to be silent. MainComponent.cpp is at its 400-line
// cap, so the notice lives here.
#include "MainComponent.h"
#include "TestProfile.h"

#include "api/ApiStatusText.h"

void MainComponent::parentHierarchyChanged() {
    if (apiServer_ == nullptr) {
        return;  // still inside the constructor
    }
    const std::string text = rta::api::apiNotRunningText(apiSettings_, apiServer_->running(),
                                                         apiServer_->refusal());
    if (text.empty()) {
        return;
    }
    // Always logged. Also shown in the title bar of the window that holds this
    // component, the one place an operator who launched with the variable set
    // is certain to look; a headless caller (tools/snapshot.cpp) has no
    // window, so there is nothing to retitle.
    juce::Logger::writeToLog(juce::String(text));
    if (auto* top = getTopLevelComponent(); top != nullptr && top != this) {
        top->setName(rta::app::testprofile::windowTitle("RTA Tool -- " + juce::String(text)));
    }
}
