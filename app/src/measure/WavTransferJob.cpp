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
        WavJobOutcome result;
        try {
            const auto source = WavFileSource::open(file);
            if (source == nullptr) {
                result.single.refusal = WavTransferRefusal::ReadFailed;
            } else {
                result.single = analyseWavTransfer(*source, request, *cancel);
            }
        } catch (const std::exception&) {
            result.single = WavTransferOutcome{};
            result.single.refusal = WavTransferRefusal::ReadFailed;
        }
        return result;
    });
}

void WavTransferJob::start(WavPairJobRequest job) {
    cancelAndDiscard();
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    running_ = std::async(std::launch::async, [job = std::move(job), cancel = cancel_] {
        WavJobOutcome result;
        result.pairMode = true;
        try {
            const auto reference = WavFileSource::open(job.reference);
            const auto measurement = WavFileSource::open(job.measurement);
            if (reference == nullptr || measurement == nullptr) {
                result.pair.transfer.refusal = WavTransferRefusal::ReadFailed;
            } else {
                WavPairRequest request = job.request;
                request.clock.priorOffsetSamples =
                    bextPriorSamples(reference->timeReferenceSamples(), measurement->timeReferenceSamples());
                result.pair = analyseWavPairTransfer(*reference, job.referenceChannel, *measurement,
                                                     job.measurementChannel, request, *cancel);
            }
        } catch (const std::exception&) {
            result.pair = WavPairOutcome{};
            result.pair.transfer.refusal = WavTransferRefusal::ReadFailed;
        }
        return result;
    });
}

std::optional<WavJobOutcome> WavTransferJob::poll() {
    if (!running_.valid() || running_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return std::nullopt;
    }
    return running_.get();
}

}  // namespace rta::measure
