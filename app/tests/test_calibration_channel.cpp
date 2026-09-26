// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Task W2-E2b fix round (verifier HIGH finding): `calibrationMeasurementChannel`
// resolves a ROUTE POSITION (`AnalysisThread::armLocateCapture`'s own
// indexing) to the CHANNEL NUMBER every other calibration-facing
// `AnalysisThread` API (`splBlockCount`, `setCalibrationInvalid`) actually
// needs. JUCE-free.
#include "measure/CalibrationChannel.h"

#include <catch2/catch_test_macros.hpp>

using rta::measure::calibrationMeasurementChannel;
using rta::measure::CalibrationChannelDecision;
using rta::measure::decideCalibrationRecordChannel;
using rta::measure::RoutingPlan;
using rta::measure::TransferRoute;

TEST_CASE("calibrationMeasurementChannel resolves route position 0 to its own "
         "measurement channel, NOT literal channel 0",
         "[calibration_channel]") {
    // The exact scenario the verifier named: Measurement assigned to channel
    // 1, Reference to channel 0 -- one click away in ChannelRoleTable, and
    // `planRouting` places this single route at position 0 regardless (its
    // own doc comment: "ascending measurement channel", and there is only
    // one route here). Before this fix, every calibration call site used the
    // route POSITION (0) directly as a channel number and silently read/wrote
    // channel 0's counters instead of channel 1's.
    RoutingPlan plan;
    plan.routes.push_back(TransferRoute{/*tfIndex=*/0, /*referenceChannel=*/0,
                                        /*measurementChannel=*/1});

    const auto channel = calibrationMeasurementChannel(plan, /*routeIndex=*/0);
    REQUIRE(channel.has_value());
    CHECK(*channel == 1);
}

TEST_CASE("calibrationMeasurementChannel picks the route at the given position, "
         "not the lowest measurement channel",
         "[calibration_channel]") {
    RoutingPlan plan;
    plan.routes.push_back(TransferRoute{0, /*referenceChannel=*/2, /*measurementChannel=*/3});
    plan.routes.push_back(TransferRoute{1, /*referenceChannel=*/4, /*measurementChannel=*/5});

    CHECK(calibrationMeasurementChannel(plan, 0) == 3);
    CHECK(calibrationMeasurementChannel(plan, 1) == 5);
}

TEST_CASE("calibrationMeasurementChannel is absent for an out-of-range route index",
         "[calibration_channel]") {
    RoutingPlan plan;  // empty -- the calibrator-only, no-REF-channel rig (record §8)
    CHECK_FALSE(calibrationMeasurementChannel(plan, 0).has_value());

    plan.routes.push_back(TransferRoute{0, 1, 2});
    CHECK_FALSE(calibrationMeasurementChannel(plan, 1).has_value());
    CHECK_FALSE(calibrationMeasurementChannel(plan, -1).has_value());
}

// Fix round 3 (verifier MEDIUM, upgraded from LOW): an operator can
// reassign ChannelRoleTable roles between CAL START and CAL END, so the two
// checks' resolved channels are not guaranteed equal. This is the pure
// decision `writeCalibrationRecordAndUpdateInvalidFlag` calls before writing
// anything.
TEST_CASE("decideCalibrationRecordChannel writes only when both checks resolved "
         "to the SAME channel",
         "[calibration_channel]") {
    CHECK(decideCalibrationRecordChannel(1, 1) == CalibrationChannelDecision::Write);
    CHECK(decideCalibrationRecordChannel(0, 0) == CalibrationChannelDecision::Write);
}

TEST_CASE("decideCalibrationRecordChannel refuses a channel mismatch between "
         "start and end",
         "[calibration_channel]") {
    // The exact scenario the verifier named: an operator swaps Measurement
    // from channel 1 to channel 0 (or vice versa) between the two checks.
    CHECK(decideCalibrationRecordChannel(1, 0) == CalibrationChannelDecision::RefuseChannelMismatch);
    CHECK(decideCalibrationRecordChannel(0, 1) == CalibrationChannelDecision::RefuseChannelMismatch);
}

TEST_CASE("decideCalibrationRecordChannel refuses when either side never resolved "
         "a channel, even when the OTHER side matches -1's own value",
         "[calibration_channel]") {
    // -1 is this header's own "no channel" sentinel (calibrationMeasurementChannel's
    // value_or(-1)) -- an empty routing plan must never be treated as "channel
    // -1", agreeing with itself.
    CHECK(decideCalibrationRecordChannel(-1, -1) ==
         CalibrationChannelDecision::RefuseNoMeasurementChannel);
    CHECK(decideCalibrationRecordChannel(-1, 2) ==
         CalibrationChannelDecision::RefuseNoMeasurementChannel);
    CHECK(decideCalibrationRecordChannel(3, -1) ==
         CalibrationChannelDecision::RefuseNoMeasurementChannel);
}
