// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/SweepIrWorker.h"

#include <exception>
#include <utility>

namespace rta::measure {

SweepIrWorker::SweepIrWorker(Analyse analyse)
    : analyse_(analyse ? std::move(analyse) : Analyse(analyseSweepCapture)) {}

SweepIrWorker::~SweepIrWorker() { join(); }

void SweepIrWorker::join() {
    if (thread_.joinable()) thread_.join();
}

void SweepIrWorker::start(SweepCapture capture, SweepIrRequest request) {
    join();
    result_.store(nullptr);
    // The thread owns copies of everything it reads, and writes only
    // `result_` -- one atomic swap, the same single handoff AnalysisThread
    // uses. `this` is safe to capture: the destructor joins.
    thread_ = std::thread([this, capture = std::move(capture), request = std::move(request)] {
        SweepIrOutcome outcome;
        try {
            outcome = analyse_(capture, request);
        } catch (const std::exception& e) {
            // An exception escaping a std::thread is std::terminate; the
            // default analyse never throws, but an injected one may.
            outcome.status = SweepIrStatus::Failed;
            outcome.message = e.what();
        }
        result_.store(std::make_shared<const SweepIrOutcome>(std::move(outcome)));
    });
}

}  // namespace rta::measure
