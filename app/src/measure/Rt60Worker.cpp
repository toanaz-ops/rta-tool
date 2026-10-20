// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/Rt60Worker.h"

#include <exception>
#include <utility>

namespace rta::measure {

Rt60Worker::~Rt60Worker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        cancel_.store(true);
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void Rt60Worker::submit(Request request) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        newestTicket_ = request.ticket;
        done_.reset();
        pending_ = std::move(request);
        // Stop the job in flight at its next band boundary: its answer is for a
        // question nobody is asking any more.
        if (running_) cancel_.store(true);
        if (!thread_.joinable()) thread_ = std::thread([this] { loop(); });
    }
    wake_.notify_all();
}

std::optional<Rt60Worker::Outcome> Rt60Worker::poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<Outcome> out = std::move(done_);
    done_.reset();
    return out;
}


void Rt60Worker::loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        wake_.wait(lock, [this] { return stop_ || pending_.has_value(); });
        if (stop_) return;
        Request request = std::move(*pending_);
        pending_.reset();
        running_ = true;
        cancel_.store(false);
        lock.unlock();

        Outcome outcome;
        outcome.ticket = request.ticket;
        try {
            if (request.primary != nullptr) {
                outcome.single = std::make_shared<const Rt60Result>(
                    analyseRt60(request.primary.get(), request.set, &cancel_));
            }
            if (request.ensemble.size() >= 2 && !cancel_.load()) {
                std::vector<const SweepIr*> irs;
                for (const auto& ir : request.ensemble) irs.push_back(ir.get());
                outcome.ensemble = std::make_shared<const Rt60EnsembleResult>(
                    analyseRt60Ensemble(irs, request.set, &cancel_));
            }
        } catch (const std::exception& e) {
            // An exception escaping a std::thread is std::terminate. The analysis
            // catches its own; this is for an allocation failure outside it.
            Rt60Result failed;
            failed.refusal = Rt60Refusal::NoDecayFound;
            failed.message = e.what();
            outcome.single = std::make_shared<const Rt60Result>(std::move(failed));
            outcome.ensemble = nullptr;
        } catch (...) {
            Rt60Result failed;
            failed.refusal = Rt60Refusal::NoDecayFound;
            failed.message = "unknown exception";
            outcome.single = std::make_shared<const Rt60Result>(std::move(failed));
            outcome.ensemble = nullptr;
        }

        lock.lock();
        running_ = false;
        // A cancelled or superseded job's answer is dropped, never published.
        if (!stop_ && !pending_.has_value() && !cancel_.load() && request.ticket == newestTicket_) {
            done_ = std::move(outcome);
        }
    }
}

}  // namespace rta::measure
