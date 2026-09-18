// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace rta::meter {

/// Why a percentile is absent. An Ln printed as the bottom of the span reads
/// as a measurement and is a confession that the span was wrong, so the
/// absence carries its reason instead (record 5;
/// memory/a-placeholder-for-an-absent-result-erases-its-state.md).
enum class LnAbsence {
    None,       ///< the value is present
    BelowSpan,  ///< the rank asked for falls among samples under baseDb
    AboveSpan,  ///< ... or over the top of the span
    NoData,     ///< nothing has been added at all -- not the same as silence
};

/// A percentile and, when it is absent, why. Returned by value so the class
/// keeps NO mutable state: the analysis thread publishes histograms, and a
/// "last absence" member would be a data race for the sake of one enum.
struct LnResult {
    std::optional<double> db;
    LnAbsence absence = LnAbsence::NoData;
};

/// Record 5's Ln accumulator: 2000 bins of 0.1 dB from `baseDb`, plus TWO
/// separate out-of-span counters. 2000*4 + 2*4 = 8 008 B, allocated once and
/// constant for any session length -- which is what it buys over keeping every
/// sample and sorting (`Leq::percentileDb` copies and sorts TWICE per call).
///
/// The shape is taken from the one commercial data model anybody can read: the
/// Larson Davis 831/LxT SDK's `NUM_STAT_BINS 2000` at 0.1 dB resolution,
/// `NUM_SLM_LN_BINS = NUM_STAT_BINS + 2` for the over and under bins, an
/// `m_nBaseDB` field, and an `m_nLnTable` of COUNTS rather than samples.
///
/// Fed the TIME-WEIGHTED level (ISO 1996-1 cl. 3.1.3: Ln is a time-weighted
/// quantity, unlike Leq), i.e. `Detector::levelDb`, at the detector sampling
/// rate.
///
/// THE BASE HAS NO DEFAULT, and that is the decision this class is most likely
/// to be "tidied" out of. Record 5 wrote `-20.0` while assuming SPL
/// throughout; uncalibrated this project publishes mean-square dBFS, where a
/// real session sits at -30..-60 dBFS and a span based at -20.0 makes every Ln
/// of every out-of-the-box session permanently `BelowSpan`. The caller derives
/// the base -- `SplConfig::histogramBaseDb()` is
/// `measure::kLevelFloorDb + referenceOffsetDb` -- and a defaulted -20.0 here
/// would hand that trap back to whoever forgot. SPL-R8; the fixtures are W1-A7
/// and W1-A8; the lesson is
/// memory/a-default-must-be-run-through-the-gate-it-feeds.md.
///
/// The bin index is OFFSET-INVARIANT: an offset applied to the level and to
/// the base cancels, so feeding un-offset levels against the un-offset floor
/// and feeding SPL against the offset base are the same histogram. That is why
/// a +100 dB offset recovers record 5's own `[-20, +180)` span exactly.
class LevelHistogram {
public:
    static constexpr double kBinWidthDb = 0.1;
    static constexpr std::size_t kBinCount = 2000;

    /// The table the record sizes at 7.82 KiB: the bins plus the two
    /// out-of-span counters, and nothing else.
    static constexpr std::size_t kTableBytes =
        kBinCount * sizeof(std::uint32_t) + 2 * sizeof(std::uint32_t);

    /// @param baseDb  the bottom of the span, in the same dB reference as the
    ///                levels that will be added. Derived by the caller; see
    ///                the class comment for why there is no default.
    explicit LevelHistogram(double baseDb) noexcept : baseDb_(baseDb) {}

    /// Back to the state of a freshly-constructed histogram; the base stays.
    void reset() noexcept;

    /// One time-weighted level. A level under the span raises `belowSpan()`,
    /// one over it raises `aboveSpan()`, and neither is clamped into a bin.
    /// A non-finite input is counted out-of-span (+inf above, NaN and -inf
    /// below) rather than binned: `Detector::levelDb` cannot produce one, and
    /// the branch exists so a caller's NaN cannot pass as a real level.
    void add(double levelDb) noexcept;

    /// Ln over bin CENTRES, through the project's ONE interpolation convention
    /// (`percentileLevelDb`, Leq.h:28 -- numpy 'linear' at q = 1 - n/100).
    /// Absent, with a reason, when an order statistic the interpolation needs
    /// falls outside the span.
    ///
    /// The residual against the exact percentile is bounded by `kBinWidthDb/2`
    /// = 0.05 dB for every n, every distribution and every sample count:
    /// quantising to a bin centre moves each value by at most w/2, a monotone
    /// map commutes with sorting so each order statistic moves by at most w/2,
    /// and Ln is a convex combination of two of them. A theorem, not a
    /// measured maximum -- and equal to the semi-range IEC 61672-2 cl. 6.21
    /// assigns a 0.1 dB display's own resolution.
    ///
    /// @throws std::invalid_argument if n is outside [0,100].
    [[nodiscard]] LnResult percentile(double n) const;

    /// `percentile(n).db`, for a caller that does not need the reason.
    [[nodiscard]] std::optional<double> percentileDb(double n) const;

    /// a[i] += b[i] -- the composability the histogram buys over sorting.
    /// Integer addition only, so a merged percentile is BITWISE equal to the
    /// percentile of the concatenated stream.
    /// @throws std::invalid_argument if the bases differ: merging misaligned
    ///         bins would silently shift every level in one of the two.
    void merge(const LevelHistogram& other);

    [[nodiscard]] double baseDb() const noexcept { return baseDb_; }

    /// The exclusive top of the span, `baseDb + kBinCount*kBinWidthDb`.
    [[nodiscard]] double topDb() const noexcept {
        return baseDb_ + static_cast<double>(kBinCount) * kBinWidthDb;
    }

    /// The level a sample in `bin` is reported as: the CENTRE, not the edge.
    /// The half-bin is load-bearing -- keying a bin to its floor biases every
    /// Ln down by w/2.
    [[nodiscard]] double binCentreDb(std::size_t bin) const noexcept {
        return baseDb_ + (static_cast<double>(bin) + 0.5) * kBinWidthDb;
    }

    [[nodiscard]] std::uint32_t count(std::size_t bin) const noexcept {
        return bin < kBinCount ? counts_[bin] : 0u;
    }
    [[nodiscard]] std::uint32_t belowSpan() const noexcept { return belowSpan_; }
    [[nodiscard]] std::uint32_t aboveSpan() const noexcept { return aboveSpan_; }

    /// Every sample added, in-span or not. Held in 64 bits because it is the
    /// divisor of the order-statistic arithmetic; the BINS are 32-bit because
    /// record 5 sizes the table on that, and a uint32 bin at the 10 Hz
    /// detector rate takes 13.6 years to overflow (W1-A1 asserts it).
    [[nodiscard]] std::uint64_t total() const noexcept { return total_; }

private:
    /// Where the k-th smallest sample sits. `inSpan == false` carries the side.
    struct Pick {
        bool inSpan = false;
        LnAbsence side = LnAbsence::NoData;
        double db = 0.0;
    };
    [[nodiscard]] Pick pick(std::uint64_t k) const noexcept;

    double baseDb_;
    std::array<std::uint32_t, kBinCount> counts_{};
    std::uint32_t belowSpan_ = 0;
    std::uint32_t aboveSpan_ = 0;
    std::uint64_t total_ = 0;
};

static_assert(LevelHistogram::kTableBytes == 8008,
              "record 5 sizes the Ln table at 2000*4 + 2*4 = 8008 bytes");

}  // namespace rta::meter
