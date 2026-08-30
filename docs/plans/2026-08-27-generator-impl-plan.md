# Signal generator — implementation plan

*2026-08-27. Station 3 of the pipeline in `docs/reports/README.md`. Turns
`docs/dsp/2026-08-27-generator.md` into exact files, exact tests, exact numbers.*

This plan implements the approved decision **exactly**. Where it adds detail the
decision did not fix (sample rates for tests, FFT sizes, frame counts, golden
row names) that detail is chosen here so the builder does not choose it mid-task,
and every such choice carries its justification. Nothing here overrides the
decision record; if the two disagree, the decision record wins and the builder
stops and reports rather than picking.

---

## 0. Binding constraints

| Constraint | Source |
|---|---|
| `core/` includes no JUCE, Qt, or audio-device API | CLAUDE.md; enforced by `core_has_no_framework_deps` |
| Hard cap 400 lines per file, aim 300 — headers included | CLAUDE.md |
| `// SPDX-License-Identifier: AGPL-3.0-or-later` first line of every source file | CLAUDE.md |
| Comments explain *why the formula is that formula* | CLAUDE.md |
| No asserted value may be "what the code printed" | CLAUDE.md verification standard |
| Sine 1 kHz @ −20 dBFS through Hann → 1000 Hz ±0.5 Hz, −20 dBFS ±0.1 dB | `FEAT-phase1-rta-spl-generator.md` |
| Pink noise through 1/3-octave bands → every band equal within ±0.5 dB | same |
| Generator pink slope → −3.01 dB/octave ±0.2 dB | same |
| Farina sweep → deconvolution → unit impulse, SNR > 60 dB | same |

The venv is already provisioned — `numpy 2.5.2`, `scipy 1.18.1` under
`.venv/Lib/site-packages`. The "việc chặn" note in the FEAT doc (no numpy/scipy)
is **stale** and should be corrected when this work lands.

---

## 1. File manifest and line budgets

New files only. No existing file is edited except the two CMakeLists, which are
handled in the serialized task (§6).

| File | Budget | Cap |
|---|---|---|
| `core/include/rta/gen/Prng.h` | ~140 | 400 |
| `core/include/rta/gen/Oscillator.h` | ~150 | 400 |
| `core/src/gen/Oscillator.cpp` | ~170 | 400 |
| `core/include/rta/gen/Noise.h` | ~140 | 400 |
| `core/src/gen/Noise.cpp` | ~180 | 400 |
| `core/include/rta/gen/Sweep.h` | ~150 | 400 |
| `core/src/gen/Sweep.cpp` | ~220 | 400 |
| `core/include/rta/gen/Mls.h` | ~110 | 400 |
| `core/src/gen/Mls.cpp` | ~130 | 400 |
| `tools/gen_generator.py` | ~330 | 400 |
| `core/tests/test_generator_prng.cpp` | ~170 | 400 |
| `core/tests/test_generator_osc.cpp` | ~230 | 400 |
| `core/tests/test_generator_noise.cpp` | ~260 | 400 |
| `core/tests/test_generator_sweep.cpp` | ~240 | 400 |
| `core/tests/test_generator_mls.cpp` | ~190 | 400 |
| `core/tests/golden/generator.txt` | generated | — |

**Deviation from the brief, declared:** the brief names one
`core/tests/test_generator.cpp`. The five tests above total ~1090 lines, so one
file cannot hold them under the 400-line cap. They keep the `test_generator_`
prefix so the grouping stays legible. This is the split the brief anticipated,
not a new decision.

`Prng.h` is header-only and therefore adds **no** entry to
`add_library(rta_core ...)`.

Namespace for everything below: `rta::gen`. Headers carry the same two-line
banner as `core/include/rta/dsp/Window.h`.

---

## 2. Module specifications

### 2.1 `gen/Prng.h` — PCG32 + SplitMix64 (header-only)

Implements `pcg32_random_r` / `pcg32_srandom_r`, the `pcg_setseq_64_xsh_rr_32`
variant, from the published algorithm (O'Neill 2014, `imneme/pcg-c`,
`pcg-random.org`). **Transcribe from the published spec, not from another file
in this repo** — the whole point of the golden fixture is that two independent
transcriptions agree.

```
struct Pcg32 {
    // state advance:  state = state * 6364136223846793005u + inc;   (inc odd)
    // output:  xorshifted = uint32(((old >> 18) ^ old) >> 27);
    //          rot        = uint32(old >> 59);
    //          return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    // seeding (pcg32_srandom_r): state = 0; inc = (seq << 1) | 1;
    //          step(); state += initState; step();
};
```

Required API:

- `Pcg32(std::uint64_t seed, std::uint64_t sequence)` — the seeding routine above,
  verbatim; a different seeding order gives a different stream and silently
  breaks every golden row.
- `std::uint32_t nextUInt32() noexcept`
- `float nextUniform() noexcept` — **pinned mapping, both sides must use it:**
  `static_cast<float>(static_cast<double>(x) * 0x1p-32 * 2.0 - 1.0)`, giving
  `[-1, 1)`. Compute in `double`, narrow once at the end. Python does
  `np.float32(x / 2**32 * 2 - 1)`. Any other mapping (int32 reinterpretation,
  `/ UINT32_MAX`, float-domain arithmetic) produces different bits and the golden
  fixture becomes noise.
- `void reseed(std::uint64_t seed, std::uint64_t sequence) noexcept`
- `constexpr std::uint64_t splitMix64(std::uint64_t& state) noexcept` — the
  standard constants `0x9E3779B97F4A7C15`, `0xBF58476D1CE4E5B9`,
  `0x94D049BB133111EB`, shifts 30/27/31. Used only for deriving per-channel
  seeds from one master seed; it never generates audio.
- `struct ChannelSeeds { static Pcg32 forChannel(std::uint64_t master, std::size_t channel); }`
  or a free function — one master `std::uint64_t` in, independent `Pcg32` out.

Header must state, in comments: the analytic moments of `nextUniform()` —
mean 0, variance 1/3, RMS `1/sqrt(3) ≈ 0.5773503`, so a raw uniform stream sits
at **−4.771 dBFS RMS**. Every level calculation in `Noise` depends on that
constant, so it is written down where it is defined.

`noexcept` throughout; no allocation; safe to call from an audio callback.

### 2.2 `gen/Oscillator.{h,cpp}` — sine, dual sine, and the output ramp

**Phase accumulator in turns, not radians.** `phase_ ∈ [0,1)`, increment
`f / fs`, wrapped by `phase_ -= 1.0` (a `while`, not an `if`, so an out-of-range
frequency cannot desynchronise the state). `std::sin(2π · phase_)` at the point
of use. Turns keep the accumulator's magnitude near 1 for the whole run, so the
`double` mantissa loses no bits to a growing radian argument — a real drift
source for a generator that runs for hours at a show.

Types:

```
class Oscillator {                 // one tone
    Oscillator(double sampleRate, double frequencyHz, double levelDbFsPeak);
    void  setFrequency(double hz) noexcept;      // phase-continuous
    void  setLevelDbFsPeak(double db) noexcept;
    float nextSample() noexcept;
    void  process(std::span<float> out) noexcept;
    [[nodiscard]] double phaseTurns() const noexcept;   // exposed so it can be checked
    [[nodiscard]] double amplitude() const noexcept;
};

class DualSine {                   // two tones, peak-referenced on the SUM
    DualSine(double sampleRate, double f1Hz, double f2Hz, double levelDbFsPeak);
    // two independent Oscillators; each gets amplitude/2 so the summed peak is
    // the requested level. Documented, because "each tone at -20 dBFS" and "the
    // pair at -20 dBFS" differ by 6 dB and both are defensible defaults.
};

class RampedGain {                 // the click-free start/stop state machine
    enum class State { Idle, Rising, Running, Falling };
    RampedGain(double sampleRate, double rampSeconds = 0.010);   // 5-20 ms per the decision
    void  requestOn()  noexcept;   // sets the atomic target; UI thread
    void  requestOff() noexcept;   // sets the atomic target; UI thread
    float nextGain()   noexcept;   // audio thread
    void  apply(std::span<float> block) noexcept;
    [[nodiscard]] State state() const noexcept;
private:
    std::atomic<bool> target_{false};   // the only cross-thread word
};
```

`RampedGain` is **one ramp at the output stage**, not one per source. The
decision record puts the ramp in the callback; a single output ramp also makes
*switching source* click-free, which per-source ramps do not. Noise, Sweep and
Mls therefore own no ramp and include `rta/gen/Oscillator.h` when they need the
type. If a builder wants it hoisted to its own `gen/Ramp.h`, that is decided at
integration (§6) by the owner, **not** invented mid-task.

Ramp shape, closed form, asserted:
`g(p) = 0.5 · (1 − cos(π · p))`, `p = rampPos / rampLen ∈ [0,1]`, rising;
falling is `g(1 − p)`. Half a cosine period, so `g` and `dg/dp` are both zero at
`p = 0` and `g = 1`, `dg/dp = 0` at `p = 1` — that C¹ continuity at both ends is
why the click disappears, and it is what the test asserts.

Ramp interruption: a `requestOff()` during `Rising` must fall from the *current*
gain, not restart from 1.0. Implement by tracking `pos_` and reversing its
direction. Test 11 catches the naive version.

### 2.3 `gen/Noise.{h,cpp}` — white and pink

```
class WhiteNoise {
    WhiteNoise(Pcg32 rng, double levelDbFsRms = kDefaultNoiseLevelDbFsRms);
    void  setLevelDbFsRms(double db) noexcept;
    float nextSample() noexcept;
    void  process(std::span<float> out) noexcept;
};

class PinkNoise {                  // WhiteNoise + the Kellet 7-term filter
    PinkNoise(Pcg32 rng, double levelDbFsRms = kDefaultNoiseLevelDbFsRms);
    ...
    static constexpr double kRmsGainVsWhite = <from golden, see below>;
};

inline constexpr double kDefaultNoiseLevelDbFsRms = -12.0;
```

**Level convention.** Noise is **RMS-referenced dBFS** (decision record; REW's
documented behaviour). `setLevelDbFsRms(d)` sets a scale such that the *output*
RMS is `10^(d/20)`. For white that is `scale = 10^(d/20) · sqrt(3)` because the
raw uniform RMS is `1/sqrt(3)`. For pink it is
`scale = 10^(d/20) · sqrt(3) / kRmsGainVsWhite`. Both formulas go in the header
comment; a reader must be able to recompute them.

The generator **does not clip**. Pink at −12 dBFS RMS with ~12 dB crest will
occasionally touch full scale; clipping it inside core would break the slope
test and hide the problem from the UI. Headroom policy belongs to `app/`.

**Pink filter — Paul Kellett's refined ("instrumentation grade") 7-term IIR**,
from the music-dsp mailing list, archived at musicdsp.org as *"pink noise
filter"*. The comment above the coefficients **must** say all three of:

1. the coefficients are an **empirical fit**, not derived from a pole placement;
2. the author's stated bound is **±0.05 dB above 9.2 Hz at 44.1 kHz** — stated
   for 44.1 kHz and *not* re-derived here for 48 kHz, which is exactly why the
   ±0.2 dB/oct slope test at 48 kHz exists as an independent check;
3. the recorded escalation if the fitted table is ever challenged is **Kasdin's
   binomial-series cascade**, which has a published error recursion.

Coefficients (`b0..b6` state, `w` = white input):

```
b0 =  0.99886*b0 + w*0.0555179;
b1 =  0.99332*b1 + w*0.0750759;
b2 =  0.96900*b2 + w*0.1538520;
b3 =  0.86650*b3 + w*0.3104856;
b4 =  0.55000*b4 + w*0.5329522;
b5 = -0.76160*b5 - w*0.0168980;
out = b0+b1+b2+b3+b4+b5+b6 + w*0.5362;
b6 =  w*0.115926;
```

State in `double`; output narrowed to `float` once. State zero-initialised —
the golden depends on it.

**`kRmsGainVsWhite` is not a measured output.** It is the exact RMS gain of this
filter for unit-variance white input, and `tools/gen_generator.py` computes it
**two independent ways** and asserts they agree to 1e-10 before writing it to the
golden:

- closed form — the filter is a parallel bank of one-poles plus a direct term
  plus a one-sample-delayed term, so
  `σ²_out = Σ_i Σ_j (g_i g_j)/(1 − p_i p_j) + cross terms with the direct and
  delayed paths`, evaluated symbolically over the seven paths;
- numerically — `scipy.signal.freqz` of the same parallel structure, then
  `(1/2π)∫|H|²dω` by the trapezoid rule on a dense grid.

The C++ hard-codes the resulting constant with the Python function that produced
it named in the comment, and test 15 asserts the constant equals the golden row.
That satisfies the verification standard: the number has an independent
derivation, and the test proves the two agree.

**Multichannel.** `Noise` owns one `Pcg32`. Decorrelated channels are `N`
instances constructed from `splitMix64`-derived seeds off one master. Correlated
(duplicated) noise for mono-compatibility checks is the caller feeding one
instance's output to several channels — no core support needed, and the header
says so.

### 2.4 `gen/Sweep.{h,cpp}` — Farina exponential sweep

```
class Sweep {
    struct Config {
        double sampleRate  = 48000.0;
        double startHz     = 20.0;
        double endHz       = 20000.0;
        double durationSec = 10.0;
        double levelDbFsPeak = -6.0;
        double fadeInSec   = 0.02;   // clamped up to 2/startHz, see below
        double fadeOutSec  = 0.02;
    };
    explicit Sweep(const Config&);

    [[nodiscard]] double lengthConstantL() const noexcept;  // T / ln(f2/f1)
    [[nodiscard]] double phaseConstantK() const noexcept;   // 2*pi*f1*L
    [[nodiscard]] std::size_t lengthSamples() const noexcept;
    /// Closed-form phase at sample n, in radians: K*(exp(n/(fs*L)) - 1).
    /// Exposed because a definition nobody can call is a definition nobody can
    /// check -- the same reason BandWeights::responseAt is public.
    [[nodiscard]] double phaseAt(std::size_t n) const noexcept;
    [[nodiscard]] double instantaneousFrequency(std::size_t n) const noexcept;

    float nextSample() noexcept;              // real-time safe
    void  process(std::span<float> out) noexcept;
    void  restart() noexcept;

    /// NOT REAL-TIME SAFE. Allocates lengthSamples() floats and runs a full
    /// pass. Build it on the message thread when parameters change and hand the
    /// buffer to the analysis thread; NEVER call it from an audio callback.
    [[nodiscard]] std::vector<float> buildInverseFilter() const;
};
```

Real-time path: one `std::exp` and one `std::sin` per sample from the integer
sample counter — no recursion, so no drift, and a restart is exact.

**Fade.** Raised-cosine (`Tukey`-shaped) fade in and out, mandatory. `fadeInSec`
is clamped up to `2.0 / startHz` — two full cycles at the start frequency. An
unfaded start at `f1` is a broadband click that pollutes the exact band the
sweep exists to measure, and a fade shorter than one cycle at `f1` is not a fade.
The clamp is documented in the header with that reason.

**Inverse filter, derived so the sign of the exponent is checkable.**
An ESS spends equal *time* per octave, so its energy per octave is constant and
its density per hertz goes as `1/f`: `|X(f)| ∝ f^(−1/2)`, i.e. **−3 dB/oct**.
For `|X|·|Inv| = 1` we need `|Inv(f)| ∝ f^(+1/2)`, **+3 dB/oct**. Time reversal
does not change magnitude, so the reversed sweep still carries `f^(−1/2)`;
the time envelope must therefore supply `a(f) ∝ f`, which is **+6 dB per
octave** — exactly what the decision record states. Concretely, for reversed
index `m` with original index `n = N−1−m`:

```
inv[m] = x[n] * (instantaneousFrequency(n) / endHz)
```

**SUPERSEDED 2026-08-30 — do not restore the second fade.** The sentence below mandates a Tukey fade applied DIRECTLY to the inverse filter, on top of the taper it already inherits from the faded forward sweep. That layer was implemented (`Sweep.cpp` cited "§2.4 of the plan" as its authority) and removed on 2026-08-30: `inv[0]` is the sweep's LAST sample, so the layer tapered the deconvolution kernel's highest frequencies and cost 13.75 dB of in-band flatness at the default configuration, rising to 72 dB once the fade-in widened. `core/include/rta/gen/Sweep.h` always specified inheritance only; the code followed this plan instead. See `docs/dsp/2026-08-30-sweep-ir-l4a.md` decision 5 and `docs/plans/2026-08-30-L4a-sweep-ir-impl-plan.md` Task 1 Step 0.

The superseded text follows, kept so the record of what was asked stays
readable: normalised so the envelope's maximum is 1, then Tukey fades at both ends of the
inverse. Note this envelope *decays with time* along the inverse while *rising
with frequency* — the two common descriptions in the literature are the same
thing, and the comment must say so, because a reader who has met only the "6 dB
per octave decay" phrasing will otherwise think the sign is wrong.

### 2.5 `gen/Mls.{h,cpp}` — maximum-length sequence

Galois-form LFSR, orders 15–18, default 17, peak-referenced level.

```
class Mls {
    static constexpr int kMinOrder = 15, kMaxOrder = 18, kDefaultOrder = 17;
    Mls(int order = kDefaultOrder, double levelDbFsPeak = -6.0);
    [[nodiscard]] int order() const noexcept;
    [[nodiscard]] std::size_t period() const noexcept;   // (1u << order) - 1
    [[nodiscard]] std::uint32_t taps() const noexcept;   // the Galois mask
    float nextSample() noexcept;                          // +/-A
    void  process(std::span<float> out) noexcept;
    void  restart() noexcept;                             // state -> 1
};
```

Tap polynomials (Xilinx XAPP052 / the same primitive polynomials
`scipy.signal.max_len_seq` uses):

| order | polynomial | scipy `taps` |
|---|---|---|
| 15 | x¹⁵ + x¹⁴ + 1 | `[14]` |
| 16 | x¹⁶ + x¹⁵ + x¹³ + x⁴ + 1 | `[15, 13, 4]` |
| 17 | x¹⁷ + x¹⁴ + 1 | `[14]` |
| 18 | x¹⁸ + x¹¹ + 1 | `[11]` |

Galois step (LSB output form):

```
uint32_t out = state & 1u;
state >>= 1;
if (out) state ^= tapMask;      // tapMask has bit (order-1) plus the tap bits
return out ? +A : -A;
```

Initial state 1 (never 0 — 0 is a fixed point and the header must say so).
Order out of 15..18 throws, matching `Window`'s and `OctaveBands`' behaviour on
an invalid argument.

---

## 3. `tools/gen_generator.py`

Follows `tools/gen_golden.py` exactly: same docstring shape, same line format,
same `fmt()` helper, writes `core/tests/golden/generator.txt`, `encoding="utf-8"`
explicit on the write. Run as
`.venv/Scripts/python.exe tools/gen_generator.py`.

**Two format traps, both fatal if missed:**

1. `core/tests/support/Golden.h` parses every row as `double`. A `std::uint64_t`
   above 2⁵³ **cannot** round-trip. So: no PCG32 *state* words, no 64-bit
   checksums, ever, as one value. Checksums are written as two 32-bit halves
   (`..._hi`, `..._lo`), each exact in a double. `uint32` values round-trip
   exactly and are fine.
2. Rows must be whitespace-separated numerics only — `Golden.h` throws on any
   unparseable token, which is the behaviour we want, but it means no labels or
   hex inside a row.

Cases to emit:

| case | rows |
|---|---|
| `pcg32_seed42_seq54` | `u32` (first 32 outputs), `uniform` (first 16 `nextUniform()` values) |
| `pink_kellet` | `white` (4096 `nextUniform()` from seed 7 / seq 1), `pink` (4096 filter outputs, zero initial state) |
| `pink_rms_gain` | `closed_form`, `numeric` (must already agree to 1e-10 in Python), `value` |
| `mls_15` … `mls_18` | `order`, `tap_mask`, `head` (first 8192 samples as ±1), `ones_count`, `fnv1a_hi`, `fnv1a_lo`, `scipy_shift` |
| `sweep_params` | `fs`, `f1`, `f2`, `T`, `L`, `K`, `n` (10 sample indices), `phase` (the 10 phases) |
| `sweep_deconv` | `fs`, `f1`, `f2`, `T`, `ir_index`, `ir_amp`, `peak_index`, `peak_amp_norm`, `snr_db` |

Rules the script must follow:

- **PCG32 is re-implemented in Python from the published algorithm**, ~10 lines,
  with the reference named in a comment. It is *not* a translation of the C++
  file. A translation would make the golden a mirror, not a check. Mask every
  arithmetic step with `& 0xFFFFFFFFFFFFFFFF`.
- **The pink case writes the white input as well as the pink output.** This is
  the whole reason PCG32 was chosen over `numpy.random`. The C++ test asserts
  the white row *first*, with `REQUIRE`, before it looks at the pink row (§4.3).
- **MLS alignment.** `scipy.signal.max_len_seq` is a **Fibonacci** LFSR; ours is
  **Galois**. Same polynomial, same sequence *set*, different phase — and
  possibly the reciprocal polynomial. The script therefore: generates the Galois
  sequence from the spec, generates scipy's, searches for the cyclic shift that
  aligns them (checking the reversed sequence too), **asserts an alignment was
  found**, and records the shift in `scipy_shift`. If no alignment exists the
  script must fail loudly — that means the tap table is wrong, which is the
  single most likely error in this module.
- **FNV-1a 64** over the bit sequence (`0xCBF29CE484222325` offset,
  `0x100000001B3` prime, one byte per bit: 0x00 / 0x01) pins the *entire*
  131071-sample order-17 sequence in two numbers, so the golden file stays small.
  The C++ side implements the same published algorithm.
- **Sweep deconvolution case parameters, chosen for CI runtime:**
  `fs = 48000`, `f1 = 100 Hz`, `f2 = 10000 Hz`, `T = 2 s` → `N = 96000`,
  linear convolution length 191999 → FFT size 262144. Synthetic IR: unit impulse
  at `n = 1000`, reflections `0.5 @ 1500` and `−0.25 @ 2300`. Both sides
  normalise the recovered IR by its peak, then compute SNR the same way:
  `10·log10( peak² / mean-square of the residual outside ±(2·fs/f1) = ±960
  samples of each peak )`. The exclusion window exists because the sweep-inverse
  product is flat only between `f1` and `f2`, so the recovered "impulse" is a
  band-limited impulse with real, physical ringing that is not noise. Excluding
  two cycles at `f1` around each peak is the justification; the number is
  derived, not tuned.

---

## 4. TDD sequence — every step is a named failing assertion first

The rule: **write the test, run it, watch it fail with the message you expect,
then write the code.** A test that passes the moment it is written proved
nothing. For each step below, the "fails with" column is what the builder must
actually see before implementing.

### 4.1 `test_generator_prng.cpp` (5 cases, ~170 lines)

| # | `TEST_CASE` name | Asserts | Fails with (before impl) |
|---|---|---|---|
| 1 | `"PCG32 reproduces the golden stream for seed 42, sequence 54"` | 32 `nextUInt32()` == golden `u32`; 16 `nextUniform()` == golden `uniform`, bit-exact (`==`, not a tolerance) | compile error: no `rta/gen/Prng.h` |
| 2 | `"PCG32 uniform output has the analytic mean and variance"` | over 2²⁰ draws: `mean` within ±0.002 of 0, `variance` within ±0.002 of 1/3, every value in `[-1, 1)` | as above |
| 3 | `"PCG32 streams with different sequence selectors decorrelate"` | \|r\| < 0.02 over 2¹⁸ pairs — 10σ, since σ ≈ 1/√M = 0.002 | as above |
| 4 | `"Re-seeding a PCG32 replays the identical stream"` | 1024 samples, `==` | as above |
| 5 | `"SplitMix64 derives distinct, decorrelated channel seeds"` | 8 channels from one master: all `Pcg32` first-outputs distinct; \|r\| < 0.02 pairwise on 2¹⁶ samples | as above |

Case 1 is the honest limitation to record in the file's header comment: the
golden is only as independent as the Python transcription. If the builder can
obtain the reference `pcg32-demo` output from `imneme/pcg-c` offline, pin those
values as an *additional* assertion and say so; otherwise state in the comment
that independence rests on the two transcriptions being written separately from
the published spec. Do not silently imply a stronger check than exists.

### 4.2 `test_generator_osc.cpp` (6 cases, ~230 lines)

| # | `TEST_CASE` name | Asserts |
|---|---|---|
| 6 | `"The phase accumulator returns to zero after an integer number of cycles"` | `fs = 48000`, `f = 1000`: after 48000 samples `phaseTurns()` within 1e-12 of 0; after 4800 samples within 1e-12 of 0 (100 cycles) |
| 7 | `"A 1 kHz sine at -20 dBFS reads back at 1000 Hz and -20.0 dBFS"` | **the FEAT acceptance row.** `fs = 64000`, `fftSize = 32768`, Hann, Linear. Bin width 1.953125 Hz puts 1 kHz **exactly on bin 512**, so scalloping loss is zero and the assertion is about the generator, not about interpolation. Peak bin index == 512 → 1000.000 Hz, error 0.0 Hz, inside ±0.5 Hz. `20·log10(sqrt(2·spectrum[512]))` within ±0.1 dB of −20.0 |
| 8 | `"The same tone off-bin at 48 kHz reads back through a flat-top window"` | `fs = 48000`, `fftSize = 65536` (bin 0.7324 Hz, so the peak bin is within 0.366 Hz of truth — inside ±0.5 Hz with no interpolation), `WindowType::FlatTop`, whose scalloping loss is under 0.01 dB by construction. Amplitude within ±0.1 dB of −20.0. Corroborates case 7 at a real sample rate |
| 9 | `"Dual sine places exactly two components and manufactures no third"` | `f1 = 1000`, `f2 = 1200` on-bin at `fs = 64000`; both peaks at their expected mean square within ±0.1 dB; **no** bin outside the two mainlobes above −100 dBFS. The generator is linear, so an IM product at 2f₁−f₂ means a coding error |
| 10 | `"RampedGain follows the raised-cosine closed form"` | every sample of a 10 ms ramp at 48 kHz within 1e-6 of `0.5(1 − cos(π·n/L))`; endpoints exactly 0 and 1; the falling ramp is the mirror |
| 11 | `"A ramp interrupted mid-rise falls from where it was"` | `requestOn()`, run half the ramp, `requestOff()`: gain is continuous — `max |g[n] − g[n−1]|` stays below the rising ramp's own maximum step. Catches the naive "restart from 1.0" implementation, which shows up as a jump |

### 4.3 `test_generator_noise.cpp` (8 cases, ~260 lines)

Shared fixture for the statistical cases, **fixed seed**, computed once:

```
fs = 48000, fftSize = 16384, hop = 8192, Hann, Averaging::Linear
N = 2^22 samples = 4194304  (87.4 s of audio)  ->  K = 511 frames
```

Why these numbers, since a statistical test with an unjustified N is a coin
toss: with `K = 511` linear averages the per-bin relative standard deviation of
a Welch estimate is `1/sqrt(K) ≈ 4.4 %`, i.e. ±0.19 dB *per bin*; the slope is
fitted across thousands of bins and each 1/3-octave band sums dozens, so both
statistics land an order of magnitude inside their tolerances. The `E[log P]`
bias of a chi-square estimate is `≈ −4.34/(2K) = −0.004 dB` — negligible, and
worth a comment because it is exactly the trap that makes a log-domain
statistical test quietly wrong at small K. Runtime: generating 4.2 M samples is
~20 ms, 511 × 16384-point real FFTs ~100 ms. Bounded well under one second, and
**deterministic** — the seed is fixed, so the tolerance is a genuine margin and
the test can never flake.

Bin width is 2.93 Hz, so a 1/3-octave band (`0.2316·fc` wide) spans ≥ 3 bins
above `fc = 38 Hz`. The analysis range **50 Hz – 10 kHz** is therefore fully
resolved; assert `!band.underResolved` for every band used, rather than assuming
it.

| # | `TEST_CASE` name | Asserts |
|---|---|---|
| 12 | `"White noise hits its RMS-referenced level"` | at −12, −20, −6 dBFS RMS: measured RMS over 2²⁰ samples within ±0.05 dB of target |
| 13 | `"White noise is flat across 1/3-octave bands"` | all resolved bands 50 Hz–10 kHz within ±0.5 dB of their mean. Validates the analysis chain the pink tests depend on |
| 14 | `"The Kellet pink filter reproduces the golden output"` | **`REQUIRE` the 4096 white samples equal the golden `white` row bit-exactly first**, then `CHECK` the 4096 pink samples against `pink` within 1e-6 relative. If the white row differs the pink comparison is meaningless, so it must be a hard stop, not a second failure |
| 15 | `"The pink RMS gain constant matches its analytic value"` | `PinkNoise::kRmsGainVsWhite` within 1e-6 relative of golden `pink_rms_gain/value` |
| 16 | `"Pink noise falls at -3.01 dB per octave"` | **FEAT row.** least-squares fit of `10·log10(density[k])` against `log2(f_k)` over 50 Hz–10 kHz: slope within ±0.2 dB/oct of −3.01 |
| 17 | `"Pink noise is flat in 1/3-octave bands within 0.5 dB"` | **FEAT row.** `BandWeights` over `OctaveBands(3, 50, 10000)` fed `density()`; every resolved band within ±0.5 dB of the set mean |
| 18 | `"Two channels seeded from one master are uncorrelated"` | pink on both, \|r\| < 0.02 over 2¹⁸ samples |
| 19 | `"Pink noise crest factor sits in the SMPTE ST 2095-1 range"` | `20·log10(peak/rms)` over 2²⁰ samples inside 10–14 dB; SMPTE ST 2095-1 fixes calibration pink at 11.5–12 dB, and the wider window is the sampling spread of a finite run. Labelled a **range check**, not a conformance claim |

### 4.4 `test_generator_sweep.cpp` (6 visible cases + 1 hidden, ~240 lines)

| # | `TEST_CASE` name | Asserts |
|---|---|---|
| 20 | `"L and K follow from f1, f2 and T"` | `L == T/ln(f2/f1)` and `K == 2π·f1·L` within 1e-12 relative, for three parameter sets; and `f1·exp(T/L) == f2` within 1e-12 — the identity that makes `L` correct |
| 21 | `"The sweep phase matches the closed form K(e^(t/L)-1)"` | `phaseAt(n)` within 1e-12 relative of the closed form at 10 indices; cross-checked against golden `sweep_params/phase` within 1e-9 |
| 22 | `"Instantaneous frequency is f1 e^(t/L), measured at the zero crossings"` | the **independent second route**: locate successive positive-going zero crossings in the rendered sweep, take the reciprocal of the interval, compare against `f1·e^(t/L)` at the interval midpoint — within 0.5 % at t = 0.1 T, 0.3 T, 0.5 T, 0.7 T, 0.9 T. Coarse by design; it checks the *rendered signal*, where case 21 checks the formula |
| 23 | `"The fades are raised-cosine and at least two cycles at f1"` | the rendered envelope over the fade region matches `0.5(1 − cos(π·p))` within 1e-5; a request for `fadeInSec` shorter than `2/startHz` is clamped up |
| 24 | `"The inverse filter envelope rises 6 dB per octave with frequency"` | sample the inverse's envelope at the instants whose original instantaneous frequency is `f`, `2f`, `4f`; consecutive ratios within 1e-9 of 2.0 (= +6.02 dB). The endpoints are excluded because the Tukey fade lives there — say so in the comment |
| 25 | `"Sweep then inverse filter recovers a synthetic IR at SNR above 60 dB"` | **FEAT row.** The §3 case, run through `rta::dsp::RealFft` at size 262144. Peak at the expected index; reflection peaks at the right *relative* offsets (500 and 1300 samples) with amplitudes 0.5 and −0.25 within ±0.5 %; measured SNR **> 60 dB**; and SNR within ±0.5 dB of golden `sweep_deconv/snr_db` |
| — | `"Full-range 20 Hz to 20 kHz sweep deconvolution"`, tagged `[.][slow][sweep]` | the same, at `f1 = 20`, `f2 = 20000`, `T = 10 s`, FFT 2²⁰. **Hidden**, so `catch_discover_tests` will not register it and it does not count toward the ctest total. Run it by name: `rta_core_tests.exe "[slow]"` |

### 4.5 `test_generator_mls.cpp` (5 cases, ~190 lines)

| # | `TEST_CASE` name | Asserts |
|---|---|---|
| 26 | `"The MLS period is exactly 2^n - 1"` | for orders 15–18: the state returns to its initial value after exactly `2ⁿ−1` steps and at no earlier step. This is the property that makes the tap table right or wrong, and it needs no reference at all |
| 27 | `"The MLS is balanced"` | exactly `2^(n−1)` `+A` and `2^(n−1) − 1` `−A` per period — a closed-form property of every m-sequence |
| 28 | `"Circular autocorrelation is 2^n - 1 at lag 0 and exactly -1 elsewhere"` | order 15 (32767), computed via `RealFft` at size 65536; lag 0 == `2ⁿ−1`, every other lag == −1 within 1e-3 (FFT round-off on a 32767-long integer sequence). The single strongest closed form available for an m-sequence |
| 29 | `"The sequence matches the golden and its FNV-1a checksum"` | first 8192 samples `==` golden `head`; FNV-1a 64 of the whole period `==` `(fnv1a_hi, fnv1a_lo)` recombined; `tap_mask` `==` golden |
| 30 | `"MLS output is peak-referenced at the requested dBFS"` | every sample is exactly `±10^(d/20)`; peak == that, within 1e-7 |

---

## 5. Build and verification commands, with expected counts

**Baseline first.** Record what is there before touching anything — my count
below is from `grep -c "^TEST_CASE" core/tests/*.cpp`, which is not the same as
what ctest registers. Measure, do not trust:

```bash
ctest --test-dir build -C Release -N
```

Expected baseline: **33 `TEST_CASE`s + `core_has_no_framework_deps` = 34
entries**, *if neither sibling plan has landed*. If one has, the baseline is
higher and the only number that matters is the **delta of +30**.

Configure and build:

```bash
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
```

```bash
cmake --build build --config Release --parallel
```

Regenerate the golden (before the C++ tests can pass):

```bash
.venv/Scripts/python.exe tools/gen_generator.py
```

Full suite:

```bash
ctest --test-dir build -C Release --output-on-failure
```

**Expected after this work: 63 `TEST_CASE`s + the guard = 64 entries, all
passing.** The hidden slow sweep case is *not* in that count.

Generator tests only, while iterating:

```bash
ctest --test-dir build -C Release -R "generator|Prng|sine|pink|Sweep|MLS" --output-on-failure
```

The opt-in slow case:

```bash
build/core/tests/Release/rta_core_tests.exe "[slow]" --success
```

Every claim of "passing" pastes the command and its output. A green build proves
it compiled.

---

## 6. CMake additions — the SERIALIZED integration task

`core/CMakeLists.txt` and `core/tests/CMakeLists.txt` are the contention points
with the **filterbank** and **weighting/meters** plans, which are in flight and
append to the same two lists. **No build agent edits either file.** They are
edited once, by the integrating session, in a single task, after the sources and
tests exist on disk.

**Edit 1 — `core/CMakeLists.txt`**, inside `add_library(rta_core STATIC ...)`,
appended after the existing `src/dsp/Window.cpp` line:

```cmake
    src/gen/Mls.cpp
    src/gen/Noise.cpp
    src/gen/Oscillator.cpp
    src/gen/Sweep.cpp
```

`Prng.h` is header-only — no entry, deliberately.

**Edit 2 — `core/tests/CMakeLists.txt`**, inside
`add_executable(rta_core_tests ...)`, appended after `test_bandweights.cpp`:

```cmake
    test_generator_prng.cpp
    test_generator_osc.cpp
    test_generator_noise.cpp
    test_generator_sweep.cpp
    test_generator_mls.cpp
```

No other CMake change is needed: `RTA_GOLDEN_DIR` and the `support/` include
directory already point where `generator.txt` and `Golden.h` live.

**Contention rules:**

- Both edits are pure appends to a contiguous list. If another plan has landed
  first, append *after* whatever it added; do not reorder existing lines, because
  a reorder turns a clean three-way merge into a conflict for the third plan.
- After **any** of the three plans lands, re-run the **full** ctest, not only the
  new tests. The three share `Window`, `RealFft`, `OctaveBands` and
  `SpectrumEngine`.
- `git status` before touching either file (global rule 7): another worktree may
  be mid-edit on the same two lines.

---

## 7. Parallelization map

Six build tasks. A, B, E, F have no dependencies on each other and go out
together; C waits on A; D waits on nothing but its test needs `RealFft`, which
already exists.

```
        (parallel, dispatch together)
   A Prng.h + test_generator_prng.cpp
   B Oscillator.{h,cpp} + test_generator_osc.cpp
   E Mls.{h,cpp} + test_generator_mls.cpp
   F tools/gen_generator.py  ---------> core/tests/golden/generator.txt
   D Sweep.{h,cpp} + test_generator_sweep.cpp
        |
        A done
        |
   C Noise.{h,cpp} + test_generator_noise.cpp
        |
        all done
        |
   G SERIALIZED: the two CMake edits, configure, build, full ctest
        |
   H station-5 verifier (no write tools) tries to refute
```

F is parallel with A **on purpose**: it re-implements PCG32 from the published
spec, not from A's output. That independence is the value of the fixture, so the
two must not be written by the same agent or in sequence with one reading the
other.

Golden-file ordering: F produces `generator.txt` before A, C, D and E can go
green. If F is slower than the others, those tasks stop at "test written,
observed failing on the missing golden file" — which is a legitimate TDD state
to hand back, not a blocked task.

Per CLAUDE.md, tasks A–F are Sonnet subagents with an exact spec and exact file
paths; G is the owner's session; H is Fable with no write tools.

---

## 8. Handoff traps

Ordered by how expensive each is to discover late.

1. **The pink golden must feed the SAME white sequence to both sides.** This is
   the entire reason PCG32 was chosen over `numpy.random`. If the C++ and Python
   white streams differ by one sample, or by the float mapping, the pink
   comparison compares two unrelated signals and fails in a way that looks like a
   coefficient error. Guard: test 14 `REQUIRE`s the white row *before* touching
   the pink row. The uniform mapping in §2.1 is pinned to the digit; changing it
   invalidates every golden row and requires regenerating the fixture.

2. **`Golden.h` parses doubles. A 64-bit checksum cannot survive that.**
   `2⁶⁴ > 2⁵³`. Split every 64-bit quantity into `_hi`/`_lo` 32-bit halves.
   Never write a PCG32 *state* word to the golden at all. The failure mode is a
   silently rounded value that mismatches by a few units in the last place and
   sends the builder hunting a nonexistent LFSR bug.

3. **`buildInverseFilter()` is not real-time safe and must never be called from
   a callback.** It allocates `lengthSamples()` floats and does a full pass. It
   is built on the message thread on parameter change and handed over as a
   buffer. Say it in the header, in the `.cpp`, and in the `app/` wiring when
   that arrives. This is the rule that, broken, produces dropouts at a live show
   — the exact situation this tool exists for.

4. **Statistical tests need a fixed seed and a justified N.** `N = 2²²` samples,
   `K = 511` frames, `fs = 48000`, `fftSize = 16384`, `hop = 8192`. Per-bin
   relative σ = `1/√511` ≈ 4.4 % (±0.19 dB), the slope is fitted over thousands
   of bins and each band sums dozens, the log-estimator bias is
   `−4.34/(2·511) ≈ −0.004 dB`. Runtime bounded under one second. The seed is
   fixed, so a pass is reproducible forever and a failure is a real regression,
   never a flake. **Do not "fix" a failing statistical test by widening the
   tolerance or raising N** — the FEAT numbers (±0.2 dB/oct, ±0.5 dB) are
   binding acceptance criteria, not knobs.

5. **The Kellet ±0.05 dB bound is stated for 44.1 kHz and is not re-derived at
   48 kHz.** The slope test at 48 kHz is the independent check, which is why it
   exists. Do not quote the ±0.05 dB figure as if it were verified here.

6. **The inverse filter's envelope rises +6 dB/oct with frequency while decaying
   in time.** Both phrasings are in the literature and they describe the same
   signal. A builder who "corrects" the sign gets a deconvolution that fails the
   SNR floor for a reason that looks like a windowing problem. The derivation is
   in §2.4; the comment must carry it.

7. **scipy's `max_len_seq` is Fibonacci, ours is Galois.** Same polynomial, same
   sequence set, different phase. `tools/gen_generator.py` must *find* and record
   the cyclic shift and fail loudly if none exists. Do not weaken this to "the
   sequences have the same statistics" — that check passes for a wrong tap table.

8. **1 kHz is never exactly on an FFT bin at 48 kHz with a power-of-two
   transform.** `N/48` must be an integer and no power of two is divisible by 3.
   Case 7 uses `fs = 64000, N = 32768` to put 1 kHz on bin 512 exactly; case 8
   covers 48 kHz with a flat-top window. If someone "simplifies" case 7 back to
   48 kHz with Hann, the amplitude assertion becomes a measurement of scalloping
   loss (up to −1.42 dB) and the ±0.1 dB tolerance fails for a reason that has
   nothing to do with the generator.

9. **Low 1/3-octave bands are under-resolved at small FFT sizes.** At `fftSize
   4096 / 48 kHz` the 50 Hz band spans about one bin. Assert
   `!band.underResolved` for every band the flatness test uses rather than
   assuming the analysis range is safe.

10. **The generator does not clip.** Pink at −12 dBFS RMS with ~12 dB crest will
    touch full scale occasionally. Clipping inside core would break the slope
    test and hide the problem from the operator. Headroom is `app/`'s job.

11. **`RampedGain` lives in `Oscillator.h`** and is the single output-stage ramp,
    not one per source. Do not add a second ramp inside `Noise`, `Sweep` or
    `Mls`; two ramps in series is not a raised cosine and the click comes back.

12. **The FEAT doc's "việc chặn" note is stale** — numpy 2.5.2 and scipy 1.18.1
    are already in `.venv`. Correct it when this work lands (global rule 12: grep
    for statements the phase made false).

---

## 9. Definition of done

- All 15 new files exist at the paths in §1, each under 400 lines, each starting
  with the SPDX line.
- `core/tests/golden/generator.txt` is committed and regenerable by the command
  in §5.
- `ctest --test-dir build -C Release --output-on-failure` passes with **+30**
  cases over the recorded baseline, and the command and its output are pasted.
- `core_has_no_framework_deps` still passes.
- The four FEAT acceptance rows this phase owns — sine 1 kHz, pink 1/3-octave
  flatness, pink slope, sweep deconvolution SNR — each map to exactly one named
  `TEST_CASE` (7, 17, 16, 25) and each of those is green.
- Station 5 (Fable, no write tools) has read the real files and failed to refute
  the claim.
