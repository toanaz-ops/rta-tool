// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. H7 task T4 (docs/plans/2026-10-21-h7-generator-
// output-plan.md): the two questions only the composition root can answer for
// the generator -- what the app looks like right now (SYNTHETIC, a running
// device, how many outputs), and which measurement already holds the output.
// MainComponent.cpp is 365 lines, so the answers live here.
#include "MainComponent.h"

rta::measure::GeneratorEnvironment MainComponent::generatorEnvironment() const {
    rta::measure::GeneratorEnvironment env;
    env.synthetic = isSyntheticMode();
    env.deviceRunning = audioIo_.isRunning();
    env.numOutputs = isSyntheticMode() ? rta::measure::SyntheticInput::kOutputChannels
                                       : audioIo_.currentState().numOutputChannels;
    env.busyBy = outputHolderName();
    return env;
}

rta::measure::MeasurementDrive MainComponent::measurementDrive() const {
    return generator_.measurementDrive(generatorEnvironment().numOutputs);
}

void MainComponent::refreshMeasurementHints() {
    // H7 T8: a button that plays a source says at what level it would play it
    // NOW, in the current measurement mode -- not the -12 it used to be.
    const rta::measure::MeasurementDrive drive = measurementDrive();
    const auto rms = rta::measure::measurementLevelText(drive.levelDb, rta::measure::LevelConvention::Rms);
    const auto peak = rta::measure::measurementLevelText(drive.levelDb, rta::measure::LevelConvention::Peak);
    const std::string outs = rta::measure::outputsText(drive.outputs.empty() ? std::vector<int>{0} : drive.outputs);
    locateButton_.getProperties().set(az::ui::hintProperty, juce::String("pink noise " + rms + ", " + outs + ", one-shot"));
    sweep_.setLevelHint(juce::String("log sweep 20 Hz-20 kHz " + peak + ", " + outs + ", needs REF on route 1"));
}

std::string MainComponent::outputHolderName() const {
    // Never names GEN: this answers "who ELSE has the output" for GEN's own
    // start, which must refuse by that name (a GEN press while GEN plays is
    // the runner's AlreadyRunning, not a conflict).
    if (locateWaitingForSettle_ || locateCaptureArmed_) return "LOCATE";
    if (calibrationCaptureArmed_) return "CAL";
    if (sweep_.busy()) return "SWEEP";
    if (align_.busy()) return "ALIGN";
    if (eq_.verifyBusy()) return "VERIFY";
    return "";
}
