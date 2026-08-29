# L2 — Dual-FFT transfer-function engine: implementation plan

> **For agentic workers:** this is station 3 of the five-station pipeline in
> `docs/reports/README.md`. Build it task by task, TDD, one commit per task, a
> fresh verifier between tasks. Steps use `- [ ]` so a worker can tick them.

*2026-08-29. Turns `docs/dsp/2026-08-28-dual-fft.md` into code. Every decision
below is **already made** in that record — this document adds only exact files,
exact signatures, exact tests and exact acceptance numbers. Where a test's
tolerance is a judgement rather than a derivation, it says so.*

**Goal:** `rta_core` can take two aligned channels of floats and return a
transfer function — H1/H2/Hv, magnitude-squared coherence, group delay — plus a
GCC-PHAT delay estimate, all provable on CI with no sound card.

**Architecture:** pull-based and stateless at the edges. The engine owns two
ring buffers and three accumulators (`Sxx`, `Syy`, `Sxy`) and nothing else; the
estimators are free functions over those accumulators; the delay finder is a
free function over two spans. Phase unwrap is **not** in core — it is a display
operation in `app/src/measure/`.

**Tech stack:** C++20, existing `rta::dsp::{RealFft, Window, RingBuffer,
Biquad}`, Catch2 v3, scipy for golden vectors only.

---

## Global constraints

These apply to every task; they are not repeated per task.

- Every new file starts with `// SPDX-License-Identifier: AGPL-3.0-or-later`.
- `core/` must not include JUCE, Qt, or any audio-device API. Guard:
  `core_has_no_framework_deps`. `app/src/measure/` is under the second guard,
  `measure_has_no_framework_deps` — plain C++ and `rta::` only.
- Headers under `core/include/rta/dsp/`, implementations under `core/src/dsp/`.
- **Hard cap 400 lines per file, aim 300.** Headers included.
- Comments explain *why a formula is that formula*. A reader must be able to
  check the DSP against Bendat & Piersol or Harris 1978 from the comments alone.
- MSVC `/W4` must stay at **zero warnings**. `/utf-8` is already on.
- Build and test for this lane:
  ```
  cmake -S . -B build-l2 -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=OFF
  cmake --build build-l2 --config Release --parallel
  ctest --test-dir build-l2 -C Release --output-on-failure
  ```
- **Baseline before task 1: 208/208 passing, 0 warnings** (core-only configure;
  the 226 figure in `docs/HANDOFF.md` is the `RTA_BUILD_APP=ON` count, which
  adds the two JUCE-linked targets). Every task states the count it must reach.
  **Those per-task counts are estimates.** If a review round adds tests, the
  later numbers shift — re-measure, do not trust the number written here.
- `core/CMakeLists.txt` and `core/tests/CMakeLists.txt` are the cross-session
  contention points. Each task edits them in **its own commit**, never in a
  long-lived branch.
- The Python venv is in the **main checkout**, not in this worktree:
  `D:\DEV CAVE EP3\PRJ010-RTA-TOOL\.venv\Scripts\python.exe`. Call it by
  absolute path; do not create a second venv.

## Naming, fixed here so tasks cannot disagree

| Symbol | Meaning |
|---|---|
| `x`, `X`, `Sxx` | **reference** channel (the electrical tap) |
| `y`, `Y`, `Syy` | **measurement** channel (the microphone) |
| `Sxy` | `conj(X) * Y`, averaged. Matches `scipy.signal.csd(x, y)`, which computes `conjugate(X) * Y` in `_spectral_helper`. |
| `referenceDelaySamples` | **positive = the measurement lags the reference.** Same sign convention `findDelayPhat` returns, so its output feeds this field directly. |

## Why several tests use a rectangular window — read before writing any test

The decision record says feeding `y[n] = g·x[n−D]` makes `H` *exactly*
`g·e^{−j2πkD/N}`. That is true **only if the analysed frame of `y` is the
circular rotation of the frame of `x`, and the window does not break the
rotation.**

- Periodicity is arranged by `SyntheticPink(blockSize == fftSize, …)`, which
  loops one block, so any frame is a rotation of that block.
- **A non-rectangular window breaks it anyway.** `w[n]·x[(n−D) mod N]` is not
  the rotation of `w[n]·x[n]`, so `Y[k] = e^{−jθk}·X[k]` no longer holds bin by
  bin. With `WindowType::Rectangular` it holds exactly, to floating point.

So: **exact identity tests use `Rectangular`.** Hann appears in the statistical
tests (coherence against theory, goldens against scipy), where it belongs.
A test that asserts the exact identity under Hann will fail for a correct
implementation — and "fixing" it by loosening the tolerance destroys the only
test in the suite that pins magnitude, phase and delay at once.

---

## File structure

| File | Responsibility | Task |
|---|---|---|
| `core/include/rta/dsp/PsdScaling.h` | the one place `2/(fs·Σw²)` and the DC/Nyquist halving live | 2 |
| `core/include/rta/dsp/AverageCount.h` + `core/src/dsp/AverageCount.cpp` | Harris overlap correlation; effective independent averages | 1 |
| `core/include/rta/dsp/DualFftEngine.h` + `core/src/dsp/DualFftEngine.cpp` | two rings, delay compensation, three accumulators, two averaging modes | 2 |
| `core/include/rta/dsp/TransferEstimator.h` + `core/src/dsp/TransferEstimator.cpp` | H1/H2/Hv, magnitude-squared coherence, the gated snapshot | 3 |
| `core/include/rta/dsp/GroupDelay.h` + `core/src/dsp/GroupDelay.cpp` | closed-form group delay | 4 |
| `core/include/rta/dsp/DelayFinder.h` + `core/src/dsp/DelayFinder.cpp` | GCC-PHAT, sub-sample peak, polarity | 5 |
| `app/src/measure/PhaseUnwrap.h` + `app/src/measure/PhaseUnwrap.cpp` | display-side unwrap, coherence-gated | 6 |
| `core/tests/check_coherence_gate.cmake` | build guard: the gate cannot be bypassed | 7 |
| `tools/gen_transfer.py` → `core/tests/golden/transfer.txt` | scipy csd/coherence goldens | 7 |
| `docs/reports/003-dual-fft-engine.md` | the station-5 report | 8 |

---

## Task 1 — Effective average count

The engine cannot gate coherence (record §3) until it can count averages
honestly (record §5), so this comes first and depends on nothing.

**Files:**
- Create: `core/include/rta/dsp/AverageCount.h`
- Create: `core/src/dsp/AverageCount.cpp`
- Create: `core/tests/test_average_count.cpp`
- Modify: `core/CMakeLists.txt` (add `src/dsp/AverageCount.cpp`)
- Modify: `core/tests/CMakeLists.txt` (add `test_average_count.cpp`)

**Interfaces produced** (later tasks call exactly these):

```cpp
namespace rta::dsp {

/// Harris 1978's overlap correlation: how much of one windowed frame survives
/// in the next one, `lag` samples later.
///
///     c(m) = sum_n w[n] * w[n+m]  /  sum_n w[n]^2
///
/// The numerator runs ONLY over samples where both frames exist -- it does not
/// wrap around the block. Wrapping turns periodic Hann's exact 1/6 at 50 %
/// overlap into 1/3, and 1/3 is exactly the kind of wrong number that looks
/// plausible.
[[nodiscard]] double overlapCorrelation(std::span<const float> window,
                                        std::size_t lag) noexcept;

/// Effective number of INDEPENDENT averages behind `frames` equally-weighted
/// overlapped frames.
///
///     Neff = K / (1 + 2 * sum_{m=1}^{K-1} (1 - m/K) * c(m*hop)^2)
///
/// Overlapped frames share samples, so K of them buy fewer than K averages'
/// worth of variance reduction. Nothing else in this codebase may report a raw
/// frame count as an average count: the coherence gate is built on this number.
[[nodiscard]] double fifoEffectiveAverages(std::span<const float> window,
                                           std::size_t hop,
                                           std::size_t frames) noexcept;

/// Effective averages for a one-pole average seeded from its own first frame
/// (the seeding SpectrumEngine already does).
///
/// Weights after K frames are w1 = (1-a)^(K-1), wi = a(1-a)^(K-i); they sum to
/// 1, so Neff_raw = 1 / sum(wi^2), which is exactly 1 at K = 1 and tends to
/// (2-a)/a. The overlap penalty is then applied to the frames BEYOND the first:
///
///     Neff = 1 + (Neff_raw - 1) / D,  D = 1 + 2 * sum_{m>=1} c(m*hop)^2
///
/// so a single frame still counts as exactly one average, and the K -> inf
/// limit agrees with fifoEffectiveAverages.
[[nodiscard]] double exponentialEffectiveAverages(std::span<const float> window,
                                                  std::size_t hop,
                                                  double alpha,
                                                  std::size_t frames) noexcept;

}  // namespace rta::dsp
```

- [ ] **Step 1: Write the failing tests** — `core/tests/test_average_count.cpp`

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/AverageCount.h"
#include "rta/dsp/Window.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace rta::dsp;

TEST_CASE("overlap correlation has closed-form values", "[average][overlap]") {
    const Window hann(WindowType::Hann, 1024);
    const Window rect(WindowType::Rectangular, 1024);

    // Lag 0 is the definition's normalisation.
    REQUIRE(overlapCorrelation(hann.coefficients(), 0) == Catch::Approx(1.0).margin(1e-12));

    // Periodic Hann, 50 % overlap. w[n]w[n+N/2] = 0.25*sin^2(2*pi*n/N), summed
    // over the N/2 samples that overlap = N/16; sum(w^2) = 3N/8; ratio = 1/6.
    // This is a derivation, not a measurement -- if it fails, the code is wrong.
    REQUIRE(overlapCorrelation(hann.coefficients(), 512) == Catch::Approx(1.0 / 6.0).margin(1e-12));

    // Rectangular at 50 %: N/2 ones over N ones.
    REQUIRE(overlapCorrelation(rect.coefficients(), 512) == Catch::Approx(0.5).margin(1e-12));

    // No overlap at all: the frames share nothing.
    REQUIRE(overlapCorrelation(hann.coefficients(), 1024) == Catch::Approx(0.0).margin(1e-15));
    REQUIRE(overlapCorrelation(hann.coefficients(), 4096) == Catch::Approx(0.0).margin(1e-15));
}

TEST_CASE("non-overlapped frames are fully independent", "[average]") {
    const Window hann(WindowType::Hann, 1024);
    for (std::size_t k : {1u, 2u, 7u, 64u}) {
        REQUIRE(fifoEffectiveAverages(hann.coefficients(), 1024, k)
                == Catch::Approx(static_cast<double>(k)).margin(1e-12));
    }
}

TEST_CASE("50 % overlapped Hann frames are derated by exactly 1/6", "[average]") {
    const Window hann(WindowType::Hann, 1024);
    // Only m = 1 contributes: c(1024) is already zero. So the whole formula
    // collapses to K / (1 + 2*(1 - 1/K)*(1/6)^2).
    for (std::size_t k : {1u, 2u, 10u, 1000u}) {
        const double kd = static_cast<double>(k);
        const double expected = kd / (1.0 + 2.0 * (1.0 - 1.0 / kd) * (1.0 / 36.0));
        REQUIRE(fifoEffectiveAverages(hann.coefficients(), 512, k)
                == Catch::Approx(expected).margin(1e-9));
    }
    // One frame is one average, whatever the overlap.
    REQUIRE(fifoEffectiveAverages(hann.coefficients(), 256, 1) == Catch::Approx(1.0).margin(1e-12));
}

TEST_CASE("75 % overlap sums EVERY lag, not just the first", "[average]") {
    const Window hann(WindowType::Hann, 1024);
    // Harris 1978 Table 1 gives 0.659 for the one-hop correlation at 75 %.
    REQUIRE(overlapCorrelation(hann.coefficients(), 256) == Catch::Approx(0.659).margin(0.01));

    // At hop = N/4 three lags overlap -- 256, 512, 768 -- and c(1024) is zero.
    // Assert the FORMULA against those three correlations rather than a loose
    // band: a band wide enough to hold 0.5196 also holds the 0.5351 an
    // implementation produces when it stops summing after the first lag, so it
    // would pass for code that ignores two thirds of the overlap. The
    // correlations themselves are pinned independently by the closed-form test
    // above, which is what stops this being a mirror of the implementation.
    constexpr std::size_t kK = 4000;
    double penalty = 0.0;
    for (std::size_t m = 1; m <= 3; ++m) {
        const double c = overlapCorrelation(hann.coefficients(), m * 256);
        penalty += (1.0 - static_cast<double>(m) / kK) * c * c;
    }
    const double expected = static_cast<double>(kK) / (1.0 + 2.0 * penalty);
    REQUIRE(fifoEffectiveAverages(hann.coefficients(), 256, kK)
            == Catch::Approx(expected).margin(1e-6));

    // And the headline number an operator would recognise: 75 % overlap costs
    // roughly half the averages.
    REQUIRE(expected / kK == Catch::Approx(0.52).margin(0.01));
}

TEST_CASE("exponential averaging counts its own memory", "[average][exponential]") {
    const Window hann(WindowType::Hann, 1024);

    // Seeded from frame one: one frame is one average, for every alpha.
    for (double a : {0.01, 0.2, 1.0}) {
        REQUIRE(exponentialEffectiveAverages(hann.coefficients(), 1024, a, 1)
                == Catch::Approx(1.0).margin(1e-12));
    }
    // alpha = 1 keeps no memory at all: always exactly one average.
    REQUIRE(exponentialEffectiveAverages(hann.coefficients(), 1024, 1.0, 500)
            == Catch::Approx(1.0).margin(1e-12));

    // Settled, non-overlapped: (2-a)/a. (0.9)^998 is ~1e-46, so 500 frames is
    // the limit for any tolerance we can measure.
    REQUIRE(exponentialEffectiveAverages(hann.coefficients(), 1024, 0.1, 500)
            == Catch::Approx(19.0).margin(1e-9));

    // Monotone in frames, and never above the settled value.
    double previous = 0.0;
    for (std::size_t k = 1; k <= 40; ++k) {
        const double n = exponentialEffectiveAverages(hann.coefficients(), 1024, 0.1, k);
        REQUIRE(n >= previous);
        REQUIRE(n <= 19.0 + 1e-9);
        previous = n;
    }

    // Overlap derates it, and never below one average.
    const double settledOverlapped = exponentialEffectiveAverages(hann.coefficients(), 512, 0.1, 500);
    REQUIRE(settledOverlapped < 19.0);
    REQUIRE(settledOverlapped == Catch::Approx(1.0 + 18.0 / (1.0 + 2.0 / 36.0)).margin(1e-9));
}
```

- [ ] **Step 2: Run and watch it fail**

```
cmake --build build-l2 --config Release --parallel
```
Expected: compile error, `rta/dsp/AverageCount.h` not found. That is the failure.

- [ ] **Step 3: Implement**

**Both average-count functions must guard `hop == 0` before their overlap loop
and return 1.0.** The loop condition `m * hop < window.size()` never goes false
at hop 0, so `exponentialEffectiveAverages` — which has no second bound — hangs
for ever in a `noexcept` function. `fifoEffectiveAverages` survives the same
input only by accident, because it also carries `m < frames`. 1.0 is the right
answer rather than a mere escape: with no advance between frames, every frame is
the same samples, which is exactly one independent average. This was found by a
verifier compiling a harness against the real `.cpp`, not by inspection.

`overlapCorrelation`: return 1.0 for lag 0; 0.0 when `lag >= window.size()`;
otherwise `sum_{n=0}^{N-1-lag} w[n]*w[n+lag] / sumSquares`, all in `double`.
Guard `sumSquares <= 0` by returning 0.0 (a window of all zeros correlates with
nothing) — and say so in a comment.

`fifoEffectiveAverages`: `frames == 0` returns 0.0. Otherwise sum `m` from 1
while `m*hop < window.size()` **and** `m < frames`, accumulating
`(1 - m/K) * c(m*hop)^2`; return `K / (1 + 2*sum)`. Note in a comment that the
loop terminates on the window length, so the cost does not grow with K.

`exponentialEffectiveAverages`: `frames == 0` returns 0.0. Clamp `alpha` to
`(0, 1]`. Compute
`s = pow(1-a, 2*(K-1)) * (2-2a)/(2-a) + a/(2-a)`, `raw = 1/s`, then
`D = 1 + 2*sum_{m>=1} c(m*hop)^2` and return `1 + (raw - 1)/D`.

- [ ] **Step 4: Run the tests**

```
ctest --test-dir build-l2 -C Release --output-on-failure
```
Expected: all pass. Count rises from 208 to roughly 213 (five new `TEST_CASE`s).

- [ ] **Step 5: Prove the tests can fail** — mandatory, not optional

Break the implementation **on purpose**, one mutation at a time, rebuild, and
record which test went red:

| Mutation | Must turn red |
|---|---|
| let the numerator wrap around the block | `closed-form values` (1/6 becomes 1/3) |
| drop the `(1 - m/K)` factor | `derated by exactly 1/6` |
| stop the lag sum after `m == 1` | `75 % overlap` |
| return the raw frame count | `derated by exactly 1/6` |

Restore the file afterwards and rebuild. **Paste the red output into the commit
message body.** A mutation that turns nothing red means the test asserts
nothing — that is a finding, and the task is not done.

- [ ] **Step 6: Commit**

```bash
git add core/include/rta/dsp/AverageCount.h core/src/dsp/AverageCount.cpp core/tests/test_average_count.cpp core/CMakeLists.txt core/tests/CMakeLists.txt
git commit -m "feat(core): count effective averages, not frames"
```

---

## Task 2 — The engine: two rings, one delay, three accumulators

**Files:**
- Create: `core/include/rta/dsp/PsdScaling.h`
- Create: `core/include/rta/dsp/DualFftEngine.h`
- Create: `core/src/dsp/DualFftEngine.cpp`
- Create: `core/tests/test_dualfft.cpp`
- Modify: `core/src/dsp/SpectrumEngine.cpp` — replace the two literal scaling
  expressions with `PsdScaling` calls. **Behaviour must not change**; the 208
  existing tests are the proof.
- Modify: `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`

**Interfaces consumed:** `rta::dsp::fifoEffectiveAverages`,
`rta::dsp::exponentialEffectiveAverages` (task 1).

**Interfaces produced:**

```cpp
// PsdScaling.h -- header-only, constexpr, no .cpp
namespace rta::dsp {
/// The single definition of the one-sided PSD scaling used by every spectral
/// estimator in core. Two divergent copies of this produce a constant dB offset
/// that nobody can find, so there is exactly one.
struct PsdScaling {
    /// PSD[k] = scale(fs, sum(w^2)) * |X[k]|^2, before the end-bin factor.
    [[nodiscard]] static constexpr double scale(double sampleRate,
                                                double windowSumSquares) noexcept {
        return 2.0 / (sampleRate * windowSumSquares);
    }
    /// DC and Nyquist have no mirror-image partner to fold in, so they do not
    /// get the factor of two.
    [[nodiscard]] static constexpr double binFactor(std::size_t bin,
                                                    std::size_t numBins) noexcept {
        return (bin == 0 || bin + 1 == numBins) ? 0.5 : 1.0;
    }
};
}
```

```cpp
// DualFftEngine.h
namespace rta::dsp {

/// How frame PAIRS are combined. Deliberately NOT `Averaging`: that enum's
/// `Linear` means "every frame since reset, unbounded", which a live transfer
/// function must not do -- it would take minutes to forget a moved microphone.
enum class TransferAveraging {
    Fifo,         ///< running sum over the last `fifoDepth` frames, exact drop-off
    Exponential,  ///< one-pole, the coefficient SpectrumEngine already uses
};

class DualFftEngine {
public:
    struct Config {
        std::size_t fftSize = 4096;          ///< power of two, >= 4
        std::size_t hopSize = 2048;          ///< 1..fftSize
        double      sampleRate = 48000.0;
        WindowType  window = WindowType::Hann;
        TransferAveraging averaging = TransferAveraging::Fifo;
        std::size_t fifoDepth = 16;          ///< Fifo only, >= 1
        double timeConstantSeconds = 0.5;    ///< Exponential only, > 0

        /// Positive = the measurement lags the reference by this many samples.
        /// Applied BEFORE the transform, as an integer-sample stream offset --
        /// never afterwards to the phase. A delay comparable to the frame
        /// length decorrelates the two channels and kills coherence at HIGH
        /// frequency first, because HF has the shortest period; compensate
        /// after the FFT and the coherence on screen measures your own
        /// misalignment instead of the system. See the decision record §4.
        int referenceDelaySamples = 0;

        /// Coherence is withheld below this many EFFECTIVE averages (task 1).
        /// For a single frame |X*Y|^2 == |X|^2|Y|^2 identically, so coherence
        /// is exactly 1.0 at every frequency and a broken engine looks perfect.
        double minimumEffectiveAverages = 8.0;
    };

    explicit DualFftEngine(const Config& config);

    [[nodiscard]] const Config& config() const noexcept;
    [[nodiscard]] std::size_t numBins() const noexcept;
    [[nodiscard]] std::size_t frameCount() const noexcept;
    /// Never a raw frame count -- see AverageCount.h.
    [[nodiscard]] double effectiveAverages() const noexcept;
    [[nodiscard]] double binWidthHz() const noexcept;
    [[nodiscard]] double binFrequency(std::size_t bin) const noexcept;

    /// Both spans must be the same length; they are the SAME time instant on
    /// two channels. Throws std::invalid_argument otherwise -- silently
    /// analysing two channels that are not the same instant is the one error
    /// this class must never make quietly.
    void process(std::span<const float> reference, std::span<const float> measurement);

    void reset() noexcept;

    /// Averaged one-sided PSDs and cross-PSD. Doubles: the estimators divide
    /// them, and a float cross-spectrum loses phase precision where |Sxx| is
    /// small, which is exactly where the operator is looking.
    [[nodiscard]] std::span<const double> referencePsd() const noexcept;    ///< Sxx
    [[nodiscard]] std::span<const double> measurementPsd() const noexcept;  ///< Syy
    [[nodiscard]] std::span<const std::complex<double>> crossPsd() const noexcept;  ///< Sxy
};

}  // namespace rta::dsp
```

**Implementation notes the builder must follow.**

1. **Delay compensation is a one-time stream skip, not per-frame arithmetic.**
   If `referenceDelaySamples == D > 0` the measurement lags, so `meas[m]`
   belongs with `ref[m-D]`; discarding the first `D` samples of the
   **measurement** stream makes the two rings index-aligned for ever after.
   `D < 0` discards `|D|` from the **reference**. Hold two `std::size_t`
   skip counters, decrement them as samples arrive, and after that the pairing
   is "both rings have a frame → take one from each, discard `hop` from each".
   This is why it cannot be got wrong per frame: there is no per-frame index
   arithmetic to get wrong.
2. **The delay is construction-only.** Changing it invalidates every average
   already accumulated, because they were computed at a different alignment.
   `reset()` clears the averages *and* re-arms the skip counters.
3. **Accumulators are means, not sums**, so they are directly comparable with
   `SpectrumEngine::density()`. FIFO keeps `fifoDepth` past frames in a flat
   ring (`depth * numBins` each) and maintains a running sum, subtracting the
   frame that falls out; divide by `min(frameCount, fifoDepth)`.
4. Exponential seeds from the first frame, exactly as `SpectrumEngine` does,
   with `alpha = 1 - exp(-hopSeconds / timeConstant)` and
   `hopSeconds = hop / fs`.
5. Apply `PsdScaling::scale` **and** `binFactor` to `Sxx`, `Syy` and `Sxy`
   alike. The cross term takes the same factor: it is a one-sided density too.
6. `reset()` is `noexcept` and allocates nothing. `process()` may not allocate
   after construction — this class runs on the analysis thread.

- [ ] **Step 1: Write the failing tests** — `core/tests/test_dualfft.cpp`

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/DualFftEngine.h"
#include "rta/dsp/SpectrumEngine.h"
#include "rta/gen/Synthetic.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace rta::dsp;

namespace {
constexpr std::size_t kN = 1024;

/// One non-repeating record of pink noise, `total` samples long.
std::vector<float> pink(std::size_t total, std::uint32_t seed) {
    rta::gen::SyntheticPink source(total, -12.0, seed);
    std::vector<float> out(total);
    source.render(out);
    return out;
}
}  // namespace

TEST_CASE("the engine rejects configurations it cannot honour", "[dualfft]") {
    DualFftEngine::Config c;
    c.fftSize = kN;
    c.hopSize = kN / 2;  // the DEFAULT hop is 2048, which would itself be
                         // invalid against a 1024 fftSize -- the valid
                         // construction at the end of this case needs a hop
                         // the case's own REQUIRE_THROWS lines call legal.
    REQUIRE_THROWS_AS(([&]{ auto d = c; d.hopSize = 0;        return DualFftEngine(d); }()),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(([&]{ auto d = c; d.hopSize = kN + 1;   return DualFftEngine(d); }()),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(([&]{ auto d = c; d.sampleRate = 0.0;   return DualFftEngine(d); }()),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(([&]{ auto d = c; d.fifoDepth = 0;      return DualFftEngine(d); }()),
                      std::invalid_argument);

    DualFftEngine engine(c);
    const std::vector<float> a(64, 0.0f), b(65, 0.0f);
    // Two channels of different length are not the same instant in time.
    REQUIRE_THROWS_AS(engine.process(a, b), std::invalid_argument);
}

TEST_CASE("a channel measured against itself is its own spectrum", "[dualfft]") {
    const auto x = pink(1 << 15, 3);
    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN / 2; c.window = WindowType::Hann;
    c.fifoDepth = 4096;  // larger than the frame count: a plain mean
    DualFftEngine engine(c);
    engine.process(x, x);

    REQUIRE(engine.frameCount() == (x.size() - kN) / (kN / 2) + 1);

    for (std::size_t k = 0; k < engine.numBins(); ++k) {
        // Sxy = conj(X)*Y with Y == X is |X|^2: real, non-negative, == Sxx.
        REQUIRE(engine.crossPsd()[k].imag() == Catch::Approx(0.0).margin(1e-18));
        REQUIRE(engine.crossPsd()[k].real()
                == Catch::Approx(engine.referencePsd()[k]).epsilon(1e-12));
        REQUIRE(engine.measurementPsd()[k]
                == Catch::Approx(engine.referencePsd()[k]).epsilon(1e-12));
    }
}

TEST_CASE("Sxx is the same density SpectrumEngine reports", "[dualfft][scaling]") {
    // Two divergent PSD scalings in one codebase surface as a constant dB
    // offset nobody can find. This is the test that stops that happening.
    const auto x = pink(1 << 15, 5);

    SpectrumEngine::Config sc;
    sc.fftSize = kN; sc.hopSize = kN / 2; sc.averaging = Averaging::Linear;
    SpectrumEngine reference(sc);
    reference.process(x);

    DualFftEngine::Config dc;
    dc.fftSize = kN; dc.hopSize = kN / 2; dc.fifoDepth = 4096;
    DualFftEngine engine(dc);
    engine.process(x, x);

    for (std::size_t k = 0; k < engine.numBins(); ++k) {
        REQUIRE(engine.referencePsd()[k]
                == Catch::Approx(static_cast<double>(reference.density()[k])).epsilon(1e-5));
    }
}

TEST_CASE("delay compensation happens before the transform", "[dualfft][delay]") {
    // y[n] = x[n-D]. Compensated, the two frames are the SAME samples, so the
    // cross-spectrum must come back real and equal to Sxx -- bit for bit, not
    // approximately. If the compensation were applied to the phase afterwards
    // this would still be complex.
    constexpr int kD = 137;
    const auto x = pink(1 << 15, 7);
    std::vector<float> y(x.size(), 0.0f);
    for (std::size_t n = static_cast<std::size_t>(kD); n < x.size(); ++n) {
        y[n] = x[n - static_cast<std::size_t>(kD)];
    }

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN / 2; c.fifoDepth = 4096;
    c.referenceDelaySamples = kD;
    DualFftEngine engine(c);
    engine.process(x, y);

    for (std::size_t k = 0; k < engine.numBins(); ++k) {
        REQUIRE(engine.crossPsd()[k].imag() == Catch::Approx(0.0).margin(1e-15));
        REQUIRE(engine.crossPsd()[k].real()
                == Catch::Approx(engine.referencePsd()[k]).epsilon(1e-9));
    }

    // The same data with NO compensation must NOT come back real -- otherwise
    // the test above would pass for an engine that ignores the field entirely.
    auto uncompensated = c;
    uncompensated.referenceDelaySamples = 0;
    DualFftEngine naive(uncompensated);
    naive.process(x, y);
    double worst = 0.0;
    for (std::size_t k = 1; k + 1 < naive.numBins(); ++k) {
        worst = std::max(worst, std::abs(naive.crossPsd()[k].imag()) / naive.referencePsd()[k]);
    }
    REQUIRE(worst > 0.1);
}

TEST_CASE("a negative delay compensates the other channel", "[dualfft][delay]") {
    // The reference cable is the long one: x[n] = y[n-D], so D is negative.
    constexpr int kD = 91;
    const auto y = pink(1 << 15, 11);
    std::vector<float> x(y.size(), 0.0f);
    for (std::size_t n = static_cast<std::size_t>(kD); n < y.size(); ++n) {
        x[n] = y[n - static_cast<std::size_t>(kD)];
    }

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN / 2; c.fifoDepth = 4096;
    c.referenceDelaySamples = -kD;
    DualFftEngine engine(c);
    engine.process(x, y);
    for (std::size_t k = 0; k < engine.numBins(); ++k) {
        REQUIRE(engine.crossPsd()[k].imag() == Catch::Approx(0.0).margin(1e-15));
    }
}

TEST_CASE("the cross-spectrum carries the delay's SIGN, not just its size",
          "[dualfft][sign]") {
    // Both delay tests above end with the two compensated frames bit-identical,
    // X == Y. And conj(X)*Y == conj(X*conj(Y)), which for X == Y is real either
    // way -- so an engine that conjugates the WRONG channel passes every
    // `imag() == 0` assertion in this file, and the `abs(imag)` check in the
    // uncompensated half erases the sign too. The convention is load-bearing:
    // findDelayPhat's output is fed straight into referenceDelaySamples, and a
    // flipped sign there compensates in the wrong direction.
    //
    // So: leave the delay UNCOMPENSATED and assert the closed form with its
    // sign, Sxy[k] = Sxx[k] * exp(-2*pi*i*k*D/N). That holds exactly for a
    // kN-periodic signal under a rectangular window -- see the plan's note on
    // windows. y is built as a CIRCULAR rotation so there is no start
    // transient to contaminate the first frame.
    constexpr int kD = 5;
    rta::gen::SyntheticPink source(kN, -12.0, 83);   // block period == kN
    std::vector<float> x(kN * 32);
    source.render(x);
    std::vector<float> y(x.size(), 0.0f);
    for (std::size_t n = 0; n < x.size(); ++n) {
        y[n] = x[(n + x.size() - static_cast<std::size_t>(kD)) % x.size()];
    }

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN; c.window = WindowType::Rectangular;
    c.fifoDepth = 4096;
    c.referenceDelaySamples = 0;   // deliberately not compensated
    DualFftEngine engine(c);
    engine.process(x, y);

    // The tolerance needs BOTH terms, and the reason is structural rather than
    // fussy. `RealFft` transforms in std::complex<float>, so the transform's
    // ABSOLUTE rounding error is set by the largest bins -- which for pink
    // noise sit near DC. By the top of the band a bin's own magnitude has
    // decayed by ~500x while that error floor has not moved, so a pure
    // parts-per-million-of-this-bin margin asks for more precision than float32
    // can deliver and fails on a CORRECT engine two bins below Nyquist.
    // (Measured: k=510, sxx=1.9e-07, residual 3.9e-13 against a 1.9e-13
    // margin.) The engine is not the limit here -- it promotes to double at the
    // earliest possible point; the ceiling is one layer down in RealFft.
    const double peak = *std::max_element(engine.referencePsd().begin(),
                                          engine.referencePsd().end());
    for (std::size_t k = 1; k + 1 < engine.numBins(); ++k) {
        const double theta = -2.0 * std::numbers::pi * static_cast<double>(k)
                           * static_cast<double>(kD) / static_cast<double>(kN);
        const double sxx = engine.referencePsd()[k];
        const double tolerance = sxx * 1e-6 + peak * 1e-7;
        REQUIRE(engine.crossPsd()[k].real()
                == Catch::Approx(sxx * std::cos(theta)).margin(tolerance));
        REQUIRE(engine.crossPsd()[k].imag()
                == Catch::Approx(sxx * std::sin(theta)).margin(tolerance));
    }
}

TEST_CASE("reset re-arms the alignment even when the skip is only half spent",
          "[dualfft][reset]") {
    // The reset test below feeds far more samples than the delay, so both skip
    // counters are back to zero before reset() runs -- at which point failing
    // to re-arm them is indistinguishable from re-arming them. Reset MID-SKIP.
    constexpr int kD = 512;
    const auto x = pink(1 << 15, 89);
    std::vector<float> y(x.size(), 0.0f);
    for (std::size_t n = static_cast<std::size_t>(kD); n < x.size(); ++n) {
        y[n] = x[n - static_cast<std::size_t>(kD)];
    }

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN / 2; c.fifoDepth = 4096;
    c.referenceDelaySamples = kD;
    DualFftEngine engine(c);

    // Half the skip consumed, no frame produced yet.
    engine.process(std::span<const float>(x.data(), kD / 2),
                   std::span<const float>(y.data(), kD / 2));
    REQUIRE(engine.frameCount() == 0);
    engine.reset();

    // A fresh, correctly aligned stream from the top. If reset() left the skip
    // counter half spent -- or left the 256 already-buffered reference samples
    // in the ring -- the two channels are 256 samples out and Sxy stops being
    // real. This pins BOTH halves of reset(): the counters and the buffers.
    engine.process(x, y);
    REQUIRE(engine.frameCount() > 0);
    for (std::size_t k = 0; k < engine.numBins(); ++k) {
        REQUIRE(engine.crossPsd()[k].imag() == Catch::Approx(0.0).margin(1e-15));
    }
}

TEST_CASE("the FIFO forgets exactly at its depth", "[dualfft][fifo]") {
    // Feed loud frames, then quiet ones. Once `depth` quiet frames have gone
    // through, nothing of the loud ones may remain -- that is what "exact
    // drop-off" means, and an exponential average would still show them.
    const auto loud = pink(kN * 8, 13);
    std::vector<float> quiet(kN * 8, 0.0f);
    for (std::size_t n = 0; n < quiet.size(); ++n) quiet[n] = loud[n] * 0.001f;

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN; c.fifoDepth = 4;
    DualFftEngine engine(c);
    engine.process(loud, loud);
    const double afterLoud = engine.referencePsd()[64];
    engine.process(quiet, quiet);
    const double afterQuiet = engine.referencePsd()[64];
    REQUIRE(afterQuiet < afterLoud * 1e-5);
}

TEST_CASE("reset clears the average and re-arms the alignment", "[dualfft]") {
    const auto x = pink(kN * 4, 17);
    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN; c.referenceDelaySamples = 40;
    DualFftEngine engine(c);
    engine.process(x, x);
    REQUIRE(engine.frameCount() > 0);
    engine.reset();
    REQUIRE(engine.frameCount() == 0);
    REQUIRE(engine.effectiveAverages() == Catch::Approx(0.0));
    for (std::size_t k = 0; k < engine.numBins(); ++k) {
        REQUIRE(engine.referencePsd()[k] == 0.0);
    }
}

TEST_CASE("effective averages never exceed the frame count", "[dualfft][average]") {
    const auto x = pink(1 << 14, 19);
    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN / 4; c.fifoDepth = 4096;
    DualFftEngine engine(c);
    engine.process(x, x);
    REQUIRE(engine.effectiveAverages() > 1.0);
    REQUIRE(engine.effectiveAverages() < static_cast<double>(engine.frameCount()));
}
```

- [ ] **Step 2: Run and watch it fail** — header not found.
- [ ] **Step 3: Implement** `PsdScaling.h`, then rewire `SpectrumEngine.cpp` to
      use it (one commit, behaviour unchanged), then `DualFftEngine`.
- [ ] **Step 4: Run the tests.** Expected: all pass; roughly 221 total. The 208
      pre-existing tests must still pass — that is the proof the
      `SpectrumEngine` edit changed nothing.
- [ ] **Step 5: Prove the tests can fail.**

| Mutation | Must turn red |
|---|---|
| apply the delay as a phase twist after the FFT | `delay compensation happens before the transform` |
| drop `binFactor` (no DC/Nyquist halving) | `Sxx is the same density SpectrumEngine reports` |
| use `X * conj(Y)` instead of `conj(X) * Y` | `the cross-spectrum carries the delay's SIGN` |
| FIFO that never subtracts the outgoing frame | `the FIFO forgets exactly at its depth` |
| `reset()` that forgets to re-arm the skip counters | `reset re-arms the alignment even when the skip is only half spent` |
| accumulate only the FIFO branch (delete the exponential one) | `the exponential mode smooths towards each frame` |
| report the raw frame count from `effectiveAverages()` | `a saturated FIFO reports its depth, not its frame count` |
| swap the returns of `referencePsd()` and `measurementPsd()` | `the two channels are not interchangeable` |

**Two rows here were wrong in the first draft of this plan, and the tests' own
comments said so before a verifier did.** `X * conj(Y)` cannot turn
`a negative delay compensates the other channel` red: after compensation the two
frames are bit-identical, `X == Y`, and `conj(X)*Y == conj(X*conj(Y))` is real
under either convention. Likewise the original `reset` test never processed
anything after `reset()`, so it could not tell re-arming from not re-arming.
Both now name the test that actually catches them. A mutation table is
guidance a later verifier trusts; a stale row in it is worse than an absent one.

- [ ] **Step 6: Commit** — `feat(core): the dual-FFT accumulators, aligned before the transform`

---

## Task 3 — Estimators, coherence, and the gate that makes coherence mean something

**Files:**
- Create: `core/include/rta/dsp/TransferEstimator.h`
- Create: `core/src/dsp/TransferEstimator.cpp`
- Create: `core/tests/test_transfer_estimator.cpp`
- Modify: `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`

**Interfaces consumed:** `DualFftEngine` accessors (task 2).

**Interfaces produced:**

```cpp
namespace rta::dsp {

enum class Estimator {
    H1,  ///< Sxy/Sxx. Unbiased when the noise is on the MEASUREMENT channel --
         ///< the live-sound case: an electrical reference tap, and a mic in a
         ///< room full of uncorrelated energy.
    H2,  ///< Syy/conj(Sxy). Unbiased when the noise is on the REFERENCE.
    Hv,  ///< Total least squares; splits the difference between the two.
};

[[nodiscard]] std::complex<double> estimateH1(std::complex<double> sxy, double sxx) noexcept;
[[nodiscard]] std::complex<double> estimateH2(std::complex<double> sxy, double syy) noexcept;
[[nodiscard]] std::complex<double> estimateHv(std::complex<double> sxy, double sxx, double syy) noexcept;

/// gamma^2 = |Sxy|^2 / (Sxx * Syy). MAGNITUDE-SQUARED, as Bendat & Piersol,
/// scipy and MATLAB define it.
///
/// Open Sound Meter displays the UN-squared |Grm|/sqrt(Grr*Gmm), so our numbers
/// read lower than theirs on identical data. That is a different quantity, not
/// a bug, and must not be "corrected". The squared form is also the only one
/// for which H1/H2 == gamma^2 holds, which is the free self-test below.
[[nodiscard]] double magnitudeSquaredCoherence(std::complex<double> sxy,
                                               double sxx, double syy) noexcept;

/// An immutable result. Absence of coherence is absence of a value -- not 0.0,
/// not -1. A sentinel would be plotted.
struct TransferSnapshot {
    Estimator estimator = Estimator::H1;
    double sampleRate = 0.0;
    double binWidthHz = 0.0;
    double effectiveAverages = 0.0;
    std::vector<std::complex<double>> h;
    std::vector<float> magnitudeDb;    ///< 20*log10|H|, floored at kMagnitudeFloorDb
    std::vector<float> phaseRadians;   ///< wrapped to (-pi, pi]; unwrapping is a VIEW job
    std::optional<std::vector<float>> coherence;  ///< empty below the gate
    static constexpr float kMagnitudeFloorDb = -120.0f;
};

/// The ONE place a TransferSnapshot is built, and therefore the one place the
/// coherence gate can be applied. Guarded by check_coherence_gate.cmake.
[[nodiscard]] TransferSnapshot makeSnapshot(const DualFftEngine& engine, Estimator estimator);

}  // namespace rta::dsp
```

Formulas, spelled out so the builder does not have to choose:

- `estimateH1` = `sxy / sxx`, returning `{0,0}` when `sxx <= 0`. A reference bin
  with no energy carries no information about the system; a zero there reads as
  the magnitude floor, which is honest.
- `estimateH2` = `syy / conj(sxy)`, returning `{0,0}` when `|sxy| == 0`.
- `estimateHv` = total least squares:
  `((syy - sxx) + sqrt((syy - sxx)^2 + 4|sxy|^2)) / (2 * conj(sxy))`.
  Real arithmetic under the root; it is non-negative by construction.
- `magnitudeSquaredCoherence` = `norm(sxy) / (sxx * syy)`, clamped to `[0, 1]`
  (rounding can produce 1 + 1e-16, and a coherence above one is a nonsense the
  view would happily draw), and 0.0 when either denominator term is `<= 0`.

- [ ] **Step 1: Write the failing tests** — `core/tests/test_transfer_estimator.cpp`

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/DualFftEngine.h"
#include "rta/dsp/TransferEstimator.h"
#include "rta/gen/Synthetic.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace rta::dsp;

namespace {
constexpr std::size_t kN = 1024;

/// A record that is EXACTLY periodic with period kN, so a delayed copy is the
/// circular rotation of the original. With WindowType::Rectangular that makes
/// H exact rather than approximate -- see the plan's note on windows.
std::vector<float> periodicPink(std::size_t total, std::uint32_t seed) {
    rta::gen::SyntheticPink source(kN, -12.0, seed);
    std::vector<float> out(total);
    source.render(out);
    return out;
}
}  // namespace

TEST_CASE("a pure gain and delay is recovered exactly", "[transfer][identity]") {
    // y[n] = g * x[n-D]. Then H[k] must be g * exp(-2*pi*i*k*D/N) at EVERY bin.
    // One identity pins magnitude, phase, and the sign convention at once.
    constexpr double kG = 0.5;
    constexpr int kD = 13;
    const auto x = periodicPink(kN * 32, 23);
    std::vector<float> y(x.size(), 0.0f);
    // Wrap, do not zero-pad. Zero-padding the first D samples breaks exact
    // periodicity in frame 0 of 32, and the identity this test exists to assert
    // is exact only for a genuine circular rotation.
    for (std::size_t n = 0; n < x.size(); ++n) {
        const std::size_t src = n >= static_cast<std::size_t>(kD)
                              ? n - static_cast<std::size_t>(kD)
                              : n + kN - static_cast<std::size_t>(kD);
        y[n] = static_cast<float>(kG) * x[src];
    }

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN; c.window = WindowType::Rectangular;
    c.fifoDepth = 4096;
    DualFftEngine engine(c);
    engine.process(x, y);

    const auto snap = makeSnapshot(engine, Estimator::H1);
    REQUIRE(snap.coherence.has_value());

    // The tolerances are set by `RealFft`, which transforms in
    // std::complex<FLOAT>: a 1024-bin transform carries ~1.3e-6 relative
    // rounding error even on a mathematically exact identity. An earlier draft
    // asked for 1e-9 here, which assumed double-precision bins and is simply
    // unattainable. This is the same ceiling the sign test in test_dualfft.cpp
    // ran into from the other direction -- see memory/float32-fft-precision.md.
    for (std::size_t k = 1; k + 1 < snap.h.size(); ++k) {
        const double theta = -2.0 * std::numbers::pi * static_cast<double>(k)
                           * static_cast<double>(kD) / static_cast<double>(kN);
        REQUIRE(std::abs(snap.h[k]) == Catch::Approx(kG).epsilon(1e-5));
        REQUIRE(snap.h[k].real() == Catch::Approx(kG * std::cos(theta)).margin(5e-6));
        REQUIRE(snap.h[k].imag() == Catch::Approx(kG * std::sin(theta)).margin(5e-6));
        REQUIRE((*snap.coherence)[k] == Catch::Approx(1.0f).margin(1e-6));
    }
    REQUIRE(snap.magnitudeDb[100] == Catch::Approx(20.0 * std::log10(kG)).margin(1e-5));
}

TEST_CASE("coherence follows theory when noise is added", "[transfer][coherence]") {
    // y = x + n with independent pink n at the SAME spectral shape gives a
    // frequency-independent SNR, so gamma^2 = SNR/(1+SNR) at every bin.
    // THIS is the test that catches an engine returning an identical 1.0 --
    // the identity test above cannot, because there gamma^2 really is 1.
    const std::size_t total = 1 << 16;
    rta::gen::SyntheticPink signal(total, -12.0, 29);
    rta::gen::SyntheticPink noise(total, -12.0, 31);  // same level => SNR = 1
    std::vector<float> x(total), n(total), y(total);
    signal.render(x);
    noise.render(n);
    for (std::size_t i = 0; i < total; ++i) y[i] = x[i] + n[i];

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN / 2; c.window = WindowType::Hann; c.fifoDepth = 4096;
    DualFftEngine engine(c);
    engine.process(x, y);

    const auto snap = makeSnapshot(engine, Estimator::H1);
    REQUIRE(snap.coherence.has_value());

    double mean = 0.0;
    std::size_t counted = 0;
    for (std::size_t k = 8; k + 8 < snap.coherence->size(); ++k) {
        mean += (*snap.coherence)[k];
        ++counted;
    }
    mean /= static_cast<double>(counted);

    // SNR = 1 => 0.5. Tolerance is the estimator bias 0.5*sqrt(pi/N) at the
    // effective average count -- a stated bound from the decision record, not
    // a number tuned until the test went green.
    const double bound = 0.5 * std::sqrt(std::numbers::pi / snap.effectiveAverages);
    REQUIRE(mean == Catch::Approx(0.5).margin(bound));
    REQUIRE(mean < 0.9);  // an engine that returns 1.0 everywhere fails here
}

TEST_CASE("H1 over H2 is exactly the coherence", "[transfer][bracket]") {
    // Bendat & Piersol: H1/H2 == gamma^2. It costs nothing and validates all
    // three estimators against each other.
    const std::size_t total = 1 << 15;
    rta::gen::SyntheticPink signal(total, -12.0, 37);
    rta::gen::SyntheticPink noise(total, -20.0, 41);
    std::vector<float> x(total), n(total), y(total);
    signal.render(x);
    noise.render(n);
    for (std::size_t i = 0; i < total; ++i) y[i] = x[i] + n[i];

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN / 2; c.fifoDepth = 4096;
    DualFftEngine engine(c);
    engine.process(x, y);

    for (std::size_t k = 1; k + 1 < engine.numBins(); ++k) {
        const auto sxy = engine.crossPsd()[k];
        const double sxx = engine.referencePsd()[k];
        const double syy = engine.measurementPsd()[k];
        const auto ratio = estimateH1(sxy, sxx) / estimateH2(sxy, syy);
        const double gamma2 = magnitudeSquaredCoherence(sxy, sxx, syy);
        REQUIRE(ratio.real() == Catch::Approx(gamma2).epsilon(1e-9));
        REQUIRE(ratio.imag() == Catch::Approx(0.0).margin(1e-12));
        // Hv sits between them.
        const double hv = std::abs(estimateHv(sxy, sxx, syy));
        REQUIRE(hv >= std::min(std::abs(estimateH1(sxy, sxx)), std::abs(estimateH2(sxy, syy))) - 1e-9);
        REQUIRE(hv <= std::max(std::abs(estimateH1(sxy, sxx)), std::abs(estimateH2(sxy, syy))) + 1e-9);
    }
}

TEST_CASE("a known biquad is recovered from its own coefficients", "[transfer][biquad]") {
    // The analytic response of the filter the data actually went through:
    //   H(e^jw) = (b0 + b1 z + b2 z^2) / (1 + a1 z + a2 z^2), z = e^-jw.
    const auto x = periodicPink(kN * 64, 43);
    const Biquad::Coeffs coeffs{0.3, -0.2, 0.1, -0.5, 0.2};
    Biquad::State state{};
    std::vector<float> y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        y[i] = static_cast<float>(Biquad::processSample(coeffs, state, x[i]));
    }
    // Discard the transient: the identity holds in steady state, where y is
    // also kN-periodic. 8 frames is far past this filter's decay.
    const std::size_t skip = kN * 8;
    std::span<const float> xs(x.data() + skip, x.size() - skip);
    std::span<const float> ys(y.data() + skip, y.size() - skip);

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN; c.window = WindowType::Rectangular; c.fifoDepth = 4096;
    DualFftEngine engine(c);
    engine.process(xs, ys);
    const auto snap = makeSnapshot(engine, Estimator::H1);

    for (std::size_t k = 1; k + 1 < snap.h.size(); ++k) {
        const double w = 2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(kN);
        const std::complex<double> z(std::cos(-w), std::sin(-w));
        const auto expected = (coeffs.b0 + coeffs.b1 * z + coeffs.b2 * z * z)
                            / (1.0 + coeffs.a1 * z + coeffs.a2 * z * z);
        REQUIRE(snap.h[k].real() == Catch::Approx(expected.real()).margin(1e-6));
        REQUIRE(snap.h[k].imag() == Catch::Approx(expected.imag()).margin(1e-6));
    }
}

TEST_CASE("coherence is withheld until the averages justify it", "[transfer][gate]") {
    // For ONE frame, |X*Y|^2 == |X|^2 |Y|^2 identically, so coherence is
    // exactly 1.0 at every bin and a completely broken engine looks flawless.
    // The gate exists for that; absence is a missing optional, not a 0.0.
    const auto x = periodicPink(kN * 64, 47);

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN; c.minimumEffectiveAverages = 8.0;
    DualFftEngine engine(c);

    for (std::size_t frame = 1; frame <= 12; ++frame) {
        engine.process(std::span<const float>(x.data() + (frame - 1) * kN, kN),
                       std::span<const float>(x.data() + (frame - 1) * kN, kN));
        const auto snap = makeSnapshot(engine, Estimator::H1);
        // hop == fftSize, so effective averages == frame count exactly.
        if (frame < 8) {
            REQUIRE_FALSE(snap.coherence.has_value());
        } else {
            REQUIRE(snap.coherence.has_value());
        }
        // H is always available: it is not the quantity that lies at N = 1.
        REQUIRE(snap.h.size() == engine.numBins());
    }

    engine.reset();
    const auto afterReset = makeSnapshot(engine, Estimator::H1);
    REQUIRE_FALSE(afterReset.coherence.has_value());
}

TEST_CASE("the estimator selector actually selects", "[transfer][selector]") {
    // Every other test in this file calls makeSnapshot with H1. Swap the H2 and
    // Hv cases in the dispatch -- or return estimateH1 from all three -- and the
    // whole suite stays green. The enum's reason for existing is unproven
    // without this, and H2 vs H1 is the diagnostic that tells an operator
    // whether the noise is on the reference or on the microphone.
    const std::size_t total = 1 << 15;
    rta::gen::SyntheticPink signal(total, -12.0, 97);
    rta::gen::SyntheticPink noise(total, -18.0, 101);
    std::vector<float> x(total), n(total), y(total);
    signal.render(x);
    noise.render(n);
    for (std::size_t i = 0; i < total; ++i) y[i] = x[i] + n[i];

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN / 2; c.fifoDepth = 4096;
    DualFftEngine engine(c);
    engine.process(x, y);

    const auto s1 = makeSnapshot(engine, Estimator::H1);
    const auto s2 = makeSnapshot(engine, Estimator::H2);
    const auto sv = makeSnapshot(engine, Estimator::Hv);

    REQUIRE(s1.estimator == Estimator::H1);
    REQUIRE(s2.estimator == Estimator::H2);
    REQUIRE(sv.estimator == Estimator::Hv);
    REQUIRE(s1.sampleRate == Catch::Approx(c.sampleRate));
    REQUIRE(s1.binWidthHz == Catch::Approx(engine.binWidthHz()));

    for (std::size_t k = 1; k + 1 < s1.h.size(); ++k) {
        const auto sxy = engine.crossPsd()[k];
        const double sxx = engine.referencePsd()[k];
        const double syy = engine.measurementPsd()[k];
        REQUIRE(s1.h[k] == estimateH1(sxy, sxx));
        REQUIRE(s2.h[k] == estimateH2(sxy, syy));
        REQUIRE(sv.h[k] == estimateHv(sxy, sxx, syy));

        // And they are genuinely different objects here. With the noise on the
        // MEASUREMENT channel, H1/H2 = gamma^2 < 1, so |H2| reads high -- which
        // is exactly the bias H1 was chosen to avoid. A dispatch that returned
        // H1 three times would pass the equality checks above only if
        // estimateH2 were also broken; this makes the difference explicit.
        REQUIRE(std::abs(s2.h[k]) > std::abs(s1.h[k]));
    }
}

TEST_CASE("the estimators guard their own denominators", "[transfer][edge]") {
    // The engine can never hand these pairs over: Sxy = conj(X)*Y, so a silent
    // reference forces Sxx == 0 AND Sxy == 0 together, never one without the
    // other. That makes the sxx <= 0 guard unreachable THROUGH the engine, and
    // an implementation that clamped to 1e-300 instead of returning zero would
    // pass every engine-level test in this file. These are free functions in a
    // public header; a later caller can pass anything. Test them directly.
    REQUIRE(estimateH1({1.0, 2.0}, 0.0) == std::complex<double>{0.0, 0.0});
    REQUIRE(estimateH1({1.0, 2.0}, -1e-30) == std::complex<double>{0.0, 0.0});
    REQUIRE(estimateH2({0.0, 0.0}, 4.0) == std::complex<double>{0.0, 0.0});
    REQUIRE(estimateHv({0.0, 0.0}, 1.0, 4.0) == std::complex<double>{0.0, 0.0});
    REQUIRE(magnitudeSquaredCoherence({1.0, 0.0}, 0.0, 1.0) == 0.0);
    REQUIRE(magnitudeSquaredCoherence({1.0, 0.0}, 1.0, 0.0) == 0.0);

    // And the clamp: rounding can put |Sxy|^2 a hair above Sxx*Syy, and a
    // coherence above one is a nonsense the view would draw quite happily.
    REQUIRE(magnitudeSquaredCoherence({1.0, 0.0}, 1.0, 0.999999999) == 1.0);

    // A healthy pair still divides normally -- otherwise the guards above
    // could be satisfied by a function that always returns zero.
    REQUIRE(estimateH1({2.0, 0.0}, 4.0) == std::complex<double>{0.5, 0.0});
}

TEST_CASE("a silent reference produces no transfer function", "[transfer][edge]") {
    const std::vector<float> silence(kN * 16, 0.0f);
    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN;
    DualFftEngine engine(c);
    engine.process(silence, silence);
    const auto snap = makeSnapshot(engine, Estimator::H1);
    for (std::size_t k = 0; k < snap.h.size(); ++k) {
        REQUIRE(std::abs(snap.h[k]) == 0.0);
        REQUIRE(snap.magnitudeDb[k] == TransferSnapshot::kMagnitudeFloorDb);
        REQUIRE(std::isfinite(snap.phaseRadians[k]));
    }
}
```

- [ ] **Step 2: Run and watch it fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Expected: all pass; roughly 227 total.
- [ ] **Step 5: Prove the tests can fail.**

| Mutation | Must turn red |
|---|---|
| return the un-squared coherence | `H1 over H2 is exactly the coherence` |
| emit coherence at any average count | `coherence is withheld until the averages justify it` |
| gate on the raw frame count instead of the effective one | that same test, with `hopSize = kN/4` — **add that variant if it does not** |
| clamp `sxx <= 0` to a tiny epsilon instead of returning zero | `a silent reference produces no transfer function` |

- [ ] **Step 6: Commit** — `feat(core): H1, H2, Hv, and a coherence that refuses to lie`

---

## Task 4 — Group delay, in core, from the closed form

**Files:** create `core/include/rta/dsp/GroupDelay.h`, `core/src/dsp/GroupDelay.cpp`,
`core/tests/test_group_delay.cpp`; modify both CMake files.

**Interface produced:**

```cpp
namespace rta::dsp {
/// tau_g = -Im( dH/dw * conj(H) ) / |H|^2, in seconds.
///
/// This lives in core, not in the view, because it is a DERIVATIVE: it
/// amplifies bin-to-bin noise, so it needs the complex H and a stated
/// smoothing width, not a difference of whatever the screen is showing.
///
/// @param h              numBins values, DC to Nyquist
/// @param binWidthHz     sampleRate / fftSize
/// @param smoothingBins  half-width of the central difference, >= 1. The
///                       result is a delay averaged over +/- this many bins,
///                       and the caller must state it alongside the number.
/// @param out            numBins seconds; edges fall back to a one-sided
///                       difference, which is the honest thing to do rather
///                       than inventing bins beyond Nyquist.
void groupDelaySeconds(std::span<const std::complex<double>> h,
                       double binWidthHz,
                       std::size_t smoothingBins,
                       std::span<double> out);
}
```

- [ ] **Step 1: Write the failing test** — the closed form for a pure delay is
      itself closed-form, so assert THAT, not `D/fs`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/GroupDelay.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace rta::dsp;

TEST_CASE("a pure delay has the discrete group delay its own formula predicts",
          "[groupdelay]") {
    // H[k] = exp(-2*pi*i*k*D/N). Feeding that through the central difference
    // gives, exactly:
    //     tau = N * sin(2*pi*m*D/N) / (2*pi*m*fs)
    // which tends to D/fs as the smoothing width m shrinks. Asserting D/fs
    // directly would be asserting the CONTINUOUS answer against a DISCRETE
    // routine, and would only pass by being loose enough to hide a real error.
    constexpr std::size_t kN = 1024, kBins = kN / 2 + 1;
    constexpr double kFs = 48000.0, kD = 24.0;
    const double binWidth = kFs / static_cast<double>(kN);

    std::vector<std::complex<double>> h(kBins);
    for (std::size_t k = 0; k < kBins; ++k) {
        const double theta = -2.0 * std::numbers::pi * static_cast<double>(k) * kD / kN;
        h[k] = {std::cos(theta), std::sin(theta)};
    }

    for (std::size_t m : {1u, 2u, 4u}) {
        std::vector<double> tau(kBins, 0.0);
        groupDelaySeconds(h, binWidth, m, tau);
        const double md = static_cast<double>(m);
        const double expected = kN * std::sin(2.0 * std::numbers::pi * md * kD / kN)
                              / (2.0 * std::numbers::pi * md * kFs);
        for (std::size_t k = m; k + m < kBins; ++k) {
            REQUIRE(tau[k] == Catch::Approx(expected).margin(1e-12));
        }
        // And it approaches the continuous answer as the smoothing narrows.
        // The gap is the sinc factor: sin(x)/x = 1 - x^2/6 + ..., so the
        // discrete answer sits BELOW the continuous one by about phi^2/6 --
        // 0.36 % at m=1, 1.44 % at m=2, 5.68 % at m=4. An earlier draft of this
        // plan wrote a flat 2 % here, which is simply false at m=4; the bound
        // has to grow with the smoothing width because the quantity does.
        const double phi = 2.0 * std::numbers::pi * md * kD / kN;
        REQUIRE(expected == Catch::Approx(kD / kFs).epsilon(phi * phi / 6.0 * 1.2));
    }
}

TEST_CASE("a constant response has zero group delay", "[groupdelay]") {
    std::vector<std::complex<double>> h(513, {0.7, 0.0});
    std::vector<double> tau(513, 1.0);
    groupDelaySeconds(h, 46.875, 1, tau);
    for (const double t : tau) REQUIRE(t == Catch::Approx(0.0).margin(1e-15));
}

TEST_CASE("group delay refuses impossible arguments", "[groupdelay]") {
    std::vector<std::complex<double>> h(16, {1.0, 0.0});
    std::vector<double> tau(16, 0.0), shortOut(4, 0.0);
    REQUIRE_THROWS_AS(groupDelaySeconds(h, 1.0, 0, tau), std::invalid_argument);
    REQUIRE_THROWS_AS(groupDelaySeconds(h, 1.0, 1, shortOut), std::invalid_argument);
    REQUIRE_THROWS_AS(groupDelaySeconds(h, 0.0, 1, tau), std::invalid_argument);
}

TEST_CASE("a null bin does not poison its neighbours", "[groupdelay]") {
    // |H| = 0 makes the closed form 0/0. It must produce 0, not NaN: one NaN
    // in a trace propagates through every min/max the view computes.
    std::vector<std::complex<double>> h(65, {1.0, 0.0});
    h[32] = {0.0, 0.0};
    std::vector<double> tau(65, 0.0);
    groupDelaySeconds(h, 46.875, 1, tau);
    for (const double t : tau) REQUIRE(std::isfinite(t));
}
```

- [ ] **Step 2: Fail.** — [ ] **Step 3: Implement.** — [ ] **Step 4: Pass** (≈231 total).
- [ ] **Step 5: Mutations** — sign flip on the `-Im(...)` (→ pure-delay test),
      forgetting `/|H|^2` (→ constant-response test with a non-unit gain; add
      that case if the existing one cannot see it), no guard on `|H| == 0`
      (→ null-bin test).
- [ ] **Step 6: Commit** — `feat(core): group delay from the closed form, with its smoothing stated`

---

## Task 5 — GCC-PHAT delay finder

**Files:** create `core/include/rta/dsp/DelayFinder.h`, `core/src/dsp/DelayFinder.cpp`,
`core/tests/test_delay_finder.cpp`; modify both CMake files.

**Interface produced:**

```cpp
namespace rta::dsp {

struct DelayEstimate {
    std::ptrdiff_t delaySamples = 0;  ///< integer peak lag; POSITIVE = the
                                      ///< measurement lags the reference, the
                                      ///< same sign DualFftEngine::Config
                                      ///< ::referenceDelaySamples wants.
    double subSample = 0.0;           ///< parabolic refinement, |.| <= 0.5
    double peak = 0.0;                ///< normalised |correlation| at the peak, 0..1
    bool   inverted = false;          ///< the peak was negative: polarity flip
};

struct PhatOptions {
    double sampleRate = 48000.0;
    /// Floor for the PHAT weighting, RELATIVE to max|G|. An absolute floor --
    /// Open Sound Meter uses -140 dBFS -- couples the estimator to input level,
    /// so the same signal 20 dB quieter behaves differently. Friture's relative
    /// 1e-10 is the right shape.
    double regularisation = 1e-10;
    double minHz = 0.0;   ///< 0 = DC
    double maxHz = 0.0;   ///< 0 = Nyquist. A band limit is PHAT's one mitigation
                          ///< for low-SNR narrowband content, where flattening
                          ///< the weighting amplifies bins carrying nothing.
};

/// Generalised cross-correlation with phase transform: psi = 1/|Gxy|.
///
/// Flat weighting is what makes PHAT robust in reverberation, which is the case
/// this tool exists for. Roth weighting (1/Gxx -- what a plain A/B
/// deconvolution is) biases towards frequencies where the reference happens to
/// be strong; on a clean electrical loopback the two agree, so the choice only
/// shows up in the situation that matters.
[[nodiscard]] DelayEstimate findDelayPhat(std::span<const float> reference,
                                          std::span<const float> measurement,
                                          const PhatOptions& options);
}
```

Implementation: zero-pad both channels to `M = bit_ceil(2 * max(len))` so the
correlation is linear rather than circular; `RealFft` both; form
`G[k] = conj(X[k]) * Y[k]`; zero bins outside `[minHz, maxHz]`; divide each by
`max(|G[k]|, regularisation * maxAbsG)`; inverse transform; scan for the largest
`|r[i]|`; map `i < M/2 → lag = i`, else `lag = i - M`; parabolic vertex on
`r[i-1], r[i], r[i+1]` (on the signed values, negated first when the peak is
negative) for `subSample`; `peak = |r[i]| / sum|r|`-style normalisation is
**not** wanted — use `|r[i]|` divided by the maximum possible, i.e. normalise
`r` by `M` before the scan so a perfect match reads ~1.0. Throw
`std::invalid_argument` on empty spans, mismatched lengths, or `maxHz` below
`minHz`.

- [ ] **Step 1: Write the failing tests.**

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/DelayFinder.h"
#include "rta/gen/Synthetic.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace rta::dsp;

namespace {
std::vector<float> aperiodicPink(std::size_t total, std::uint32_t seed) {
    // blockSize == total, so nothing repeats inside the record. A LOOPED source
    // would put correlation peaks at D plus every multiple of the loop, and the
    // finder would be choosing between them arbitrarily.
    rta::gen::SyntheticPink source(total, -12.0, seed);
    std::vector<float> out(total);
    source.render(out);
    return out;
}
}  // namespace

TEST_CASE("an integer delay is found exactly", "[delay][phat]") {
    constexpr std::size_t kTotal = 1 << 14;
    for (int d : {0, 1, 37, 512, 4095}) {
        const auto x = aperiodicPink(kTotal, 53);
        std::vector<float> y(kTotal, 0.0f);
        for (std::size_t n = static_cast<std::size_t>(d); n < kTotal; ++n) {
            y[n] = x[n - static_cast<std::size_t>(d)];
        }
        const auto est = findDelayPhat(x, y, PhatOptions{});
        REQUIRE(est.delaySamples == d);
        REQUIRE(std::abs(est.subSample) < 0.05);
        REQUIRE(est.peak > 0.5);
        REQUIRE_FALSE(est.inverted);
    }
}

TEST_CASE("a delay in the other direction is negative", "[delay][phat]") {
    constexpr std::size_t kTotal = 1 << 14;
    const auto y = aperiodicPink(kTotal, 59);
    std::vector<float> x(kTotal, 0.0f);
    for (std::size_t n = 200; n < kTotal; ++n) x[n] = y[n - 200];
    const auto est = findDelayPhat(x, y, PhatOptions{});
    REQUIRE(est.delaySamples == -200);
}

TEST_CASE("an inverted cable is reported, not hidden in the delay", "[delay][polarity]") {
    constexpr std::size_t kTotal = 1 << 14;
    const auto x = aperiodicPink(kTotal, 61);
    std::vector<float> y(kTotal, 0.0f);
    for (std::size_t n = 64; n < kTotal; ++n) y[n] = -x[n - 64];
    const auto est = findDelayPhat(x, y, PhatOptions{});
    REQUIRE(est.delaySamples == 64);
    REQUIRE(est.inverted);
}

TEST_CASE("sub-sample interpolation beats the sample grid", "[delay][subsample]") {
    // Half a sample at 48 kHz is 3.6 mm of air. Without interpolation the
    // finder cannot see it at all, and Friture's own source concedes that this
    // caps delay resolution at roughly 3 cm.
    constexpr std::size_t kTotal = 1 << 14;
    const auto x = aperiodicPink(kTotal, 67);
    std::vector<float> y(kTotal, 0.0f);
    // Linear interpolation is a mild low-pass, but it shifts the peak by
    // exactly 0.5 samples, which is what is being measured here.
    for (std::size_t n = 100; n < kTotal; ++n) {
        y[n] = 0.5f * (x[n - 100] + x[n - 101]);
    }
    const auto est = findDelayPhat(x, y, PhatOptions{});
    const double total = static_cast<double>(est.delaySamples) + est.subSample;
    REQUIRE(total == Catch::Approx(100.5).margin(0.1));
}

TEST_CASE("noise degrades the peak height without moving the peak", "[delay][phat]") {
    constexpr std::size_t kTotal = 1 << 15;
    const auto x = aperiodicPink(kTotal, 71);
    const auto n = aperiodicPink(kTotal, 73);
    std::vector<float> y(kTotal, 0.0f);
    for (std::size_t i = 300; i < kTotal; ++i) y[i] = x[i - 300] + 2.0f * n[i];
    const auto est = findDelayPhat(x, y, PhatOptions{});
    REQUIRE(est.delaySamples == 300);
    REQUIRE(est.peak < 0.9);   // the operator must be able to see it is poor
    REQUIRE(est.peak > 0.0);
}

TEST_CASE("the band limit restricts what is correlated", "[delay][band]") {
    constexpr std::size_t kTotal = 1 << 14;
    const auto x = aperiodicPink(kTotal, 79);
    std::vector<float> y(kTotal, 0.0f);
    for (std::size_t i = 150; i < kTotal; ++i) y[i] = x[i - 150];
    PhatOptions options;
    options.minHz = 200.0;
    options.maxHz = 4000.0;
    const auto est = findDelayPhat(x, y, options);
    REQUIRE(est.delaySamples == 150);
}

TEST_CASE("the finder refuses arguments it cannot use", "[delay][edge]") {
    const std::vector<float> a(128, 0.0f), b(64, 0.0f), empty;
    REQUIRE_THROWS_AS(findDelayPhat(a, b, PhatOptions{}), std::invalid_argument);
    REQUIRE_THROWS_AS(findDelayPhat(empty, empty, PhatOptions{}), std::invalid_argument);
    PhatOptions bad; bad.minHz = 5000.0; bad.maxHz = 100.0;
    REQUIRE_THROWS_AS(findDelayPhat(a, a, bad), std::invalid_argument);
}

TEST_CASE("silence produces a zero peak rather than a NaN", "[delay][edge]") {
    const std::vector<float> silence(4096, 0.0f);
    const auto est = findDelayPhat(silence, silence, PhatOptions{});
    REQUIRE(std::isfinite(est.peak));
    REQUIRE(std::isfinite(est.subSample));
}
```

- [ ] **Step 2: Fail.** — [ ] **Step 3: Implement.** — [ ] **Step 4: Pass** (≈239 total).
- [ ] **Step 5: Mutations** — absolute instead of relative regularisation (→ add
      a case that scales both inputs by 1e-4 and asserts the same answer; write
      it if the mutation survives), no sub-sample step (→ `sub-sample`
      test), `X*conj(Y)` (→ `other direction`), `|r|` before the parabola
      (→ `inverted cable` gets the wrong sub-sample sign).
- [ ] **Step 6: Commit** — `feat(core): GCC-PHAT, weighted flat and floored relatively`

---

## Task 6 — Phase unwrap, in the view layer where it belongs

Unwrapping is ambiguous wherever coherence is low, and one bad bin propagates a
360° step to **every bin above it**. In the engine that would corrupt an
accumulator later frames depend on; in the view it is one bad frame on screen.

**Files:** create `app/src/measure/PhaseUnwrap.h`, `app/src/measure/PhaseUnwrap.cpp`,
`app/tests/test_phase_unwrap.cpp`; modify `app/tests/CMakeLists.txt` (add both
the test and `../src/measure/PhaseUnwrap.cpp` to `rtatool_analysis_tests`).

**Interface produced:**

```cpp
namespace rta::measure {

struct UnwrapOptions {
    /// Bins at or below this coherence restart the unwrap instead of carrying
    /// a step upward. 0 disables the gate.
    float minimumCoherence = 0.0f;
};

/// Classic 1-D unwrap: add or subtract 2*pi whenever the step between
/// neighbours exceeds pi.
///
/// This is a DISPLAY operation and never feeds back into the engine. It is
/// history-dependent, so it is recomputed from the wrapped trace every frame --
/// never accumulated across frames.
void unwrapPhase(std::span<const float> wrapped, std::span<float> out,
                 const UnwrapOptions& options = {});

/// Coherence-gated overload. `coherence` must be the same length as `wrapped`.
void unwrapPhase(std::span<const float> wrapped, std::span<const float> coherence,
                 std::span<float> out, const UnwrapOptions& options);
}
```

- [ ] **Step 1: Write the failing tests** — `app/tests/test_phase_unwrap.cpp`

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/PhaseUnwrap.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace rta::measure;

TEST_CASE("a pure delay unwraps to a straight line", "[unwrap]") {
    // phi[k] = -2*pi*k*D/N wrapped. Unwrapped it must be that line again --
    // an exact closed form, so no tolerance-shopping is possible.
    constexpr std::size_t kBins = 513, kN = 1024;
    constexpr double kD = 7.0;
    std::vector<float> wrapped(kBins), out(kBins);
    for (std::size_t k = 0; k < kBins; ++k) {
        const double exact = -2.0 * std::numbers::pi * static_cast<double>(k) * kD / kN;
        wrapped[k] = static_cast<float>(std::remainder(exact, 2.0 * std::numbers::pi));
    }
    unwrapPhase(wrapped, out);
    for (std::size_t k = 0; k < kBins; ++k) {
        const double exact = -2.0 * std::numbers::pi * static_cast<double>(k) * kD / kN;
        REQUIRE(out[k] == Catch::Approx(exact).margin(1e-4));
    }
}

TEST_CASE("an already-continuous trace is unchanged", "[unwrap]") {
    std::vector<float> in{0.0f, 0.1f, 0.2f, 0.15f, -0.3f}, out(5);
    unwrapPhase(in, out);
    for (std::size_t i = 0; i < in.size(); ++i) REQUIRE(out[i] == Catch::Approx(in[i]));
}

TEST_CASE("low coherence stops a step propagating upward", "[unwrap][gate]") {
    // Without the gate, an ambiguous region shifts EVERY bin above it by 2*pi.
    //
    // The data has to be chosen with care, and this plan got it wrong once. A
    // SINGLE outlier bin cannot demonstrate propagation at all: the step in and
    // the step out are equal and opposite, so any correct unwrap self-heals one
    // bin later whatever the outlier's size. (And an outlier of 3.0 rad does
    // not even trip the unwrap -- 3.0 < pi, so nothing happens in either
    // direction and the "ungated must be dirty" half of the test can never
    // fail.) Two ADJACENT low-coherence bins with an internal jump past pi have
    // no such symmetry: the correction is applied once and never undone.
    constexpr std::size_t kBins = 16;
    std::vector<float> wrapped(kBins, 0.0f), coherence(kBins, 1.0f), out(kBins);
    wrapped[8] = 2.0f;
    wrapped[9] = -2.0f;          // a 4.0 rad step: past pi, so it reads as a wrap
    coherence[8] = 0.05f;
    coherence[9] = 0.05f;
    UnwrapOptions options; options.minimumCoherence = 0.2f;

    unwrapPhase(wrapped, coherence, out, options);
    REQUIRE(out[15] == Catch::Approx(0.0f).margin(1e-5));

    // Prove the gate is what did it: the same data ungated must NOT be clean.
    std::vector<float> ungated(kBins);
    unwrapPhase(wrapped, ungated);
    REQUIRE(std::abs(ungated[15]) > 1.0f);
}

TEST_CASE("unwrap refuses mismatched spans", "[unwrap][edge]") {
    const std::vector<float> a(8, 0.0f), c(4, 1.0f);
    std::vector<float> out(8);
    REQUIRE_THROWS_AS(unwrapPhase(a, c, out, UnwrapOptions{}), std::invalid_argument);
    std::vector<float> shortOut(4);
    REQUIRE_THROWS_AS(unwrapPhase(a, shortOut), std::invalid_argument);
}
```

- [ ] **Step 2: Fail.** — [ ] **Step 3: Implement.** — [ ] **Step 4: Pass** (≈243 total).
- [ ] **Step 5: Mutations** — gate ignored (→ `low coherence` test), `fmod`
      instead of a running offset (→ `pure delay` test).
- [ ] **Step 6: Commit** — `feat(app): unwrap phase where a bad unwrap costs one frame`

---

## Task 7 — The guard, and goldens from scipy

Two things the earlier tasks cannot do for themselves: stop a future edit from
bypassing the coherence gate, and check our scaling against an independent
implementation.

**Files:**
- Create: `core/tests/check_coherence_gate.cmake`
- Create: `tools/gen_transfer.py`
- Create: `core/tests/golden/transfer.txt` (generated, committed)
- Create: `core/tests/test_transfer_golden.cpp`
- Modify: `core/tests/CMakeLists.txt`

**The guard.** `makeSnapshot` is the only function allowed to assign
`TransferSnapshot::coherence`. The guard greps `core/src` and `core/include`
for `.coherence` assignments and fails if any appears outside
`core/src/dsp/TransferEstimator.cpp`.

**It must also prove it is still watching.** `measure_has_no_framework_deps`
once passed while scanning an empty file list, because a `GLOB_RECURSE` on a
mistyped path returns nothing and only a *fully* empty result was fatal. So this
guard asserts two things before it asserts anything about content:

1. the scanned file list is non-empty, and
2. `core/src/dsp/TransferEstimator.cpp` is **in** it.

If either fails, `message(FATAL_ERROR)` — a guard that cannot find the file it
exists to guard is broken, not satisfied.

```cmake
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The coherence gate (docs/dsp/2026-08-28-dual-fft.md §3) is only real if
# something enforces it. For ONE frame, |X*Y|^2 == |X|^2|Y|^2 identically, so
# coherence is exactly 1.0 at every frequency and a broken engine looks
# flawless. TransferEstimator.cpp's makeSnapshot() is the single place the gate
# is applied, and therefore the single place the field may be written.
file(GLOB_RECURSE SOURCES "${CORE_DIR}/src/*.cpp" "${CORE_DIR}/include/*.h")

if(SOURCES STREQUAL "")
    message(FATAL_ERROR "coherence-gate guard scanned nothing -- CORE_DIR=${CORE_DIR} is wrong")
endif()

set(SENTINEL "${CORE_DIR}/src/dsp/TransferEstimator.cpp")
if(NOT SENTINEL IN_LIST SOURCES)
    message(FATAL_ERROR "coherence-gate guard did not see ${SENTINEL} -- it is not watching")
endif()

set(OFFENDERS "")
foreach(FILE ${SOURCES})
    if(FILE STREQUAL SENTINEL)
        continue()
    endif()
    file(READ "${FILE}" CONTENT)
    if(CONTENT MATCHES "\\.coherence[ \t]*=" OR CONTENT MATCHES "coherence[ \t]*=[ \t]*std::vector")
        list(APPEND OFFENDERS "${FILE}")
    endif()
endforeach()

list(LENGTH SOURCES SCANNED)
if(OFFENDERS)
    message(FATAL_ERROR "coherence assigned outside the gate: ${OFFENDERS}")
endif()
message(STATUS "coherence-gate guard OK (${SCANNED} files scanned)")
```

Registered as:

```cmake
add_test(NAME coherence_gate_is_not_bypassed
    COMMAND ${CMAKE_COMMAND}
            -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/..
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_coherence_gate.cmake)
```

**The goldens.** `tools/gen_transfer.py`, modelled line for line on
`tools/gen_welch.py` (same line format, same `fmt`, same header block):

```python
kwargs = dict(fs=FS, window="hann", nperseg=nperseg, noverlap=noverlap,
              detrend=False, return_onesided=True)
freqs, pxy = signal.csd(x, y, **kwargs)      # conj(X) * Y, same as ours
_,     pxx = signal.welch(x, **kwargs)
_,     pyy = signal.welch(y, **kwargs)
_,     cxy = signal.coherence(x, y, **kwargs)  # magnitude-SQUARED
```

Three cases, all `nperseg=512, noverlap=256, fs=48000`, inputs generated in
float32 and written widened to float64 so the C++ side reads bit-identical
samples:

| case | data |
|---|---|
| `delayed` | `y[n] = 0.5*x[n-16]`, seed 101 |
| `noisy` | `y = x + 0.5*n`, seeds 103 and 107 |
| `filtered` | `y = sosfilt([[0.3,-0.2,0.1, 1,-0.5,0.2]], x)`, seed 109 |

**Use `sosfilt`, never `lfilter`.** The `filter_design_has_no_polynomial_form`
guard scans `tools/` as well as `core/`, and it fails the pipeline on the `ba`
(transfer-function polynomial) form — because that form's denominator roots
leave the unit circle at the bottom of the band table, which is a real numerical
defect and not a stylistic preference. An earlier draft of this plan specified
`lfilter([0.3,-0.2,0.1],[1,-0.5,0.2], x)` and was wrong. The SOS row above is
the identical filter, and it is also the exact `Biquad::Coeffs{b0,b1,b2,a1,a2}`
the C++ side uses, so the two cannot drift apart. A guard biting across tracks
is the guard working; conform to it rather than carving an exception.

Rows: `input_x`, `input_y`, `freq`, `pxx`, `pyy`, `pxy_real`, `pxy_imag`,
`coherence`.

The C++ test feeds the same `input_x`/`input_y` through `DualFftEngine` with
`fifoDepth` above the frame count (so our FIFO is a plain mean, which is what
scipy's default `average="mean"` computes) and asserts `Sxx`, `Syy`, `Sxy` and
the coherence against those rows to `epsilon(1e-5)` — float32 inputs through a
double accumulator; anything looser would hide a scaling error, anything tighter
is below the noise of a different summation order.

**Regenerate with** (from the repo root, main-checkout venv):

```
"D:\DEV CAVE EP3\PRJ010-RTA-TOOL\.venv\Scripts\python.exe" tools/gen_transfer.py
```

- [ ] **Step 1: Write `test_transfer_golden.cpp` against a file that does not exist yet.**
- [ ] **Step 2: Fail** — `loadGolden` throws, which is the point of it throwing.
- [ ] **Step 3: Write `gen_transfer.py`, run it, commit the .txt; write the guard.**
- [ ] **Step 4: Pass** — expected ≈247 tests, including
      `coherence_gate_is_not_bypassed`.
- [ ] **Step 5: Prove BOTH can fail.** Point `CORE_DIR` at a nonexistent path →
      the guard must go red on the sentinel check, not pass quietly. Add a
      `.coherence =` line to another core file → red. Perturb one golden value
      → red.
- [ ] **Step 6: Commit** — `test(core): scipy goldens, and a guard that proves it is watching`

---

## Task 8 — Close the lane in public

Nothing is delivered until someone else can see that it works.

**Files:**
- Create: `docs/reports/003-dual-fft-engine.md`
- Modify: `docs/reports/README.md` (index row)
- Modify: `docs/plans/MASTER-EXECUTION-PLAN.md` (L2 status; the spine moved)
- Modify: `docs/HANDOFF.md` (measured numbers, in **one** place)
- Create: `memory/dual-fft-conventions.md` + index line in `memory/MEMORY.md` —
  the sign conventions (`Sxy = conj(X)·Y`, positive delay = measurement lags)
  and the reason coherence reads lower than Open Sound Meter's.

- [ ] **Step 1: Re-measure everything from a clean build.** An incremental build
      can silently reuse a stale `.obj`, so no number that is going into a
      document comes from one:

```
cmake --build build-l2 --config Release --clean-first --parallel
ctest --test-dir build-l2 -C Release --output-on-failure
```

- [ ] **Step 2: Write the report** — what was built, what the verifier refuted,
      the exact commands and their output. Every count appears **once**; if two
      documents need it, the second links to the first.
- [ ] **Step 3: Grep for statements this lane made false.**

```
git grep -n "not started\|code not started\|P2 is the spine" -- docs
```

- [ ] **Step 4: Commit** — `docs(L2): the dual-FFT engine, and the numbers that prove it`

---

## Self-review against the decision record

| Record § | Where it is implemented | Where it is proven |
|---|---|---|
| §1 three accumulators, H1 default, H1/H2 gap exposed | Task 2 accumulators, Task 3 estimators | `H1 over H2 is exactly the coherence` |
| §2 magnitude-**squared** coherence | Task 3 `magnitudeSquaredCoherence` | the same bracket test + scipy golden |
| §3 no coherence below the minimum, absence in the type | Task 3 `std::optional`, Task 1 effective count | `coherence is withheld until…`, `coherence_gate_is_not_bypassed` |
| §4 GCC-PHAT, relative floor, sub-sample, applied before the FFT | Task 5 finder, Task 2 skip counters | `delay compensation happens before the transform`, `sub-sample interpolation` |
| §5 FIFO + exponential, **effective** frames | Task 2 modes, Task 1 counts | `the FIFO forgets exactly at its depth`, the whole of `test_average_count` |
| §6 unwrap in the view, group delay in core | Task 6, Task 4 | `low coherence stops a step propagating` |
| §7 CI proves it with no sound card | every task | 208 → ≈247 tests, no device opened |

**Not in scope, and deliberately:** multi-time-window stitching (L3),
multi-channel transfer functions (L6b), and anything about drawing or storing
these traces (L5). The engine returns a snapshot; nothing in this plan draws it.
