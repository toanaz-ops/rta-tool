// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Differential test: the per-stage-table Fft against a verbatim copy of the
// transform it replaced (single N/2 twiddle table read at stride N/len, the
// conjugate chosen per butterfly).
//
// This is an equivalence test, not a correctness test -- test_fft.cpp's NumPy
// goldens carry correctness. It exists because the rewrite promises to change
// memory layout and loop structure and NOTHING about the arithmetic: every
// twiddle comes from the same expression and every butterfly performs the same
// float operations in the same order. That promise is checkable bit for bit,
// so the assertion is bitwise equality, not a tolerance. A tolerance would
// accept a wrong stage index that happens to land on a nearby angle.
//
// If a future toolchain contracts the two loop bodies differently (an fma in
// one and not the other -- see memory/a-bitwise-identity-can-belong-to-the-isa-
// not-the-arithmetic.md), this is the test that goes red, and the right answer
// is a derived float32 bound per memory/float32-fft-precision.md, not a
// loosened memcmp.

#include <catch2/catch_test_macros.hpp>

#include "rta/dsp/Fft.h"

#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <random>
#include <utility>
#include <vector>

using namespace rta::dsp;

namespace {

/// The transform as it was before the stage tables, kept here only.
class OldFft {
public:
    explicit OldFft(const std::size_t size) : size_(size) {
        const int levels = std::countr_zero(size);
        reversed_.resize(size);
        for (std::size_t i = 0; i < size; ++i) {
            std::size_t r = 0;
            for (int bit = 0; bit < levels; ++bit) {
                r |= ((i >> bit) & std::size_t{1}) << (levels - 1 - bit);
            }
            reversed_[i] = r;
        }
        constexpr double kTwoPi = 2.0 * std::numbers::pi;
        twiddles_.resize(size / 2);
        for (std::size_t k = 0; k < twiddles_.size(); ++k) {
            const double angle = -kTwoPi * static_cast<double>(k) / static_cast<double>(size);
            twiddles_[k] = { static_cast<float>(std::cos(angle)),
                             static_cast<float>(std::sin(angle)) };
        }
    }

    void forward(std::vector<std::complex<float>>& data) { transform(data, false); }

    void inverse(std::vector<std::complex<float>>& data) {
        transform(data, true);
        const float scale = 1.0f / static_cast<float>(size_);
        for (auto& value : data) value *= scale;
    }

private:
    void transform(std::vector<std::complex<float>>& data, const bool conjugate) {
        for (std::size_t i = 0; i < size_; ++i) {
            const std::size_t j = reversed_[i];
            if (i < j) std::swap(data[i], data[j]);
        }
        for (std::size_t len = 2; len <= size_; len <<= 1) {
            const std::size_t half = len / 2;
            const std::size_t step = size_ / len;
            for (std::size_t start = 0; start < size_; start += len) {
                for (std::size_t j = 0; j < half; ++j) {
                    const auto w = conjugate ? std::conj(twiddles_[j * step])
                                             : twiddles_[j * step];
                    const auto upper = data[start + j];
                    const auto lower = data[start + j + half] * w;
                    data[start + j]        = upper + lower;
                    data[start + j + half] = upper - lower;
                }
            }
        }
    }

    std::size_t size_;
    std::vector<std::size_t> reversed_;
    std::vector<std::complex<float>> twiddles_;
};

/// mt19937 output is fixed by the standard; std::uniform_real_distribution's is
/// not, so map the integer stream to floats by hand to get the same inputs on
/// every standard library.
std::vector<std::complex<float>> randomInput(const std::size_t n, const std::uint32_t seed) {
    std::mt19937 rng(seed);
    const auto next = [&rng] {
        return static_cast<float>(rng() >> 8) / static_cast<float>(1u << 24) * 2.0f - 1.0f;
    };
    std::vector<std::complex<float>> data(n);
    for (auto& value : data) {
        const float re = next();
        const float im = next();
        value = { re, im };
    }
    return data;
}

bool bitwiseEqual(const std::vector<std::complex<float>>& a,
                  const std::vector<std::complex<float>>& b) {
    return a.size() == b.size()
        && std::memcmp(a.data(), b.data(), a.size() * sizeof(std::complex<float>)) == 0;
}

}  // namespace

TEST_CASE("Stage-table Fft is bitwise identical to the strided-table transform",
          "[fft][equivalence]") {
    for (std::size_t n = 2; n <= 65536; n <<= 1) {
        OldFft oldFft(n);
        Fft newFft(n);
        for (std::uint32_t seed = 1; seed <= 4; ++seed) {
            const auto input = randomInput(n, seed * 7919u + static_cast<std::uint32_t>(n));

            auto expectedForward = input;
            auto actualForward = input;
            oldFft.forward(expectedForward);
            newFft.forward(actualForward);
            INFO("forward, N=" << n << ", seed=" << seed);
            CHECK(bitwiseEqual(expectedForward, actualForward));

            auto expectedInverse = input;
            auto actualInverse = input;
            oldFft.inverse(expectedInverse);
            newFft.inverse(actualInverse);
            INFO("inverse, N=" << n << ", seed=" << seed);
            CHECK(bitwiseEqual(expectedInverse, actualInverse));
        }
    }
}

TEST_CASE("Stage-table Fft inverse is not a copy of forward", "[fft][equivalence]") {
    // Guards the equivalence test itself: if both directions silently ran the
    // same conjugation, old-vs-new would still agree for a wrong reason. A
    // non-symmetric input must come out different from the two directions.
    const std::size_t n = 64;
    const auto input = randomInput(n, 99);
    Fft fft(n);
    auto viaForward = input;
    auto viaInverse = input;
    fft.forward(viaForward);
    fft.inverse(viaInverse);
    for (auto& value : viaInverse) value *= static_cast<float>(n);
    CHECK_FALSE(bitwiseEqual(viaForward, viaInverse));
}
