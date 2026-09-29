// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. See MainComponentEq.h.
#include "MainComponentEq.h"

#include "export/EqTextExport.h"
#include "export/FirExport.h"
#include "export/FirWavExport.h"
#include "measure/EqFirDesign.h"

#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace {

/// Documents/RTA Tool/<sub>: where every save dialog opens, alongside the
/// sessions/ folder MainComponentSession already uses.
juce::File exportFolder(const char* sub) {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("RTA Tool")
        .getChildFile(sub);
}

/// Writes `text` byte for byte: UTF-8, no BOM, and NO line-ending rewrite
/// (juce::File::replaceWithText would turn every "\n" into "\r\n"). The
/// replace is atomic through a temporary file.
bool writeTextFile(const juce::File& file, const std::string& text) {
    if (!file.getParentDirectory().createDirectory().wasOk()) return false;
    return file.replaceWithData(text.data(), text.size());
}

}  // namespace

MainComponentEq::MainComponentEq() = default;

rta::view::EqPaneBinding MainComponentEq::binding() {
    rta::view::EqPaneBinding out;
    out.model = &model_;
    out.actions.exportFirText = [this] { exportFirTextClicked(); };
    out.actions.exportFirWav = [this] { exportFirWavClicked(); };
    out.actions.exportList = [this] { exportListClicked(); };
    return out;
}

juce::File MainComponentEq::defaultFirFile(const char* extension) const {
    if (!model_.firPhase() || !model_.firTaps()) return {};
    const std::string stem = rta::firexport::firFilenameStem("eq", model_.sampleRate(), *model_.firTaps(),
                                                             *model_.firPhase());
    return exportFolder("fir").getChildFile(juce::String(stem) + extension);
}

void MainComponentEq::launchSave(const juce::String& title, const juce::File& defaultFile,
                                 const char* extension, std::function<bool(const juce::File&)> perform) {
    chooser_ = std::make_unique<juce::FileChooser>(title, defaultFile, juce::String("*") + extension);
    const auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                       juce::FileBrowserComponent::warnAboutOverwriting;
    chooser_->launchAsync(flags, [perform = std::move(perform), extension](const juce::FileChooser& fc) {
        const auto result = fc.getResult();
        if (result == juce::File{}) return;  // cancelled: nothing happens
        perform(result.hasFileExtension(extension) ? result : result.withFileExtension(extension));
    });
}

void MainComponentEq::exportFirTextClicked() {
    if (!model_.canExportFir()) {
        model_.setStatus("EXPORT FIR: answer FIR PHASE and FIR LENGTH first");
        return;
    }
    launchSave("Export FIR (text)", defaultFirFile(".txt"), ".txt",
               [this](const juce::File& f) { return performExportFirText(f); });
}

void MainComponentEq::exportFirWavClicked() {
    if (!model_.canExportFir()) {
        model_.setStatus("EXPORT FIR: answer FIR PHASE and FIR LENGTH first");
        return;
    }
    launchSave("Export FIR (WAV)", defaultFirFile(".wav"), ".wav",
               [this](const juce::File& f) { return performExportFirWav(f); });
}

void MainComponentEq::exportListClicked() {
    if (!model_.canExportList()) {
        model_.setStatus("EXPORT LIST: no filters committed");
        return;
    }
    launchSave("Export filter list", exportFolder("eq").getChildFile("eq_filters.txt"), ".txt",
               [this](const juce::File& f) { return performExportList(f); });
}

bool MainComponentEq::performExportFirText(const juce::File& file) {
    if (!model_.canExportFir()) {
        model_.setStatus("EXPORT FIR TXT: answer FIR PHASE and FIR LENGTH first");
        return false;
    }
    try {
        const auto result = rta::measure::designEqFir(model_.session().committed(), model_.sampleRate(),
                                                      *model_.firTaps(), *model_.firPhase());
        if (!writeTextFile(file, rta::firexport::renderFirText(result, model_.normalization()))) {
            model_.setStatus("EXPORT FIR TXT FAILED: cannot write " + file.getFullPathName().toStdString());
            return false;
        }
    } catch (const std::exception& e) {
        model_.setStatus(std::string("EXPORT FIR TXT FAILED: ") + e.what());
        return false;
    }
    model_.setStatus("EXPORT FIR TXT: wrote " + file.getFileName().toStdString());
    return true;
}

bool MainComponentEq::performExportFirWav(const juce::File& file) {
    if (!model_.canExportFir()) {
        model_.setStatus("EXPORT FIR WAV: answer FIR PHASE and FIR LENGTH first");
        return false;
    }
    try {
        const auto result = rta::measure::designEqFir(model_.session().committed(), model_.sampleRate(),
                                                      *model_.firTaps(), *model_.firPhase());
        if (!file.getParentDirectory().createDirectory().wasOk()) {
            model_.setStatus("EXPORT FIR WAV FAILED: cannot create " + file.getParentDirectory().getFullPathName().toStdString());
            return false;
        }
        rta::firexport::writeFirWav(file, result, model_.normalization());
    } catch (const std::exception& e) {
        model_.setStatus(std::string("EXPORT FIR WAV FAILED: ") + e.what());
        return false;
    }
    model_.setStatus("EXPORT FIR WAV: wrote " + file.getFileName().toStdString());
    return true;
}

bool MainComponentEq::performExportList(const juce::File& file) {
    if (!model_.canExportList()) {
        model_.setStatus("EXPORT LIST: no filters committed");
        return false;
    }
    // The applied flag travels with each row (EQ record Sec.7, amendment 2):
    // dropping it would make the list unsafe to load into the very rig the
    // measurement came through.
    std::vector<rta::eqexport::ExportedFilter> rows;
    for (const auto& filter : model_.session().committed()) {
        rows.push_back(rta::eqexport::ExportedFilter{ filter.spec, filter.applied });
    }
    if (!writeTextFile(file, rta::eqexport::renderFilterList(rows, model_.sampleRate()))) {
        model_.setStatus("EXPORT LIST FAILED: cannot write " + file.getFullPathName().toStdString());
        return false;
    }
    model_.setStatus("EXPORT LIST: wrote " + file.getFileName().toStdString());
    return true;
}
