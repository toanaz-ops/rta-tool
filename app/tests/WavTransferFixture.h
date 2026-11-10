// SPDX-License-Identifier: AGPL-3.0-or-later
// H11 W2 fixtures: an in-memory multichannel "file" and the signals it carries.
#pragma once

#include "measure/WavTransfer.h"
#include "rta/dsp/Biquad.h"
#include "rta/eq/BiquadDesign.h"
#include "rta/gen/Synthetic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace wavfix {

constexpr double kFs = 48000.0;

class MemorySource final : public rta::measure::SampleBlockSource {
public:
    std::vector<std::vector<float>> channels;
    double fs = kFs;
    /// Any read touching a sample at or past this index fails (default: none).
    std::int64_t failAtOrAfter = std::numeric_limits<std::int64_t>::max();

    int numChannels() const override { return static_cast<int>(channels.size()); }
    double sampleRate() const override { return fs; }
    std::int64_t lengthSamples() const override {
        return channels.empty() ? 0 : static_cast<std::int64_t>(channels.front().size());
    }
    bool read(int channel, std::int64_t start, std::span<float> out) const override {
        if (channel < 0 || channel >= numChannels()) return false;
        const auto& data = channels[static_cast<std::size_t>(channel)];
        const auto end = start + static_cast<std::int64_t>(out.size());
        if (start < 0 || end > static_cast<std::int64_t>(data.size()) || end > failAtOrAfter) return false;
        std::copy_n(data.begin() + start, out.size(), out.begin());
        return true;
    }
};

/// Deterministic pink, a 2^20-sample loop (21.8 s): longer than every fixture,
/// so the signal never repeats inside a delay window (a loop shorter than the
/// +-1 s lag window would put a second, equal correlation peak inside it).
inline std::vector<float> pink(std::size_t n, std::uint32_t seed) {
    rta::gen::SyntheticPink source(std::size_t{1} << 20, -12.0, seed);
    std::vector<float> out(n);
    source.render(out);
    return out;
}

/// The test system: +6 dB peaking at 1 kHz, Q = 1 (RBJ cookbook).
inline rta::dsp::Biquad::Coeffs peaking() {
    return rta::eq::designBiquad({rta::eq::FilterType::Peaking, 1000.0, 1.0, 6.0}, kFs);
}

/// y = peaking(x), in double, from rest, over `n + extra` samples of x.
inline std::vector<float> filtered(const std::vector<float>& x) {
    rta::dsp::BiquadCascade cascade({peaking()});
    std::vector<float> y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) y[i] = static_cast<float>(cascade.processSample(x[i]));
    return y;
}

/// `ref` and a measurement lagging it by `delayAt(n)` samples through the test
/// system: meas[n] = y[n - delayAt(n)], 0 before the signal exists. A negative
/// delay means the measurement LEADS (it reads y ahead of ref). `ref` is
/// longer than the file by the largest lead so the lead never runs off the end.
template <class DelayAt>
void makePair(std::size_t n, std::uint32_t seed, DelayAt delayAt, std::vector<float>& ref,
              std::vector<float>& meas) {
    const std::size_t pad = 4096;
    const auto x = pink(n + pad, seed);
    const auto y = filtered(x);
    ref.assign(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(n));
    meas.assign(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        const std::int64_t from = static_cast<std::int64_t>(i) - delayAt(i);
        if (from >= 0 && from < static_cast<std::int64_t>(y.size())) meas[i] = y[static_cast<std::size_t>(from)];
    }
}

inline MemorySource twoChannel(std::size_t n, int delay, std::uint32_t seed = 5) {
    MemorySource s;
    s.channels.resize(2);
    makePair(n, seed, [delay](std::size_t) { return delay; }, s.channels[0], s.channels[1]);
    return s;
}

/// A one-file request with its display fields left at their defaults. Positional
/// brace-init of `WavTransferRequest` omits the H12 display members, which gcc and
/// clang report under -Wmissing-field-initializers (CI's warning gate).
inline rta::measure::WavTransferRequest req(int referenceChannel, int measurementChannel, double startSeconds,
                                            double lengthSeconds, const char* fileName) {
    rta::measure::WavTransferRequest r;
    r.referenceChannel = referenceChannel;
    r.measurementChannel = measurementChannel;
    r.startSeconds = startSeconds;
    r.lengthSeconds = lengthSeconds;
    r.fileName = fileName;
    return r;
}

}  // namespace wavfix
