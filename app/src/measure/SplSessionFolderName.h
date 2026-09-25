// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// Station-4 fix round (PR #31, round 3, LOW finding 3): the SPL session
// folder name, pulled out of MainComponentSpl.cpp's own utcTimestampForFolder
// () + string concatenation so the ONE property that matters -- two
// EnableFresh actions at the same wall-clock second still get different
// folder names -- is provable OFF, deterministically, rather than only by
// reading a real clock in a JUCE test.
//
// The UTC timestamp alone has 1-second resolution: two EnableFresh actions
// inside the same second (a rapid device bounce, exactly the case finding 1's
// epoch freeze exists for) would otherwise resolve to the SAME folder, and
// the second session's fresh files would truncate the first's still-unread
// ones. The epoch only ever increases (CaptureBus::prepare()'s own
// fetch_add), so appending it makes the name unique by construction, with no
// clock precision to lose chasing a shorter collision window instead.
#pragma once

#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace rta::measure {

/// An ISO-8601-shaped, filesystem-safe UTC stamp ("20260925T143007Z") for a
/// given `std::time_t` -- a pure function of its argument, so a test can pin
/// the instant rather than reading the real clock. `<chrono>`/`<ctime>`
/// rather than juce::Time::formatted()/toISO8601(): both of those read LOCAL
/// time (JUCE's own juce_Time.cpp: every getter goes through
/// millisToLocal()), and the session folder wants the WALL CLOCK independent
/// of the operator's timezone, matching SplLogHeaderInfo::startedAtUnixMs's
/// own "no wall clock in any arithmetic, but one is recorded once as
/// metadata" rule (record §2).
inline std::string formatUtcTimestamp(std::time_t t) {
    std::tm utc{};
#if defined(_MSC_VER)
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y%m%dT%H%M%SZ");
    return out.str();
}

/// The session directory's own leaf name: the UTC timestamp above, suffixed
/// with the bus epoch it started at. `epoch` is what keeps the name unique
/// across two EnableFresh actions in the same second -- see this header's
/// own top comment.
inline std::string sessionFolderName(std::time_t now, std::uint64_t epoch) {
    return formatUtcTimestamp(now) + "-e" + std::to_string(epoch);
}

}  // namespace rta::measure
