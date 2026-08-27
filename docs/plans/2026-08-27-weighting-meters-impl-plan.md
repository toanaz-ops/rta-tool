# Implementation plan — A/C/Z weighting, detectors, Leq/Ln

> **Build-time corrections (2026-08-28).** Four literals in this plan and its
> decision record were refuted by independent re-derivation during the build;
> every test asserts formulas, so none reached an assertion: the detector step
> constant (−1.9895 → −1.9920), the D1 off-by-one case (−1.99080 → −1.9915797,
> annotated inline below), the L1 duty-cycle case (−2.7003 → −2.9671, inline
> below), and §7/W1's Table 3 C-weighting column, which is SHIFTED BY ONE INDEX
> for n ≥ 2 — verified against python-acoustics' shipped IEC 61672-1 CSV; the
> corrected column lives in core/tests/test_weighting.cpp with both sources
> cited. Lesson, again: literals in planning prose are hints, never oracles.


*Station 3 of the pipeline in `docs/reports/README.md`. Turns
`docs/dsp/2026-08-27-weighting-and-meters.md` into exact files, exact tests and
exact acceptance numbers. Written 2026-08-27.*

Every number in the acceptance tables below was **reproduced against this repo's
own `.venv` (scipy 1.18.1 / numpy 2.5.2) while writing this plan**, not quoted
from the decision record. Where the reproduction disagreed with the decision
record, the disagreement is called out in §12 — it is not silently propagated.

---

## 1. Scope

**In:** four core modules and their tests, all pure `rta_core`, no JUCE.

| # | Deliverable | Layer |
|---|---|---|
| 1 | `core/include/rta/dsp/Weighting.h` + `core/src/dsp/Weighting.cpp` | `core/` |
| 2 | `core/include/rta/meter/Detector.h` + `core/src/meter/Detector.cpp` | `core/` |
| 3 | `core/include/rta/meter/Leq.h` + `core/src/meter/Leq.cpp` | `core/` |
| 4 | `tools/gen_weighting.py` → `core/tests/golden/weighting.txt` | tooling |
| 5 | `core/tests/test_weighting.cpp`, `test_detector.cpp`, `test_leq.cpp` | tests |

**Out of scope, deliberately:**

- **True-peak / 4x polyphase interpolation.** Deferred by the decision record to
  the meter build's second commit; it lands with its own `fs/4`, phase-π/4
  closed form. `Leq` here reports *sampled* peak only, and its doc comment must
  say so in those words.
- **Any conformance claim.** The honesty rule from the decision record is
  binding on this plan: **no identifier, comment, doc string, test name or UI
  string produced by this work may contain the words "Class 1" or "class 1"** as
  a claim about this software. The permitted claim is *"analytic weighting
  within 0.05 dB of IEC 61672-1 Table 3; digital filter error as published in
  `docs/dsp/2026-08-27-weighting-and-meters.md`"*. Task **W5** enforces this
  with a grep test.
- The `cal/` calibration layer. `Leq` and `Detector` take a
  `referenceOffsetDb` as **data** and add it; where that number comes from is
  somebody else's file.
- Anything in `app/`, `ui/`, `platform/`.

---

## 2. Inherited decisions (do not relitigate)

From `docs/dsp/2026-08-27-weighting-and-meters.md`:

1. Weighting is a **bilinear SOS cascade** designed from the IEC 61672-1
   Annex E analog poles. Not matched-Z, not oversampled, not a long FIR.
2. The design is normalised **at 1 kHz in the analog domain**, which is what
   python-acoustics does — so golden vectors and the reference implementation
   agree about what "correct" means. See the trap in §11.1: this leaves a small,
   sample-rate-dependent residual at 1 kHz in the *digital* filter, and it is
   not a bug.
3. Detectors are **exponential mean-square** one-poles: Fast τ=125 ms,
   Slow τ=1 s. Impulse is the **two-τ asymmetric approximation** (35 ms rise /
   1.5 s decay) and must be *labelled an approximation of the historical
   circuit* everywhere it appears.
4. `Leq = 10·log10(mean(p²)/p0²)`. Peak level is `10·log10(max|p|²/p0²)` —
   the squared form.
5. **Ln project convention:** percentiles over **Fast-weighted levels sampled
   every 100 ms**, with linear interpolation between order statistics. This is a
   project choice, not a standard; the doc comment and the UI must say so.

From `CLAUDE.md`: SPDX header on every file; headers under
`core/include/rta/<area>/`; hard cap 400 lines per file, aim 300; comments
explain *why the formula is that formula*; frequency displays as whole hertz,
dB to one decimal.

---

## 3. Cross-plan contract: the SOS type

The filter-bank plan (written in parallel from
`docs/dsp/2026-08-27-filterbank.md`) also needs a double-precision Direct Form
II Transposed second-order-section cascade. **There must be exactly one.**

**Ownership: the filter-bank track owns `core/include/rta/dsp/Biquad.h` and
`core/src/dsp/Biquad.cpp`.** The weighting track must not create those files.

Task **S1** below is the serialized adoption step. It is a no-op if the
filter-bank track has already landed the file; otherwise the agent running S1
creates it *to the spec below and nothing more*, and the filter-bank track
adopts it as-is.

### 3.1 Required API (binding on both plans)

```cpp
namespace rta::dsp {

/// One second-order section, `a0` already normalised to 1.
struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
};

/// A cascade of second-order sections, state and coefficients in double.
///
/// SOS and not a transfer-function polynomial, and double and not float,
/// for the reason measured in docs/dsp/2026-08-27-filterbank.md: the
/// direct-form polynomial of a low-frequency narrow band diverges to NaN in
/// double precision while the identical filter as SOS stays bounded in float32.
class SosCascade {
public:
    SosCascade() = default;
    explicit SosCascade(std::vector<BiquadCoeffs> sections);

    void reset() noexcept;                                     ///< zero the state
    [[nodiscard]] float processSample(float x) noexcept;       ///< in/out float, state double
    void process(std::span<const float> in, std::span<float> out) noexcept;

    [[nodiscard]] std::span<const BiquadCoeffs> sections() const noexcept;
    [[nodiscard]] std::size_t sectionCount() const noexcept;

    /// H(e^{j*omega}) with omega in radians/sample. Used by tests and by
    /// Weighting::responseDb; never on the audio path.
    [[nodiscard]] std::complex<double> response(double omega) const noexcept;
};

}  // namespace rta::dsp
```

Recurrence, per section, in this exact form (DF2T — the form whose state is the
delayed *output* accumulators, which is what keeps a near-unit-circle pole from
losing precision the way DF1 does):

```
w  = b0*x + s1
s1 = b1*x - a1*w + s2
s2 = b2*x - a2*w
y  = w
```

**Section ordering is part of the contract:** sections are stored in **ascending
pole radius**, which is what `scipy.signal.zpk2sos(..., pairing='nearest')`
emits and what the filter-bank decision record already pinned. Golden
coefficient comparison depends on this; if the ordering differs the coefficients
compare unequal even though the filter is identical.

### 3.2 If S1 must create the file

Line budget: `Biquad.h` ≤ 110, `Biquad.cpp` ≤ 70. Add to `core/CMakeLists.txt`
in the same serialized edit as §9.

---

## 4. Module 1 — `dsp/Weighting`

### 4.1 Header sketch — `core/include/rta/dsp/Weighting.h` (budget 150 lines)

```cpp
namespace rta::dsp {

enum class WeightingType { A, C, Z };

std::string_view toString(WeightingType type) noexcept;

class Weighting {
public:
    /// @param sampleRate  hertz; must be > 0
    Weighting(WeightingType type, double sampleRate);

    /// The ANALYTIC (continuous-time) weighting in dB at `frequencyHz`.
    ///
    /// This is the design target and the closed form the tests assert against,
    /// not a convenience: it is IEC 61672-1 Annex E evaluated directly, with no
    /// sample rate anywhere in it. `Weighting`'s digital response is *an
    /// approximation of this function*, and the size of that approximation is
    /// published in docs/dsp/2026-08-27-weighting-and-meters.md.
    [[nodiscard]] static double analyticDb(double frequencyHz,
                                           WeightingType type) noexcept;

    [[nodiscard]] WeightingType type() const noexcept;
    [[nodiscard]] double sampleRate() const noexcept;

    /// The DIGITAL cascade's magnitude response in dB at `frequencyHz`.
    /// Off the audio path; for tests, plots and the error table.
    [[nodiscard]] double responseDb(double frequencyHz) const noexcept;

    [[nodiscard]] const SosCascade& cascade() const noexcept;

    void reset() noexcept;
    void process(std::span<const float> in, std::span<float> out) noexcept;
    [[nodiscard]] float processSample(float x) noexcept;

private:
    WeightingType type_;
    double        sampleRate_;
    SosCascade    cascade_;   // empty for Z
};

}  // namespace rta::dsp
```

`Z` is the flat case: **zero sections**, and `process` must copy input to output
**bit-identically** (not multiply by 1.0f — the test asserts bit equality, and a
`*1.0f` on a denormal or a NaN is not the identity).

### 4.2 Implementation chain — `core/src/dsp/Weighting.cpp` (budget 200 lines)

Constants, verbatim, from IEC 61672-1 Annex E:

```
f1 = 20.598997   f2 = 107.65265   f3 = 737.86223   f4 = 12194.217   (hertz)
```

Analog zero/pole sets (`w_i = 2*pi*f_i`):

| type | zeros (s-plane) | poles (s-plane) |
|---|---|---|
| A | 0, 0, 0, 0 | −w4, −w4, −w1, −w1, −w2, −w3 |
| C | 0, 0 | −w4, −w4, −w1, −w1 |
| Z | — | — (gain 1) |

Gain `k` is chosen so `|H(j·2π·1000)| = 1`, i.e. `k = 1 / |H_unnormalised(j2π·1000)|`.
For A that is `k = 7.390100623925276e9`, i.e. the unnormalised cascade reads
−197.373 dB at 1 kHz. **Do not hard-code `k`; compute it** — a hard-coded gain
silently survives a typo'd pole.

`analyticDb` evaluates the rational function directly at `s = j·2πf`. Closed
forms worth writing as the implementation (they are the standard's own
formulas and are cheaper and more obviously right than a generic zpk evaluation):

```
A(f) = 20*log10( k_A * f^4 /
       ( (f^2+f1^2) * sqrt(f^2+f2^2) * sqrt(f^2+f3^2) * (f^2+f4^2) ) )
C(f) = 20*log10( k_C * f^2 / ( (f^2+f1^2) * (f^2+f4^2) ) )
Z(f) = 0
```

with `k_A`, `k_C` derived (not typed in) by requiring the bracket to equal 1 at
1000 Hz. Note the exponents: the A numerator is `f^4` and the double poles at
f1 and f4 appear **un**-square-rooted while the single poles at f2 and f3 appear
under a square root. Getting one of those backwards produces a curve that still
looks like A-weighting.

Digitisation, in this order:

1. Bilinear-transform the **analog zpk** with the standard (non-prewarped)
   substitution, `fs2 = 2*sampleRate`:
   - `z_digital[i] = (fs2 + z[i]) / (fs2 - z[i])`, same for poles;
   - the degree deficit `n_poles - n_zeros` contributes that many extra zeros at
     `z = -1` (analog infinity maps there);
   - gain `k_digital = k * real( prod(fs2 - z[i]) / prod(fs2 - p[i]) )`.
2. Pair conjugate poles with their nearest zeros, one conjugate pair per
   section, and **order sections by ascending pole radius** (§3.1).
3. Expand each pair into `BiquadCoeffs`; fold `k_digital` into the numerator of
   the **first** section only.

Structural consequences, which are the free tests in §7:

- A has 4 analog zeros at `s=0` → 4 digital zeros at **z = +1**, plus 2 at
  `z = −1` from the degree deficit; 6 poles → **3 sections**.
- C: 2 zeros at `z = +1`, 2 at `z = −1`, 4 poles → **2 sections**.
- Therefore `|H(z=+1)| = 0` (exact DC rejection) and `|H(z=−1)| = 0` (exact
  Nyquist rejection) for both A and C.

Reference SOS for A at 48 kHz, ascending pole radius (regenerate, do not type
in — reproduced here so a reviewer can eyeball a wrong pairing at a glance):

```
[ 0.23430059286647212  0.46860118573294424  0.23430059286647212  1  -0.22455845805977914  0.0126066252715464  ]
[ 1.0                 -2.0                  1.0                  1  -1.8938704947230707   0.8951597690946617  ]
[ 1.0                 -2.0                  1.0                  1  -1.9946144559930215   0.9946217070140843  ]
```

Section 1's numerator is `k*(1,2,1)` — the two zeros at z = −1. Sections 2 and 3
are `(1,−2,1)` — zeros at z = +1. If your section 1 numerator is `(1,−2,1)` the
degree-deficit zeros went to the wrong place.

---

## 5. Module 2 — `meter/Detector`

### 5.1 Header sketch — `core/include/rta/meter/Detector.h` (budget 130 lines)

```cpp
namespace rta::meter {

/// IEC 61672-1 clause 5 time weightings, plus the legacy Impulse.
enum class TimeWeighting {
    Fast,     ///< tau = 125 ms
    Slow,     ///< tau = 1 s
    Impulse,  ///< APPROXIMATION: 35 ms rise / 1.5 s decay. See below.
};

std::string_view toString(TimeWeighting weighting) noexcept;

/// Exponential mean-square detector.
///
/// y[n] = y[n-1] + alpha * (x[n]^2 - y[n-1]),  alpha = 1 - exp(-1/(fs*tau))
///
/// The state is a MEAN SQUARE, not an amplitude and not a dB value. That is
/// what makes the closed forms in test_detector.cpp exact: the step response is
/// 1 - exp(-t/tau) in the state itself, so 10*log10 of it is the level, and a
/// decay is a straight line in dB with slope 10*log10(e)/tau.
///
/// Impulse is NOT the IEC quasi-peak rectifier followed by a 1.5 s decay; it is
/// the two-time-constant asymmetric approximation of that historical circuit,
/// and is labelled an approximation because none of the surveyed
/// implementations reproduce the original either. It is outside the current IEC
/// normative scope.
class Detector {
public:
    Detector(TimeWeighting weighting, double sampleRate);

    /// Seeds the state. The DEFAULT IS ZERO and that is deliberate: the step
    /// response 1 - exp(-t/tau) is only the closed form if the detector starts
    /// from silence. A caller that wants to prime the detector -- resuming a
    /// measurement, say -- passes the mean square explicitly.
    void reset(double initialMeanSquare = 0.0) noexcept;

    double processSample(float x) noexcept;                    ///< returns the new mean square
    double process(std::span<const float> in) noexcept;        ///< mean square after the last sample
    void   process(std::span<const float> in,
                   std::span<double> outMeanSquare) noexcept;  ///< per-sample trace

    [[nodiscard]] double meanSquare() const noexcept;
    [[nodiscard]] double levelDb(double referenceOffsetDb = 0.0) const noexcept;

    [[nodiscard]] static double riseTimeConstant(TimeWeighting) noexcept;   ///< seconds
    [[nodiscard]] static double decayTimeConstant(TimeWeighting) noexcept;  ///< seconds
    [[nodiscard]] double sampleRate() const noexcept;
    [[nodiscard]] TimeWeighting weighting() const noexcept;
};

}  // namespace rta::meter
```

For Fast and Slow, rise and decay time constants are equal. For Impulse the
implementation holds **two** alphas and selects per sample:

```cpp
const double alpha = (squared > state_) ? alphaRise_ : alphaDecay_;
```

`levelDb` returns `10*log10(meanSquare) + referenceOffsetDb`, and must return a
finite floor rather than `-inf` for a zero state. Floor: `-200.0 dB` before the
offset, chosen because it is far below any real measurement and finite, so a
`Lmin` that has seen silence does not poison every later arithmetic. Document
the floor as a constant, `kLevelFloorDb`.

### 5.2 `core/src/meter/Detector.cpp` (budget 110 lines)

`alpha = 1 - std::exp(-1.0 / (sampleRate * tau))`. Compute it in double; do not
use the `T/tau` first-order approximation — at Slow/48 kHz the two differ in the
11th digit but at Fast/8 kHz they differ in the 4th, and this code has no
business caring what sample rate it is at.

The block overload must be exactly the per-sample loop, not a decimated
approximation; test **D4** asserts bit equality between them.

---

## 6. Module 3 — `meter/Leq`

### 6.1 Header sketch — `core/include/rta/meter/Leq.h` (budget 170 lines)

```cpp
namespace rta::meter {

/// Ln percentile over a level history, project convention.
///
/// Ln is "the level exceeded n% of the observation time", so L10 is a HIGH
/// number and L90 a LOW one. Implemented as the (1 - n/100) quantile of the
/// ascending-sorted levels with linear interpolation between order statistics:
///
///     pos = (count - 1) * (1 - n/100)
///     Ln  = v[floor(pos)] + frac(pos) * (v[floor(pos)+1] - v[floor(pos)])
///
/// This is numpy's default 'linear' method. It is a CONVENTION, not a standard:
/// Ln does not appear in IEC 61672-1 and implementations differ in both the
/// sampling interval and the interpolation. The choice is recorded in
/// docs/dsp/2026-08-27-weighting-and-meters.md and the UI must label it.
[[nodiscard]] double percentileLevelDb(std::span<const double> levelsDb, double n);

class Leq {
public:
    /// @param timeWeighting  drives Lmax/Lmin and the Ln history. The project
    ///                       Ln convention is Fast; another choice makes the
    ///                       Leq/SEL/peak results still valid and the Ln
    ///                       results non-conforming to that convention.
    explicit Leq(double sampleRate,
                 double referenceOffsetDb = 0.0,
                 TimeWeighting timeWeighting = TimeWeighting::Fast);

    void reset() noexcept;
    void process(std::span<const float> in) noexcept;

    [[nodiscard]] double leqDb()   const noexcept;  ///< 10*log10(mean(p^2)) + offset
    [[nodiscard]] double selDb()   const noexcept;  ///< leq + 10*log10(T / 1 s)
    /// SAMPLED peak: 10*log10(max(p^2)) + offset. This is NOT true peak; the
    /// inter-sample maximum of a band-limited signal can exceed it, and the
    /// 4x-interpolated true-peak meter is a later commit.
    [[nodiscard]] double peakDb()  const noexcept;
    [[nodiscard]] double maxDb()   const noexcept;  ///< max of the time-weighted level
    [[nodiscard]] double minDb()   const noexcept;
    [[nodiscard]] double percentileDb(double n) const;  ///< Ln

    [[nodiscard]] double elapsedSeconds() const noexcept;
    [[nodiscard]] std::size_t sampleCount() const noexcept;
    [[nodiscard]] std::span<const double> levelHistory() const noexcept;
    [[nodiscard]] std::size_t historyIntervalSamples() const noexcept;
};

}  // namespace rta::meter
```

### 6.2 `core/src/meter/Leq.cpp` (budget 210 lines)

- Energy accumulator: `sumSquares_ += double(x)*double(x)` — **accumulate in
  double even though the samples are float**; a float accumulator loses the tail
  of a long measurement entirely.
- `leqDb = 10*log10(sumSquares_ / count_) + offset`, floored at `kLevelFloorDb`.
- `selDb = leqDb + 10*log10(count_ / sampleRate)` — the `T0 = 1 s` reference is
  what makes SEL a *number* and not a *rate*; write that in the comment.
- `peakDb = 10*log10(maxSquare_) + offset` where `maxSquare_` tracks
  `max(x*x)`. **Track the square, not `max|x|`** — see the trap in §11.2.
- Ln / Lmax / Lmin feed: an internal `Detector` runs over every sample; its
  level is appended to `history_` every `historyIntervalSamples()` samples,
  where that count is `std::lround(0.1 * sampleRate)` (4800 at 48 kHz, 4410 at
  44.1 kHz). Lmax/Lmin update from the **same sampled series**, so the three
  statistics are consistent with each other.
- `percentileDb(n)` sorts a copy of `history_` ascending and calls
  `percentileLevelDb`. Throws `std::invalid_argument` for `n` outside `[0,100]`
  and for an empty history — an empty history returning `0.0 dB` would read as a
  plausible measurement.

If the file goes past 300 lines, the split seam is pre-agreed: move
`percentileLevelDb` and the history buffer into `core/{include/rta,src}/meter/LevelHistory.{h,cpp}`.
Do not split anywhere else.

---

## 7. Test plan — TDD sequence with named failing assertions

Every task below is *write the test, watch the named assertion fail for the
named reason, then implement*. "Fails to compile because the type does not
exist" counts as the first failure only for the first task of a track; after
that the failure must be an assertion.

### Track W — weighting (`core/tests/test_weighting.cpp`, budget 260 lines)

**W1 — analytic curve against its own closed form and against Table 3**

`TEST_CASE("Analytic A/C weighting matches IEC 61672-1 Table 3", "[weighting]")`

- First failing assertion:
  `CHECK_THAT(Weighting::analyticDb(1000.0, WeightingType::A), WithinAbs(0.0, 1e-12));`
- Then, at the **exact** third-octave frequencies `1000*std::pow(10.0, 0.1*n)`
  for `n = -20..13`, compare to the published Table 3 column with
  `WithinAbs(published, 0.05)`. Tables to hard-code (these are the published
  standard's values, and they are what the analytic formula reproduces — the
  0.05 dB window is the decision record's measured agreement):

  ```
  n:      -20   -19   -18   -17   -16   -15   -14   -13   -12   -11   -10    -9    -8    -7    -6    -5    -4
  A:    -70.4 -63.4 -56.7 -50.5 -44.7 -39.4 -34.6 -30.2 -26.2 -22.5 -19.1 -16.1 -13.4 -10.9  -8.6  -6.6  -4.8
  C:    -14.3 -11.2  -8.5  -6.2  -4.4  -3.0  -2.0  -1.3  -0.8  -0.5  -0.3  -0.2  -0.1  -0.0   0.0   0.0   0.0
  n:       -3    -2    -1     0     1     2     3     4     5     6     7     8     9    10    11    12    13
  A:     -3.2  -1.9  -0.8   0.0   0.6   1.0   1.2   1.3   1.2   1.0   0.5  -0.1  -1.1  -2.5  -4.3  -6.6  -9.3
  C:      0.0   0.0   0.0   0.0   0.0  -0.0  -0.1  -0.2  -0.3  -0.5  -0.8  -1.3  -2.0  -3.0  -4.4  -6.2  -8.5
  ```
  (`n = 13` is 19952.6 Hz; C at `n = 13` is −11.2 with the A value −9.3 — see
  the generated table in §8 for the full row if you prefer to read it there.)
- `CHECK(Weighting::analyticDb(f, WeightingType::Z) == 0.0);` exactly, for every f.
- **The trap this test exists for:** the loop must build frequencies as
  `1000*pow(10, 0.1*n)`, **not** from the nominal labels 20/25/31.5/…/16000.
  Add a second, explicitly-named check that pins the difference so nobody
  "tidies" it later:
  `CHECK_THAT(std::abs(Weighting::analyticDb(16000.0, A) - Weighting::analyticDb(1000*std::pow(10.0,0.1*12), A)), WithinAbs(0.104, 0.02));`
  with a comment that using nominal frequencies throughout introduces up to
  0.28 dB of spurious "error".

**W2 — the designed cascade's structure**

`TEST_CASE("Weighting cascades have the pole/zero structure Annex E implies", "[weighting]")`

- First failing assertion: `CHECK(Weighting(WeightingType::A, 48000.0).cascade().sectionCount() == 3u);`
- `CHECK(Weighting(C, 48000.0).cascade().sectionCount() == 2u);`
- `CHECK(Weighting(Z, 48000.0).cascade().sectionCount() == 0u);`
- Exact DC and Nyquist rejection, for A and C, at 44100/48000/96000:
  `CHECK_THAT(std::abs(cascade.response(0.0)), WithinAbs(0.0, 1e-12));`
  `CHECK_THAT(std::abs(cascade.response(pi)), WithinAbs(0.0, 1e-12));`
- Stability: every section's pole radius `sqrt(a2) < 1` and
  `|a1| < 1 + a2`. At 192 kHz the largest radius is 0.999326 — assert
  `< 1.0` strictly, not `<= 1.0`.
- Ascending pole radius across sections (the §3.1 ordering contract).
- Constructor rejects a non-positive sample rate:
  `CHECK_THROWS_AS(Weighting(A, 0.0), std::invalid_argument);`

**W3 — digital response near the analytic target, and the 1 kHz residual**

`TEST_CASE("Digital weighting tracks the analytic curve within the published error", "[weighting]")`

- First failing assertion (the residual trap, §11.1):
  `CHECK_THAT(Weighting(A, 48000.0).responseDb(1000.0), WithinAbs(0.0, 0.01));`
  — **not** `WithinAbs(0.0, 1e-9)`. The measured residual is **+0.004359 dB**
  at 48 kHz. A test demanding exact zero fails against a correct
  python-acoustics-parity design.
- Below 4 kHz the digital and analytic curves agree to better than 0.1 dB:
  for exact third-octaves with `f <= 3981.07`,
  `CHECK_THAT(w.responseDb(f) - Weighting::analyticDb(f, A), WithinAbs(0.0, 0.1));`
- The published error table is *asserted, not merely documented*, at 48 kHz and
  exact third-octave frequencies:

  | f (Hz, exact) | A error (dB) | C error (dB) | assert |
  |---|---|---|---|
  | 6309.573 | −0.2209 | −0.2276 | `WithinAbs(err, 0.01)` |
  | 7943.282 | −0.5259 | −0.5326 | `WithinAbs(err, 0.01)` |
  | 10000.000 | −1.2118 | −1.2183 | `WithinAbs(err, 0.01)` |
  | 12589.254 | −2.7337 | −2.7401 | `WithinAbs(err, 0.01)` |
  | 15848.932 | −6.2097 | −6.2156 | `WithinAbs(err, 0.01)` |

  Label this block, in a comment, as **a regression lock on a documented
  approximation error** — per `CLAUDE.md` that label is mandatory when the
  expectation is not a closed form. Its justification is that it is the exact
  table the decision record published and escalation to matched-Z is gated on.

**W4 — golden vectors** (after G1)

`TEST_CASE("Weighting design matches scipy's bilinear design", "[weighting][golden]")`

- SOS coefficients, section by section, `WithinRel(1e-9)`. A failure at 1e-9
  is a different *design*, not rounding — both sides are closed-form double
  arithmetic and agree to ~1e-14 in practice.
- `responseDb` against the golden `digital_db` row, `WithinAbs(1e-9)`.
- `analyticDb` against the golden `analytic_db` row, `WithinAbs(1e-10)`.
- Repeat for A/C at 44100, 48000, 96000 — three sample rates catch an fs
  hard-coded into the bilinear step, which a single-rate test cannot.

**W5 — the honesty rule, as a test**

Add to `core/tests/CMakeLists.txt` (serialized task S2):

```cmake
add_test(NAME core_makes_no_class_1_claim
    COMMAND ${CMAKE_COMMAND}
            -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/..
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_conformance_claim.cmake)
```

`core/tests/check_no_conformance_claim.cmake` (new, ~30 lines, modelled on
`check_no_framework_deps.cmake`) greps `core/` for a case-insensitive
`class *[01]` and fails with a message pointing at
`docs/dsp/2026-08-27-weighting-and-meters.md`. The full IEC 61672-1 Table 2
tolerance list is paywalled and only 4 points are corroborated; until it is
sourced, no such claim may be made.

### Track D — detector (`core/tests/test_detector.cpp`, budget 190 lines)

**D1 — time constants and the step response**

`TEST_CASE("Detector step response is 10*log10(1-exp(-t/tau))", "[detector]")`

- First failing assertion:
  `CHECK_THAT(Detector::riseTimeConstant(TimeWeighting::Fast), WithinRel(0.125, 1e-12));`
- Then the closed form. Feed **exactly `N = tau*fs` samples** of a signal whose
  instantaneous square is exactly 1 at every sample — use the alternating
  `+1, -1, +1, …` sequence, **not** a sine: a sine's `x²` ripples at 2f and
  contaminates the closed form (by ~0.006 dB at 1 kHz/Fast, which is enough to
  force a loose tolerance and hide a real error). With the alternating signal,
  `x[n]² = 1` exactly, and the discrete recursion gives exactly the continuous
  solution.

  ```cpp
  Detector d(TimeWeighting::Fast, 48000.0);
  std::vector<float> x(6000);                       // N = 0.125 * 48000
  for (std::size_t n = 0; n < x.size(); ++n) x[n] = (n % 2 == 0) ? 1.0f : -1.0f;
  const double ms = d.process(x);
  CHECK_THAT(10.0 * std::log10(ms), WithinAbs(10.0 * std::log10(1.0 - std::exp(-1.0)), 1e-9));
  ```

  The right-hand side evaluates to **−1.9920008 dB**. See §12 — the decision
  record says −1.9895 and that figure is wrong in the third decimal. Assert the
  formula, never the literal.
- **Off-by-one is the point of this test.** After feeding `N` samples the state
  is `1 - exp(-N*T/tau)`, i.e. the continuous solution at `t = N*T`, because
  `y[0] = alpha` already includes one time step. Feeding `N+1` samples and
  asserting `-1.992` fails by 0.0012 dB, and that failure is the test doing its
  job. Add the neighbouring assertion explicitly:
  `CHECK_THAT(10*log10(1 - exp(-6001.0/6000.0)), WithinAbs(-1.9915797 (corrected during build; the plan originally said -1.99080, refuted by 10*log10(1-exp(-6001/6000))), 1e-4));`
  with the comment "this is what index N+1 reads; the assertion above is index N".
- Repeat for Slow at `N = 48000`.
- Same-answer-at-a-different-sample-rate: repeat Fast at 44100 (N = 5512.5, so
  use 8000 Hz where `tau*fs` is an integer: `tau=0.125, fs=8000 -> N=1000`) —
  the point is that τ is in seconds and the result must not move with fs.

**D2 — decay slope by regression**

`TEST_CASE("Detector decay is a straight line in dB at 10*log10(e)/tau", "[detector]")`

- First failing assertion:
  `CHECK_THAT(slopeDbPerSecond, WithinRel(-34.7435585522, 1e-6));` for Fast.
- Method: charge the detector to steady state (feed 20·τ of the alternating
  unit signal), then feed 0.5 s of silence capturing the per-sample trace, and
  **least-squares fit** `10*log10(y[n])` against `t[n]`. A regression rather
  than a two-point slope because a two-point slope also passes for a decay that
  is not exponential; the fit's residual is the real assertion. Also check
  `CHECK(maxAbsResidualDb < 1e-9);`
- Slow: `-4.3429448190`. Impulse decay: `-2.8952965460`.
- These are `10*log10(e)/tau = 4.342944819032518/tau`. Compute them in the test
  from `std::log10(std::exp(1.0))`, and cross-check against the literals above
  with `WithinAbs(1e-6)` — same pattern as `test_window.cpp`, where a closed
  form and a published value each catch the other's typo.

**D3 — Impulse asymmetry**

`TEST_CASE("Impulse detector rises fast and decays slowly (approximation)", "[detector]")`

- First failing assertion: rise fitted τ ≈ 0.035 s.
  Feed `N = 0.035*fs` samples of the unit-square signal from zero and assert
  `WithinAbs(-1.9920008, 1e-9)` on the level — the same closed form, at the rise
  τ.
- Then assert the decay slope is the **1.5 s** value, −2.8953 dB/s, in the same
  run, which is what makes it asymmetric.
- `CHECK(Detector::riseTimeConstant(Impulse) != Detector::decayTimeConstant(Impulse));`
- The test file's header comment must state that this is an approximation of the
  historical circuit and is outside current IEC normative scope.

**D4 — block and per-sample APIs are the same code**

`TEST_CASE("Detector block API is bit-identical to the sample loop", "[detector]")`

- First failing assertion: `REQUIRE(blockResult == sampleResult);` — `==` on
  doubles, deliberately, because these must be the *same arithmetic in the same
  order*, not merely close.
- Drive with seeded pseudorandom floats (`std::mt19937 rng(20260827)`), split
  into ragged blocks (1, 7, 4096, 3 samples) and compare to one sample-at-a-time
  run. Ragged blocks catch a detector that resets or re-primes per block.
- `reset()` returns to the documented seed:
  `d.reset(); CHECK(d.meanSquare() == 0.0);`
  `d.reset(4.0); CHECK(d.meanSquare() == 4.0);`

### Track L — Leq (`core/tests/test_leq.cpp`, budget 240 lines)

**L1 — Leq of a piecewise signal is the exact energy sum**

`TEST_CASE("Leq of a piecewise signal is the duty-weighted energy sum", "[leq]")`

- First failing assertion, the simplest case: full-scale alternating ±1 for 1 s
  gives mean square 1 → `CHECK_THAT(leq.leqDb(), WithinAbs(0.0, 1e-12));`
- Then the two-level case: half the time at amplitude `a1`, half at `a2`, with
  `a1 = 1.0`, `a2 = 0.1` (i.e. 0 dB and −20 dB):
  `expected = 10*log10(0.5*1.0 + 0.5*0.01) = -2.9671 (corrected during build; the plan originally said -2.7003, refuted by 10*log10(0.505)) dB`, asserted as the
  formula `10*std::log10(0.5*a1*a1 + 0.5*a2*a2)`, `WithinAbs(1e-12)`.
- Three-segment case with unequal durations (0.2 / 0.3 / 0.5) against
  `10*log10(sum(duty_i * a_i^2))`.
- `referenceOffsetDb` is additive and exact:
  `CHECK_THAT(withOffset.leqDb() - noOffset.leqDb(), WithinAbs(94.0, 1e-12));`
  for an offset of 94.0 (the conventional 1 Pa reference).
- Empty measurement: `CHECK(leq.sampleCount() == 0); CHECK(leq.leqDb() == kLevelFloorDb);`

**L2 — sampled peak, and the squared-form trap**

`TEST_CASE("Peak level is 10*log10(max(p^2)), not 10*log10(max|p|)", "[leq]")`

- First failing assertion, and it **must use an amplitude that is not 1.0**:
  ```cpp
  // amplitude 0.5 -> 20*log10(0.5) = -6.0206 dB.
  // The half-dB bug 10*log10(0.5) reads -3.0103 dB. At amplitude 1.0 BOTH
  // formulas read 0.0 dB, so a full-scale test cannot see this bug at all.
  CHECK_THAT(leq.peakDb(), WithinAbs(20.0 * std::log10(0.5), 1e-12));
  ```
- Sine of amplitude `A`: `peakDb - leqDb == 3.0103 dB` exactly (the crest factor
  of a sine), asserted as `10*std::log10(2.0)` with `WithinAbs(1e-6)` — a whole
  number of periods so the mean square is exactly `A²/2`.
- Negative peak counts: a signal whose maximum magnitude is a *negative* sample
  must produce the same `peakDb`. A `max(x)` instead of `max(|x|)`/`max(x*x)`
  passes every symmetric test and fails this one.

**L3 — SEL**

`TEST_CASE("SEL is Leq plus 10*log10(T/1s)", "[leq]")`

- First failing assertion: 10 s of the unit-square signal (Leq = 0 dB) gives
  `CHECK_THAT(leq.selDb(), WithinAbs(10.0, 1e-12));`
- A 0.1 s measurement gives `selDb = leqDb - 10.0`; the sign of the correction
  for `T < 1 s` is where this gets written backwards.

**L4 — percentile convention, hand-computable**

`TEST_CASE("percentileLevelDb follows the project Ln convention", "[leq]")`

Pure function, no detector, exact answers:

| input (dB) | call | expected | why |
|---|---|---|---|
| `{60,61,…,70}` (N=11) | `L10` | `69.0` | pos = 10·0.90 = 9 → the 10th ascending value |
| same | `L50` | `65.0` | pos = 5 |
| same | `L90` | `61.0` | pos = 1 |
| `{50,60,70,80,90}` (N=5) | `L10` | `86.0` | pos = 4·0.9 = 3.6 → 80 + 0.6·10 |
| same | `L90` | `54.0` | pos = 0.4 → 50 + 0.4·10 |
| same | `L50` | `70.0` | pos = 2 |
| `{42.0}` | any n | `42.0` | single sample |

- First failing assertion: `CHECK_THAT(percentileLevelDb(ramp, 10.0), WithinAbs(69.0, 1e-12));`
- Direction invariant, asserted separately because it is the error that survives
  every symmetric fixture:
  `CHECK(percentileLevelDb(v,10.0) > percentileLevelDb(v,50.0));`
  `CHECK(percentileLevelDb(v,50.0) > percentileLevelDb(v,90.0));`
- Input order must not matter: shuffle the same values with a seeded RNG and get
  the identical result (`==`, not `WithinAbs` — it is a sort).
- `CHECK_THROWS_AS(percentileLevelDb({}, 50.0), std::invalid_argument);`
  and the same for `n = -1` and `n = 101`.

**L5 — Ln end-to-end, from the detector output**

`TEST_CASE("Ln is taken from the time-weighted level at 100 ms, not from raw samples", "[leq]")`

Two cases, and the second is the one that matters.

*Case A — settled staircase, exact plateaus.* At 48 kHz, five 4-second
segments of the alternating `±A` signal at levels 60, 70, 80, 90, 60 dB
(`A = 10^(L/20)` with a 0 dB offset). 20 s → 200 history samples at 100 ms.
Each segment is 32 τ long, so by its end the detector is within
`10*log10(1-exp(-32))` ≈ 5e-14 dB of its target.

- `CHECK(leq.levelHistory().size() == 200u);`
- `CHECK(leq.historyIntervalSamples() == 4800u);`
- `CHECK_THAT(leq.percentileDb(10.0), WithinAbs(90.0, 1e-6));` — the 90 dB
  plateau contributes 35 settled samples, positions 165..199 in the sorted
  series, and `pos = 199·0.9 = 179.1` lands inside it.
- `CHECK_THAT(leq.percentileDb(90.0), WithinAbs(60.0, 1e-6));` — the two 60 dB
  segments contribute 70 settled samples at sorted positions ~5..74, and
  `pos = 19.9` lands inside.
- `CHECK_THAT(leq.percentileDb(50.0), WithinAbs(70.0, 1e-6));` — `pos = 99.5`
  lands inside the 70 dB run at ~82..116.
- `CHECK_THAT(leq.maxDb(), WithinAbs(90.0, 1e-6));`
- Write the counting argument above into the test as a comment. It is what makes
  these exact numbers a derivation rather than a recorded output. Segments are
  4 s and not 2 s precisely so every percentile lands well inside a plateau with
  margin to spare.

*Case B — the raw-sample fallacy.* 30 s at 48 kHz: 20 ms of 90 dB every 1 s,
50 dB the rest of the time (2% duty).

- Raw samples are at 50 dB 98% of the time, so a percentile taken over **raw
  samples** returns `L10 ≈ 50.0`.
- The Fast detector rises to `90 + 10*log10(1-exp(-0.02/0.125)) = 81.7 dB` at
  the end of each burst and decays at 34.74 dB/s, so most of the ten 100 ms
  samples in each second sit well above 50 dB.
- First failing assertion: `CHECK(leq.percentileDb(10.0) > 65.0);`
  A deliberately coarse bound: it is a *property*, and an implementation that
  percentiles raw samples returns ≈50.0 and misses it by 15 dB.
- Also `CHECK(leq.peakDb() > leq.percentileDb(10.0));` — peak is instantaneous
  and unweighted, Ln is time-weighted.

---

## 8. Golden generator — `tools/gen_weighting.py` (budget 210 lines)

Follows the `gen_bands.py` / `gen_golden.py` pattern exactly: module docstring
saying *why this reference and not another*, `fmt()` with `repr(float(v))`,
the `case / size / <key> <values> / end` line format, `encoding="utf-8"` on the
write, a header naming the scipy version, "DO NOT EDIT BY HAND".

Run:

```
.venv/Scripts/python.exe tools/gen_weighting.py
```

Writes `core/tests/golden/weighting.txt`, which is committed.

### 8.1 Cases

For `type` in `A, C` and `fs` in `44100, 48000, 96000` — case
`weighting_<type>_<fs>`:

| row | content |
|---|---|
| `freq` | `1000*10**(0.1*n)` for `n = -20..13` (34 exact third-octave frequencies) |
| `analytic_db` | `20*log10(abs(freqs(...)))` of the normalised analog zpk |
| `digital_db` | `20*log10(abs(sosfreqz(sos, worN=2*pi*freq/fs)))` |
| `sos_b0`,`sos_b1`,`sos_b2`,`sos_a1`,`sos_a2` | one row each, one value per section, ascending pole radius |

Design chain in the generator, in this exact order — it is the reference the
C++ must reproduce:

```python
z, p, k_unit = analog_zpk(type)                 # k_unit = 1.0
b, a = signal.zpk2tf(z, p, 1.0)
k = 1.0 / abs(signal.freqs(b, a, [2*np.pi*1000])[1][0])   # normalise at 1 kHz, ANALOG
zd, pd, kd = signal.bilinear_zpk(z, p, k, fs)
sos = signal.zpk2sos(zd, pd, kd)                # 'nearest' pairing, ascending pole radius
```

**No `output='ba'` anywhere in this script, not even for a debug print** — the
filter-bank decision record measured the direct-form polynomial diverging to NaN
for a near-unit-circle design, and a garbage "sanity check" masquerading as a
project bug costs a day.

Case `weighting_Z`: rows `freq` and `analytic_db` (all zeros), `sections 0`.

Case `leq_weighted_noise_<type>` — end-to-end, seeded:

```python
rng = np.random.default_rng(seed=20260827)
x = (0.25 * rng.standard_normal(48000 * 2)).astype(np.float32)   # 2 s at 48 kHz
y = signal.sosfilt(sos, x.astype(np.float64))
```

| row | content |
|---|---|
| `size` | 96000 |
| `input` | the float32 samples widened to float64 (exact, per the house format) |
| `leq_db` | `10*log10(mean(y**2))` |
| `peak_db` | `10*log10(max(x**2))` — of the **unweighted** input |
| `weighted_peak_db` | `10*log10(max(y**2))` |

C++ side (part of **W4**): filter with `Weighting`, accumulate with `Leq`,
compare `leqDb()` `WithinAbs(1e-4)`. Tolerance rationale: input samples are
bit-identical by construction, both sides accumulate in double, and the
cascades agree to ~1e-9 dB in response — so 1e-4 dB is ~5 orders of slack, and
a failure means a real disagreement, not accumulation order.

One caveat to state in the script's docstring: `sosfilt` and this project's DF2T
are the *same filter* but not the *same arithmetic order*, so bit equality is
not expected and is not asserted.

---

## 9. CMake edits — SERIALIZED, shared with the filter-bank plan

`core/CMakeLists.txt` and `core/tests/CMakeLists.txt` are **contention points**:
the filter-bank plan edits the same two files, and two agents editing them
concurrently produces a conflict or, worse, a silently dropped source file that
only shows up as a link error much later.

**Rule: one agent, one edit, at the end of each track.** Whichever track
finishes first performs its edit; the second track re-reads the file before
editing.

### S1 — adopt `dsp/Biquad` (serialized, runs before either track's code)

- If `core/include/rta/dsp/Biquad.h` exists: verify it satisfies §3.1 and stop.
- Otherwise create it to §3.1 and add to `core/CMakeLists.txt`:
  ```cmake
      src/dsp/Biquad.cpp
  ```
  (alphabetical position: before `src/dsp/Fft.cpp`.)

### S2 — register the new sources and tests (serialized, after the code exists)

`core/CMakeLists.txt`, inside `add_library(rta_core STATIC ...)`, keeping the
list alphabetical:

```cmake
    src/dsp/BandWeights.cpp
    src/dsp/Biquad.cpp          # from S1
    src/dsp/Fft.cpp
    src/dsp/OctaveBands.cpp
    src/dsp/RealFft.cpp
    src/dsp/SpectrumEngine.cpp
    src/dsp/Weighting.cpp       # NEW
    src/dsp/Window.cpp
    src/meter/Detector.cpp      # NEW
    src/meter/Leq.cpp           # NEW
```

`core/tests/CMakeLists.txt`, inside `add_executable(rta_core_tests ...)`:

```cmake
    test_weighting.cpp
    test_detector.cpp
    test_leq.cpp
```

and the new architectural guard from **W5**:

```cmake
add_test(NAME core_makes_no_class_1_claim
    COMMAND ${CMAKE_COMMAND}
            -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/..
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_conformance_claim.cmake
)
```

No change is needed for `RTA_GOLDEN_DIR` — it already points at
`core/tests/golden`, so `weighting.txt` is found by
`std::string(RTA_GOLDEN_DIR) + "/weighting.txt"` with no build-system work.

---

## 10. Commands and expected output

Regenerate golden vectors (only when the generator changes; the file is
committed):

```
.venv/Scripts/python.exe tools/gen_weighting.py
```

Expected, modulo counts:

```
wrote D:\DEV CAVE EP3\PRJ010-RTA-TOOL\core\tests\golden\weighting.txt -- 9 cases, scipy 1.18.1
```

Configure and build:

```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --parallel
```

Run just this work's tests:

```
ctest --test-dir build -C Release --output-on-failure -R "weighting|detector|leq|class_1"
```

Expected when the plan is complete — every named test case from §7 present and
passing, plus the new guard:

```
    Start  N: Analytic A/C weighting matches IEC 61672-1 Table 3
    Start  N: Weighting cascades have the pole/zero structure Annex E implies
    Start  N: Digital weighting tracks the analytic curve within the published error
    Start  N: Weighting design matches scipy's bilinear design
    Start  N: Detector step response is 10*log10(1-exp(-t/tau))
    Start  N: Detector decay is a straight line in dB at 10*log10(e)/tau
    Start  N: Impulse detector rises fast and decays slowly (approximation)
    Start  N: Detector block API is bit-identical to the sample loop
    Start  N: Leq of a piecewise signal is the duty-weighted energy sum
    Start  N: Peak level is 10*log10(max(p^2)), not 10*log10(max|p|)
    Start  N: SEL is Leq plus 10*log10(T/1s)
    Start  N: percentileLevelDb follows the project Ln convention
    Start  N: Ln is taken from the time-weighted level at 100 ms, not from raw samples
    Start  N: core_makes_no_class_1_claim

100% tests passed, 0 tests failed out of 14
```

Full suite, which must stay green including the existing architectural guard:

```
ctest --test-dir build -C Release --output-on-failure
```

Per `CLAUDE.md`, none of the above may be reported as "passing" without the
pasted output.

Verification of the acceptance numbers in this plan (what produced them, so a
reviewer can re-derive rather than trust):

```
.venv/Scripts/python.exe -c "import scipy, numpy; print(scipy.__version__, numpy.__version__)"
```
→ `1.18.1 2.5.2`. Every dB figure in §4.2, §7 and §12 came from
`scipy.signal.freqs` / `bilinear_zpk` / `zpk2sos` at those versions on
2026-08-27.

---

## 11. Traps — read this section before writing any code

### 11.1 Exact vs nominal third-octave frequencies

The test frequencies are `1000·10^(0.1n)`, **not** the labels 20/25/31.5/…/16000.
Measured today: the analytic A curve at nominal 16000 Hz is −6.7063 dB and at
the exact 15848.93 Hz it is −6.6026 dB — 0.10 dB apart, and up to 0.28 dB apart
elsewhere in the table. Comparing an exact-frequency implementation against a
nominal-frequency expectation manufactures an "error" that is entirely an
artefact of the frequency you asked at.

**This bites the error table too.** The decision record's published table
(−0.54 at 8 kHz, −1.22 at 10 kHz, −2.67 at 12.5 kHz, −6.43 at 16 kHz) was
computed at **nominal** frequencies. Reproduced today at nominal: −0.5399,
−(10 kHz is both), −2.6662, −6.4298 — matches. At **exact** third-octaves the
same errors are −0.5259, −1.2118, −2.7337, −6.2097. §7/W3 asserts the *exact*
column. Do not "fix" one against the other; they are two different questions.

### 11.2 The peak-level squared form

`Lpeak = 10·log10(max|p|²/p0²)`. Note that `10·log10(max|p|²)` and
`20·log10(max|p|)` are the *same thing* and both are correct. The bug is
`10·log10(max|p|)` — half the decibels. **It is invisible at full scale**: at
`|p| = 1` both read 0.0 dB. Every peak test must therefore use an amplitude that
is not 1.0; §7/L2 uses 0.5, where correct is −6.0206 and the bug is −3.0103.

Also: track `max(x*x)`, not `max(x)`. A `max(x)` implementation passes every
symmetric test fixture and reports the wrong peak for an asymmetric waveform —
which is most real programme material.

### 11.3 Seeding the detector: from zero, not from the first sample

`reset()` defaults the mean-square state to **0.0**. Seeding from `x[0]²`
"to avoid the startup transient" is a real technique and it destroys the closed
form: the step response is only `1 − e^(−t/τ)` if the detector starts from
silence, and every assertion in D1/D2/D3 depends on that. A caller who genuinely
wants to prime the detector calls `reset(meanSquare)` explicitly.

The matching off-by-one: after feeding **N = τ·fs** samples, the state is
`1 − e^(−1)`, because `y[0] = α` already advances one step. Index `N` (the
`N+1`-th sample) reads −1.9915797 (corrected during build; originally −1.99080, refuted by the formula) dB, not −1.99200. Feed exactly N.

### 11.4 The 100 ms Ln sampling comes from the DETECTOR output

`Leq` samples **its internal time-weighted detector** every
`lround(0.1·sampleRate)` samples. It does not percentile raw audio samples, and
it does not percentile per-block Leq values. The distinction is not academic:
for a 2%-duty burst train the raw-sample L10 is 50 dB and the detector-sourced
L10 is above 65 dB — the same signal, 15 dB apart. §7/L5 case B is the test that
holds this in place; do not delete it as "redundant with case A".

### 11.5 The 1 kHz residual of the digital filter is not a bug

Normalisation happens on the **analog** zpk before the bilinear transform, which
is what python-acoustics does and what the decision record chose for reference
parity. The digital cascade therefore does not read exactly 0 dB at 1 kHz.
Measured today:

| type | 44100 | 48000 | 96000 | 192000 |
|---|---|---|---|---|
| A | +0.005164 | +0.004359 | +0.001090 | +0.000272 |
| C | −0.000184 | −0.000156 | −0.000039 | −0.000010 |

All are ≥ 200× below the loosest tolerance in play. A test asserting
`responseDb(1000) == 0` to 1e-9 fails against a correct implementation, and the
"fix" — renormalising the digital cascade at 1 kHz — silently breaks golden
parity with scipy. If a future session wants exact-0 at 1 kHz, that is a
decision-record change, not a code change.

### 11.6 SOS section ordering is load-bearing for the golden test

Coefficients are compared section by section, so the C++ pairing and ordering
must match `zpk2sos`'s: nearest-neighbour pole/zero pairing, sections ordered by
**ascending pole radius**. A cascade with the sections permuted is numerically
the same filter and fails the coefficient comparison — if `responseDb` matches
the golden but the coefficients do not, look here first, not at the design math.

### 11.7 Do not accumulate energy in float

`Leq` over a 15-minute measurement at 48 kHz sums 43 million squares. In float32
the accumulator stops growing long before that. Double, always — including the
detector state, which is why `SosCascade` and `Detector` carry double state
behind a float I/O boundary.

---

## 12. Discrepancy found while writing this plan — must be resolved

`docs/dsp/2026-08-27-weighting-and-meters.md` states:

> step response: ΔL(t) = 10·log10(1 − e^(−t/τ)); at t=τ exactly **−1.9895 dB**

The formula is right; the literal is wrong. `10·log10(1 − e^(−1)) =
10·log10(0.6321205588) = **−1.9920008 dB**`. Reproduced two ways today: the
closed form directly, and by running the discrete recursion
`y += α(1−y)` for N = τ·fs = 6000 samples at 48 kHz, which gives
`0.632120558829` → −1.992001 dB.

**Resolution for the build track:** assert the **formula**, never the literal —
`10.0 * std::log10(1.0 - std::exp(-1.0))`. That is house rule 1 in `CLAUDE.md`
("a closed-form identity") and it makes the code immune to the typo either way.

**Action for a human (station 2 owns the doc, not station 3 and not station 4):**
correct −1.9895 to −1.9920 in
`docs/dsp/2026-08-27-weighting-and-meters.md`. This plan does not edit that file.
Everything else in the decision record that this plan re-derived checks out:
the decay slopes 34.74 / 4.343 / 2.895 dB/s, the analytic-vs-Table-3 agreement
within 0.05 dB, and the digital error table (at nominal frequencies — §11.1).

---

## 13. Parallelization map

```
                    S1 (serialized: adopt dsp/Biquad)
                             |
        +--------------------+--------------------+
        |                    |                    |
   Track W (weighting)  Track D (detector)   Track L (Leq)
   W1 -> W2 -> W3          D1 -> D2             L1 -> L2 -> L3
        |                  D3, D4 (parallel)     L4 (independent of L1-L3)
        |                    |                    |
   G1 (generator) --> W4     |                   L5 (needs Detector: after D1)
        |                    |                    |
        +--------------------+--------------------+
                             |
                   S2 (serialized: CMake) -> full ctest -> station 5 review
```

| Track | Depends on | Can run concurrently with | Notes |
|---|---|---|---|
| **S1** | nothing | — | must complete first; also gates the filter-bank track |
| **W1** | S1 (compile only) | D, L | pure static function; no cascade needed |
| **W2, W3** | W1 | D, L | |
| **G1** | nothing | everything | Python only; touches no C++ |
| **W4** | W3 + G1 | D, L | |
| **W5** | nothing | everything | the grep guard; lands with S2 |
| **D1–D4** | S1 | W, L1–L4 | Detector needs no SOS; could start before S1 in principle, but the include graph is simpler if it waits |
| **L1–L3** | S1 | W, D | energy path only, no detector |
| **L4** | nothing | everything | pure function; the cheapest independent task in the plan |
| **L5** | D1, L1 | W | the only cross-track dependency inside this plan |
| **S2** | all code exists | — | one agent, re-read the file first |

Suggested dispatch (per `CLAUDE.md`, Sonnet subagents with exact file paths):

1. One agent: **S1** alone. Report the file's final API back before anything else starts.
2. Three agents in parallel: **W1–W3**, **D1–D4**, **L1–L4** + **G1**.
3. One agent: **W4** and **L5** (both cross-track).
4. One agent: **S2** + **W5**, then the full `ctest` run.
5. A **fresh** agent with no write tools for station 5. Per the global rule, the
   agent that wrote the code does not get to say it works.

Cross-plan serialization with the filter-bank track: only **S1** and **S2**
collide. Everything else in both plans touches disjoint files.

---

## 14. Definition of done

- [ ] All 13 `TEST_CASE`s from §7 exist, and each was observed **failing** on
      its named assertion before its implementation existed.
- [ ] `ctest -R "weighting|detector|leq|class_1"` output pasted into the
      station-4 report, showing 14/14.
- [ ] Full `ctest` green, including `core_has_no_framework_deps` and the new
      `core_makes_no_class_1_claim`.
- [ ] `core/tests/golden/weighting.txt` committed, with the generator's stdout
      pasted into the report.
- [ ] Every new file ≤ 400 lines, and the three `.cpp` implementations ≤ 300.
- [ ] No file produced by this work contains a "Class 1" claim (the guard test
      proves it, but check the *comments* too — the guard greps `core/`, not
      `docs/` or `tools/`).
- [ ] The §12 discrepancy is either fixed in the decision record by its owner or
      carried forward as an open item in the station-4 report.
- [ ] A report added under `docs/reports/` per the index in
      `docs/reports/README.md`, and the index row updated.
