# IEC 61260 filter bank — implementation plan

*Station 3 of the pipeline in `docs/reports/README.md`. Turns
`docs/dsp/2026-08-27-filterbank.md` into exact files, exact tests and exact
acceptance numbers. Nothing here re-opens the decision; where this plan adds a
policy the decision doc left open (the Nyquist clamp) it says so and pins it.*

Every number in the "acceptance" tables below was **measured in this repo's own
`.venv` (scipy 1.18.1, numpy 2.5.2)** during planning, not quoted.

---

## 0. What is being built

Four pure-`core` modules, one generator, three test files, one CMake guard.
No JUCE, no Qt, no device API anywhere — `core_has_no_framework_deps` already
greps `core/include`, `core/src` and `core/tests` and will fail the pipeline if
one appears.

```
core/include/rta/dsp/Biquad.h              header-only  DF2T section + cascade + response
core/include/rta/dsp/ButterworthDesign.h                ZPK -> SOS design chain (declarations)
core/src/dsp/ButterworthDesign.cpp                      ... implementation
core/include/rta/dsp/FilterBank.h                       one band-pass per OctaveBands band
core/src/dsp/FilterBank.cpp                             ... implementation
tools/gen_filterbank.py                                 golden fixtures from scipy
core/tests/golden/filterbank.txt           generated, committed
core/tests/test_biquad.cpp
core/tests/test_butterworth.cpp
core/tests/test_filterbank.cpp
core/tests/check_no_polynomial_form.cmake  CI guard: no ba-form anywhere
```

Conventions that are not negotiable and are already enforced elsewhere in the
repo: SPDX header on every file; headers under `core/include/rta/<area>/`;
comments explain *why the formula is that formula*; **hard cap 400 lines per
file, aim for 300**; UTF-8 explicit on every read-modify-write in Python.

---

## 1. The design chain, written out

This is the decision doc's chain expanded to the level a build agent can type.
`N` is the **number of second-order sections** = scipy's `N` = the analog
low-pass prototype order = **half** the band-pass pole count. `N = 6` for this
bank, so 12 poles, 12 zeros, 6 sections. (See trap 1 in section 9.)

**Step 1 — analog low-pass prototype (`buttap`).** No zeros, gain 1, and

```
p_k = -exp( j*pi*(2k - N - 1) / (2N) )        k = 1 .. N
```

so the exponent runs over the odd integers -N+1, -N+3, ... , N-1. All N poles
lie on the unit circle of the s-plane in the left half plane; for even N they
form N/2 conjugate pairs and none is real.

**Step 2 — prewarp both band edges.** The bilinear transform maps the analog
axis onto the digital one through a tangent, so an edge specified at `f` in
hertz has to be pushed out to the analog frequency that lands back on `f`:

```
w_lo = 2*fs*tan(pi*f_lo/fs)
w_hi = 2*fs*tan(pi*f_hi/fs)
```

Prewarping *both* edges is what makes the -3.0103 dB points land exactly on the
IEC band edges. It is also what makes test W3 below exact rather than
approximate — see section 5.2.

**Step 3 — low-pass to band-pass (`lp2bp_zpk`).** With

```
bw = w_hi - w_lo
w0 = sqrt(w_lo * w_hi)
```

each prototype pole `p` becomes two poles:

```
s  = p * bw/2
p± = s ± sqrt(s*s - w0*w0)            (complex sqrt, principal branch)
```

The prototype has no zeros, so the band-pass gets `N` zeros at the origin
(degree = #poles - #zeros = N), and the gain becomes

```
k_bp = k_lp * bw^N = bw^N
```

Result: 2N analog poles, N analog zeros at s = 0.

**Step 4 — bilinear in ZPK form (`bilinear_zpk`).** With `fs2 = 2*fs`:

```
z_d = (fs2 + z_a) / (fs2 - z_a)        for each analog zero
p_d = (fs2 + p_a) / (fs2 - p_a)        for each analog pole
```

then append `degree = #poles - #zeros = N` zeros at `z = -1` (the transform
sends s = infinity to z = -1), and

```
k_d = k_bp * real( prod(fs2 - z_a) / prod(fs2 - p_a) )
```

Result: 2N digital poles, 2N digital zeros — **N at z = +1 and N at z = -1**.

**Step 5 — pairing into sections.** Reproduce `scipy.signal.zpk2sos` with
`pairing='nearest'` exactly, because the golden SOS fixtures are its output and
the whole point of those fixtures is to catch a pairing bug. The algorithm,
read out of scipy 1.18.1 `_filter_design.zpk2sos` during planning:

1. Reduce each list to one representative per conjugate pair plus all real
   roots (`_cplxreal`): complex representatives are the ones with **positive**
   imaginary part, sorted by real part then imaginary part; reals sorted
   ascending. For our band-pass the pole list becomes N complex representatives
   and the zero list becomes 2N reals: N copies of -1 followed by N copies of +1.
2. Fill the section array **from the last index down to the first**. At each
   step take the remaining pole closest to the unit circle,
   `argmin |1 - |p||`, first index wins a tie.
3. That pole is complex, so its partner is its conjugate.
4. Take the remaining zero nearest to `p1` by `|z - p1|` (first index wins).
   It is real, so take a **second** real zero, again nearest to `p1`.
5. Emit the section as `b = [1, -(z1+z2), z1*z2]`, `a = [1, -(p1+p2), p1*p2]`,
   all imaginary parts discarded (they cancel exactly).
6. After the loop multiply the **first** section's three `b` coefficients by the
   overall gain `k_d`.

Net effect, and this is worth stating because it is the shape of the bug:
**sections come out ordered by ascending pole radius, and the whole gain lives
in section 0.** Verified at 1 kHz / 48 kHz — section pole radii came out
`0.98509, 0.98594, 0.98852, 0.99023, 0.99567, 0.99653`, ascending, and
`sos[0][0] == k == 1.1203481935216695e-11`.

**A section's numerator is not `[1, 0, -1]`.** Because both `+1` zeros and both
`-1` zeros sit at real positions and the nearest-zero rule picks by distance to
the pole, sections get *pairs* of identical zeros. Measured at 1 kHz / 48 kHz
the six numerators were

```
k*[1, 2, 1]   [1, 2, 1]   [1, 2, 1]   [1, -2, 1]   [1, -2, 1]   [1, -2, 1]
```

i.e. three sections with a double zero at z = -1 and three with a double zero at
z = +1. An implementation that hands every section one zero at +1 and one at -1
produces the identical overall transfer function, is worse conditioned, and
will fail the golden SOS test — which is exactly what that test is for.

**No transfer-function polynomial is ever formed.** See section 3 for how that
is made structural rather than aspirational.

---

## 2. The Nyquist-edge policy (decided here — record it)

The decision doc did not settle what happens to a band whose upper edge exceeds
Nyquist. It has to be settled, because `scipy.signal.butter` raises
`ValueError: Digital filter critical frequencies must be 0 < Wn < fs/2` and a
hand-rolled `tan(pi*f/fs)` for `f > fs/2` silently returns a negative warped
frequency and designs an unstable filter. Measured: at 44.1 kHz the 19952.6 Hz
third-octave band has an upper edge of 22387.2 Hz against a Nyquist of 22050 Hz.

**Policy, pinned:**

```
constexpr double kNyquistEdgeFraction = 0.995;   // of fs/2
```

* **centre >= 0.995 * fs/2** — the band is **not constructed at all**. It cannot
  be measured; a band that reports a number it cannot measure is worse than a
  missing band. The bank's band list is therefore a *subset* of the
  `OctaveBands` list, and `FilterBank::size()` may be smaller than
  `OctaveBands::size()`. Never index one with the other's index (see trap 5).
* **centre below that but upper edge above it** — the upper edge is clamped to
  `0.995 * fs/2`, the band is constructed, and `Band::nyquistClamped` is set
  **true**. A clamped band is a real, stable, useful measurement of the part of
  the band that exists below Nyquist. It is not a conforming IEC band, so it is
  excluded from the Class-1 mask assertion and flagged so the UI can say so.
* Lower edges need no policy: the lowest third-octave band at 48 kHz warps to a
  pole radius of 0.99991 and stays comfortably stable.

Measured consequences for `OctaveBands(3, 20.0, 20000.0)`, base ten (30 bands,
centres 19.9526 Hz .. 19952.6 Hz):

| fs | bands designed | clamped | max pole radius |
|---|---|---|---|
| 44100 | 30 | 1 (19952.6 Hz) | 0.999904971 |
| 48000 | 30 | 0 | 0.999912692 |
| 96000 | 30 | 0 | 0.999956345 |

`tools/gen_filterbank.py` must apply the *same* clamp before calling
`butter`, or the fixture and the code will describe different filters.

---

## 3. Making "no ba polynomial" structural

Two mechanisms, neither of them a comment.

**(a) No type can hold one.** The only coefficient aggregate that exists in the
module is `Biquad::Coeffs` — exactly five doubles (`a0` normalised to 1). There
is no `std::vector<double> b`, no `Polynomial`, no `std::array<double, 13>`.
The design chain's only intermediate is `Zpk` (two `std::vector<std::complex<
double>>` and a gain), and its only consumer is the pairing routine, which
emits `Biquad::Coeffs`. A degree-12 numerator has nowhere to live.

**(b) A CI guard, in the shape the repo already uses.** New file
`core/tests/check_no_polynomial_form.cmake`, modelled line-for-line on
`check_no_framework_deps.cmake` including its "no sources found means the check
is not actually running" guard. It scans `core/include`, `core/src`,
`core/tests` and `tools/*.py` and fails on any of:

```
output[ ]*=[ ]*['"]ba['"]        zpk2tf        tf2sos        tf2zpk
sos2tf                           np\.poly(?!fit)             numpy\.poly(?!fit)
signal\.lfilter                  freqz\(b
```

Wired as `add_test(NAME filter_design_has_no_polynomial_form ...)`. The one
legitimate exception is the docstring in `gen_filterbank.py` that *explains* the
prohibition, so the patterns above are chosen not to match prose: the guard
matches `output='ba'` with quotes and `signal.lfilter` with the module prefix,
neither of which appears in a sentence about them. If the build agent finds a
false positive, the fix is to reword the prose, never to weaken the pattern.

Why this matters, reproduced during planning at 15.85 Hz / 48 kHz:

```
ba form:  max |root(denominator)| = 1.0864141167436472   (outside the unit circle)
          impulse response peaks at 7.45e+130
sos form: impulse response peaks at 1.995e-06
```

The `ba` coefficient vector no longer represents the designed filter. Anyone who
prints it "just to check" will spend a day chasing a bug that is not in our code.

---

## 4. File-by-file specification

### 4.1 `core/include/rta/dsp/Biquad.h` — header-only, budget **<= 190 lines** (aim 160)

```cpp
namespace rta::dsp {

/// One second-order section, Direct Form II Transposed, double throughout.
///
/// DF2T is the form scipy's sosfilt uses and the form with the best rounding
/// behaviour for the pole radii this bank works at -- the lowest third-octave
/// band at 48 kHz sits 8.7e-5 from the unit circle, where a Direct Form I
/// accumulator loses the difference between a decaying and a growing mode.
struct Biquad {
    /// a0 is normalised to 1 and therefore not stored: storing a coefficient
    /// that is always one invites a caller to set it to something else.
    struct Coeffs { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };
    struct State  { double s1 = 0.0, s2 = 0.0; };

    ///     y  = b0*x + s1
    ///     s1 = b1*x - a1*y + s2
    ///     s2 = b2*x - a2*y
    static double processSample(const Coeffs& c, State& s, double x) noexcept;
};

/// A run of sections applied in order. Owns its coefficients and its state.
class BiquadCascade {
public:
    BiquadCascade() = default;
    explicit BiquadCascade(std::vector<Biquad::Coeffs> sections);

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::span<const Biquad::Coeffs> sections() const noexcept;

    /// Allocation-free. Sections run ascending, which is also the order the
    /// gain was placed in, so the running value stays near unity magnitude
    /// even for a band whose overall gain is 1.9e-22.
    double processSample(double x) noexcept;
    void   process(std::span<const float> in, std::span<float> out) noexcept;
    void   reset() noexcept;

    /// Largest |pole| over all sections. A cascade with any value >= 1 is not
    /// a filter, it is a sawtooth generator, so this is asserted in CI.
    [[nodiscard]] double maxPoleRadius() const noexcept;

    /// -20*log10|H(e^{jw})|, in decibels of ATTENUATION (positive = down).
    ///
    /// Summed per section rather than multiplied, because the product form
    /// spans 1e-22 to 1e+16 across a single low band's sections and the sum of
    /// logarithms cannot overflow at all.
    [[nodiscard]] static double attenuationDb(std::span<const Biquad::Coeffs>,
                                              double omegaRadiansPerSample);
    [[nodiscard]] double attenuationDb(double omegaRadiansPerSample) const;
};

}  // namespace rta::dsp
```

`maxPoleRadius` per section: `a2` is the product of the pole pair, so for a
complex pair `|p| = sqrt(a2)`; for real poles solve `x^2 + a1 x + a2` directly.
Handle both — a section that has degenerated to real poles is legal input.

No `.cpp`. Nothing here needs one, and adding one would put a second name into
`core/CMakeLists.txt` for no reason.

### 4.2 `core/include/rta/dsp/ButterworthDesign.h` — budget **<= 130 lines** (aim 110)

```cpp
namespace rta::dsp {

/// Zeros, poles, gain. The only intermediate representation this module has.
struct Zpk {
    std::vector<std::complex<double>> zeros;
    std::vector<std::complex<double>> poles;
    double gain = 1.0;
};

/// Closed-form Butterworth band-pass design. No scipy at runtime, no
/// transfer-function polynomial at any point -- see
/// docs/dsp/2026-08-27-filterbank.md for why the polynomial form is not a
/// convenience but a wrong answer.
class ButterworthDesign {
public:
    /// Bands whose edge would reach Nyquist are clamped to this fraction of it.
    static constexpr double kNyquistEdgeFraction = 0.995;

    struct Result {
        std::vector<Biquad::Coeffs> sections;   ///< ascending pole radius
        double lowerHz = 0.0;   ///< as DESIGNED, i.e. after any clamp
        double upperHz = 0.0;
        double maxPoleRadius = 0.0;
        bool   nyquistClamped = false;
    };

    /// @param sections  N: the SOS-section count, the analog prototype order,
    ///                  and HALF the band-pass pole count. Six for this bank.
    ///                  Throws std::invalid_argument for N < 1 or N > 12, for
    ///                  a non-positive sample rate, for lower >= upper, and for
    ///                  a lower edge at or below zero.
    [[nodiscard]] static Result bandPass(double lowerHz, double upperHz,
                                         double sampleRate, int sections);

    /// Steps 1-4 of the chain, exposed because a definition nobody can call is
    /// a definition nobody can check -- the same reason BandWeights::responseAt
    /// is public.
    [[nodiscard]] static Zpk bandPassZpk(double lowerHz, double upperHz,
                                         double sampleRate, int sections);

    /// Step 5. Reproduces scipy.signal.zpk2sos(pairing='nearest') exactly,
    /// including the section ordering and the gain landing in section 0.
    [[nodiscard]] static std::vector<Biquad::Coeffs> pairIntoSections(const Zpk&);
};

}  // namespace rta::dsp
```

### 4.3 `core/src/dsp/ButterworthDesign.cpp` — budget **<= 340 lines** (aim 300)

Internal statics in an anonymous namespace: `buttapPoles(N)`, `prewarp(f, fs)`,
`lowPassToBandPass(Zpk, w0, bw)`, `bilinear(Zpk, fs)`, `splitConjugateReal(...)`
(`_cplxreal`), `indexOfWorstPole(...)`, `indexOfNearestZero(...)`,
`sectionFromRoots(z1, z2, p1, p2)`.

Conjugate detection tolerance: pair `z` with `conj(z)` when
`|z - conj(w)| <= 1e-10 * max(1.0, |z|)`, matching `_cplxreal`'s default
`tol = 100 * eps` scaled by magnitude. Poles produced by step 4 come out in
exact conjugate pairs when the arithmetic is done symmetrically, so the
tolerance is a safety net, not a load-bearing constant — do not widen it to make
a test pass.

**Contingency on the line cap.** If the file crosses 340 lines, split step 5
into `core/src/dsp/SosPairing.cpp` implementing
`ButterworthDesign::pairIntoSections` — same header, second translation unit,
one extra line in `core/CMakeLists.txt`. Do **not** solve a long file by
deleting comments; the comment budget here is what lets a reader check the chain
against a textbook.

### 4.4 `core/include/rta/dsp/FilterBank.h` — budget **<= 150 lines** (aim 125)

```cpp
namespace rta::dsp {

/// One IEC 61260 band-pass per OctaveBands band, single rate.
///
/// Single-rate, not a decimation cascade: measured at ~43 M multiply-adds/s for
/// the full 1/3-octave bank at 48 kHz, which is nothing on an analysis thread,
/// and it removes the per-band group-delay bookkeeping a decimating bank has to
/// manage. See docs/dsp/2026-08-27-filterbank.md.
///
/// Not thread-safe. One instance per analysis thread.
class FilterBank {
public:
    /// N = SOS sections = half the pole count. Six is the value the conformance
    /// analysis settled on; it passes every ANSI S1.11 Table B1 Class-1
    /// breakpoint with margin.
    static constexpr int kDefaultSections = 6;

    /// IEC 61672-1 clause 5 exponential detector time constants.
    static constexpr double kFastSeconds = 0.125;
    static constexpr double kSlowSeconds = 1.0;

    struct Config {
        int        fraction     = 3;
        double     lowestHz     = 20.0;
        double     highestHz    = 20000.0;
        double     sampleRate   = 48000.0;
        int        sections     = kDefaultSections;
        double     timeConstantSeconds = kFastSeconds;
        OctaveBase base         = OctaveBase::BaseTen;
    };

    struct Band {
        int    index = 0;            ///< the OctaveBands index, NOT the bank index
        double centre = 0.0;
        double lower = 0.0, upper = 0.0;   ///< as designed, after any clamp
        double maxPoleRadius = 0.0;
        bool   nyquistClamped = false;
    };

    explicit FilterBank(const Config& config);

    [[nodiscard]] const Config& config() const noexcept;

    /// May be SMALLER than the OctaveBands table: bands whose centre reaches
    /// Nyquist are not constructed. Use Band::index to get back to the table.
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const Band& band(std::size_t i) const;
    [[nodiscard]] std::span<const Biquad::Coeffs> sections(std::size_t i) const;

    /// Allocation-free, lock-free, no I/O. Everything is sized in the ctor.
    void process(std::span<const float> samples) noexcept;
    void reset() noexcept;

    [[nodiscard]] std::uint64_t sampleCount() const noexcept;

    /// Mean square of each band's output over every sample since reset().
    /// Equal weight to every sample -- the settled measurement.
    [[nodiscard]] std::span<const float> meanSquare() const noexcept;

    /// One-pole exponential mean square, time constant from the config.
    /// This is the IEC 61672-1 detector and the number a live display shows.
    [[nodiscard]] std::span<const float> smoothed() const noexcept;

    /// alpha = 1 - exp(-1/(fs*tau)). Public because the closed-form step and
    /// decay tests below are tests OF this expression.
    [[nodiscard]] static double detectorAlpha(double tau, double sampleRate);
};

}  // namespace rta::dsp
```

### 4.5 `core/src/dsp/FilterBank.cpp` — budget **<= 180 lines** (aim 150)

Constructor: build `OctaveBands`, then for each band apply the section-2 policy,
call `ButterworthDesign::bandPass`, store a `BiquadCascade` and a `Band`.
Pre-size `meanSquare_`, `smoothed_` (float, exposed) and the two double
accumulators. Throw `std::invalid_argument` if the resulting bank is empty —
an empty bank makes every downstream assertion vacuously true.

Per sample, per band, in double:

```
y   = cascade.processSample(x)
p   = y * y
n  += 1                                            (once per sample, not per band)
msAccum[i] += (p - msAccum[i]) / n                 running mean, same shape as
                                                   SpectrumEngine's Linear mode
expAccum[i] += alpha * (p - expAccum[i])
```

Then narrow both accumulators into the exposed float spans. Keeping the
accumulators in double and exposing float is the existing house pattern
(`SpectrumEngine::accumulator_`); do not change it here.

Loop order: outer over samples, inner over bands, so the running mean's divisor
`n` is unambiguous. An outer-over-bands variant is permissible only if test F7
(chunking invariance) still holds bit-exactly.

### 4.6 `tools/gen_filterbank.py` — budget **<= 260 lines**

Docstring must open with the prohibition, in the repo's voice:

> **This script must never call scipy with the `ba` output form, and no
> debugging line added to it later may either.** For the low bands that output
> is not a less convenient representation of the same filter, it is a different
> and unstable filter: at 15.85 Hz / 48 kHz its denominator has a root at
> |root| = 1.0864 and its impulse response reaches 7.4e+130, while the identical
> design as SOS peaks at 2.0e-06. A number produced that way will masquerade as
> a bug in `rta_core`. See `docs/dsp/2026-08-27-filterbank.md`.

Follow `gen_bands.py`: `from __future__ import annotations`, module-level
`OUT_DIR`, `fmt()` using `repr(float(v))`, `main() -> int`, and
`path.write_text(..., encoding="utf-8")` — **UTF-8 explicit**, global rule 6.
Run as `.venv/Scripts/python.exe tools/gen_filterbank.py`.

Writes `core/tests/golden/filterbank.txt`. Expected size ~500 KB, in line with
`welch.txt` (569 KB).

Cases:

| case | contents |
|---|---|
| `design_fs48000_idx-17` | lowest band, 19.9526 Hz |
| `design_fs48000_idx0` | mid band, 1000 Hz |
| `design_fs48000_idx13` | highest band at 48 kHz, 19952.6 Hz, unclamped |
| `design_fs44100_idx13` | same band at 44.1 kHz — **clamped**, `clamped 1` |
| `impulse_fs48000_idx-17` | `impulse` = `sosfilt(sos, unit impulse)`, 2048 long |
| `impulse_fs48000_idx0` | same, 2048 long |
| `noise_fs48000_frac3` | `input` 8192 float32 samples, plus `index`/`centre`/`band_power` rows, one entry per band of the whole bank |
| `tone_fs48000_idx0` | `input` 8192 samples of a unit sine at exactly 1000 Hz, plus the same three per-band rows |

Every `design_*` case carries: `fs`, `sections 6`, `centre`, `lower`, `upper`
(post-clamp), `clamped 0|1`, `zpk_gain`, `pole_re`/`pole_im` (12 each),
`zero_re`/`zero_im` (12 each), and `sos` — 36 numbers, row-major
`b0 b1 b2 a0 a1 a2` per section in scipy's own section order.

Do **not** emit a `size` row; the two payload lengths differ per case and the
C++ side reads `row("impulse").size()`. `Golden.h` tolerates its absence.

Noise: `rng = np.random.default_rng(20260827)`, `rng.standard_normal(8192)`
cast to `float32` and written widened to float64, exactly as `gen_golden.py`
does — a golden vector compared against a slightly different input tests
nothing. The C++ side reads it back with `GoldenCase::floatRow`.

The clamp in section 2 must be applied here **before** calling `butter`, which
otherwise raises `ValueError: Digital filter critical frequencies must be
0 < Wn < fs/2`.

### 4.7 `core/tests/check_no_polynomial_form.cmake` — budget **<= 60 lines**

Copy `check_no_framework_deps.cmake`'s structure exactly, including its
`n_sources EQUAL 0` fatal error. Globs `${CORE_DIR}/include/*.h`,
`${CORE_DIR}/src/*.cpp`, `${CORE_DIR}/tests/*.cpp` and `${TOOLS_DIR}/*.py`.

---

## 5. Tests — TDD sequence, first failing assertion named

Each block below is written in the order it must be typed. **The test is
written and observed failing before the implementation exists.** Stage 0 in
section 7 exists precisely so "failing" means a red assertion rather than a
missing target.

### 5.1 `core/tests/test_biquad.cpp` — budget **<= 200 lines**, 7 cases

| # | TEST_CASE | closed form it rests on |
|---|---|---|
| B1 | "A section with b = {1,0,0} and no poles passes its input through" | identity |
| B2 | "The impulse response of a zero-pole section is exactly b0, b1, b2, 0, 0" | DF2T definition — pins the state update order |
| B3 | "A one-pole section decays as r^n" | `b={1,0,0}, a1=-r, a2=0` gives `y[n] = r^n` exactly; assert `WithinRel(pow(r,n), 1e-12)` for n up to 200 |
| B4 | "reset() restores a cascade to its construction state" | run 500 samples, reset, run the same 500, require bit-identical output |
| B5 | "A cascade equals its sections applied one after another" | three hand-written sections, chained by hand vs `BiquadCascade` |
| B6 | "attenuationDb of a half-sum section is -20log10 of cos(w/2)" | `b = {0.5, 0.5, 0}`, `a = 0` gives `abs(H(w)) = abs(cos(w/2))` exactly |
| B7 | "attenuationDb of a cascade is the sum of its sections'" | additivity of logarithms |

**First failing assertion (the TDD red for this file):** B2's
`CHECK_THAT(y[0], WithinRel(c.b0, 1e-15))` against the stage-0 stub, which
returns 0.

### 5.2 `core/tests/test_butterworth.cpp` — budget **<= 300 lines**, 8 cases

The mask table, transcribed from the decision doc's research pass. These are
the **one-third-octave Class 1** breakpoints of ANSI S1.11 Table B1; they do not
apply to a 1/1 or 1/6-octave bank (trap 6).

```cpp
// Attenuation limits in dB, relative to the nominal 0 dB pass-band gain.
// Positive attenuation means "down". Each row gives a frequency ratio and its
// reciprocal, so a row contributes two evaluation frequencies, fm*r and fm/r.
struct MaskRow { double high, low, minDb, maxDb; };
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr MaskRow kClass1[] = {
    {1.00000, 1.00000, -0.3,  0.3},
    {1.02667, 0.97402, -0.3,  0.4},
    {1.05575, 0.94719, -0.3,  0.6},
    {1.08746, 0.91958, -0.3,  1.3},
    {1.12202, 0.89125, -0.3,  5.0},
    {1.29437, 0.77257,  2.0, kInf},
    {1.88173, 0.53143, 17.5, kInf},
    {3.06955, 0.32578, 61.0, kInf},
};
```

| # | TEST_CASE | acceptance |
|---|---|---|
| W1 | "Six sections means six sections and twelve poles" | `bandPass(...,6).sections.size() == 6`; `bandPassZpk(...,6).poles.size() == 12` and `.zeros.size() == 12`. This assertion is the order-convention lock (trap 1) |
| W2 | "Every designed pole is inside the unit circle" | for fs in {44100, 48000, 96000}, 1/3-octave 20 Hz..20 kHz: `CHECK(r.maxPoleRadius < 1.0)`. **Measured maxima: 0.999904971 / 0.999912692 / 0.999956345** — also assert `> 0.9` so a stub returning 0 cannot pass |
| W3 | "The designed band edges are exactly -3.0103 dB" | bilinear is conformal and both edges were prewarped, so this is exact, not approximate. **Measured at 1 kHz / 48 kHz: 3.0102999566399724 and 3.0102999566403543 against the closed form 10*log10(2) = 3.0102999566398120.** Assert `WithinAbs(3.01029995663981, 1e-9)` for every unclamped band. A loose tolerance here hides a prewarp bug, which is why it is tight |
| W4 | "Every band meets the ANSI S1.11 Class 1 mask" | the table above, `A(f) = attenuationDb(sections, 2*pi*f/fs)`. Skip clamped bands; skip breakpoints at or above `0.995*fs/2`. **Measured: 442 breakpoints at 48 kHz, 429 at 44.1 kHz, 448 at 96 kHz, all passing, tightest margin 0.3000 dB** (that margin is the -0.3 dB minimum-attenuation row against a measured 3.9e-14 dB at mid-band). `REQUIRE(evaluated >= 1300)` across the three rates so the case cannot pass vacuously |
| W5 | "Sections come out ordered by ascending pole radius" | per-section `sqrt(a2)` non-decreasing. Catches a pairing loop that forgot to fill the array backwards |
| W6 | "SOS coefficients match scipy" | golden `design_*` cases, section by section, `WithinRel(expected, 1e-9)` OR `WithinAbs(expected, 1e-12)` — several coefficients are exactly 0, +-1 or +-2, so a bare relative tolerance is not usable |
| W7 | "Poles and zeros match scipy" | golden `pole_*`/`zero_*` rows compared as multisets sorted by (re, im) with `WithinAbs(1e-12)`; `zpk_gain` by `WithinRel(1e-12)`. Independent of pairing, so it localises a failure to the chain or to the pairing |
| W8 | "A band that reaches Nyquist is clamped and says so" | at 44.1 kHz the 19952.6 Hz band: `nyquistClamped == true`, `upperHz == WithinRel(0.995*22050.0, 1e-12)`, `maxPoleRadius < 1`. At 48 kHz the same band: `nyquistClamped == false`. And `bandPass` with `lowerHz >= upperHz` throws `std::invalid_argument` |

**First failing assertion:** W1's `REQUIRE(result.sections.size() == 6)` against
a stub returning an empty vector.

### 5.3 `core/tests/test_filterbank.cpp` — budget **<= 280 lines**, 8 cases

| # | TEST_CASE | acceptance |
|---|---|---|
| F1 | "A tone at a band centre reads its mean square in that band" | unit sine at the 3981.07 Hz centre, 48 kHz; run 0.1 s, `reset()`, then run 0.4 s so the filter transient is outside the average. **Measured with scipy: 0.500020705702662.** Assert `WithinAbs(0.5, 0.002)` |
| F2 | "A tone is rejected by neighbouring bands as the design goal says" | the analog design-goal attenuation `10*log10(1 + ((r - 1/r)/(G^(1/6) - G^(-1/6)))^(2N))` with N = 6. At one band away, r = G^(1/3) = 1.2589254, this gives **36.47 dB**; measured on the digital filter at 1 kHz and 3981 Hz: **36.50 and 37.00 dB**. Assert the neighbour band's level is at least 30 dB below the on-centre band, only where `fm*r < fs/8` so bilinear warping stays small (trap 8: at 15848.9 Hz the ratio-G point folds past Nyquist and reads 0.00 dB) |
| F3 | "reset() discards the running mean square and the sample count" | all zero after reset |
| F4 | "Band powers on white noise match scipy" | golden `noise_fs48000_frac3`; `WithinRel(1e-9)`. No settling requirement — both sides process the identical 8192 samples through the identical filters, transient included |
| F5 | "Band powers on an on-centre tone match scipy" | golden `tone_fs48000_idx0` |
| F6 | "The impulse response matches scipy sosfilt sample by sample" | golden `impulse_*`, `WithinRel(1e-9)` OR `WithinAbs(1e-18)`. This is the DF2T state-order test; a Direct Form II or a transposed-with-swapped-states implementation diverges within ten samples |
| F7 | "Processing in chunks equals processing in one block" | sixteen 1024-sample calls vs one 16384-sample call, **bit-identical** (`CHECK(a[i] == b[i])`, not a matcher) — the per-sample update order is identical, so anything less than exact equality means state is leaking between calls |
| F8 | "The detector rises and decays at its time constant" | three parts. (a) `detectorAlpha(0.125, 48000)` == `WithinRel(1.6665277854932548e-4, 1e-12)` and `detectorAlpha(1.0, 48000)` == `WithinRel(2.083311632095075e-5, 1e-12)`. (b) step: from reset, a unit sine on the 3981 Hz band centre; at t = tau the smoothed value is `0.5*(1 - exp(-1))`, i.e. **-1.99200085 dB** relative to the settled 0.5 — assert `WithinAbs(-1.99200085, 0.15)`, the tolerance covering the band's own ~7 ms settling against the 125 ms constant. (c) decay: after the tone stops, wait 100 ms for the ring to die, then the slope is `10*log10(e)/tau` = **34.7436 dB/s** for Fast and **4.3429 dB/s** for Slow; assert `WithinRel(..., 0.05)` |

**First failing assertion:** F1's
`CHECK_THAT((double) bank.meanSquare()[i], WithinAbs(0.5, 0.002))` — F3 would
pass against a zero-returning stub, so F1 is the named red for this file.

**Note on F8(b):** `docs/dsp/2026-08-27-weighting-and-meters.md` states the
value at t = tau as **-1.9895 dB**. That does not reproduce:
`10*log10(1 - exp(-1)) = -1.99200085`. Assert the derived value, not the doc's.
See handoff item 12.

---

## 6. CMake — exactly two edited files, one new file

`core/CMakeLists.txt`, inside `add_library(rta_core STATIC ...)`, keeping the
list alphabetical:

```cmake
    src/dsp/BandWeights.cpp
    src/dsp/ButterworthDesign.cpp     # <-- new
    src/dsp/Fft.cpp
    src/dsp/FilterBank.cpp            # <-- new
    src/dsp/OctaveBands.cpp
    src/dsp/RealFft.cpp
    src/dsp/SpectrumEngine.cpp
    src/dsp/Window.cpp
```

(plus `src/dsp/SosPairing.cpp` only if the section-4.3 contingency fires.)

`core/tests/CMakeLists.txt`, inside `add_executable(rta_core_tests ...)`:

```cmake
    test_biquad.cpp
    test_butterworth.cpp
    test_filterbank.cpp
```

and after the existing architectural guard:

```cmake
# --- Representation guard ------------------------------------------------
# The ba (transfer-function polynomial) form of these filters is numerically
# invalid at the bottom of the band table -- its denominator roots leave the
# unit circle. This fails the pipeline if anyone reintroduces it, in core or in
# the generator scripts. See docs/dsp/2026-08-27-filterbank.md.
add_test(NAME filter_design_has_no_polynomial_form
    COMMAND ${CMAKE_COMMAND}
            -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/..
            -DTOOLS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../../tools
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_polynomial_form.cmake
)
```

`Biquad.h`, `ButterworthDesign.h` and `FilterBank.h` need no CMake entry —
they are picked up through the existing `target_include_directories`, and both
guard scripts already glob `core/include/*.h`.

---

## 7. Task graph — who may run in parallel, and what they must not touch

**The two contention points are `core/CMakeLists.txt` and
`core/tests/CMakeLists.txt`.** Both are edited by exactly one task, once.

### Stage 0 — INTEGRATION, serial, one agent

Creates the skeletons so that "the test fails first" means a red assertion and
not a missing build target.

* Write `Biquad.h`, `ButterworthDesign.h`, `FilterBank.h` with the **exact**
  declarations in section 4 and nothing else — full comments, no bodies beyond
  what a header needs.
* Write `ButterworthDesign.cpp` and `FilterBank.cpp` as compiling stubs: every
  function returns a default-constructed result. No `#error`, no `throw` — a
  throwing stub turns every red assertion into the same exception message.
* Write the three test files containing only their **first failing case**
  (B2, W1, F1 above).
* Make both CMake edits and add `check_no_polynomial_form.cmake`.
* Configure, build, run ctest. **Expected: 3 failures, 0 errors**, and the
  failure text must name the three assertions above. Paste that output.

Stage 0 completes before stage 1 starts. It is the only task that touches CMake.

### Stage 1 — four agents in parallel, disjoint file sets

| agent | owns, exclusively | may read |
|---|---|---|
| A | `core/include/rta/dsp/Biquad.h`, `core/tests/test_biquad.cpp` | everything |
| B | `tools/gen_filterbank.py`, `core/tests/golden/filterbank.txt` | everything |
| C | `core/include/rta/dsp/ButterworthDesign.h`, `core/src/dsp/ButterworthDesign.cpp`, `core/tests/test_butterworth.cpp` | everything |
| D | `core/include/rta/dsp/FilterBank.h`, `core/src/dsp/FilterBank.cpp`, `core/tests/test_filterbank.cpp` | everything |

No agent edits a file another agent owns. No agent edits CMake. C and D compile
against the interfaces stage 0 already committed, which is why those
declarations are pinned verbatim in section 4 rather than left to the
implementer: **the interface is the contract that makes the parallelism safe.**
If an agent believes a declaration must change, it stops and reports rather than
editing — a header change is a stage-0-owned edit.

Ordering inside stage 1: A's header is needed to *compile* C and D, and B's
golden file is needed for W6/W7 and F4/F5/F6 to do anything but throw
"cannot open golden file". Both are already present as stage-0 stubs, and the
golden file simply does not exist yet, so all four can be *written*
simultaneously; only the final green build needs all four landed. Agents C and D
should expect their golden-backed cases to fail until B lands and must say so in
their report rather than deleting the case.

### Stage 2 — INTEGRATION, serial, same agent as stage 0

Full clean configure + build + ctest, count check, then hand to station 5
(a fresh verifier agent that reads the real files — per global rule 1, nobody
grades their own work).

---

## 8. Build and test commands, with expected counts

Baseline measured before any of this work: **34 ctest tests, all passing.**

```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Regenerate the fixture (agent B only):

```
.venv/Scripts/python.exe tools/gen_filterbank.py
```

Expected: `wrote .../core/tests/golden/filterbank.txt -- 8 cases`, file about
500 KB, committed, and the diff reviewed rather than waved through.

Count check:

```
ctest --test-dir build -C Release -N | tail -1
```

| stage | expected total | of which failing |
|---|---|---|
| baseline | 34 | 0 |
| after stage 0 | 38 (34 + 3 stub cases + 1 CMake guard) | 3 |
| after stage 1+2 | **58** (34 + 23 Catch2 cases + 1 CMake guard) | **0** |

23 = 7 (biquad) + 8 (butterworth) + 8 (filterbank). `catch_discover_tests`
registers one ctest entry per `TEST_CASE`, so if the build agent merges or
splits a case the total moves with it. **The invariant to report is: the total
grew by exactly (number of TEST_CASEs added) + 1, and zero tests fail.** Report
the number you measured, not the number in this table, and if they differ say
which cases you merged and why.

Do not report "passing" without pasting the command and its output — project
CLAUDE.md, verification standard, and global rule 1.

---

## 9. Handoff — the traps, in the order they will bite

1. **`order` means SOS sections, not poles.** `N = 6` here is scipy's `N`: the
   analog prototype order, the section count, and **half** the 12-pole
   band-pass. phonometry's "order 6" means this; python-acoustics' "order 8"
   means the true order. Reading one library's number into the other's
   parameter builds a different filter that still looks plausible. Test W1 is
   the lock; do not relax it.
2. **Never materialise a `ba` polynomial**, not in `core`, not in the
   generator, not in a scratch debugging line. Reproduced during planning at
   15.85 Hz / 48 kHz: `max |root(a)| = 1.0864`, impulse response `7.4e+130`,
   against `2.0e-06` for the identical filter as SOS. The guard test in
   section 3 is there because a comment would not have stopped anyone.
3. **Double everywhere** — coefficients, DF2T state, both accumulators.
   scipy's convention puts the *entire* gain in section 0, which for the lowest
   third-octave band at 48 kHz is `b0 = 1.8797419929251175e-22`. The cascade
   then climbs back to unity through six resonant sections. Double has the
   headroom; float32 does not once the intermediates are included, and the
   symptom is silence in the bottom third of the plot, not a crash.
4. **Nyquist clamp policy** (section 2, decided in this plan, not in the
   decision doc): centre at or above `0.995*fs/2` -> band not built at all;
   upper edge above it -> clamped to it with `nyquistClamped = true`; clamped
   bands are excluded from the Class-1 mask and still asserted stable. At
   44.1 kHz this affects exactly one band of a 20 Hz..20 kHz third-octave set.
   `gen_filterbank.py` must apply the identical clamp or the fixture describes
   a different filter than the code.
5. **The bank's band count can be smaller than the table's.** `FilterBank::size()`
   is not `OctaveBands::size()`. `Band::index` maps back. Indexing one array
   with the other's index is silent and wrong.
6. **The mask table is the one-third-octave Class 1 table.** Applying those
   ratios to a 1/1 or 1/6-octave bank asserts the wrong thing and will pass,
   because the wider band is more permissive. Guard the test with
   `REQUIRE(config.fraction == 3)` and say so in a comment.
7. **The table's limits are on ATTENUATION**, positive meaning down, referenced
   to the nominal 0 dB pass-band gain — not to `abs(H(fm))`. Measured at
   1 kHz / 48 kHz, `A(fm) = 3.9e-14 dB`, so the `-0.3 dB` minimum is the
   tightest constraint in the whole table (margin exactly 0.3000 dB). A sign
   flip still passes the f/fm = 1 row and fails everything else — or, worse,
   an implementation that normalises by `abs(H(fm))` makes that row trivially
   true and loses the only check on the pass-band gain.
8. **A neighbour-rejection assertion must check that the neighbour frequency is
   below Nyquist.** Measured: the 15848.9 Hz band evaluated at `fm*G` reads
   `0.00 dB` of attenuation, because `fm*G = 31624 Hz` folds. Every
   ratio-scaled evaluation in every test needs the `f < 0.995*fs/2` guard.
9. **scipy's section order and gain placement are not obvious**: ascending pole
   radius (worst-conditioned section last) and the whole gain in section 0.
   A mismatch shows up only in the golden SOS test W6, never in the response.
10. **A section's numerator is not `[1, 0, -1]`.** Measured at 1 kHz / 48 kHz
    the six numerators were `k*[1,2,1] [1,2,1] [1,2,1] [1,-2,1] [1,-2,1]
    [1,-2,1]` — double zeros at z = -1 and z = +1, assigned by nearest-neighbour
    distance to each section's pole. The "one zero at DC, one at Nyquist per
    section" implementation gives the same overall `H` and fails W6.
11. **`BandWeights::kFilterOrder` is 3; this bank's `sections` is 6.** They are
    not the same quantity in disguise: `BandWeights` implements the IEC 61260
    *design-goal* response shape for the FFT path with `2N = 6`, while the IIR
    bank is a real 12-pole filter. `docs/dsp/2026-08-26-banding-and-averaging.md`
    already records that the two paths will disagree and that this is not a bug.
    **Do not "unify" them.** Whoever writes the closed-form neighbour-rejection
    expectation in F2 must use N = 6, not `BandWeights::kFilterOrder`.
12. **Open item for a human:** `docs/dsp/2026-08-27-weighting-and-meters.md`
    gives the exponential detector's step response at `t = tau` as
    **-1.9895 dB**. That does not reproduce — `10*log10(1 - exp(-1))` is
    **-1.99200085 dB** (the decay figures 34.7436 / 4.3429 dB/s in the same doc
    do reproduce). This plan asserts the derived value. Someone should decide
    whether that doc has a transcription error or is quoting a different
    quantity, and fix it there. Out of scope for this plan, which writes one
    file.
13. **Measured ENBW cross-check, if anyone wants a fourth verification tier:**
    the noise bandwidth of this bank relative to the -3 dB bandwidth is
    `(pi/2N)/sin(pi/2N)` with N = 6 = **1.0115151599274625** analytically, and
    **1.011507618134393** measured by numerical integration of the digital
    filter at 1 kHz / 48 kHz — 7.5e-6 relative. It is not in the test list above
    because F4 (golden white-noise powers) already covers the same ground
    exactly rather than to 1%, but it is the closed form to reach for if F4 ever
    disagrees and the question is which side is wrong.

---

## 10. Definition of done

* All ten source/test/generator files exist at the paths in section 0.
* No file exceeds 400 lines; none of the five new C++ files exceeds its budget.
* `ctest` total grew by (TEST_CASEs added) + 1 and **zero** tests fail, with the
  command and its output pasted.
* `core_has_no_framework_deps` and `filter_design_has_no_polynomial_form` both
  pass.
* `core/tests/golden/filterbank.txt` is committed and regenerable by
  `.venv/Scripts/python.exe tools/gen_filterbank.py` with an empty diff.
* A station-5 verifier agent with **no write tools** has read the real files and
  failed to refute the claim.
