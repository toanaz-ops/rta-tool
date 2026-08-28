// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. See
// docs/specs/2026-08-28-trace-library-and-session.md §1.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rta::trace {

enum class Field { Magnitude, Phase, Coherence };

/// dBFS and dBSPL differ by the calibration offset. Storing the offset without
/// the unit lets a trace be reloaded as the other one: the numbers stay
/// plausible, they are wrong by that offset, and nothing on screen says so.
enum class LevelUnit { DbFs, DbSpl };

/// What the measurement WAS. Fixed at capture and never edited -- editing it
/// would make the file describe a measurement that never happened. Everything
/// the user can change lives in the library instead (spec §2).
struct CaptureMeta {
    std::string id;
    std::int64_t capturedAtUnixMs = 0;
    std::string deviceName;
    std::string channelRoles;
    double sampleRate = 0.0;
    int fftSize = 0;
    std::string window;
    std::string averagingType;
    int averagingDepth = 0;
    /// Effective, not raw: overlapped frames are not independent (dual-FFT
    /// record §5), and a gate that trusts a raw count opens too early.
    double effectiveAverages = 0.0;
    int appliedDelaySamples = 0;
    float calibrationOffsetDb = 0.0f;
    LevelUnit calibrationUnit = LevelUnit::DbFs;
};

/// Bins in a real FFT of `fftSize`: DC through Nyquist inclusive.
[[nodiscard]] constexpr std::size_t pointCountFor(int fftSize) noexcept {
    return fftSize > 0 ? static_cast<std::size_t>(fftSize) / 2u + 1u : 0u;
}

/// One complete capture. NOT a tagged RTA/TF union: a view asks what fields
/// this trace HAS. A single-channel capture simply has no phase, so nothing
/// ever has to invent one (spec §1).
class Trace {
public:
    /// Refuses a magnitude whose length disagrees with `meta.fftSize`. A trace
    /// whose axis and data disagree is not a trace worth constructing.
    [[nodiscard]] static std::optional<Trace> make(CaptureMeta meta,
                                                   std::vector<float> magnitudeDb) {
        if (magnitudeDb.size() != pointCountFor(meta.fftSize) || magnitudeDb.empty()) {
            return std::nullopt;
        }
        return Trace(std::move(meta), std::move(magnitudeDb));
    }

    [[nodiscard]] bool setPhase(std::vector<float> phase) {
        if (phase.size() != magnitude_.size()) return false;
        phase_ = std::move(phase);
        return true;
    }

    [[nodiscard]] bool setCoherence(std::vector<float> coherence) {
        if (coherence.size() != magnitude_.size()) return false;
        coherence_ = std::move(coherence);
        return true;
    }

    [[nodiscard]] bool has(Field f) const noexcept { return !field(f).empty(); }

    [[nodiscard]] std::span<const float> field(Field f) const noexcept {
        switch (f) {
            case Field::Magnitude: return magnitude_;
            case Field::Phase: return phase_;
            case Field::Coherence: return coherence_;
        }
        return {};
    }

    [[nodiscard]] const CaptureMeta& meta() const noexcept { return meta_; }
    [[nodiscard]] std::size_t pointCount() const noexcept { return magnitude_.size(); }

    /// Derived, never stored: one source of truth for the axis, so a stored
    /// axis can never disagree with the data it indexes (spec §1).
    [[nodiscard]] double binHz() const noexcept {
        return meta_.fftSize > 0 ? meta_.sampleRate / static_cast<double>(meta_.fftSize) : 0.0;
    }

private:
    Trace(CaptureMeta m, std::vector<float> mag)
        : meta_(std::move(m)), magnitude_(std::move(mag)) {}

    CaptureMeta meta_;
    std::vector<float> magnitude_;
    std::vector<float> phase_;
    std::vector<float> coherence_;
};

}  // namespace rta::trace
