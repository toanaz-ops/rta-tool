// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
#include "measure/GeneratorRunner.h"

#include "rta/platform/ChannelConfig.h"

#include <algorithm>
#include <utility>

namespace rta::measure {

namespace {

/// 'GEN'. A fixed seed: the operator's pink noise is the same stream every
/// press, which makes a swap's level change the ONLY difference between two
/// plays.
constexpr std::uint64_t kGeneratorNoiseSeed = 0x47454Eull;

bool contains(const std::vector<int>& v, int ch) {
    return std::find(v.begin(), v.end(), ch) != v.end();
}

}  // namespace

GeneratorRunner::GeneratorRunner(rta::platform::OutputEngine& engine, GeneratorHost host)
    : engine_(engine), host_(std::move(host)) {}

GeneratorRunner::~GeneratorRunner() {
    if (state_ != GeneratorState::Idle) engine_.disarmSource();
}

void GeneratorRunner::routeSelected() noexcept {
    // ADDITIVE, record sec.13.2: each ticked output is routed on its own, the
    // rest are switched off -- not `soloOutput`, which would keep only the
    // last of several ticks.
    for (int ch = 0; ch < rta::platform::kMaxChannels; ++ch) {
        engine_.routeOutput(ch, contains(settings_.outputs, ch));
    }
}

void GeneratorRunner::goIdle(const std::string& reason) {
    // Disarm and stay down. Nothing in this class arms again except start().
    engine_.disarmSource();
    state_ = GeneratorState::Idle;
    reason_ = reason;
}

GeneratorRefusal GeneratorRunner::start() {
    if (state_ != GeneratorState::Idle) return GeneratorRefusal::AlreadyRunning;
    const GeneratorEnvironment env = host_.environment();
    if (!env.busyBy.empty()) {
        reason_ = "refused (" + env.busyBy + " is running)";
        return GeneratorRefusal::Busy;
    }
    if (!env.synthetic && !env.deviceRunning) {
        // F5: a stopped LIVE device never renders, so nothing would ever play
        // and Stopping could never see quiescence.
        reason_ = "refused (no audio device running)";
        return GeneratorRefusal::NoDevice;
    }
    settings_.outputs = pruneOutputs(settings_.outputs, env.numOutputs);
    const std::uint64_t epoch = engine_.outputEpoch();
    // setSource FIRST and its bool is the refusal (output-path record sec.6):
    // routing or arming after a refused setSource would play somebody else's
    // slot at the generator's gates.
    if (!engine_.setSource(makeSource(settings_, engine_.sampleRate(), kGeneratorNoiseSeed))) {
        reason_ = "refused (output engine busy)";
        return GeneratorRefusal::EngineNotQuiescent;
    }
    routeSelected();
    engine_.armSource();
    armedEpoch_ = epoch;
    state_ = GeneratorState::Playing;
    reason_.clear();
    return GeneratorRefusal::None;
}

void GeneratorRunner::stop() {
    if (state_ != GeneratorState::Playing && state_ != GeneratorState::Swapping) return;
    engine_.disarmSource();
    state_ = GeneratorState::Stopping;
    waitStartMs_ = host_.nowMs();
}

void GeneratorRunner::cancel(const std::string& reason) {
    if (state_ == GeneratorState::Idle) return;
    goIdle("stopped (" + reason + ")");
}

GeneratorRefusal GeneratorRunner::apply(GeneratorSettings next) {
    if (next.outputs.empty()) return GeneratorRefusal::LastOutput;
    const GeneratorEnvironment env = host_.environment();
    next.levelDb = clampGeneratorLevelDb(next.levelDb);
    next.outputs = pruneOutputs(std::move(next.outputs), env.numOutputs);

    const bool sourceChanged =
        next.signal != settings_.signal || next.levelDb != settings_.levelDb ||
        (next.signal == GeneratorSignal::Sine && next.frequencyHz != settings_.frequencyHz);
    const bool outputsChanged = next.outputs != settings_.outputs;
    settings_ = std::move(next);

    if (state_ == GeneratorState::Playing || state_ == GeneratorState::Swapping) {
        if (outputsChanged) routeSelected();  // live, no swap
    }
    if (state_ == GeneratorState::Playing && sourceChanged) {
        engine_.disarmSource();  // the swap: poll() installs settings_ once quiescent
        state_ = GeneratorState::Swapping;
        waitStartMs_ = host_.nowMs();
    }
    return GeneratorRefusal::None;
}

void GeneratorRunner::poll() {
    const GeneratorEnvironment env = host_.environment();
    if (env.numOutputs > 0) {
        std::vector<int> pruned = pruneOutputs(settings_.outputs, env.numOutputs);
        if (pruned != settings_.outputs) {
            settings_.outputs = std::move(pruned);
            if (state_ == GeneratorState::Playing || state_ == GeneratorState::Swapping) routeSelected();
        }
    }
    if (state_ == GeneratorState::Idle) return;

    // F4: a device restart already silenced the engine (prepare() bumps the
    // epoch and disarms). All the UI has to do is NOT arm again.
    if (engine_.outputEpoch() != armedEpoch_) {
        goIdle("stopped (audio device restarted)");
        return;
    }
    if (!env.synthetic && !env.deviceRunning &&
        (state_ == GeneratorState::Playing || state_ == GeneratorState::Swapping)) {
        goIdle("stopped (audio device stopped)");
        return;
    }

    if (state_ == GeneratorState::Swapping) {
        if (engine_.sourceIsQuiescent()) {
            if (engine_.setSource(makeSource(settings_, engine_.sampleRate(), kGeneratorNoiseSeed))) {
                engine_.armSource();
                state_ = GeneratorState::Playing;
                return;
            }
        }
        if (host_.nowMs() - waitStartMs_ >= kGeneratorStopTimeoutMs) {
            goIdle("stopped (output did not settle)");
        }
        return;
    }

    if (state_ == GeneratorState::Stopping) {
        if (engine_.sourceIsQuiescent()) {
            state_ = GeneratorState::Idle;
            reason_.clear();
        } else if (host_.nowMs() - waitStartMs_ >= kGeneratorStopTimeoutMs) {
            // F5: no callback is draining the gate. Give up waiting; the
            // source stays disarmed, and the next start() is refused by the
            // engine until a device renders again.
            state_ = GeneratorState::Idle;
            reason_ = "stopped (no audio callback; output not confirmed silent)";
        }
    }
}

std::string GeneratorRunner::signalPhrase() const {
    std::string phrase = signalName(settings_.signal);
    if (settings_.signal == GeneratorSignal::Sine) phrase += " " + frequencyText(settings_.frequencyHz);
    return phrase + " " + levelText(settings_);
}

std::string GeneratorRunner::readout() const {
    const std::string where = outputsText(settings_.outputs);
    switch (state_) {
        case GeneratorState::Playing:
        case GeneratorState::Swapping: {
            std::string text = "GEN: playing " + signalPhrase() + " on " + where;
            if (settings_.levelDb >= kGeneratorCeilingDb) text += " (ceiling)";
            return text;
        }
        case GeneratorState::Stopping:
            return "GEN: stopping -- " + signalPhrase() + ", " + where;
        case GeneratorState::Idle:
            break;
    }
    return "GEN: " + (reason_.empty() ? std::string("stopped") : reason_) + " -- " + signalPhrase() +
           ", " + where;
}

}  // namespace rta::measure
