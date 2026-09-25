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
