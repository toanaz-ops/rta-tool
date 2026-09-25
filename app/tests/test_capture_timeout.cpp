// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L6a Wave 3 fix round, verifier round 1 finding 6: a calibrator-only rig
// has no REF channel, so armLocateCapture's shared accumulator (Locate's own,
// reused for calibration) never fills, and calibrationCaptureArmed_ stays
// true for the rest of the session -- locking LOCATE out too. captureTimedOut
// is the one decision that ends that lock; this pins it, JUCE-free.
#include <catch2/catch_test_macros.hpp>

#include "measure/CaptureTimeout.h"

using rta::measure::captureTimedOut;

TEST_CASE("captureTimedOut is false before the deadline, true after", "[capturetimeout]") {
    CHECK_FALSE(captureTimedOut(/*armedAtMs=*/0, /*nowMs=*/0, /*timeoutMs=*/5000));
    CHECK_FALSE(captureTimedOut(0, 4999, 5000));
    CHECK_FALSE(captureTimedOut(0, 5000, 5000));  // AT the deadline: not yet timed out
    CHECK(captureTimedOut(0, 5001, 5000));

    // A non-zero arm time is the real shape: only the ELAPSED time matters.
    CHECK_FALSE(captureTimedOut(1'000'000, 1'004'999, 5000));
    CHECK(captureTimedOut(1'000'000, 1'005'001, 5000));
}
