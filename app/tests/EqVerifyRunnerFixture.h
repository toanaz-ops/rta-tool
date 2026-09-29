// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shared rig for the VERIFY / ADOPT tests (L7-EQ UI wave B, plan T7/T8). A
// standalone rta::platform::OutputEngine rendered by hand (the
// test_eq_verify.cpp pattern) plus two scripted snapshots -- no device, no
// JUCE. Header-only and JUCE-free so the OFF tests and the JUCE
// MainComponent tests build the same rig.
//
// The two snapshots are the whole physics of the fixture:
//   pre  = the room as first measured (the bump, no filter in the rig)
//   post = the room with the committed filters dialled in, i.e. exactly the
//          session's prediction (ghost), as a float magnitude
// `latest()` serves `pre` until depth * fftSize samples of excitation have
// rendered -- the live transfer FIFO still holds pre-excitation frames until
// then -- and `post` after. A runner that submits the first Measuring
// snapshot therefore reports the unchanged room as a failed correction.
#pragma once

#include "EqTraceFixture.h"

#include "measure/Analyser.h"
#include "measure/EqPaneModel.h"
#include "measure/EqVerifyRunner.h"
#include "measure/Snapshot.h"
#include "trace/TraceLibrary.h"

#include "rta/platform/OutputEngine.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace verifyfixture {

inline constexpr int kFft = 4096;
inline constexpr std::size_t kBins = static_cast<std::size_t>(kFft) / 2 + 1;
inline constexpr int kBlock = 512;
inline constexpr int kChannels = 2;

using rta::measure::EqVerifyEnvironment;
using rta::measure::Snapshot;
using rta::measure::SnapshotPtr;
using rta::measure::TransferBlock;

[[nodiscard]] inline SnapshotPtr makeSnapshot(std::vector<float> magnitudeDb, std::vector<float> coherence,
                                              int fftSize = kFft, double sampleRate = eqfixture::kFs) {
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->sampleRate = sampleRate;
    snapshot->fftSize = static_cast<std::size_t>(fftSize);
    snapshot->hasReference = true;
    TransferBlock transfer;
    transfer.magnitudeDb = std::move(magnitudeDb);
    transfer.phaseDeg.assign(transfer.magnitudeDb.size(), 0.0f);
    if (!coherence.empty()) transfer.coherence = std::move(coherence);
    transfer.effectiveAverages = 100.0;
    snapshot->transfer = std::move(transfer);
    return snapshot;
}

struct Rig {
    rta::trace::TraceLibrary library;
    rta::measure::EqPaneModel model;
    rta::platform::OutputEngine engine;
    rta::measure::EqVerifyRunner runner{ model };

    EqVerifyEnvironment env{ /*synthetic*/ false, /*deviceRunning*/ true, /*captureBusy*/ false };
    rta::measure::CaptureConfig config{};
    double nowMs = 1000.0;
    SnapshotPtr pre;
    SnapshotPtr post;
    bool serveAlwaysPre = false;   ///< true: latest() never turns over (a stalled FIFO)
    bool nullSnapshot = false;
    int freezes = 0;
    std::vector<float> coherence;
    std::vector<rta::eq::FilterSpec> planned;  ///< the filters AUTO EQ committed

    /// `pick`: bind the bump measurement. `autoEq`: commit filters against it.
    explicit Rig(bool pick = true, bool autoEq = true) {
        const auto fixture = eqfixture::makeLinearFixture(kBins);
        coherence = fixture.coherence;
        library.add(eqfixture::makeBumpTrace("bump", kFft, true), "TRANSFER @ 12:00:00", "TRANSFER");
        engine.prepare(eqfixture::kFs, kChannels);
        if (pick) model.selectMeasurement(library, "bump");
        if (pick && autoEq) model.runAutoEq();

        pre = makeSnapshot(std::vector<float>(model.measuredDb().begin(), model.measuredDb().end()), coherence);
        std::vector<float> postDb;
        for (const double g : model.session().ghostDb()) postDb.push_back(static_cast<float>(g));
        post = makeSnapshot(postDb, coherence);
        for (const auto& f : model.session().committed()) planned.push_back(f.spec);

        rta::measure::EqVerifyHost host;
        host.environment = [this] { return env; };
        host.latest = [this]() -> SnapshotPtr {
            if (nullSnapshot) return nullptr;
            return (!serveAlwaysPre && engine.renderedSamples() >= turnoverSamples()) ? post : pre;
        };
        host.captureConfig = [this] { return config; };
        host.freeze = [this](const Snapshot& s) -> std::optional<rta::trace::Trace> {
            ++freezes;
            return eqfixture::makeEqTrace("verify-" + std::to_string(freezes), kFft, eqfixture::kFs,
                                          s.transfer->magnitudeDb, *s.transfer->coherence,
                                          std::vector<float>(s.transfer->magnitudeDb.size(), 0.0f));
        };
        host.nowMs = [this] { return nowMs; };
        host.timeLabel = [] { return std::string("12:34:56"); };
        runner.attach(engine, library, std::move(host));
    }

    /// depth * fftSize rendered samples: the physical turnover of the live
    /// transfer FIFO (Analyser.h CaptureConfig::transferFifoDepth), derived
    /// from the same constant the runner reads, not a literal.
    [[nodiscard]] std::uint64_t turnoverSamples() const {
        return static_cast<std::uint64_t>(config.transferFifoDepth) * static_cast<std::uint64_t>(kFft);
    }

    /// Renders `blocks` blocks of kBlock samples, polling after each -- the
    /// finest cadence the runner can be driven at.
    void pump(int blocks) {
        for (int b = 0; b < blocks; ++b) {
            std::vector<std::vector<float>> buffers(kChannels, std::vector<float>(kBlock, 0.0f));
            std::vector<float*> ptrs;
            for (auto& channel : buffers) ptrs.push_back(channel.data());
            engine.render(ptrs.data(), kChannels, kBlock);
            runner.poll();
        }
    }
    /// Renders WITHOUT polling: the engine advances, the runner does not.
    void renderOnly(int blocks) {
        for (int b = 0; b < blocks; ++b) {
            std::vector<std::vector<float>> buffers(kChannels, std::vector<float>(kBlock, 0.0f));
            std::vector<float*> ptrs;
            for (auto& channel : buffers) ptrs.push_back(channel.data());
            engine.render(ptrs.data(), kChannels, kBlock);
        }
    }
    /// Pumps until `state` (bounded), returning whether it was reached.
    bool pumpUntil(rta::measure::VerifyState state, int maxBlocks = 400) {
        for (int b = 0; b < maxBlocks && runner.stateForTest() != state; ++b) pump(1);
        return runner.stateForTest() == state;
    }
    /// press + run to Done (the pre/post physics does the rest).
    bool runToDone() {
        if (!runner.press()) return false;
        return pumpUntil(rta::measure::VerifyState::Done);
    }
};

}  // namespace verifyfixture
