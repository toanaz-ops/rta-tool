// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE, no Qt, no audio-device API.
//
// SWEEP lane task T5 (owner decision 6): the impulse response behind a SWEEP
// trace, kept in memory next to the library and keyed by the trace's id. NOT
// persisted: after Save/Open a trace comes back without its IR, which ALIGN
// already handles as "no IR" (`AlignmentWizard.cpp:282-290`, AskingCycle).
//
// Capped at 8 with FIFO eviction because one entry is the whole linear
// deconvolution -- about 4.6 MB at 48 kHz with the shipped 10 s sweep and
// 3 s tail -- so an unbounded store would grow by that every press.
// Message thread only; no locking.
//
// There is deliberately no lookup. ALIGN's lookup arrives with ALIGN-UI (H5);
// building it here would be an orphan with no caller. `containsForTest` is
// the observation point the eviction test needs and nothing else.
#pragma once

#include "measure/SweepIrOutcome.h"

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <utility>

namespace rta::trace {

class IrStore {
public:
    static constexpr std::size_t kCapacity = 8;

    /// Stores `ir` under `traceId`. An id already present is replaced in
    /// place (it keeps its age); a new id past `kCapacity` evicts the oldest.
    void add(std::string traceId, std::shared_ptr<const rta::measure::SweepIr> ir) {
        for (auto& entry : entries_) {
            if (entry.first == traceId) {
                entry.second = std::move(ir);
                return;
            }
        }
        entries_.emplace_back(std::move(traceId), std::move(ir));
        if (entries_.size() > kCapacity) entries_.pop_front();
    }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    [[nodiscard]] bool containsForTest(const std::string& traceId) const {
        for (const auto& entry : entries_) {
            if (entry.first == traceId) return true;
        }
        return false;
    }

private:
    std::deque<std::pair<std::string, std::shared_ptr<const rta::measure::SweepIr>>> entries_;
};

}  // namespace rta::trace
