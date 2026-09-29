// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-EQ UI wave B, task T8 (docs/plans/2026-09-29-eq-ui-lane-plan.md): ADOPT.
// After a completed VERIFY the operator adopts the result: every filter that
// was in the prediction is marked applied and the VERIFY trace becomes the
// bound measurement. EqSession.h's one semantics then says the ghost IS the
// measurement (an applied filter is already in the room), which is asserted
// bitwise, and SUGGEST does not propose the adopted filters a second time.
// The run that produces the result is test_eq_verify_runner_flow.cpp.
#include "EqVerifyRunnerFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

using rta::measure::VerifyOutcome;
using verifyfixture::Rig;

TEST_CASE("ADOPT marks every predicted filter applied and binds the VERIFY trace: ghost == measured, bitwise") {
    Rig rig;
    REQUIRE(rig.runToDone());
    const std::size_t predicted = rig.model.session().committed().size();
    REQUIRE(predicted >= 1);
    REQUIRE(rig.model.unappliedCount() == predicted);
    const std::string afterId = rig.runner.afterTraceId();

    REQUIRE(rig.runner.adopt());

    CHECK(rig.model.measurement().id == afterId);
    CHECK(rig.model.session().committed().size() == predicted);  // none added, none lost
    CHECK(rig.model.unappliedCount() == 0);
    for (const auto& filter : rig.model.session().committed()) CHECK(filter.applied);

    // EqSession.h: ghost_k = m_k + sum over NOT-applied filters. None is left,
    // so the ghost is the (new) measurement itself -- the same double, not
    // merely close. Bound to the VERIFY trace, that measurement is `post`.
    const auto ghost = rig.model.session().ghostDb();
    const auto measured = rig.model.measuredDb();
    REQUIRE(ghost.size() == measured.size());
    REQUIRE(measured.size() == rig.post->transfer->magnitudeDb.size());
    for (std::size_t k = 0; k < ghost.size(); ++k) {
        CHECK(ghost[k] == static_cast<double>(measured[k]));
        CHECK(measured[k] == rig.post->transfer->magnitudeDb[k]);
    }

    CHECK(rig.runner.outcome() == VerifyOutcome::Adopted);
    CHECK_FALSE(rig.runner.canAdopt());  // one result, adopted once
    CHECK(rig.model.status().find("ADOPT: " + std::to_string(predicted) + " filter(s)") != std::string::npos);

    // test_eq_session.cpp:172's invariant, reached through the runner: the
    // session does not propose an adopted filter a second time.
    rig.model.suggest();
    for (const auto& chip : rig.model.chips()) {
        for (const auto& adopted : rig.planned) {
            const bool same = std::abs(chip.spec.fcHz - adopted.fcHz) < 1.0 &&
                              std::abs(chip.spec.gainDb - adopted.gainDb) < 0.1;
            CHECK_FALSE(same);
        }
    }

    CHECK_FALSE(rig.runner.adopt());
}

TEST_CASE("ADOPT refuses, changing nothing, when the filter list changed since VERIFY") {
    SECTION("a filter was cleared") {
        Rig rig;
        REQUIRE(rig.runToDone());
        rig.model.clearFilters();
        CHECK_FALSE(rig.runner.adopt());
        CHECK(rig.runner.outcome() == VerifyOutcome::InputsChanged);
        CHECK(rig.model.measurement().id == "bump");
        CHECK(rig.model.status().find("changed since VERIFY") != std::string::npos);
    }
    SECTION("one filter was marked applied by hand") {
        Rig rig;
        REQUIRE(rig.runToDone());
        rig.model.setApplied(0, true);
        CHECK_FALSE(rig.runner.adopt());
        CHECK(rig.model.measurement().id == "bump");
        CHECK(rig.model.unappliedCount() == rig.planned.size() - 1);
    }
}
