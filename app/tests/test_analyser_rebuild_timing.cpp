// SPDX-License-Identifier: AGPL-3.0-or-later
// Measurement, not a correctness test: how long does building the full set of
// kMaxTransferFunctions Analysers at MTW defaults take on THIS machine, against
// how long the capture ring can absorb. Hidden ([.]) because a wall-clock number
// is not a gate; run it by name to refresh the figures quoted in
// docs/dsp/2026-10-21-analyser-rebuild.md.
#include <catch2/catch_test_macros.hpp>

#include "measure/Analyser.h"
#include "measure/RoutingPlan.h"

#include "rta/platform/CaptureBus.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

TEST_CASE("analyser rebuild timing at MTW defaults", "[.][bench][analyser_rebuild]") {
    using Clock = std::chrono::steady_clock;
    using rta::measure::Analyser;
    constexpr int kSlots = rta::measure::kMaxTransferFunctions;

    std::vector<std::unique_ptr<Analyser>> set;
    for (const double rate : {48000.0, 96000.0, 192000.0}) {
        Analyser::Config cfg;
        cfg.sampleRate = rate;
        std::vector<double> totalMs;
        std::vector<double> worstSlotMs;
        for (int rep = 0; rep < 5; ++rep) {
            double worst = 0.0;
            const auto t0 = Clock::now();
            for (int i = 0; i < kSlots; ++i) {
                const auto s0 = Clock::now();
                if (set.size() < static_cast<std::size_t>(kSlots)) {
                    set.push_back(std::make_unique<Analyser>(cfg));
                } else {
                    set[static_cast<std::size_t>(i)] = std::make_unique<Analyser>(cfg);
                }
                worst = std::max(
                    worst, std::chrono::duration<double, std::milli>(Clock::now() - s0).count());
            }
            totalMs.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
            worstSlotMs.push_back(worst);
        }
        std::sort(totalMs.begin(), totalMs.end());
        std::sort(worstSlotMs.begin(), worstSlotMs.end());
        const double headroomMs =
            1000.0 * static_cast<double>(rta::platform::kFixedRingCapacitySamples) / rate;
        std::printf("BENCH rate=%.0f total_ms min=%.1f median=%.1f max=%.1f worst_slot_ms median=%.1f "
                    "ring_headroom_ms=%.1f\n",
                    rate, totalMs.front(), totalMs[totalMs.size() / 2], totalMs.back(),
                    worstSlotMs[worstSlotMs.size() / 2], headroomMs);
    }
}
