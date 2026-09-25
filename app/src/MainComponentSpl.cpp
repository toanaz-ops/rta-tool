// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. MainComponent.cpp's own L6a task W2-E2a split
// (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md "W2-E -- the wiring nobody
// was assigned"; record docs/dsp/2026-09-16-spl-pro-l6a.md §10, §13 Q7): the
// composition root's SPL wiring, moved here to keep MainComponent.cpp under
// the project's 400-line cap -- the same reason MainComponentDelay.cpp and
// MainComponentCalibration.cpp exist. Member-function definitions, declared
// in MainComponent.h, no different in kind from anything else in that class.
//
// WHAT THIS FILE DOES AND DOES NOT DO. It starts/stops the log-writing
// pipeline (AnalysisThread::enableSplLogging/disableSplLogging) when the bus
// this app measures from becomes active or inactive -- a real device opening
// or closing, OR `setSyntheticMode` switching on (the CI-testable path: no
// audio hardware is available on a CI runner, so synthetic mode is what
// proves this wiring in app/tests_juce/test_spl_log_wiring.cpp). It does
// NOT react to a measurement-channel ROLE CHANGE made while already active
// -- SPL-R11 has no preferences store, and re-deriving the channel list on
// every routing change is a defensible follow-up this task's own scope
// (W2-E2a, not W2-E2b) does not reach; the channel list is read once, at the
// moment logging turns on.
#include "MainComponent.h"

#include "measure/SplConfig.h"
#include "rta/platform/ChannelConfig.h"

#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>

namespace {

// The session directory's own leaf name: an ISO-8601-shaped, filesystem-safe
// UTC stamp ("20260925T143007Z"). Built from <chrono>/<ctime> rather than
// juce::Time::formatted()/toISO8601() -- both of those read LOCAL time
// (JUCE's own juce_Time.cpp: every getter goes through millisToLocal()) and
// this composition root wants the WALL CLOCK independent of the operator's
// timezone, matching SplLogHeaderInfo::startedAtUnixMs's own "no wall clock
// in any arithmetic, but one is recorded once as metadata" rule (record §2).
std::string utcTimestampForFolder() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTimeT = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_MSC_VER)
    gmtime_s(&utc, &nowTimeT);
#else
    gmtime_r(&nowTimeT, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y%m%dT%H%M%SZ");
    return out.str();
}

}  // namespace

void MainComponent::pollSplLogging() {
    const bool active = isSyntheticMode() || audioIo_.isRunning();
    if (active == splLoggingActive_) {
        return;  // no transition -- enable/disable only ever run on the edge
    }
    splLoggingActive_ = active;

    if (!active) {
        analysisThread_.disableSplLogging();
        return;
    }

    // Whichever channels currently hold the Measurement role -- exactly what
    // W2-E2's own task text asks for ("measurement channel(s)"), read once
    // here rather than tracked continuously (this file's own header
    // comment). `channelsWithRole` never allocates and never reads past
    // `buf`'s own size (ChannelConfig.h's own contract).
    std::array<int, static_cast<std::size_t>(rta::platform::kMaxChannels)> buf{};
    const int found =
        audioIo_.bus().config().channelsWithRole(rta::platform::ChannelRole::Measurement, buf);
    const std::span<const int> channels(buf.data(), static_cast<std::size_t>(found));

    // Documents/RTA Tool/spl/<UTC timestamp>/ -- the JUCE path resolution
    // happens HERE, at the composition root, exactly as the task brief asks;
    // AnalysisThread and SplLogPipeline below it receive a plain path string
    // and know nothing about juce::File (measure_has_no_framework_deps'
    // whole point).
    const auto sessionDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                 .getChildFile("RTA Tool")
                                 .getChildFile("spl")
                                 .getChildFile(utcTimestampForFolder());
    sessionDir.createDirectory();

    // SplConfig{} defaults, no preferences store (SPL-R11) -- the task
    // brief's own instruction. Wave 3's calibration offset is applied to a
    // LIVE session by starting a new log (W2-E2b, out of scope here); this
    // is the very first log of a session, so the default, uncalibrated
    // offset is correct for it.
    analysisThread_.enableSplLogging(rta::measure::SplConfig{}, channels,
                                     sessionDir.getFullPathName().toStdString());
}
