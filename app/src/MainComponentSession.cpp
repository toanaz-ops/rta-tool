// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See MainComponentSession.h's own header
// comment for why this class exists and what it is trusted with.
#include "MainComponentSession.h"

#include "JuceFsPath.h"
#include "trace/SessionCodec.h"
#include "trace/SessionStore.h"
#include "trace/Trace.h"

#include <az_ui/az_ui.h>

#include <filesystem>
#include <optional>
#include <string>

namespace {

/// A human reason for every refusal Open can meet -- never a raw StoreStatus
/// on screen; an operator reading "refused: 2" has no idea what to do next.
juce::String refusalReason(rta::trace::StoreStatus status) {
    switch (status) {
        case rta::trace::StoreStatus::NotFound: return "no session here";
        case rta::trace::StoreStatus::NewerSchema: return "needs a newer build of this app";
        case rta::trace::StoreStatus::IoError: return "could not read the folder";
        case rta::trace::StoreStatus::Malformed: return "corrupt session";
        case rta::trace::StoreStatus::Ok: break;
    }
    return "corrupt session";
}

/// What Open reconstructs for one entry, before the live library is touched
/// at all -- see performOpen()'s own comment for why the whole batch is read
/// into a local vector first.
struct LoadedTrace {
    rta::trace::Trace trace;
    std::string name;
    std::string group;
    int shadeIndex = 0;
    bool visible = true;
};

}  // namespace

MainComponentSession::MainComponentSession(rta::trace::TraceLibrary& library,
                                           CurrentPaneSpec currentPaneSpec,
                                           RestorePaneView restorePaneView)
    : library_(library), currentPaneSpec_(std::move(currentPaneSpec)),
      restorePaneView_(std::move(restorePaneView)) {
    saveButton_.getProperties().set(az::ui::hintProperty, "writes traces + layout to a folder");
    saveButton_.onClick = [this] { saveClicked(); };

    openButton_.getProperties().set(az::ui::hintProperty, "replaces the library from a folder");
    openButton_.onClick = [this] { openClicked(); };

    readout_.setText("session: not saved yet", juce::dontSendNotification);
    readout_.setJustificationType(juce::Justification::centredLeft);
}

void MainComponentSession::attachTo(juce::Component& parent) {
    parent.addAndMakeVisible(saveButton_);
    parent.addAndMakeVisible(openButton_);
    parent.addAndMakeVisible(readout_);
}

void MainComponentSession::layout(juce::Rectangle<int> area) {
    auto buttonRow = area.removeFromTop(az::ui::buttonCellHeight);
    const int buttonWidth = (buttonRow.getWidth() - az::ui::gap) / 2;
    saveButton_.setBounds(buttonRow.removeFromLeft(buttonWidth));
    buttonRow.removeFromLeft(az::ui::gap);
    openButton_.setBounds(buttonRow);
    area.removeFromTop(az::ui::gap);
    readout_.setBounds(area.removeFromTop(az::ui::buttonCellHeight));
}

void MainComponentSession::saveClicked() {
    const auto defaultDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                .getChildFile("RTA Tool")
                                .getChildFile("sessions");
    chooser_ = std::make_unique<juce::FileChooser>("Save session to folder", defaultDir);
    const auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectDirectories;
    chooser_->launchAsync(flags, [this](const juce::FileChooser& fc) {
        const auto result = fc.getResult();
        if (result == juce::File{}) return;  // cancelled
        performSave(result);
    });
}

void MainComponentSession::openClicked() {
    const auto defaultDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                .getChildFile("RTA Tool")
                                .getChildFile("sessions");
    chooser_ = std::make_unique<juce::FileChooser>("Open session folder", defaultDir);
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    chooser_->launchAsync(flags, [this](const juce::FileChooser& fc) {
        const auto result = fc.getResult();
        if (result == juce::File{}) return;  // cancelled
        performOpen(result);
    });
}

void MainComponentSession::performSave(const juce::File& folder) {
    const rta::trace::SessionStore store(toFsPath(folder));

    rta::trace::SessionDocument doc;
    doc.panes = {currentPaneSpec_()};

    // Traces are written BEFORE the index names them: a write that fails
    // partway (disk full, permission lost mid-session) must never leave an
    // index claiming a blob that is not actually on disk -- Open's own
    // contract, and the reason this loop skips adding an entry whose blob
    // write failed rather than adding it optimistically.
    for (const auto& entry : library_.entries()) {
        const auto* trace = library_.trace(entry.traceId);
        if (trace == nullptr) continue;  // entries_/traces_ are parallel by TraceLibrary's own contract; defensive only
        if (store.writeTrace(*trace) != rta::trace::StoreStatus::Ok) continue;
        doc.captures.push_back(trace->meta());
        doc.entries.push_back(entry);
    }

    const auto status = store.writeIndex(doc);
    readout_.setText(status == rta::trace::StoreStatus::Ok
                         ? juce::String("SAVED ") + folder.getFullPathName()
                         : juce::String("SAVE FAILED: ") + folder.getFullPathName() + " (could not write)",
                     juce::dontSendNotification);
}

void MainComponentSession::performOpen(const juce::File& folder) {
    const rta::trace::SessionStore store(toFsPath(folder));

    rta::trace::SessionDocument doc;
    const auto indexStatus = store.readIndex(doc);
    if (indexStatus != rta::trace::StoreStatus::Ok) {
        // Refused before anything is touched: the current library and pane
        // layout are exactly what they were before this click.
        readout_.setText(juce::String("OPEN FAILED: ") + folder.getFullPathName() + " (" +
                             refusalReason(indexStatus) + ")",
                         juce::dontSendNotification);
        return;
    }

    // Read every trace the index names into a LOCAL vector first, before
    // touching library_ at all -- so a folder whose index parses but whose
    // traces/ blobs are missing or corrupt (a partial folder: an interrupted
    // Save, or a hand-edited index) can still refuse wholesale, leaving the
    // current library untouched, rather than replacing it with a half-empty
    // one partway through.
    std::vector<LoadedTrace> loaded;
    int skipped = 0;
    for (const auto& capture : doc.captures) {
        std::optional<rta::trace::Trace> trace;
        if (store.readTrace(capture, trace) != rta::trace::StoreStatus::Ok || !trace.has_value()) {
            ++skipped;  // this one trace's blob is missing or corrupt
            continue;
        }
        std::string name = capture.id;
        std::string group;
        int shadeIndex = 0;
        bool visible = true;
        for (const auto& entry : doc.entries) {
            if (entry.traceId == capture.id) {
                name = entry.name;
                group = entry.group;
                shadeIndex = entry.shadeIndex;
                visible = entry.visible;
                break;
            }
        }
        loaded.push_back(LoadedTrace{std::move(*trace), std::move(name), std::move(group), shadeIndex, visible});
    }

    if (loaded.empty() && !doc.captures.empty()) {
        // Every trace failed to read even though the index itself parsed --
        // refuse wholesale rather than opening to an empty library that
        // LOOKS like a deliberately empty session. library_.clear() has not
        // run yet, so nothing has changed.
        readout_.setText(juce::String("OPEN FAILED: ") + folder.getFullPathName() + " (no readable traces)",
                         juce::dontSendNotification);
        return;
    }

    // Everything needed is already in hand -- replace the library in one
    // step (TraceLibrary::clear(), a single revision bump) rather than
    // leaving a moment where the old and new sessions' traces coexist.
    library_.clear();
    for (auto& item : loaded) {
        const auto id = library_.add(std::move(item.trace), item.name, item.group);
        if (id.empty()) continue;  // duplicate id in a hand-edited index -- defensive, never hit by writeIndex's own output
        if (!item.visible) [[maybe_unused]] const bool okVisible = library_.setVisible(id, false);
        [[maybe_unused]] const bool okShade = library_.setShadeIndex(id, item.shadeIndex);
    }

    // The loaded session's own pane layout always wins over whatever was
    // showing before Open was clicked -- normalisePanes turns an empty list
    // (a session saved before workspaces existed) into the same single
    // default `rta` pane a brand-new session gets.
    const auto paneReport = restorePaneView_(rta::trace::normalisePanes(doc.panes));

    juce::String message = juce::String("OPENED ") + folder.getFullPathName();
    if (paneReport.fellBack) {
        // PaneRegistry.h's PaneResolution: "the caller REPORTS this" -- this
        // is that report (fix round, PR #43 verifier MEDIUM F2).
        message += juce::String(" -- pane \"") + juce::String(paneReport.requested) +
                   "\" unknown, showing RTA";
    }
    if (skipped > 0) message += " (" + juce::String(skipped) + " trace(s) skipped)";
    readout_.setText(message, juce::dontSendNotification);
}
