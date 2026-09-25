// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. MainComponent.cpp's own L6a Wave 3 split
// (record docs/dsp/2026-09-16-spl-pro-l6a.md §8, §13 Q2): the calibration
// flow's UI half, moved here to keep MainComponent.cpp under the project's
// 400-line cap -- the same reason MainComponentDelay.cpp exists.
// Member-function definitions, declared in MainComponent.h, no different in
// kind from anything else in that class.
#include "MainComponent.h"

#include "export/SplCalibrationRecord.h"
#include "measure/CaptureTimeout.h"

namespace {

// Any length works -- CalibrationSession closes exactly one block over
// whatever span it is given (its own .cpp comment) -- so this is a plain
// responsiveness choice, the same shape as MainComponentDelay.cpp's own
// kCaptureLength.
constexpr std::size_t kCalibrationCaptureLength = 8192;
// kCalibrationRouteIndex (route 0's measurement channel -- where a
// calibrator clipped onto the mic capsule shows up) moved to
// `MainComponent::kCalibrationRouteIndex` (task W2-E2b part A): this file's
// own anonymous-namespace copy was invisible to MainComponentSpl.cpp, which
// needs the SAME channel number to mark a calibration verdict invalid and to
// bracket a calibration record's block-index range.
// Fix round finding 6: a calibrator-only rig has no REF channel, so the
// route-0 accumulator this shares with Locate never fills. Long enough that
// no real capture (well under a second at any audio sample rate) ever trips
// it; short enough that an operator is not left staring at a dead button.
constexpr std::int64_t kCalibrationCaptureTimeoutMs = 5000;

}  // namespace

void MainComponent::calibrationStartClicked() {
    if (locateWaitingForSettle_ || locateCaptureArmed_ || calibrationCaptureArmed_) {
        return;  // the one shared capture accumulator is busy
    }
    analysisThread_.armLocateCapture(kCalibrationRouteIndex, kCalibrationCaptureLength);
    calibrationCaptureArmed_ = true;
    calibrationCaptureIsStart_ = true;
    calibrationCaptureArmedAtMs_ = juce::Time::getMillisecondCounterHiRes();
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
    calibrationCaptureArmedAtMs_ = juce::Time::getMillisecondCounterHiRes();
    calibrationReadout_.setText("calibration: measuring end check...",
                                juce::dontSendNotification);
}

void MainComponent::pollCalibrationPipeline() {
    if (!calibrationCaptureArmed_) {
        return;
    }
    const auto capture = analysisThread_.locateCapture();
    if (capture != nullptr && capture != lastHandledCalibrationCapture_) {
        lastHandledCalibrationCapture_ = capture;
        calibrationCaptureArmed_ = false;

        // IEC 60942's 94.0 dB nominal (A4): the operator-supplied-level UI is
        // a separate affordance CalibrationSession already supports
        // (calibrationLevel accepts any value) and is not wired to a text
        // field in this pass. Sample rate comes from the INPUT bus, not the
        // output device (fix round finding 6) -- a calibrator is read on an
        // input channel, and the two devices' rates can differ or the output
        // device may not even be open on a capture-only rig.
        const auto level = rta::measure::calibrationLevel(rta::measure::kIec60942Level94Db);
        const double sampleRate = audioIo_.bus().sampleRate();
        const auto unixMs = static_cast<std::uint64_t>(juce::Time::currentTimeMillis());
        if (calibrationCaptureIsStart_) {
            calibrationSession_.recordStartCheck(level, capture->measurement, sampleRate, unixMs);
            // Task W2-E2b part A (record §8, §10 C4): the offset this check
            // just found is applied to the LIVE session by starting a fresh
            // log -- never to blocks the OLD, uncalibrated log already wrote.
            restartSplLoggingForCalibration();
        } else {
            calibrationSession_.recordEndCheck(level, capture->measurement, sampleRate, unixMs);
            writeCalibrationRecordAndUpdateInvalidFlag();
        }
        updateCalibrationReadout();
        return;
    }

    // Fix round finding 6: give up rather than leave calibrationCaptureArmed_
    // true forever, which would also lock LOCATE out (both check this flag)
    // on a rig where route 0 has no REF channel wired and the accumulator
    // this shares with Locate can never complete.
    if (rta::measure::captureTimedOut(calibrationCaptureArmedAtMs_,
                                      juce::Time::getMillisecondCounterHiRes(),
                                      static_cast<double>(kCalibrationCaptureTimeoutMs))) {
        calibrationCaptureArmed_ = false;
        calibrationReadout_.setText("calibration: capture timed out -- no REF channel routed?",
                                    juce::dontSendNotification);
    }
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

void MainComponent::restartSplLoggingForCalibration() {
    // Calibrating with nothing logging yet has no live session to restart --
    // SPL-R11's no-preferences-store simplicity means the offset just
    // measured only reaches a log the NEXT time one opens fresh (a device or
    // epoch change, `startFreshSplLog`, uncalibrated by that path's own
    // design); it is not persisted here to be replayed later.
    if (!splLoggingActive_) return;

    analysisThread_.disableSplLogging();

    rta::measure::SplConfig config;  // SplConfig{} defaults, SPL-R11, same as startFreshSplLog
    config.referenceOffsetDb = calibrationSession_.referenceOffsetDb();
    config.calibrated = true;
    const double calibratorLevelDb = calibrationSession_.startCheck().level.nominalDb;

    // `splLoggingActive_`/`lastSplEpoch_` are left exactly as they were: this
    // restart changes neither whether the bus is active nor its epoch, so
    // `pollSplLogging()`'s own edge-detected decision correctly reads NoOp on
    // the very next tick (measure/SplLoggingDecision.h) -- no state to
    // re-synchronise here beyond the fresh session directory itself.
    startFreshSplLogWithConfig(config, calibratorLevelDb, audioIo_.bus().epoch());

    // A fresh log is an unverified calibration state again -- see
    // AnalysisThreadSpl.cpp's own reset-block comment for why
    // `enableSplLogging` (started by the call above) already clears this
    // mirror; this call is defence in depth for the same fact stated once
    // more at the call site that most needs it to be true.
    analysisThread_.setCalibrationInvalid(kCalibrationRouteIndex, false);
}

void MainComponent::writeCalibrationRecordAndUpdateInvalidFlag() {
    if (currentSplSessionDir_.empty()) return;  // nothing logging -- nowhere to write the record

    const auto fields = calibrationSession_.reportFields();
    if (!fields.performed) return;  // both checks are required; recordEndCheck's own guard already
                                    // refuses an empty capture, so reaching here with !performed
                                    // would mean recordStartCheck never ran either

    // The new log's own blockIndex 0 IS the calibration START check
    // (restartSplLoggingForCalibration ran before this log wrote a single
    // block), so the range is [0, latest block on the calibration route].
    const auto blockCount = analysisThread_.splBlockCount(kCalibrationRouteIndex);
    const std::uint64_t endBlockIndex = blockCount > 0 ? blockCount - 1 : 0;

    rta::splexport::SplCalibrationRecordInfo info;
    info.fields = fields;
    info.channel = kCalibrationRouteIndex;
    info.startBlockIndex = 0;
    info.endBlockIndex = endBlockIndex;
    rta::splexport::writeCalibrationRecordFile(currentSplSessionDir_ + "/calibration.txt", info);

    // Record §15 A2 / §8: drift > 0.5 dB does NOT silently invalidate the
    // log (W3-A A3) -- it publishes the fact so the operator sees it live
    // and the report can apply it to the bracketed range when it reads the
    // record above.
    const bool invalid =
        fields.verdict.has_value() && *fields.verdict == rta::measure::CalibrationVerdict::Fail;
    analysisThread_.setCalibrationInvalid(kCalibrationRouteIndex, invalid);
}
