// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. Station-4 fix round, PR #31, verifier
// findings 1 (HIGH, epoch tracking) and 2 (MEDIUM, the composition root's
// decision is now a pure, provable function -- see SplLoggingDecision.h's
// own header comment for why this replaces the false "tested" claim that
// used to sit in MainComponentSpl.cpp).
#include "measure/SplLoggingDecision.h"

#include <catch2/catch_test_macros.hpp>

using rta::measure::decideSplLoggingAction;
using rta::measure::SplLoggingAction;
using rta::measure::SplLoggingDecisionInput;

TEST_CASE("off to off is NoOp", "[spllogging]") {
    SplLoggingDecisionInput in;
    in.wasActive = false;
    in.isActive = false;
    in.lastEpoch = 5;
    in.currentEpoch = 5;
    CHECK(decideSplLoggingAction(in) == SplLoggingAction::NoOp);
}

TEST_CASE("off to on is EnableFresh regardless of epoch", "[spllogging]") {
    SplLoggingDecisionInput in;
    in.wasActive = false;
    in.isActive = true;
    in.lastEpoch = 0;
    in.currentEpoch = 0;
    CHECK(decideSplLoggingAction(in) == SplLoggingAction::EnableFresh);
}

TEST_CASE("on to off is Disable", "[spllogging]") {
    SplLoggingDecisionInput in;
    in.wasActive = true;
    in.isActive = false;
    in.lastEpoch = 3;
    in.currentEpoch = 3;
    CHECK(decideSplLoggingAction(in) == SplLoggingAction::Disable);
}

TEST_CASE("on to on with the same epoch is NoOp", "[spllogging]") {
    SplLoggingDecisionInput in;
    in.wasActive = true;
    in.isActive = true;
    in.lastEpoch = 42;
    in.currentEpoch = 42;
    CHECK(decideSplLoggingAction(in) == SplLoggingAction::NoOp);
}

TEST_CASE("on to on with a DIFFERENT epoch is EnableFresh -- the HIGH finding",
          "[spllogging]") {
    // This is the exact case a sample-rate or device-list change produces:
    // AudioIo::setSampleRate / AudioIo_Devices.cpp restart the device
    // without ever clearing `running_`, so `isActive` reads true on both
    // sides of the change and the OLD edge-detect (isRunning() alone) could
    // never see it. The epoch is the only signal left standing.
    SplLoggingDecisionInput in;
    in.wasActive = true;
    in.isActive = true;
    in.lastEpoch = 7;
    in.currentEpoch = 8;
    CHECK(decideSplLoggingAction(in) == SplLoggingAction::EnableFresh);
}
