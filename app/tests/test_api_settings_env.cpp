// SPDX-License-Identifier: AGPL-3.0-or-later
// H8 lane b: the API enable switch. Default OFF is the control that matters
// (nothing observable changes for an operator who did not ask), so it is
// tested with the variable unset, empty, and with junk values.
#include <catch2/catch_test_macros.hpp>

#include "api/ApiPolicy.h"
#include "api/ApiSettingsFromEnv.h"

#include <map>
#include <string>

namespace {
rta::api::EnvLookup fakeEnv(std::map<std::string, std::string> vars) {
    return [vars = std::move(vars)](const char* name) {
        const auto it = vars.find(name);
        return it == vars.end() ? std::string{} : it->second;
    };
}
}  // namespace

TEST_CASE("API stays off and on defaults when no variable is set", "[api_settings_env]") {
    const auto s = rta::api::apiSettingsFromEnvironment(fakeEnv({}));
    const rta::api::ApiSettings defaults;
    CHECK_FALSE(s.enabled);
    CHECK(s.bindAddress == defaults.bindAddress);
    CHECK(s.port == defaults.port);
    CHECK(s.token.empty());
}

TEST_CASE("Only an explicit yes enables the API", "[api_settings_env]") {
    for (const char* on : {"1", "true", "TRUE", "On", "yes"}) {
        CAPTURE(on);
        CHECK(rta::api::apiSettingsFromEnvironment(fakeEnv({{"RTA_API_ENABLED", on}})).enabled);
    }
    for (const char* off : {"", "0", "false", "off", "no", "2", "enabled", " 1"}) {
        CAPTURE(off);
        CHECK_FALSE(rta::api::apiSettingsFromEnvironment(fakeEnv({{"RTA_API_ENABLED", off}})).enabled);
    }
}

TEST_CASE("Port and token are read; a bad port keeps the default; bind never moves",
          "[api_settings_env]") {
    const rta::api::ApiSettings defaults;
    auto s = rta::api::apiSettingsFromEnvironment(
        fakeEnv({{"RTA_API_ENABLED", "1"}, {"RTA_API_PORT", "5001"}, {"RTA_API_TOKEN", "t0k"},
                 {"RTA_API_BIND", "0.0.0.0"}}));
    CHECK(s.enabled);
    CHECK(s.port == 5001);
    CHECK(s.token == "t0k");
    CHECK(s.bindAddress == "127.0.0.1");  // no variable can change it
    CHECK_FALSE(rta::api::startRefusal(s).has_value());

    for (const char* bad : {"0", "65536", "-1", "abc", "99999", "123456", "12 3"}) {
        CAPTURE(bad);
        CHECK(rta::api::apiSettingsFromEnvironment(fakeEnv({{"RTA_API_PORT", bad}})).port == defaults.port);
    }
    CHECK(rta::api::apiSettingsFromEnvironment(fakeEnv({{"RTA_API_PORT", "65535"}})).port == 65535);
}
