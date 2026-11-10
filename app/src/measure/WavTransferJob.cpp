// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/WavTransferJob.h"

#include "measure/WavFileSource.h"

#include <chrono>
#include <exception>
#include <utility>

namespace rta::measure {

WavTransferJob::~WavTransferJob() { cancelAndDiscard(); }

void WavTransferJob::cancelAndDiscard() {
    if (cancel_) cancel_->store(true);
    if (running_.valid()) running_.wait();
    running_ = {};
}

void WavTransferJob::start(const juce::File& file, WavTransferRequest request) {
    cancelAndDiscard();
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    running_ = std::async(std::launch::async, [file, request = std::move(request), cancel = cancel_] {
        // Nothing may escape through the future: poll() would rethrow on the message thread.
        try {
            const auto source = WavFileSource::open(file);
            if (source == nullptr) {
                WavTransferOutcome out;
                out.refusal = WavTransferRefusal::ReadFailed;
                return out;
            }
            return analyseWavTransfer(*source, request, *cancel);
        } catch (const std::exception&) {
            WavTransferOutcome out;
            out.refusal = WavTransferRefusal::ReadFailed;
            return out;
        }
    });
}

std::optional<WavTransferOutcome> WavTransferJob::poll() {
    if (!running_.valid() || running_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return std::nullopt;
    }
    return running_.get();
}

}  // namespace rta::measure
