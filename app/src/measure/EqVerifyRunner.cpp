// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/EqVerifyRunner.h"

#include "measure/CaptureTimeout.h"
#include "measure/EqTrustMask.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace rta::measure {
namespace {

bool sameFilter(const rta::eq::FilterSpec& a, const rta::eq::FilterSpec& b) {
    return a.type == b.type && a.fcHz == b.fcHz && a.q == b.q && a.gainDb == b.gainDb;
}

}  // namespace

std::string verifyBlockText(VerifyBlock kind) {
    switch (kind) {
        case VerifyBlock::None: return "";
        case VerifyBlock::NotAttached: return "VERIFY is not wired to an output engine";
        case VerifyBlock::NoMeasurement: return "VERIFY needs a measurement: pick one first";
        case VerifyBlock::NoUnappliedFilter:
            return "VERIFY needs a filter not yet marked applied: it checks the prediction those filters make";
        case VerifyBlock::Running: return "VERIFY is already running";
        case VerifyBlock::Synthetic:
            return "SYNTHETIC input stops the audio device, so no output can play the excitation: switch to LIVE";
        case VerifyBlock::DeviceNotRunning:
            return "the audio device is not running: open it in the device panel";
        case VerifyBlock::CaptureBusy:
            return "LOCATE or CAL is running and owns the output or the capture: wait for it to finish";
        case VerifyBlock::NoTransfer:
            return "no TRANSFER measurement is published: feed a reference channel";
        case VerifyBlock::GridMismatch:
            return "the live fftSize or sample rate differs from the bound trace";
        case VerifyBlock::ExponentialAveraging:
            return "transfer averaging is Exponential: its memory never fully clears, so no dwell "
                   "guarantees a post-excitation estimate; switch to Fifo";
        case VerifyBlock::EngineNotQuiescent:
            return "the output engine is not quiescent: another excitation is running or still ramping out";
    }
    return "";
}

EqVerifyRunner::EqVerifyRunner(EqPaneModel& model) : model_(model) {}

void EqVerifyRunner::attach(rta::platform::OutputEngine& engine, rta::trace::TraceLibrary& library,
                            EqVerifyHost host) {
    engine_ = &engine;
    library_ = &library;
    host_ = std::move(host);
}

double EqVerifyRunner::dwellSeconds(const CaptureConfig& config, std::size_t fftSize, double sampleRate) {
    // Analyser.h: the transfer FIFO holds `transferFifoDepth` frames of one
    // window each. depth * fftSize samples of pure excitation guarantee no
    // pre-excitation frame is left in it (deliberately the generous reading:
    // frames overlap, so the true flush is shorter).
    return static_cast<double>(config.transferFifoDepth) * static_cast<double>(fftSize) / sampleRate;
}

std::size_t EqVerifyRunner::boundFftSize() const {
    const auto bins = model_.measuredDb().size();
    return bins < 2 ? 0 : 2 * (bins - 1);
}

bool EqVerifyRunner::busy() const noexcept {
    if (!verify_) return false;
    const auto s = verify_->state();
    return s == VerifyState::Waiting || s == VerifyState::Measuring || s == VerifyState::Settling;
}

VerifyState EqVerifyRunner::stateForTest() const noexcept { return verify_ ? verify_->state() : VerifyState::Idle; }

bool EqVerifyRunner::canAdopt() const noexcept {
    return verify_ && verify_->state() == VerifyState::Done && verify_->report().has_value() &&
           !afterTraceId_.empty();
}

const std::optional<VerifyReport>& EqVerifyRunner::reportForTest() const noexcept {
    static const std::optional<VerifyReport> kNone;
    return verify_ ? verify_->report() : kNone;
}

VerifyBlocker EqVerifyRunner::blocker() const {
    const auto block = [](VerifyBlock kind, std::string extra = {}) {
        std::string text = verifyBlockText(kind);
        if (!extra.empty()) text += " (" + extra + ")";
        return VerifyBlocker{ kind, std::move(text) };
    };
    if (engine_ == nullptr || library_ == nullptr || !host_.environment || !host_.latest || !host_.captureConfig) {
        return block(VerifyBlock::NotAttached);
    }
    if (!model_.hasMeasurement()) return block(VerifyBlock::NoMeasurement);
    if (model_.unappliedCount() == 0) return block(VerifyBlock::NoUnappliedFilter);
    if (busy()) return block(VerifyBlock::Running);
    const EqVerifyEnvironment env = host_.environment();
    if (env.synthetic) return block(VerifyBlock::Synthetic);
    if (!env.deviceRunning) return block(VerifyBlock::DeviceNotRunning);
    if (env.captureBusy) return block(VerifyBlock::CaptureBusy);

    const SnapshotPtr snapshot = host_.latest();
    if (!snapshot || !snapshot->transfer.has_value()) return block(VerifyBlock::NoTransfer);
    const std::size_t boundFft = boundFftSize();
    if (snapshot->fftSize != boundFft || snapshot->sampleRate != model_.sampleRate() ||
        snapshot->transfer->magnitudeDb.size() != model_.measuredDb().size()) {
        char detail[128];
        std::snprintf(detail, sizeof detail, "live fftSize %zu at %.0f Hz, bound trace %zu at %.0f Hz",
                      snapshot->fftSize, snapshot->sampleRate, boundFft, model_.sampleRate());
        return block(VerifyBlock::GridMismatch, detail);
    }
    if (host_.captureConfig().transferAveraging == rta::dsp::TransferAveraging::Exponential) {
        return block(VerifyBlock::ExponentialAveraging);
    }
    return block(VerifyBlock::None);
}

bool EqVerifyRunner::press() {
    const VerifyBlocker why = blocker();
    if (why.kind != VerifyBlock::None) {
        refusal_ = why.kind;
        model_.setStatus("VERIFY refused: " + why.text);
        return false;
    }
    EqVerify::Config config;
    config.outputChannel = kEqVerifyOutputChannel;
    config.sampleRate = engine_->sampleRate() > 0.0 ? engine_->sampleRate() : model_.sampleRate();
    config.tolerance.corridorHalfWidthDb = kEqVerifyCorridorDb;
    config.tolerance.sigmaMultiple = kEqVerifySigmaMultiple;
    config.excitationDbFsRms = kEqVerifyLevelDbFs;

    auto fresh = std::make_unique<EqVerify>(*engine_, config);
    fresh->arm();
    if (fresh->lastRefusal() != VerifyRefusal::None) {
        refusal_ = VerifyBlock::EngineNotQuiescent;
        model_.setStatus("VERIFY refused: " + verifyBlockText(VerifyBlock::EngineNotQuiescent));
        return false;
    }
    verify_ = std::move(fresh);
    refusal_ = VerifyBlock::None;
    outcome_ = VerifyOutcome::None;
    afterTraceId_.clear();
    armedAtMs_ = host_.nowMs();
    pressedMeasurementId_ = model_.measurement().id;
    pressedFilters_.assign(model_.session().committed().begin(), model_.session().committed().end());
    model_.setStatus("VERIFY: pink noise playing on output " + std::to_string(kEqVerifyOutputChannel + 1) +
                     ", waiting for the excitation to settle");
    return true;
}

void EqVerifyRunner::abort(VerifyOutcome why, std::string text) {
    // disarmSource() first: nothing past a failed run may keep the loudspeaker
    // playing (output-path record sec.8).
    engine_->disarmSource();
    verify_.reset();
    outcome_ = why;
    model_.setStatus(std::move(text));
}

void EqVerifyRunner::poll() {
    if (!busy()) return;
    if (!host_.environment().deviceRunning) {
        abort(VerifyOutcome::DeviceStopped, "VERIFY aborted: the audio device stopped mid-run");
        return;
    }
    const double now = host_.nowMs();
    if (verify_->state() == VerifyState::Waiting) {
        verify_->poll();
        if (verify_->state() == VerifyState::Waiting) {
            if (captureTimedOut(armedAtMs_, now, kEqVerifyTimeoutMs)) {
                abort(VerifyOutcome::TimedOut,
                      "VERIFY timed out after 5000 ms: the excitation never started rendering "
                      "-- is the output device calling back?");
            }
            return;
        }
        measuringAtMs_ = now;
        measuringRendered_ = engine_->renderedSamples();
    }
    if (verify_->state() == VerifyState::Measuring) {
        trySubmit(now);
        return;
    }
    if (verify_->state() == VerifyState::Settling) {
        verify_->poll();
        if (verify_->state() == VerifyState::Done) {
            outcome_ = VerifyOutcome::Done;
            model_.setStatus(renderVerifySummary(*verify_->report()));
        }
    }
}

bool EqVerifyRunner::inputsUnchanged() const {
    if (model_.measurement().id != pressedMeasurementId_) return false;
    const auto now = model_.session().committed();
    if (now.size() != pressedFilters_.size()) return false;
    for (std::size_t i = 0; i < now.size(); ++i) {
        if (now[i].applied != pressedFilters_[i].applied || !sameFilter(now[i].spec, pressedFilters_[i].spec)) {
            return false;
        }
    }
    return true;
}

void EqVerifyRunner::trySubmit(double now) {
    const CaptureConfig config = host_.captureConfig();
    const std::size_t fftSize = boundFftSize();
    const double fs = model_.sampleRate();
    // The dwell counts RENDERED samples since Measuring was observed, the same
    // clock the excitation runs on. A snapshot published before the FIFO has
    // turned over still carries the room WITHOUT the excitation; submitting it
    // would report the unchanged room as a failed correction.
    const double dwellSamples = dwellSeconds(config, fftSize, fs) * engine_->sampleRate();
    const double dwellMs = dwellSeconds(config, fftSize, fs) * 1000.0;
    const double renderedSince = static_cast<double>(engine_->renderedSamples() - measuringRendered_);
    const auto stalled = [&] { return captureTimedOut(measuringAtMs_, now, dwellMs + kEqVerifyTimeoutMs); };
    const auto stall = [&] {
        abort(VerifyOutcome::TimedOut,
              "VERIFY timed out: no usable post-excitation snapshot within 5000 ms of the dwell");
    };
    if (renderedSince < dwellSamples) {
        if (stalled()) stall();
        return;
    }

    const SnapshotPtr snapshot = host_.latest();
    if (!snapshot || !snapshot->transfer.has_value() || !snapshot->transfer->coherence.has_value()) {
        if (stalled()) stall();  // the coherence gate has not opened yet: keep waiting
        return;
    }
    const auto& transfer = *snapshot->transfer;
    if (snapshot->fftSize != fftSize || snapshot->sampleRate != fs ||
        transfer.magnitudeDb.size() != model_.measuredDb().size()) {
        abort(VerifyOutcome::SettingsChanged, "VERIFY aborted: the live fftSize or sample rate changed mid-run");
        return;
    }
    if (config.transferAveraging == rta::dsp::TransferAveraging::Exponential) {
        abort(VerifyOutcome::SettingsChanged, "VERIFY aborted: transfer averaging changed to Exponential mid-run");
        return;
    }
    if (!inputsUnchanged()) {
        abort(VerifyOutcome::InputsChanged,
              "VERIFY aborted: the measurement or filter list changed mid-run; press VERIFY again");
        return;
    }

    // Trusted means trusted in BOTH: the prediction was built on the picked
    // measurement's trusted bins, and a bin the new capture's coherence
    // refuses is not evidence either (EqVerify.h: an untrusted bin is
    // reported, never flagged).
    const std::size_t bins = model_.measuredDb().size();
    const auto afterMask = buildTrustMask(*transfer.coherence, bins, model_.session().config().trustFloor);
    std::vector<std::uint8_t> trusted(bins, 0);
    const auto beforeMask = model_.session().trusted();
    for (std::size_t k = 0; k < bins; ++k) trusted[k] = (beforeMask[k] != 0 && afterMask[k] != 0) ? 1 : 0;

    const std::vector<double> predicted = model_.session().ghostDb();
    const std::vector<double> targetLine = model_.targetLineDb();
    std::vector<float> target(targetLine.size());
    for (std::size_t k = 0; k < target.size(); ++k) target[k] = static_cast<float>(targetLine[k]);
    verify_->submitSnapshot(transfer.magnitudeDb, predicted, target, model_.measuredDb(), *transfer.coherence,
                            trusted, transfer.effectiveAverages);

    if (host_.freeze) {
        if (auto trace = host_.freeze(*snapshot)) {
            const std::string name = "VERIFY @ " + (host_.timeLabel ? host_.timeLabel() : std::string("--"));
            afterTraceId_ = library_->add(std::move(*trace), name, kEqVerifyGroup);
        }
    }
    model_.setStatus("VERIFY: measurement taken, excitation ramping out");
}

bool EqVerifyRunner::adopt() {
    if (!canAdopt()) {
        model_.setStatus("ADOPT: run VERIFY to completion first");
        return false;
    }
    if (!inputsUnchanged()) {
        outcome_ = VerifyOutcome::InputsChanged;
        model_.setStatus("ADOPT refused: the measurement or filter list changed since VERIFY; press VERIFY again");
        return false;
    }
    // Bind first: a refused bind leaves everything as it was, where marking
    // applied first would leave filters marked against a measurement that does
    // not contain them.
    if (!model_.selectMeasurement(*library_, afterTraceId_)) return false;
    std::size_t marked = 0;
    for (std::size_t i = 0; i < model_.session().committed().size(); ++i) {
        if (model_.session().committed()[i].applied) continue;
        model_.setApplied(i, true);
        ++marked;
    }
    const std::string name = model_.measurement().name;
    verify_.reset();
    outcome_ = VerifyOutcome::Adopted;
    model_.setStatus("ADOPT: " + std::to_string(marked) + " filter(s) marked applied; the measurement is now \"" +
                     name + "\"");
    return true;
}

}  // namespace rta::measure
