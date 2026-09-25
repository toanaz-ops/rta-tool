// SPDX-License-Identifier: AGPL-3.0-or-later
// Station-4 fix round, PR #31, round 3, verifier finding 3 (LOW): the
// session folder name's collision property, pulled into a pure function
// (measure/SplSessionFolderName.h) so it is provable OFF against a FIXED
// instant rather than only by reading a real clock in a JUCE test.
#include "measure/SplSessionFolderName.h"

#include <catch2/catch_test_macros.hpp>

using rta::measure::formatUtcTimestamp;
using rta::measure::sessionFolderName;

TEST_CASE("sessionFolderName is unique across epochs for the identical "
         "instant",
         "[spl_session_folder_name]") {
    // A fixed instant, not the real clock -- the collision this fix closes
    // is "same second, different epoch", so the test pins the second and
    // varies only the epoch, exactly the axis the fix adds.
    constexpr std::time_t kInstant = 1'700'000'000;

    const auto epoch7 = sessionFolderName(kInstant, 7);
    const auto epoch8 = sessionFolderName(kInstant, 8);
    CHECK(epoch7 != epoch8);

    const auto timestamp = formatUtcTimestamp(kInstant);
    CHECK(epoch7 == timestamp + "-e7");
    CHECK(epoch8 == timestamp + "-e8");

    // The same (instant, epoch) pair is still deterministic -- this is a
    // folder name, not a nonce; two calls describing the SAME session must
    // agree.
    CHECK(sessionFolderName(kInstant, 7) == epoch7);
}
