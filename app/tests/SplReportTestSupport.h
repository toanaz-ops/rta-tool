// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shared fixtures for test_spl_report.cpp and test_spl_report_fixes.cpp
// (PR #28 fix round split, the CodeLines.h precedent: one reader/fixture
// set shared by more than one test file, `inline` so it is not an ODR
// violation to include this from both).
#pragma once

#include "export/SplReport.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

namespace rta::splexport::test {

inline ReportPayload minimalPayload() {
    ReportPayload p;
    p.venue = "Warehouse 9";
    p.event = "Load-in";
    p.appName = "rtatool";
    p.appVersion = "0.0.0";
    p.buildId = "test-build";
    p.device = "Test Device";
    p.channel = "Main L";
    p.sampleRate = 48000.0;
    return p;
}

inline std::string extractSection(const std::string& html, std::string_view id) {
    const auto start = html.find("id=\"" + std::string(id) + "\"");
    REQUIRE(start != std::string::npos);
    const auto end = html.find("</section>", start);
    REQUIRE(end != std::string::npos);
    return html.substr(start, end - start);
}

}  // namespace rta::splexport::test
