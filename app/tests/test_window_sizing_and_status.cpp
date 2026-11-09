// SPDX-License-Identifier: AGPL-3.0-or-later
// H8 review follow-ups: window sizing (UI test finding 6a), the SPL empty
// state (6b), the "API enabled but not running" text (M-2), the constant-time
// bearer compare (L-2), the locale-free env parsing (L-3) and the low-SNR hint
// rule (L-1). All pure: no window, no device.
#include <catch2/catch_test_macros.hpp>

#include "WindowSizing.h"
#include "api/ApiPolicy.h"
#include "api/ApiSettingsFromEnv.h"
#include "api/ApiStatusText.h"
#include "measure/SweepIrPipeline.h"
#include "view/SplEmptyState.h"

#include <string>

TEST_CASE("window sizing: a 1366x768 work area gets a window that fits and a minimum below it",
          "[window_sizing]") {
    const auto s = rta::app::windowSizingFor(1366, 728);
    CHECK(s.width <= 1366);
    CHECK(s.height <= 728);
    CHECK(s.minWidth <= s.width);
    CHECK(s.minHeight <= s.height);
    CHECK(s.minWidth == rta::app::kMinimumWidth);
    CHECK(s.minHeight == rta::app::kMinimumHeight);
    CHECK(s.height == 728);  // the preferred 820 is clamped DOWN to the work area
}

TEST_CASE("window sizing: a large screen keeps the preferred size; a tiny one clamps the minimum too",
          "[window_sizing]") {
    const auto big = rta::app::windowSizingFor(2560, 1400);
    CHECK(big.width == rta::app::kPreferredWidth);
    CHECK(big.height == rta::app::kPreferredHeight);

    // 125% scaling of a 1366x768 panel: ~1093x582 logical units. The minimum
    // must never exceed the screen, or the window could not be shown whole.
    const auto small = rta::app::windowSizingFor(1093, 582);
    CHECK(small.minWidth == 1093);
    CHECK(small.minHeight == 582);
    CHECK(small.width == 1093);
    CHECK(small.height == 582);
}

TEST_CASE("SPL empty state explains itself in both empty cases", "[spl_empty_state]") {
    const auto none = rta::view::splEmptyStateLines(false);
    const auto open = rta::view::splEmptyStateLines(true);
    REQUIRE(none.size() >= 2);
    CHECK(none.front() == "NO SPL SESSION");
    CHECK(open.front() != none.front());
    for (const auto* lines : {&none, &open}) {
        std::string all;
        for (const auto& l : *lines) all += l + "\n";
        CHECK(all.find("CAL START") != std::string::npos);
        CHECK(all.find("SYNTHETIC") != std::string::npos);
    }
}

TEST_CASE("API status text: silent unless enabled and not running (H8 M-2)", "[api_status]") {
    rta::api::ApiSettings off;
    CHECK(rta::api::apiNotRunningText(off, false, std::nullopt).empty());

    rta::api::ApiSettings on;
    on.enabled = true;
    CHECK(rta::api::apiNotRunningText(on, true, std::nullopt).empty());

    const auto bind = rta::api::apiNotRunningText(on, false, std::nullopt);
    CHECK(bind.find("NOT RUNNING") != std::string::npos);
    CHECK(bind.find("4736") != std::string::npos);

    const auto refused = rta::api::apiNotRunningText(on, false, std::string("some reason"));
    CHECK(refused.find("some reason") != std::string::npos);
}

TEST_CASE("bearer compare accepts exactly the token, byte for byte (H8 L-2)", "[api_policy]") {
    rta::api::ApiSettings s;
    s.token = "s3cr3t";
    CHECK(rta::api::bearerAccepted("Bearer s3cr3t", s));
    CHECK_FALSE(rta::api::bearerAccepted("Bearer s3cr3T", s));
    CHECK_FALSE(rta::api::bearerAccepted("Bearer s3cr3", s));
    CHECK_FALSE(rta::api::bearerAccepted("Bearer s3cr3tt", s));
    // A prefix of the token is not the token, even though every compared byte matches.
    CHECK_FALSE(rta::api::bearerAccepted("Bearer s3", s));
}

TEST_CASE("env parsing is ASCII-only and locale-free (H8 L-3)", "[api_settings_env]") {
    const auto lookup = [](std::string v) {
        return [v](const char* n) { return std::string(n) == "RTA_API_ENABLED" ? v : std::string{}; };
    };
    CHECK(rta::api::apiSettingsFromEnvironment(lookup("YeS")).enabled);
    // Bytes >= 0x80 (negative as `char`) must neither crash nor enable.
    CHECK_FALSE(rta::api::apiSettingsFromEnvironment(lookup("\xC4\xB0")).enabled);
    const auto port = [](std::string v) {
        return [v](const char* n) { return std::string(n) == "RTA_API_PORT" ? v : std::string{}; };
    };
    CHECK(rta::api::apiSettingsFromEnvironment(port("\xDF""5")).port == rta::api::ApiSettings{}.port);
}

TEST_CASE("low-SNR hint applies only to the two no-peak refusals below the shipped level (H8 L-1)",
          "[sweep_ir_pipeline]") {
    using rta::measure::SweepIrStatus;
    const double shipped = rta::measure::shippedSweepConfig(48000.0).levelDbFsPeak;
    CHECK(rta::measure::lowSnrHintApplies(SweepIrStatus::ReferenceNotFound, shipped - 6.0));
    CHECK(rta::measure::lowSnrHintApplies(SweepIrStatus::MeasurementNotFound, shipped - 0.1));
    // Negative cases: at or above the shipped level, and a geometry refusal.
    CHECK_FALSE(rta::measure::lowSnrHintApplies(SweepIrStatus::ReferenceNotFound, shipped));
    CHECK_FALSE(rta::measure::lowSnrHintApplies(SweepIrStatus::ReferenceNotFound, shipped + 6.0));
    CHECK_FALSE(rta::measure::lowSnrHintApplies(SweepIrStatus::ArrivalOutOfWindow, shipped - 20.0));
    CHECK_FALSE(rta::measure::lowSnrHintApplies(SweepIrStatus::TailTooShort, shipped - 20.0));
}
