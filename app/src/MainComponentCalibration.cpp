// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. MainComponent.cpp's own L6a Wave 3 split
// (record docs/dsp/2026-09-16-spl-pro-l6a.md §8, §13 Q2): the calibration
// flow's UI half, moved here to keep MainComponent.cpp under the project's
// 400-line cap -- the same reason MainComponentDelay.cpp exists.
// Member-function definitions, declared in MainComponent.h, no different in
// kind from anything else in that class.
#include "MainComponent.h"

namespace {

// Any length works -- CalibrationSession closes exactly one block over
// whatever span it is given (its own .cpp comment) -- so this is a plain
// responsiveness choice, the same shape as MainComponentDelay.cpp's own
// kCaptureLength.
constexpr std::size_t kCalibrationCaptureLength = 8192;
// Calibrating route 0's MEASUREMENT channel: that is where a calibrator
// clipped onto the mic capsule shows up, the same channel Locate's own
// capture already reads from that route.
constexpr int kCalibrationRouteIndex = 0;

}  // namespace

void MainComponent::calibrationStartClicked() {
    if (locateWaitingForSettle_ || locateCaptureArmed_ || calibrationCaptureArmed_) {
        return;  // the one shared capture accumulator is busy
    }
    analysisThread_.armLocateCapture(kCalibrationRouteIndex, kCalibrationCaptureLength);
    calibrationCaptureArmed_ = true;
    calibrationCaptureIsStart_ = true;
    calibrationReadout_.setText("calibration: measuring start check...",
                                juce::dontSendNotification);
}

void MainComponent::calibrationEndClicked() {
    if (locateWaitingForSettle_ || locateCaptureArmed_ || calibrationCaptureArmed_) {
        return;
    }
    if (!calibrationSession_.hasStartCheck()) {
        calibrationReadout_.setText("calibration: run CAL START first",
                                    juce::dontSendNotification);
        return;
    }
    analysisThread_.armLocateCapture(kCalibrationRouteIndex, kCalibrationCaptureLength);
    calibrationCaptureArmed_ = true;
    calibrationCaptureIsStart_ = false;
    calibrationReadout_.setText("calibration: measuring end check...",
                                juce::dontSendNotification);
}

void MainComponent::pollCalibrationPipeline() {
    if (!calibrationCaptureArmed_) {
        return;
    }
    const auto capture = analysisThread_.locateCapture();
    if (capture == nullptr || capture == lastHandledCalibrationCapture_) {
        return;
    }
    lastHandledCalibrationCapture_ = capture;
    calibrationCaptureArmed_ = false;

    // IEC 60942's 94.0 dB nominal (A4): the operator-supplied-level UI is a
    // separate affordance CalibrationSession already supports (calibrationLevel
    // accepts any value) and is not wired to a text field in this pass.
    const auto level = rta::measure::calibrationLevel(rta::measure::kIec60942Level94Db);
    const double sampleRate = audioIo_.output().sampleRate();
    const auto unixMs = static_cast<std::uint64_t>(juce::Time::currentTimeMillis());
    if (calibrationCaptureIsStart_) {
        calibrationSession_.recordStartCheck(level, capture->measurement, sampleRate, unixMs);
    } else {
        calibrationSession_.recordEndCheck(level, capture->measurement, sampleRate, unixMs);
    }
    updateCalibrationReadout();
}

void MainComponent::updateCalibrationReadout() {
    if (!calibrationSession_.hasStartCheck()) {
        calibrationReadout_.setText("calibration: not started", juce::dontSendNotification);
        return;
    }
    if (!calibrationSession_.hasEndCheck()) {
        calibrationReadout_.setText(
            "calibration: start " +
                juce::String(calibrationSession_.startCheck().measuredLevelDb, 1) +
                " dB, offset " + juce::String(calibrationSession_.referenceOffsetDb(), 1) + " dB",
            juce::dontSendNotification);
        return;
    }

    const auto fields = calibrationSession_.reportFields();
    const juce::String verdictWord =
        fields.verdict == rta::measure::CalibrationVerdict::Pass ? "pass" : "FAIL";
    calibrationReadout_.setText(
        "calibration: drift " + juce::String(fields.driftDb, 1) + " dB -- " + verdictWord +
            " (" + juce::String(std::string(rta::measure::CalibrationSession::kClause)) + ")",
        juce::dontSendNotification);
}
