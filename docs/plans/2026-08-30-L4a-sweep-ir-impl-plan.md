# L4a — sweep deconvolution, impulse response, polarity: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: use `superpowers:subagent-driven-development`
> (recommended) or `superpowers:executing-plans` to implement this plan task by
> task. Steps use checkbox (`- [ ]`) syntax for tracking. Station 5 (adversarial
> verify, reviewer with **no Write tools**) is not optional — see
> `docs/reports/README.md`.

*2026-08-30, lane L4a, station 3. Written from
`docs/dsp/2026-08-30-sweep-ir-l4a.md` (station 2) after reading the real files,
not the record's description of them.*

**Goal:** a captured sweep response becomes an impulse response, a frequency
response, and a polarity verdict — in `core/`, provable on CI with no sound card.

**Architecture:** one new area, `rta::ir`, with three units that each do one
thing: `Deconvolver` (linear convolution against a *supplied* inverse filter),
`IrSpectrum` (transform of the causal region, with the lead-in decision 7
derives), `Polarity` (first arrival, sign, and two figures that let a caller
refuse the answer). `rta::gen::Sweep` gains one Config field and a corrected
clamp. Nothing outside `core/` is touched.

**Tech stack:** C++20, Catch2 v3.16.0, CMake. `rta::dsp::RealFft` is consumed;
no new dependency.

## Global Constraints

- Every source file starts with `// SPDX-License-Identifier: AGPL-3.0-or-later`.
- `core/` must never include JUCE, Qt, or any audio-device API. Enforced by the
  `core_has_no_framework_deps` ctest, which scans **70 files today**; it must
  scan 70 + n after this lane, and that number must be **read, not assumed**
  (HANDOFF trap #1: a mis-globbed guard passes while watching nothing).
- Headers under `core/include/rta/<area>/`, implementation under `core/src/<area>/`.
- **Hard cap 400 lines per file, aim 300.** Applies to headers.
- Comments explain *why a formula is that formula*. A reader must be able to
  check the DSP against a textbook from the comments alone.
- Frequency reads as a whole number of hertz; dB keeps one decimal.
- Never assert a value the implementation produced unless a closed form, a
  published standard, or a golden vector justifies it. A regression lock is
  allowed **only if labelled as one**.
- Build: `cmake --build build --config Release --parallel`, then
  `ctest --test-dir build -C Release --output-on-failure`. Use `--clean-first`
  before any number you intend to report (HANDOFF trap #11).
- The venv is at the **main checkout**, not this worktree:
  `D:\DEV CAVE EP3\PRJ010-RTA-TOOL\.venv\Scripts\python.exe`. Call it by
  absolute path; do not build a second one (trap #8).

---

## What is already true, and must not be re-derived

Read these before writing anything. Two of them are load-bearing:

1. **`core/tests/test_generator_sweep.cpp:87` already contains a working
   deconvolution** — `runSweepDeconvolution()` renders a sweep, convolves a
   3-tap IR, deconvolves with `buildInverseFilter()`, and measures the result.
   `Deconvolver` is that code path lifted into `core/` and given a contract.
   Read it first; do not invent a second one.
2. **The committed golden `sweep_deconv` already asserts `peak_index 96999`**
   for a 2 s / 48 kHz sweep with an impulse at IR sample 1000. That is
   `95999 + 1000` — decision 3's `originIndex = Ninv − 1`, already confirmed by
   an independent NumPy pipeline. **This number must not move in Task 1.** If it
   does, the fade change broke the origin and everything downstream is wrong.
3. `rta::dsp::RealFft` takes a power-of-two size ≥ 4, is single precision, and
   its `inverse()` includes the 1/N. Record decision 8: single precision is not
   the limiting error anywhere this lane can reach.

## Scope

**In:** decisions 1–9 of the record. **Out, deliberately:** ETC, Schroeder,
Lundeby, RT60, clarity (L4b); the draggable gate, min/excess phase, offline WAV
(L4c); STI (L4d, blocked on IEC 60268-16); THD from the harmonic packets
(G3/P4b — this lane only guarantees the packets survive and are locatable).

---

## Task 1: `Sweep`'s fade clamp in octaves, and the golden that moves with it

This task is first because every later measurement depends on the sweep's
behaviour, and it is the only task that changes shipped, tested code.

**Files:**
- Modify: `core/include/rta/gen/Sweep.h` (Config field + accessor)
- Modify: `core/src/gen/Sweep.cpp:63-75` (the clamp)
- Modify: `core/tests/test_generator_sweep.cpp:266-277` (re-derive the clamp test)
- Modify: `tools/gen_generator.py:284-298, 300-321` (independent fade lengths)
- Regenerate: `core/tests/golden/generator.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `Sweep::Config::fadeInOctaves` (double, default `2.0`);
  `Sweep::fadeInOctavesAchieved() const noexcept -> double`;
  `Sweep::validBandLowHz() const noexcept -> double`;
  `Sweep::validBandHighHz() const noexcept -> double`.

### Why this is a defect and not a preference

Record decision 5. The clamp `2/startHz` is expressed in *cycles at f1*, i.e. in
seconds; the quantity that governs the pre-arrival artefact floor is *octaves of
sweep travel*. Measured (`tools/probe_sweep_fade.py`): 0.5 octave gives −75.4 dB,
2 octaves gives −108.7 dB, and the result is independent of sweep duration in
seconds. Under today's clamp a **longer** sweep gets a **narrower** fade in
octaves, so lengthening the sweep makes the band edges worse.

### Step 0 comes first, and without it every later step is a regression

**`buildInverseFilter()` fades the inverse filter twice, and the fade-in clamp
cannot land until it stops.** Record decision 5's blocker section carries the
numbers: with a two-octave fade-in the shipped construction reads 51–72 dB of
in-band unflatness where the intended one reads 0.17–0.27 dB. The second layer
is also costing 13.75 dB at the default configuration **today**.

The strongest argument for removing it is not the measurement. It is that
**two specifications in this repo contradict each other, and the code follows
the older one**:

| | says |
|---|---|
| `docs/plans/2026-08-27-generator-impl-plan.md:317` | "normalised so the envelope's maximum is 1, then Tukey fades at both ends" — **mandates** the second layer |
| `core/include/rta/gen/Sweep.h:44-50` | `inv[m] = x[n] * (instantaneousFrequency(n)/endHz)`, "the already-rendered, **already-faded** sweep, reversed" — **inheritance only** |

So `Sweep.h` is currently a false description of `Sweep.cpp`. Removing the layer
does not change a documented design; it makes the code match the header it
already ships with. (A third divergence sits in the same lines: the old plan
mandates normalising the inverse to unit peak, `tools/gen_generator.py` does it,
and the C++ does not. It cancels in every ratio this lane takes, so it is noted
and not acted on.)

- [x] **Step 0a: Remove the second Tukey layer**

Delete the two loops at `core/src/gen/Sweep.cpp:151-162`. Their stated reason —
"without this the inverse filter starts/ends with a step" — is false in the
ordinary case: `forward[]` is multiplied by `fadeEnvelope`, which is zero at
both ends, and the reversal carries those zeros.

- [x] **Step 0b: Give the fade-out a floor, so the reason stops being true at all**

The layer is load-bearing in exactly one configuration: `fadeOutSec == 0`, where
the *forward* sweep ends with a step. Fix that where it happens rather than
compensating on the kernel. In the constructor, beside the existing
`2/startHz` fade-in floor:

```cpp
    // Two cycles at the END frequency, the mirror of the fade-in's two cycles
    // at the start frequency, and for the same reason: an unfaded switch-off is
    // a broadband click, and after reversal it lands at the very start of the
    // deconvolution kernel where the +6 dB/oct envelope is at its maximum.
    // 5 samples at 20 kHz -- inaudible, spectrally invisible (0.02 octave is
    // already measured harmless), and dormant at every default, where
    // fadeOutSec = 0.02 s gives 960 samples.
    const double fadeOutSec = std::max(config.fadeOutSec, 2.0 / endHz_);
    fadeOutSamples_ = roundToSamples(fadeOutSec, sampleRate_);
```

Four alternatives were weighed and rejected: keeping the layer with its widths
swapped (diverges from every measured number, all of which would need re-taking);
removing it with no floor (leaves a real click at `fadeOutSec == 0`, in the
emitted signal as well as the kernel); fading inside `buildInverseFilter` only
when `fadeOutSamples_ == 0` (special-case logic that patches the kernel and
leaves the loudspeaker clicking); and rejecting `fadeOutSec == 0` outright
(over-strict — zero is a legitimate request under the sweep-to-Nyquist option in
the record's open decisions).

- [x] **Step 0c: Test it with an ASYMMETRIC fade, or the test proves nothing**

**Under a symmetric fade every candidate construction agrees.** That is exactly
why this defect survived: no fixture in the suite uses different fade-in and
fade-out widths. The assertion that has teeth is a spec-level one — that the
inverse filter IS the element-wise inheritance, at a configuration where the two
fades differ:

```cpp
TEST_CASE("The inverse filter is the faded sweep reversed and shaped, and nothing else",
          "[sweep]") {
    Sweep::Config cfg;
    cfg.sampleRate = 48000.0;
    cfg.startHz = 100.0;
    cfg.endHz = 10000.0;
    cfg.durationSec = 2.0;
    cfg.fadeInSec = 0.30;      // ASYMMETRIC on purpose: under equal fades every
    cfg.fadeOutSec = 0.02;     // construction agrees, which is how a second,
    Sweep sweep(cfg);          // undocumented fade layer stayed invisible.

    const auto n = sweep.lengthSamples();
    std::vector<float> forward(n);
    sweep.process(forward);
    const auto inv = sweep.buildInverseFilter();
    REQUIRE(inv.size() == n);

    // Sweep.h specifies inv[m] = x[N-1-m] * (instantaneousFrequency(N-1-m)/endHz)
    // -- the already-faded sweep, reversed, shaped. No further windowing. This
    // locks that as the spec, so any later "helpful" extra layer goes red.
    for (std::size_t m = 0; m < n; ++m) {
        const std::size_t original = n - 1 - m;
        const double expected = static_cast<double>(forward[original])
                              * (sweep.instantaneousFrequency(original) / cfg.endHz);
        CAPTURE(m, original);
        REQUIRE_THAT(static_cast<double>(inv[m]), WithinAbs(expected, 1.0e-6));
    }
}
```

- [x] **Step 0d: Fix the statements this makes false, and the one it makes true**

- `core/include/rta/gen/Sweep.h:71-72` — "`fadeOutSec` ... NOT clamped against
  endHz — only the start fade is mandated a minimum" becomes **false**. Rewrite.
- `core/src/gen/Sweep.cpp:76` — `if (fadeOutSamples_ != 0 && fadeOutSamples_ < 2)`
  becomes **dead**: the floor guarantees at least `2*fs/endHz` samples, which is
  5 at 20 kHz. Remove the `!= 0` half, or say why it stays.
- `docs/plans/2026-08-27-generator-impl-plan.md:317` — **mark superseded, with a
  pointer to this plan and to record decision 5.** This is the highest-value
  line in Step 0. A future session reading that plan will otherwise restore the
  second layer in good faith, exactly as it was added in good faith the first
  time; the code comment even cites "§2.4 of the plan" as its authority.
- `core/include/rta/gen/Sweep.h:44-50` needs no change and becomes **correct**.

- [x] **Step 0e: Now the flatness span can be asserted, and Task 3 unblocks**

With the layer gone, `bandFlatness` over the valid band measures the analysis
pulse rather than the defect. Add to `test_ir_deconvolver.cpp`'s normalisation
case, which currently asserts only that the range is finite:

```cpp
    // REGRESSION LOCK, labelled as one. Record decision 4's table, last row:
    // this fixture measures -0.03..+0.14 dB, a span of 0.17. 0.30 leaves room
    // for the float32 transform without admitting the 10.65 dB a narrow
    // fade-in produces. Could not be asserted before the second Tukey layer
    // was removed, because it measured that layer.
    CHECK(flat.maxDb - flat.minDb < 0.30);
```

- [x] **Step 0f: Full suite, then commit Step 0 on its own**

```
ctest --test-dir build-l2 -C Release --output-on-failure
```

`sweep_deconv`'s `peak_index` must stay **96999** — the closed form `Ninv−1`
does not depend on the fade layer, so any movement means something else broke.
`snr_db` may move; if it does, regenerate in Step 8, not here.

```bash
git add core/include/rta/gen/Sweep.h core/src/gen/Sweep.cpp core/tests/test_generator_sweep.cpp core/tests/test_ir_deconvolver.cpp docs/plans/2026-08-27-generator-impl-plan.md
git commit -m "fix(core): the inverse filter was faded twice, and the header said otherwise"
```

> **The 144.9 dB prediction in Step 8 is only valid after Step 0.** The NumPy
> model that produced it uses the inherited construction, so it describes the
> corrected code, not the code as it stands. Running Step 8 before Step 0 would
> compare against a figure for a different filter.

---

- [x] **Step 1: Write the failing test**

Add to `core/tests/test_generator_sweep.cpp`. The literals are derived, not
observed: `L = 3/ln(100) = 0.651442 s`, `2·ln2·L = 0.903090 s`,
`round(0.903090 × 48000) = 43348`.

```cpp
TEST_CASE("The fade-in clamp is expressed in octaves of sweep travel", "[sweep]") {
    Sweep::Config cfg;
    cfg.sampleRate = 48000.0;
    cfg.startHz = 50.0;
    cfg.endHz = 5000.0;
    cfg.durationSec = 3.0;
    cfg.fadeInSec = 0.001;      // shorter than every floor, so a floor must win

    // Three floors compete: the requested 0.001 s, two cycles at f1 (0.04 s),
    // and fadeInOctaves octaves of travel. L = T/ln(f2/f1) = 0.651442 s, so
    // two octaves is 2*ln2*L = 0.903090 s -- an order of magnitude above the
    // cycles floor, which is exactly the point.
    Sweep sweep(cfg);
    const double lengthL = 3.0 / std::log(100.0);
    const double octaveFloorSec = 2.0 * std::log(2.0) * lengthL;
    CHECK(sweep.fadeInSamples()
          == (std::size_t) std::llround(octaveFloorSec * cfg.sampleRate));
    CHECK_THAT(sweep.fadeInOctavesAchieved(), WithinRel(2.0, 1e-12));

    // The cycles floor still governs when it is the larger of the two: a very
    // short sweep travels its two octaves faster than two cycles at f1 take.
    auto shortCfg = cfg;
    shortCfg.durationSec = 0.05;          // L = 0.010857 s, 2 octaves = 0.015 s
    shortCfg.fadeInOctaves = 0.5;         // ... but half an octave is 0.00376 s
    Sweep shortSweep(shortCfg);
    CHECK(shortSweep.fadeInSamples()
          == (std::size_t) std::llround((2.0 / shortCfg.startHz) * shortCfg.sampleRate));

    // Setting it to zero restores the pre-2026-08-30 behaviour exactly, which
    // is what makes the trade-off in record decision 5 selectable rather than
    // imposed.
    auto legacyCfg = cfg;
    legacyCfg.fadeInOctaves = 0.0;
    Sweep legacy(legacyCfg);
    CHECK(legacy.fadeInSamples()
          == (std::size_t) std::llround((2.0 / cfg.startHz) * cfg.sampleRate));
}

TEST_CASE("The valid band's edges follow from the fades", "[sweep]") {
    Sweep::Config cfg;
    cfg.sampleRate = 48000.0;
    cfg.startHz = 100.0;
    cfg.endHz = 10000.0;
    cfg.durationSec = 2.0;
    Sweep sweep(cfg);

    // f_lo = f1*exp(fadeInSec/L). When the octave clamp binds, fadeInSec is
    // exactly octaves*ln2*L, so the exponential collapses to f1 * 2^octaves --
    // the lower band edge IS the knob. 100 Hz * 2^2 = 400 Hz.
    CHECK_THAT(sweep.validBandLowHz(), WithinRel(400.0, 1e-9));

    // f_hi = f2*exp(-fadeOutSec/L); L = 2/ln(100) = 0.434294 s, fadeOut 0.02 s.
    const double lengthL = 2.0 / std::log(100.0);
    CHECK_THAT(sweep.validBandHighHz(),
               WithinRel(10000.0 * std::exp(-0.02 / lengthL), 1e-9));
}
```

- [x] **Step 2: Run to verify it fails**

```
ctest --test-dir build -C Release -R rta_core_tests --output-on-failure
```
Expected: compile error, `'fadeInOctaves': is not a member of 'Sweep::Config'`.

- [x] **Step 3: Add the Config field and accessors**

In `core/include/rta/gen/Sweep.h`, inside `struct Config`, after `fadeOutSec`:

```cpp
        /// Minimum fade-in width expressed in OCTAVES of sweep travel, which is
        /// the unit that governs the deconvolution's pre-arrival artefact floor
        /// -- not seconds. Measured (tools/probe_sweep_fade.py, and the table in
        /// docs/dsp/2026-08-30-sweep-ir-l4a.md decision 5): 0.5 octave gives a
        /// -75.4 dB floor, 2 octaves gives -108.7 dB, and the figure does not
        /// depend on the sweep's duration in seconds. The old `2/startHz` floor
        /// below is a CYCLES rule and therefore shrinks, in octaves, as the
        /// sweep lengthens -- so under it a longer sweep measured worse.
        ///
        /// This is a trade-off the caller owns, not a setting with one right
        /// value. A wide fade-in tapers the bottom of the sweep: at 2 octaves
        /// from 20 Hz everything below 80 Hz is amplitude-shaped, costing
        /// signal-to-noise where room modes live. The way to have both is to
        /// set `startHz` two octaves BELOW the band of interest -- 5 Hz for a
        /// 20 Hz band -- which costs sweep duration instead of low-frequency
        /// energy. Set to 0.0 to restore the pre-2026-08-30 behaviour exactly.
        double fadeInOctaves = 2.0;
```

Then, with the other accessors:

```cpp
    /// The fade-in width actually achieved, in octaves of sweep travel, after
    /// all three floors have competed. Exposed for the same reason
    /// fadeInSamples() is: a clamp nobody can observe is a clamp nobody can
    /// check -- and this is the number that predicts the artefact floor.
    [[nodiscard]] double fadeInOctavesAchieved() const noexcept;

    /// Lower edge of the band in which this sweep's deconvolution is
    /// meaningful: f1 * exp(fadeInSec/L), i.e. the frequency the sweep had
    /// reached when the fade-in finished. Record decision 4.
    [[nodiscard]] double validBandLowHz() const noexcept;

    /// Upper edge: f2 * exp(-fadeOutSec/L). Note the fade-OUT is not clamped in
    /// octaves and must not be -- measured, a WIDER fade-out makes the artefact
    /// floor worse by about 3.5 dB per octave, the opposite of the fade-in.
    [[nodiscard]] double validBandHighHz() const noexcept;
```

- [x] **Step 4: Implement the clamp**

In `core/src/gen/Sweep.cpp`, replace the two-floor clamp at lines 63-75:

```cpp
    // Three floors compete and the widest wins.
    //
    //   fadeInSec               what the caller asked for
    //   2 / startHz             two cycles at f1 -- an unfaded start is a
    //                           broadband click landing in the exact band the
    //                           sweep exists to measure
    //   fadeInOctaves*ln2*L     the width that actually governs the
    //                           deconvolution's pre-arrival artefact floor
    //
    // The third is not a refinement of the second; they are floors in different
    // units and neither implies the other. For a 10 s sweep from 20 Hz the
    // cycles floor is 0.1 s and the octave floor is 2.007 s; for a 0.05 s sweep
    // the cycles floor is the larger. Both are kept.
    const double octaveFloorSec = config.fadeInOctaves > 0.0
        ? config.fadeInOctaves * std::log(2.0) * lengthL_
        : 0.0;
    const double fadeInSec = std::max({config.fadeInSec, 2.0 / startHz_, octaveFloorSec});
    fadeInSamples_ = roundToSamples(fadeInSec, sampleRate_);
    fadeOutSamples_ = roundToSamples(config.fadeOutSec, sampleRate_);
    fadeOutSec_ = config.fadeOutSec;   // new member, for validBandHighHz()

    if (fadeInSamples_ < 2) fadeInSamples_ = 2;
    if (fadeOutSamples_ != 0 && fadeOutSamples_ < 2) fadeOutSamples_ = 2;
```

Add `#include <algorithm>` if absent (for `std::max` with an initializer list),
a `double fadeOutSec_;` member, and the three accessors:

```cpp
double Sweep::fadeInOctavesAchieved() const noexcept {
    // Invert fadeInSec = octaves*ln2*L. Uses the SAMPLE count actually stored,
    // not the requested seconds, so rounding is included rather than assumed.
    const double seconds = static_cast<double>(fadeInSamples_) / sampleRate_;
    return seconds / (std::log(2.0) * lengthL_);
}

double Sweep::validBandLowHz() const noexcept {
    const double seconds = static_cast<double>(fadeInSamples_) / sampleRate_;
    return startHz_ * std::exp(seconds / lengthL_);
}

double Sweep::validBandHighHz() const noexcept {
    return endHz_ * std::exp(-fadeOutSec_ / lengthL_);
}
```

- [x] **Step 5: Run — the new tests pass and an OLD test now fails**

```
ctest --test-dir build -C Release -R rta_core_tests --output-on-failure
```
Expected: the two new cases PASS, and
`"The fades are raised-cosine and at least two cycles at f1"` FAILS at
`CHECK(sweep.fadeInSamples() == expectedClamp)` — it expects 1920, it gets
43348. **This failure is correct.** Do not adjust the number until it goes
green: re-derive it.

- [x] **Step 6: Re-derive the old test rather than patch it**

That test now asserts something the code deliberately no longer does. Its
*shape* check is still valuable and unaffected (it uses `fadeInSec = 1.0`, which
still wins against the 0.903 s octave floor). Change only the clamp assertion
and rename the case so the title stops claiming a rule that has been superseded:

```cpp
TEST_CASE("The fade is raised-cosine, and the cycles floor still applies", "[sweep]") {
    Sweep::Config cfg;
    cfg.sampleRate = 48000.0;
    cfg.startHz = 50.0;
    cfg.endHz = 5000.0;
    cfg.durationSec = 3.0;
    cfg.levelDbFsPeak = -6.0;
    cfg.fadeInSec = 0.001;
    cfg.fadeInOctaves = 0.0;    // isolate the cycles floor being tested here
    Sweep sweep(cfg);

    const auto expectedClamp = (std::size_t) std::llround((2.0 / cfg.startHz) * cfg.sampleRate);
    CHECK(sweep.fadeInSamples() == expectedClamp);
    // ... shape check below is unchanged; shapeCfg.fadeInSec = 1.0 still wins.
```

Set `shapeCfg.fadeInOctaves = 0.0` too, so the shape check measures the fade it
names.

- [x] **Step 7: Fix the golden generator's fade, which fades both ends equally**

`tools/gen_generator.py:284` takes ONE `fade_len` and applies it to both ends.
`core` has always had independent `fadeInSamples_` and `fadeOutSamples_`. The
two agree today only because `2/f1 = 0.02 s` happens to equal the default
`fadeOutSec` at the golden's configuration — a coincidence, not an invariant.
Measured, once the fade-in widens, the symmetric Python fade diverges from core
by **−3.49 dB at 1 octave**, outside the golden's ±2 dB tolerance.

```python
def raised_cosine_fade(x: np.ndarray, fade_in_len: int, fade_out_len: int) -> np.ndarray:
    """Independent fade lengths, matching core's Sweep exactly.

    A single length for both ends was wrong from the start and merely invisible:
    it coincided with core only while 2/f1 equalled the default fadeOutSec. It
    matters now because a wide fade-OUT makes the deconvolution's artefact floor
    WORSE (about 3.5 dB per octave) while a wide fade-in makes it better, so the
    two ends cannot share a number.
    """
    window = np.ones(len(x))
    fade_in_len = min(fade_in_len, len(x) // 2)
    fade_out_len = min(fade_out_len, len(x) // 2)
    if fade_in_len > 1:
        # Divide by (len - 1), not len: the ramp must land EXACTLY on unity.
        window[:fade_in_len] *= 0.5 * (
            1.0 - np.cos(np.pi * np.arange(fade_in_len) / (fade_in_len - 1)))
    if fade_out_len > 1:
        ramp = 0.5 * (1.0 - np.cos(np.pi * np.arange(fade_out_len) / (fade_out_len - 1)))
        window[-fade_out_len:] *= ramp[::-1]
    return x * window
```

And in `render_sweep_and_inverse`, replace the single `fade_len`:

```python
    # Three floors, widest wins -- transcribed from core/src/gen/Sweep.cpp.
    # fadeInOctaves defaults to 2.0 there, so it must default to 2.0 here.
    fade_in_len = int(round(max(0.02, 2.0 / f1, 2.0 * np.log(2.0) * length_l) * fs))
    fade_out_len = int(round(0.02 * fs))
    sweep = raised_cosine_fade(raw.copy(), fade_in_len, fade_out_len)
    envelope = raw * (instantaneous_freq / f2)
    inverse = envelope[::-1].copy()
    inverse /= np.max(np.abs(inverse))
    inverse = raised_cosine_fade(inverse, fade_in_len, fade_out_len)
```

- [x] **Step 8: Regenerate the goldens and READ the diff**

```
"D:/DEV CAVE EP3/PRJ010-RTA-TOOL/.venv/Scripts/python.exe" tools/gen_generator.py
```

Then `git diff core/tests/golden/generator.txt`. Exactly two things may change,
and one thing must not:

| field | before | after | |
|---|---|---|---|
| `snr_db` | 86.877 | **≈ 144.9** | a ~58 dB improvement — this IS decision 5 |
| `peak_index` | 96999 | **96999** | **must not move.** If it does, stop. |
| `L`, `K`, `phase` | — | unchanged | the fade touches no closed form |

The predicted 144.9 dB comes from a NumPy replication that reproduced the
committed 86.877 dB to 0.09 dB before the change, so treat a result more than
2 dB from 144.9 as a signal that something else moved too.

- [x] **Step 9: Full suite, clean**

```
cmake --build build --config Release --parallel --clean-first
```
```
ctest --test-dir build -C Release --output-on-failure
```
Expected: all green. Record the total; it is the baseline for Task 6.

- [x] **Step 10: Commit**

```bash
git add core/include/rta/gen/Sweep.h core/src/gen/Sweep.cpp core/tests/test_generator_sweep.cpp tools/gen_generator.py core/tests/golden/generator.txt
git commit -m "fix(core): the fade-in clamp was in seconds; the artefact floor is in octaves"
```

### An owner decision that lives in this task, and must not be taken silently

Novak's **synchronized** swept-sine requires `f1·L` to be an integer, i.e.
`L = round(T̂·f1/ln(f2/f1))/f1`. Without it, every harmonic packet carries a
frequency-dependent phase rotation: amplitude THD survives, **phase-accurate
per-harmonic analysis does not** (record decision 3's qualifier).

Adding it is one `round()` here and one regeneration of the sweep goldens, and
it moves `durationSec` by under 1%. Not adding it means G3/P4b can only ever do
amplitude THD unless every stored capture is retaken.

**Do not decide this inside the task.** It is listed under "What this record
does not decide". If the owner says yes, it belongs in *this* commit — the
goldens are already moving — and the plan gains a step asserting `f1·L` is
integral to within floating-point tolerance. If the owner says no, note the
refusal in the commit body so the next session does not re-open it from scratch.

---

## Task 2: `rta::ir::Deconvolver` — the linear convolution and its origin

**Files:**
- Create: `core/include/rta/ir/Deconvolver.h`
- Create: `core/src/ir/Deconvolver.cpp`
- Create: `core/tests/test_ir_deconvolver.cpp`
- Modify: `core/CMakeLists.txt` (add `src/ir/Deconvolver.cpp`)
- Modify: `core/tests/CMakeLists.txt` (add `test_ir_deconvolver.cpp`)

**Interfaces:**
- Consumes: `Sweep::buildInverseFilter()`, `Sweep::lengthConstantL()` (Task 1),
  `rta::dsp::RealFft`.
- Produces:
  ```cpp
  namespace rta::ir {
  struct Deconvolution {
      std::vector<float> samples;
      std::size_t        originIndex      = 0;
      double             sampleRate       = 0.0;
      double             normalisationGain = 1.0;
      double             harmonicSpacingL = 0.0;
      [[nodiscard]] double harmonicOffsetSamples(int order) const noexcept;
  };
  struct DeconvolverConfig {
      double sampleRate = 0.0;
      double harmonicSpacingL = 0.0;
      double normalisationGain = 1.0;
  };
  [[nodiscard]] Deconvolution deconvolve(std::span<const float> response,
                                         std::span<const float> inverseFilter,
                                         const DeconvolverConfig& config);
  }
  ```

- [ ] **Step 1: Write the failing test**

Create `core/tests/test_ir_deconvolver.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L4a: linear deconvolution of a swept-sine response.
// Decisions 1, 2 and 3 of docs/dsp/2026-08-30-sweep-ir-l4a.md.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/gen/Sweep.h"
#include "rta/ir/Deconvolver.h"

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using rta::gen::Sweep;

namespace {
Sweep::Config testConfig() {
    Sweep::Config cfg;
    cfg.sampleRate = 48000.0;
    cfg.startHz = 100.0;
    cfg.endHz = 10000.0;
    cfg.durationSec = 2.0;
    return cfg;
}
}  // namespace

TEST_CASE("The analysis pulse peaks at Ninv-1, positive", "[ir][deconv]") {
    Sweep sweep(testConfig());
    std::vector<float> excitation(sweep.lengthSamples());
    sweep.process(excitation);
    const auto inverse = sweep.buildInverseFilter();

    const auto result = rta::ir::deconvolve(
        excitation, inverse,
        {sweep.lengthSamples() > 0 ? 48000.0 : 0.0, sweep.lengthConstantL(), 1.0});

    // Closed form: inv[m] = s[Ns-1-m]*e[m], so at lag k = Ns-1 every term of
    // the convolution sum becomes s[j]^2 * e[Ns-1-j] -- all non-negative, the
    // one lag at which the sum adds coherently. Hence the peak sits at Ninv-1
    // and its sign is POSITIVE, which is what makes polarity readable at all.
    REQUIRE(result.originIndex == inverse.size() - 1);
    std::size_t peak = 0;
    for (std::size_t i = 0; i < result.samples.size(); ++i)
        if (std::abs(result.samples[i]) > std::abs(result.samples[peak])) peak = i;
    CHECK(peak == result.originIndex);
    CHECK(result.samples[peak] > 0.0f);

    // Length is the full linear convolution: nothing is chopped, because the
    // negative-time region IS the distortion measurement lane G3 will read.
    CHECK(result.samples.size() == excitation.size() + inverse.size() - 1);
}

TEST_CASE("A known response returns at the right index with the right gain", "[ir][deconv]") {
    Sweep sweep(testConfig());
    std::vector<float> excitation(sweep.lengthSamples());
    sweep.process(excitation);
    const auto inverse = sweep.buildInverseFilter();

    constexpr std::size_t kDelay = 137;
    constexpr float kGain = 0.8f;
    std::vector<float> response(excitation.size() + kDelay, 0.0f);
    for (std::size_t i = 0; i < excitation.size(); ++i)
        response[i + kDelay] = kGain * excitation[i];

    const auto reference = rta::ir::deconvolve(excitation, inverse,
                                               {48000.0, sweep.lengthConstantL(), 1.0});
    const auto measured  = rta::ir::deconvolve(response, inverse,
                                               {48000.0, sweep.lengthConstantL(), 1.0});

    std::size_t peak = 0;
    for (std::size_t i = 0; i < measured.samples.size(); ++i)
        if (std::abs(measured.samples[i]) > std::abs(measured.samples[peak])) peak = i;
    CHECK(peak == measured.originIndex + kDelay);
    CHECK_THAT((double) measured.samples[peak] / (double) reference.samples[reference.originIndex],
               WithinRel((double) kGain, 1e-3));
}

TEST_CASE("Harmonic packets land at -L*ln(N)", "[ir][deconv]") {
    Sweep sweep(testConfig());
    std::vector<float> excitation(sweep.lengthSamples());
    sweep.process(excitation);
    const auto inverse = sweep.buildInverseFilter();

    // A memoryless square law produces H2 and nothing else of interest; a cubic
    // produces H3. Two orders, so the LOGARITHMIC spacing is exercised rather
    // than a single offset that any monotone law would have fitted.
    for (int order : {2, 3}) {
        std::vector<float> distorted(excitation.size());
        for (std::size_t i = 0; i < excitation.size(); ++i) {
            const float x = excitation[i];
            distorted[i] = x + 0.10f * (order == 2 ? x * x : x * x * x);
        }
        const auto result = rta::ir::deconvolve(distorted, inverse,
                                                {48000.0, sweep.lengthConstantL(), 1.0});

        const double expected = (double) result.originIndex
                              + result.harmonicOffsetSamples(order);
        // Search strictly between this packet and the next lower one, so H3
        // cannot re-find H2.
        const auto guard = (std::size_t) std::llround(0.005 * 48000.0);
        const auto hi = (std::size_t) (expected + guard);
        const auto lo = (std::size_t) ((double) result.originIndex
                                       + result.harmonicOffsetSamples(order + 1) + guard);
        std::size_t found = lo;
        for (std::size_t i = lo; i < hi; ++i)
            if (std::abs(result.samples[i]) > std::abs(result.samples[found])) found = i;

        CAPTURE(order, expected, found);
        CHECK_THAT((double) found, WithinAbs(expected, 1.0));
    }
}

TEST_CASE("Deconvolve refuses input it cannot interpret", "[ir][deconv]") {
    const std::vector<float> ok(1024, 0.1f);
    CHECK_THROWS_AS(rta::ir::deconvolve(ok, {}, {48000.0, 0.4, 1.0}),
                    std::invalid_argument);
    CHECK_THROWS_AS(rta::ir::deconvolve({}, ok, {48000.0, 0.4, 1.0}),
                    std::invalid_argument);
    CHECK_THROWS_AS(rta::ir::deconvolve(ok, ok, {0.0, 0.4, 1.0}),
                    std::invalid_argument);
    // A response SHORTER than the inverse filter cannot contain a full sweep,
    // so the origin would fall outside the data. Refuse rather than return a
    // structure whose originIndex indexes nothing.
    const std::vector<float> tooShort(64, 0.1f);
    CHECK_THROWS_AS(rta::ir::deconvolve(tooShort, ok, {48000.0, 0.4, 1.0}),
                    std::invalid_argument);
}
```

- [ ] **Step 2: Run to verify it fails**

Register the test file first (`core/tests/CMakeLists.txt`, add
`test_ir_deconvolver.cpp` to `add_executable(rta_core_tests ...)`), then:

```
cmake --build build --config Release --parallel
```
Expected: `Cannot open include file: 'rta/ir/Deconvolver.h'`.

- [ ] **Step 3: Write the header**

`core/include/rta/ir/Deconvolver.h`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace rta::ir {

/// The result of deconvolving a captured sweep response.
///
/// ## Nothing is removed, and that is deliberate
///
/// Müller & Massarani advise chopping the negative-time half away. That is
/// right for room acoustics and wrong here: for this project that region IS the
/// distortion measurement. The Nth harmonic of an exponential sweep has, at
/// time t, the instantaneous frequency the fundamental had at t + L*ln(N);
/// deconvolution maps the fundamental at t onto zero, so it maps that harmonic
/// onto -L*ln(N). Discarding the region would make a later lane re-measure what
/// was already captured.
struct Deconvolution {
    /// The whole linear convolution, length `response + inverse - 1`.
    std::vector<float> samples;

    /// Index at which t = 0 sits: `inverseFilter.size() - 1`. Everything below
    /// it is negative time. See the derivation in the .cpp.
    std::size_t originIndex = 0;

    double sampleRate = 0.0;

    /// The scalar already applied to `samples`. Carried so a reader can undo it.
    double normalisationGain = 1.0;

    /// L = T / ln(f2/f1) for the sweep that produced this, in seconds. Carried
    /// so a later lane can locate the harmonic packets without needing the
    /// `Sweep` object -- which by then may not exist.
    double harmonicSpacingL = 0.0;

    /// Offset in samples from `originIndex` to the Nth harmonic packet:
    /// -L*ln(N)*fs, hence NEGATIVE for every order >= 2. Returns 0 for order 1
    /// (the fundamental is the origin) and for orders below 1.
    [[nodiscard]] double harmonicOffsetSamples(int order) const noexcept;
};

struct DeconvolverConfig {
    double sampleRate = 0.0;
    double harmonicSpacingL = 0.0;
    /// Applied to every output sample. Compute it with `inBandNormalisation`
    /// (Task 3) from a reference deconvolution; 1.0 leaves the result raw.
    double normalisationGain = 1.0;
};

/// Linear (non-cyclic) deconvolution: `response` convolved with `inverseFilter`,
/// zero-padded so no wrap-around occurs.
///
/// **`inverseFilter` is an argument, never constructed here.** Today the caller
/// passes `rta::gen::Sweep::buildInverseFilter()`. Müller prefers inverting a
/// *measured* loopback reference instead, which needs hardware this project
/// does not yet have; when it does, adopting the better method must be a change
/// of argument, not a rewrite of this function. That is why this file does not
/// include `rta/gen/Sweep.h` and must not start.
///
/// NOT real-time safe: allocates, and is O(N log N) over the whole capture.
///
/// Throws `std::invalid_argument` if either span is empty, if `sampleRate` is
/// not positive, or if `response` is shorter than `inverseFilter` (the origin
/// would then fall outside the data).
[[nodiscard]] Deconvolution deconvolve(std::span<const float> response,
                                       std::span<const float> inverseFilter,
                                       const DeconvolverConfig& config);

}  // namespace rta::ir
```

- [ ] **Step 4: Write the implementation**

`core/src/ir/Deconvolver.cpp`. The FFT size is `nextPow2(Ny + Ninv - 1)`; at a
10 s sweep and a 12 s capture that is 2^21, about 8 MB of `RealFft` tables —
acceptable offline, and the reason this must never touch the audio thread.

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/ir/Deconvolver.h"

#include "rta/dsp/RealFft.h"

#include <cmath>
#include <complex>
#include <stdexcept>

namespace rta::ir {
namespace {

std::size_t nextPowerOfTwo(std::size_t n) {
    std::size_t size = 4;               // RealFft's minimum
    while (size < n) size <<= 1;
    return size;
}

}  // namespace

double Deconvolution::harmonicOffsetSamples(int order) const noexcept {
    if (order <= 1) return 0.0;
    return -harmonicSpacingL * std::log(static_cast<double>(order)) * sampleRate;
}

Deconvolution deconvolve(std::span<const float> response,
                         std::span<const float> inverseFilter,
                         const DeconvolverConfig& config) {
    if (inverseFilter.empty())
        throw std::invalid_argument("deconvolve: inverseFilter is empty");
    if (response.empty())
        throw std::invalid_argument("deconvolve: response is empty");
    if (!(config.sampleRate > 0.0))
        throw std::invalid_argument("deconvolve: sampleRate must be positive");
    if (response.size() < inverseFilter.size())
        throw std::invalid_argument(
            "deconvolve: response is shorter than the inverse filter, so t=0 "
            "would fall outside the result");

    const std::size_t linearLength = response.size() + inverseFilter.size() - 1;
    const std::size_t fftSize = nextPowerOfTwo(linearLength);

    dsp::RealFft fft(fftSize);
    std::vector<float> padded(fftSize, 0.0f);
    std::vector<std::complex<float>> a(fft.numBins()), b(fft.numBins());

    std::copy(response.begin(), response.end(), padded.begin());
    fft.forward(padded, a);

    std::fill(padded.begin(), padded.end(), 0.0f);
    std::copy(inverseFilter.begin(), inverseFilter.end(), padded.begin());
    fft.forward(padded, b);

    for (std::size_t k = 0; k < a.size(); ++k) a[k] *= b[k];
    fft.inverse(a, padded);

    Deconvolution out;
    out.samples.assign(padded.begin(), padded.begin() + linearLength);
    if (config.normalisationGain != 1.0) {
        const float gain = static_cast<float>(config.normalisationGain);
        for (auto& sample : out.samples) sample *= gain;
    }
    // inv[m] = s[Ns-1-m]*e[m], so lag Ns-1 is the one at which every term of
    // the convolution sum becomes s[j]^2*e[Ns-1-j] -- all non-negative, adding
    // coherently. Ninv == Ns, hence Ninv-1.
    out.originIndex = inverseFilter.size() - 1;
    out.sampleRate = config.sampleRate;
    out.normalisationGain = config.normalisationGain;
    out.harmonicSpacingL = config.harmonicSpacingL;
    return out;
}

}  // namespace rta::ir
```

Add `src/ir/Deconvolver.cpp` to `core/CMakeLists.txt`, keeping the list
alphabetical (after the `src/gen/*` block).

- [ ] **Step 5: Run to verify it passes**

```
ctest --test-dir build -C Release -R "ir|deconv" --output-on-failure
```
Expected: four cases PASS. If `ctest -R` matches zero tests it reports success —
HANDOFF trap "xanh mà không chứng minh gì" #2. **Read the count in the output**,
do not trust the exit code.

- [ ] **Step 6: Commit**

```bash
git add core/include/rta/ir/Deconvolver.h core/src/ir/Deconvolver.cpp core/tests/test_ir_deconvolver.cpp core/CMakeLists.txt core/tests/CMakeLists.txt
git commit -m "feat(core): a linear deconvolution that keeps its negative time"
```

---

## Task 3: normalisation, the valid band, and the flatness nobody may assume

**Files:**
- Modify: `core/include/rta/ir/Deconvolver.h` (two free functions)
- Modify: `core/src/ir/Deconvolver.cpp`
- Modify: `core/tests/test_ir_deconvolver.cpp`

**Interfaces:**
- Consumes: `Deconvolution` (Task 2); `Sweep::validBandLowHz/HighHz` (Task 1).
- Produces:
  ```cpp
  struct BandFlatness { double minDb; double maxDb; };
  [[nodiscard]] double inBandNormalisation(const Deconvolution& reference,
                                           double lowHz, double highHz);
  [[nodiscard]] BandFlatness bandFlatness(const Deconvolution& reference,
                                          double lowHz, double highHz);
  ```

### Why flatness is returned rather than promised

Record decision 4, corrected. "A unity path reads 0 dB flat in band" is false at
the fade widths this repo shipped: measured deviation was **−7.06 … +2.40 dB**.
It is ±0.03 dB only once decision 5's fade widths are in force. So the band
formula says where the answer is *meaningful*; a caller who needs to know how
flat must be able to ask.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("Normalisation makes a unity path read 0 dB across the valid band", "[ir][deconv]") {
    Sweep sweep(testConfig());          // fadeInOctaves defaults to 2.0
    std::vector<float> excitation(sweep.lengthSamples());
    sweep.process(excitation);
    const auto inverse = sweep.buildInverseFilter();

    const auto reference = rta::ir::deconvolve(excitation, inverse,
                                               {48000.0, sweep.lengthConstantL(), 1.0});
    const double lowHz = sweep.validBandLowHz();     // 400 Hz for this config
    const double highHz = sweep.validBandHighHz();
    const double gain = rta::ir::inBandNormalisation(reference, lowHz, highHz);

    const auto normalised = rta::ir::deconvolve(excitation, inverse,
                                                {48000.0, sweep.lengthConstantL(), gain});
    const auto flat = rta::ir::bandFlatness(normalised, lowHz, highHz);

    // Mean level across the band is 0 dB by construction -- that is what the
    // scalar is FOR, so this asserts the scalar was applied, nothing deeper.
    CHECK_THAT(0.5 * (flat.minDb + flat.maxDb), WithinAbs(0.0, 0.2));

    // REGRESSION LOCK, labelled as one. The record's flatness table has a row
    // for THIS fixture -- 100 Hz-10 kHz, 2 s, fades 2.00/0.07 oct, band
    // 400-9549.9 Hz -- measured at -0.03..+0.14 dB, a span of 0.17 dB. 0.30
    // leaves room for the float32 transform without admitting the 1.4 dB a
    // narrow fade-in produces. It locks an empirical table, not a closed form;
    // tools/verify_l4a_pulse.py section C re-derives the table.
    CHECK(flat.maxDb - flat.minDb < 0.30);
}

TEST_CASE("A narrow fade-in is measurably less flat", "[ir][deconv]") {
    // The falsifier for the test above: if bandFlatness returned a constant, or
    // measured the wrong thing, this case would not separate from it.
    auto cfg = testConfig();
    cfg.fadeInOctaves = 0.0;
    Sweep narrow(cfg);
    std::vector<float> excitation(narrow.lengthSamples());
    narrow.process(excitation);
    const auto inverse = narrow.buildInverseFilter();
    const auto reference = rta::ir::deconvolve(excitation, inverse,
                                               {48000.0, narrow.lengthConstantL(), 1.0});
    const auto flat = rta::ir::bandFlatness(reference,
                                            narrow.validBandLowHz(),
                                            narrow.validBandHighHz());
    CHECK(flat.maxDb - flat.minDb > 1.0);
}

TEST_CASE("A capture truncated inside the decay is visibly wrong", "[ir][deconv]") {
    // Record decision 9: the capture must outlast the sweep by 1.5x RT60.
    // Measured: at 0.5x the worst bin is still out by 4.61 dB while the rms has
    // already fallen to 0.07 dB -- so this asserts the WORST bin, and asserts
    // the bad case is bad as well as the good case being good.
    Sweep sweep(testConfig());
    std::vector<float> excitation(sweep.lengthSamples());
    sweep.process(excitation);
    const auto inverse = sweep.buildInverseFilter();

    // A room with an exactly known decay: a one-pole exponential, RT60 = 0.5 s.
    constexpr double kRt60 = 0.5;
    const auto irLength = (std::size_t) std::llround(2.0 * kRt60 * 48000.0);
    std::vector<float> room(irLength, 0.0f);
    room[0] = 1.0f;
    for (std::size_t i = 1; i < irLength; ++i)
        room[i] = 0.25f * (float) std::pow(10.0, -3.0 * (double) i / (kRt60 * 48000.0))
                        * ((i % 97 == 0) ? 1.0f : 0.0f);   // sparse, so the tail is checkable

    // Full capture and a capture cut at 0.5 x RT60 after the sweep ends.
    std::vector<float> full(excitation.size() + irLength - 1, 0.0f);
    for (std::size_t i = 0; i < excitation.size(); ++i)
        for (std::size_t j = 0; j < irLength; ++j)
            full[i + j] += excitation[i] * room[j];       // O(N*M): keep irLength small

    const auto shortLen = excitation.size() + (std::size_t) llround(0.5 * kRt60 * 48000.0);
    std::span<const float> truncated(full.data(), shortLen);

    const auto whole = rta::ir::deconvolve(full, inverse, {48000.0, sweep.lengthConstantL(), 1.0});
    const auto cut   = rta::ir::deconvolve(truncated, inverse, {48000.0, sweep.lengthConstantL(), 1.0});

    double worstDb = 0.0;
    for (std::size_t i = 0; i < irLength; ++i) {
        const double a = std::abs((double) whole.samples[whole.originIndex + i]);
        const double b = std::abs((double) cut.samples[cut.originIndex + i]);
        if (a > 1e-6) worstDb = std::max(worstDb, std::abs(20.0 * std::log10(b / a)));
    }
    CHECK(worstDb > 1.0);   // truncation is NOT harmless, and the suite says so
}
```

> **Note for the implementer:** the direct convolution in the last case is
> O(N·M). Keep `kRt60` at 0.5 s so `irLength` is 48000 and the sweep is 96000 —
> about 4.6e9 multiply-adds is too slow. **Use `deconvolve`'s own FFT path
> instead**: build the room response by calling `deconvolve(excitation, room, …)`
> is *not* the same operation, so add a small file-local `convolveDirect` that
> skips zero taps (the room above is sparse: only every 97th sample is non-zero,
> about 495 taps, so the loop is ~4.8e7 operations and runs in well under a
> second). Guard the inner loop with `if (room[j] == 0.0f) continue;`.

- [ ] **Step 2: Run to verify it fails**

```
cmake --build build --config Release --parallel
```
Expected: `'inBandNormalisation': identifier not found`.

- [ ] **Step 3: Implement both functions**

Append to `core/src/ir/Deconvolver.cpp`, and declare in the header:

```cpp
namespace {

/// Magnitudes of a Deconvolution's spectrum, plus the bin spacing.
struct Magnitudes {
    std::vector<float> values;
    double binHz;
};

Magnitudes magnitudesOf(const Deconvolution& d) {
    const std::size_t fftSize = nextPowerOfTwo(d.samples.size());
    dsp::RealFft fft(fftSize);
    std::vector<float> padded(fftSize, 0.0f);
    std::copy(d.samples.begin(), d.samples.end(), padded.begin());
    std::vector<std::complex<float>> bins(fft.numBins());
    fft.forward(padded, bins);

    Magnitudes out;
    out.values.resize(bins.size());
    for (std::size_t k = 0; k < bins.size(); ++k) out.values[k] = std::abs(bins[k]);
    out.binHz = d.sampleRate / static_cast<double>(fftSize);
    return out;
}

void requireBand(const Deconvolution& d, double lowHz, double highHz) {
    if (!(d.sampleRate > 0.0))
        throw std::invalid_argument("band query: sampleRate must be positive");
    if (!(lowHz > 0.0) || !(highHz > lowHz))
        throw std::invalid_argument("band query: need 0 < lowHz < highHz");
}

}  // namespace

double inBandNormalisation(const Deconvolution& reference, double lowHz, double highHz) {
    requireBand(reference, lowHz, highHz);
    const auto mag = magnitudesOf(reference);
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t k = 0; k < mag.values.size(); ++k) {
        const double hz = static_cast<double>(k) * mag.binHz;
        if (hz < lowHz || hz > highHz) continue;
        sum += mag.values[k];
        ++count;
    }
    if (count == 0)
        throw std::invalid_argument("inBandNormalisation: no bins inside the band");
    // The MEAN magnitude, not the peak. The peak height depends on band-edge
    // phase as well as in-band gain, so normalising by it would make "0 dB"
    // approximately rather than derivably true, by an amount no test could state.
    return static_cast<double>(count) / sum;
}

BandFlatness bandFlatness(const Deconvolution& reference, double lowHz, double highHz) {
    requireBand(reference, lowHz, highHz);
    const auto mag = magnitudesOf(reference);
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t k = 0; k < mag.values.size(); ++k) {
        const double hz = static_cast<double>(k) * mag.binHz;
        if (hz < lowHz || hz > highHz) continue;
        sum += mag.values[k];
        ++count;
    }
    if (count == 0)
        throw std::invalid_argument("bandFlatness: no bins inside the band");
    const double mean = sum / static_cast<double>(count);

    BandFlatness out{ 1e30, -1e30 };
    for (std::size_t k = 0; k < mag.values.size(); ++k) {
        const double hz = static_cast<double>(k) * mag.binHz;
        if (hz < lowHz || hz > highHz) continue;
        const double db = 20.0 * std::log10(std::max(1e-30, mag.values[k] / mean));
        out.minDb = std::min(out.minDb, db);
        out.maxDb = std::max(out.maxDb, db);
    }
    return out;
}
```

- [ ] **Step 4: Run to verify it passes**

```
ctest --test-dir build -C Release -R "ir|deconv" --output-on-failure
```
Expected: seven cases PASS. Check the reported count went from 4 to 7.

- [ ] **Step 5: Commit**

```bash
git add core/include/rta/ir/Deconvolver.h core/src/ir/Deconvolver.cpp core/tests/test_ir_deconvolver.cpp
git commit -m "feat(core): a normalisation that reports the flatness it achieved"
```

---

## Task 4: `rta::ir::IrSpectrum` — the window that must start before t = 0

**Files:**
- Create: `core/include/rta/ir/IrSpectrum.h`
- Create: `core/src/ir/IrSpectrum.cpp`
- Create: `core/tests/test_ir_spectrum.cpp`
- Modify: `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Deconvolution` (Task 2), `Sweep::validBandLowHz()` (Task 1).
- Produces:
  ```cpp
  struct IrSpectrumConfig {
      double lowBandEdgeHz = 0.0;   ///< f_lo; the lead-in is two cycles of it
  };
  struct IrSpectrum {
      std::vector<std::complex<float>> bins;   ///< phase in RADIANS, referenced to t=0
      double binHz = 0.0;
      std::size_t leadInSamples = 0;
  };
  [[nodiscard]] IrSpectrum analyseSpectrum(const Deconvolution&, const IrSpectrumConfig&);
  ```

### The error this exists to avoid

The analysis pulse is symmetric about its peak (measured to 3.0e−5), so the
deconvolved signal has real content on **both** sides of `originIndex`. A
transform starting exactly there slices the pulse down the middle. Measured
against a 4th-order 300 Hz highpass, over 100 Hz – 10 kHz: **+23.5 dB** of error
with no lead-in, 0.00 dB with 2 cycles of `f_lo`. A pure-delay round trip passes
either way, because it only inspects the peak.

- [ ] **Step 1: Write the failing test**

Create `core/tests/test_ir_spectrum.cpp` (SPDX header, includes as Task 2):

```cpp
TEST_CASE("The window leads t=0 by two cycles of the low band edge", "[ir][spectrum]") {
    Sweep sweep(testConfig());          // f1 = 100 Hz, fadeInOctaves 2 -> f_lo = 400 Hz
    std::vector<float> excitation(sweep.lengthSamples());
    sweep.process(excitation);
    const auto inverse = sweep.buildInverseFilter();
    const auto reference = rta::ir::deconvolve(excitation, inverse,
                                               {48000.0, sweep.lengthConstantL(), 1.0});

    const auto spectrum = rta::ir::analyseSpectrum(reference, {sweep.validBandLowHz()});
    // 2 / 400 Hz = 5.00 ms = 240 samples at 48 kHz. Derived from the Config the
    // caller already supplied -- not a chosen number of milliseconds.
    CHECK(spectrum.leadInSamples == 240);
}

TEST_CASE("Phase is referenced to t=0, not to the window start", "[ir][spectrum]") {
    Sweep sweep(testConfig());
    std::vector<float> excitation(sweep.lengthSamples());
    sweep.process(excitation);
    const auto inverse = sweep.buildInverseFilter();

    constexpr std::size_t kDelay = 96;   // 2 ms at 48 kHz
    std::vector<float> response(excitation.size() + kDelay, 0.0f);
    for (std::size_t i = 0; i < excitation.size(); ++i) response[i + kDelay] = excitation[i];

    const auto measured = rta::ir::deconvolve(response, inverse,
                                              {48000.0, sweep.lengthConstantL(), 1.0});
    const auto spectrum = rta::ir::analyseSpectrum(measured, {sweep.validBandLowHz()});

    // A pure delay of D samples has phase -2*pi*f*D/fs. If the lead-in's linear
    // phase were left in, every reading would be offset by a further
    // leadInSamples -- a constant group-delay error nobody could later find.
    for (double hz : {500.0, 1000.0, 2000.0, 4000.0}) {
        const auto k = (std::size_t) std::llround(hz / spectrum.binHz);
        REQUIRE(k < spectrum.bins.size());
        const double expected = -2.0 * 3.14159265358979323846 * hz
                              * (double) kDelay / 48000.0;
        double got = std::arg(spectrum.bins[k]);
        double want = std::remainder(expected, 2.0 * 3.14159265358979323846);
        CAPTURE(hz, got, want);
        CHECK_THAT(std::remainder(got - want, 2.0 * 3.14159265358979323846),
                   WithinAbs(0.0, 0.05));
    }
}

namespace {
/// |H(e^jw)| of a cascade, evaluated from the coefficients themselves. The
/// expectation is therefore the filter's own closed form, not a number this
/// test printed on a previous run.
double cascadeMagnitude(std::span<const rta::dsp::Biquad::Coeffs> sections,
                        double hz, double sampleRate) {
    const double omega = 2.0 * 3.14159265358979323846 * hz / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -omega);
    const std::complex<double> z2 = z1 * z1;
    double magnitude = 1.0;
    for (const auto& c : sections) {
        const std::complex<double> num = c.b0 + c.b1 * z1 + c.b2 * z2;
        const std::complex<double> den = 1.0 + c.a1 * z1 + c.a2 * z2;
        magnitude *= std::abs(num / den);
    }
    return magnitude;
}
}  // namespace

TEST_CASE("A steep skirt comes back right, which is what needs the lead-in",
          "[ir][spectrum]") {
    // The assertion the naive window fails: measured, a window starting at
    // originIndex reads up to +23.5 dB high on a steep low-frequency skirt,
    // while the pure-delay round trip stays exact. ButterworthDesign offers
    // bandPass only, so the skirt under test is a band-pass whose UPPER edge is
    // far above the comparison band -- across 100 Hz to 10 kHz it is a 300 Hz
    // high-pass in everything but name.
    const auto design = rta::dsp::ButterworthDesign::bandPass(300.0, 23000.0, 48000.0, 2);

    Sweep sweep(testConfig());
    std::vector<float> excitation(sweep.lengthSamples());
    sweep.process(excitation);

    rta::dsp::BiquadCascade cascade(design.sections);
    std::vector<float> driven(excitation.size());
    for (std::size_t i = 0; i < excitation.size(); ++i)
        driven[i] = (float) cascade.processSample((double) excitation[i]);

    const auto inverse = sweep.buildInverseFilter();
    const auto reference = rta::ir::deconvolve(excitation, inverse,
                                               {48000.0, sweep.lengthConstantL(), 1.0});
    const auto measured  = rta::ir::deconvolve(driven, inverse,
                                               {48000.0, sweep.lengthConstantL(), 1.0});

    const double lowEdge = sweep.validBandLowHz();
    const auto refSpec = rta::ir::analyseSpectrum(reference, {lowEdge});
    const auto gotSpec = rta::ir::analyseSpectrum(measured, {lowEdge});
    REQUIRE(refSpec.bins.size() == gotSpec.bins.size());

    // Dividing by the reference spectrum removes the analysis pulse's own
    // shaping, so what remains is the filter and nothing else.
    for (double hz : {500.0, 800.0, 1500.0, 3000.0, 6000.0, 10000.0}) {
        const auto k = (std::size_t) std::llround(hz / gotSpec.binHz);
        REQUIRE(k < gotSpec.bins.size());
        const double ratio = std::abs(gotSpec.bins[k]) / std::abs(refSpec.bins[k]);
        const double want = cascadeMagnitude(design.sections, hz, 48000.0);
        CAPTURE(hz, ratio, want);
        CHECK_THAT(20.0 * std::log10(ratio / want), WithinAbs(0.0, 0.5));
    }
}
```

The 0.5 dB bound is not negotiable downward: it is 47 dB tighter than the error
the naive window produces, and comfortably looser than the 0.00 dB the
measurement reached, so it separates the two without locking float32 noise.

- [ ] **Step 2: Run to verify it fails**

Expected: `Cannot open include file: 'rta/ir/IrSpectrum.h'`.

- [ ] **Step 3: Implement**

`core/src/ir/IrSpectrum.cpp`, core of it:

```cpp
IrSpectrum analyseSpectrum(const Deconvolution& source, const IrSpectrumConfig& config) {
    if (!(source.sampleRate > 0.0))
        throw std::invalid_argument("analyseSpectrum: sampleRate must be positive");
    if (!(config.lowBandEdgeHz > 0.0))
        throw std::invalid_argument("analyseSpectrum: lowBandEdgeHz must be positive");

    // Two cycles of the lowest frequency the sweep can speak for. The analysis
    // pulse is symmetric about t=0, so a window starting AT t=0 cuts it in half
    // and the recovered response is wrong in the skirts while remaining perfect
    // at the peak. Two cycles is one doubling past where the measured error
    // vanishes (it is already 0.00-0.01 dB at one cycle, at two fade widths).
    // The same reasoning gives Sweep its 2/startHz fade floor.
    const auto leadIn = static_cast<std::size_t>(
        std::llround(2.0 * source.sampleRate / config.lowBandEdgeHz));
    if (leadIn > source.originIndex)
        throw std::invalid_argument(
            "analyseSpectrum: the deconvolution has less pre-arrival room than "
            "two cycles of the low band edge");

    const std::size_t start = source.originIndex - leadIn;
    const std::size_t length = source.samples.size() - start;
    const std::size_t fftSize = nextPowerOfTwo(length);

    dsp::RealFft fft(fftSize);
    std::vector<float> padded(fftSize, 0.0f);
    std::copy(source.samples.begin() + start, source.samples.end(), padded.begin());

    IrSpectrum out;
    out.bins.resize(fft.numBins());
    fft.forward(padded, out.bins);
    out.binHz = source.sampleRate / static_cast<double>(fftSize);
    out.leadInSamples = leadIn;

    // Undo the lead-in's linear phase so the caller reads phase referenced to
    // t = 0. Left in, it is a constant group-delay offset of leadIn samples --
    // 5 ms at a 400 Hz band edge -- that would silently bias every delay
    // reading downstream and be very hard to trace back to a windowing choice.
    const double twoPi = 6.283185307179586476925286766559;
    for (std::size_t k = 0; k < out.bins.size(); ++k) {
        const double theta = twoPi * static_cast<double>(k)
                           * static_cast<double>(leadIn) / static_cast<double>(fftSize);
        out.bins[k] *= std::complex<float>(static_cast<float>(std::cos(theta)),
                                           static_cast<float>(std::sin(theta)));
    }
    return out;
}
```

Phase stays in **radians**. `app/` converts to degrees exactly once, in
`Analyser::pushPair`; a second conversion point in `core/` is how a factor of
57.3 gets applied twice.

- [ ] **Step 4: Run to verify it passes**

```
ctest --test-dir build -C Release -R "ir" --output-on-failure
```

- [ ] **Step 5: Add the falsifier for the lead-in**

The three cases above pass with the lead-in. Prove they would FAIL without it —
this is the step HANDOFF's "xanh mà không chứng minh gì" section exists for:
temporarily set `leadIn = 0`, rebuild, and confirm the highpass case goes RED.
Restore it. Record the observed error in the commit message.

- [ ] **Step 6: Commit**

```bash
git add core/include/rta/ir/IrSpectrum.h core/src/ir/IrSpectrum.cpp core/tests/test_ir_spectrum.cpp core/CMakeLists.txt core/tests/CMakeLists.txt
git commit -m "feat(core): a spectrum window that starts before the impulse, not on it"
```

---

## Task 5: `rta::ir::Polarity` — and the case it must refuse to answer

**Files:**
- Create: `core/include/rta/ir/Polarity.h`
- Create: `core/src/ir/Polarity.cpp`
- Create: `core/tests/test_ir_polarity.cpp`
- Modify: `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Deconvolution` (Task 2).
- Produces:
  ```cpp
  enum class Sign { Negative = -1, Unknown = 0, Positive = 1 };
  struct PolarityConfig {
      double arrivalFraction   = 0.5;
      double searchSeconds     = 0.05;
      double minConfidenceDb   = 20.0;
      double minBandwidthOctaves = 2.5;
  };
  struct PolarityResult {
      Sign        sign = Sign::Unknown;
      std::size_t arrivalIndex = 0;
      double      confidenceDb = 0.0;
      double      bandwidthOctaves = 0.0;
  };
  [[nodiscard]] PolarityResult findPolarity(const Deconvolution&, const PolarityConfig&);
  ```

### Read decision 6 before writing a line of this

It contains a proposal that was **withdrawn after being measured**, and the
withdrawn version is the one an implementer would reinvent. In short:

- Confidence (peak over noise RMS) does **not** detect the failure. On the one
  system that answers backwards it reads **129.7 dB — the highest of any system
  tested** — because it measures signal-to-noise, and a narrowband system has
  excellent signal-to-noise and no definable polarity.
- A **symmetry** figure (largest positive over largest negative excursion) was
  proposed as the discriminator and does not work either. Across 28 cells,
  agreeing systems span 0.09–12.71 dB and disagreeing ones span 0.26–4.18 dB;
  4 octaves at 4 kHz reads **0.09 dB and is correct**, 2 octaves at 250 Hz reads
  **4.18 dB and is wrong**. No threshold separates them. **Do not add a
  symmetry gate.**
- What does separate them is **bandwidth**: at 2.5 octaves and above every
  surveyed cell agrees; at 2 octaves it agrees at 4 of 5 bass centres and only
  1 of 4 wideband ones, with nothing on screen distinguishing them.

And the reason, which belongs in the header comment: for a band-limited system
the sign of the first arrival is a property of that system's phase response, not
of how it is wired. Relative polarity — this box against that one, or before
against after — is sound at any bandwidth, because negating the drive negates
the response exactly. Only the **absolute** verdict needs the bandwidth.

`minBandwidthOctaves = 2.5` is a boundary **observed, not derived**: it is the
lowest width measured clean at all nine centre frequencies, with one filter
family and one order. Say so in the
comment. It is exposed for the same reason `arrivalFraction` is.

- [ ] **Step 1: Write the failing test**

```cpp
namespace {
/// Drive the sweep through a Butterworth band-pass, deconvolve, read polarity.
/// A fresh BiquadCascade per call, so no state leaks between cases.
rta::ir::PolarityResult readPolarity(double lowHz, double highHz, float polarity) {
    Sweep sweep(testConfig());
    std::vector<float> excitation(sweep.lengthSamples());
    sweep.process(excitation);

    const auto design = rta::dsp::ButterworthDesign::bandPass(lowHz, highHz, 48000.0, 2);
    rta::dsp::BiquadCascade cascade(design.sections);
    std::vector<float> driven(excitation.size());
    for (std::size_t i = 0; i < excitation.size(); ++i)
        driven[i] = (float) cascade.processSample((double) polarity * excitation[i]);

    const auto inverse = sweep.buildInverseFilter();
    const auto result = rta::ir::deconvolve(driven, inverse,
                                            {48000.0, sweep.lengthConstantL(), 1.0});
    return rta::ir::findPolarity(result, {});
}
}  // namespace

TEST_CASE("Polarity reads correctly through a wideband passband", "[ir][polarity]") {
    // 60 Hz to 15 kHz is about 8 octaves, comfortably past the boundary. Every
    // surveyed cell at 2.5 octaves and above agreed with the drive polarity.
    for (float polarity : {+1.0f, -1.0f}) {
        const auto got = readPolarity(60.0, 15000.0, polarity);
        CHECK(got.sign == (polarity > 0 ? rta::ir::Sign::Positive : rta::ir::Sign::Negative));
        CHECK(got.confidenceDb > 20.0);
        CHECK(got.bandwidthOctaves > 2.5);
    }
}

TEST_CASE("Polarity refuses a narrowband system rather than guessing", "[ir][polarity]") {
    // THE case with teeth. Measured: this system answers BACKWARDS under the
    // sign rule alone, and its confidence is 129.7 dB -- the HIGHEST of any
    // system measured. A suite without it passes an implementation that would
    // send an operator to rewire a working loudspeaker.
    for (float polarity : {+1.0f, -1.0f}) {
        const auto got = readPolarity(200.0, 250.0, polarity);
        CHECK(got.sign == rta::ir::Sign::Unknown);
        CHECK(got.bandwidthOctaves < 2.5);
        CHECK(got.confidenceDb > 20.0);   // and confidence did NOT notice
    }
}

TEST_CASE("Polarity pins its behaviour at the boundary, on a real subwoofer",
          "[ir][polarity]") {
    // 40-100 Hz is 1.3 octaves: an ordinary subwoofer pass band, sitting just
    // under the 2.5-octave minimum. It exists so that moving the boundary
    // cannot move it THROUGH a real use case with nothing going red. If the
    // chosen behaviour ever changes, change it here deliberately.
    //
    // Note what this suite deliberately does NOT assert: that some subwoofer
    // yields a sign. Paired with the narrowband case above, that would only be
    // satisfiable for one hand-picked fixture -- a suite testing its fixture.
    for (float polarity : {+1.0f, -1.0f}) {
        const auto got = readPolarity(40.0, 100.0, polarity);
        CHECK(got.sign == rta::ir::Sign::Unknown);
        CHECK(got.bandwidthOctaves < 2.5);
    }
}

TEST_CASE("Polarity survives an inverted reflection louder than the direct sound",
          "[ir][polarity]") {
    // The 50% threshold latches onto the direct arrival before the reflection
    // lands, so a boundary bounce -- or a second box wired backwards -- does not
    // flip the reading. Measured correct at every threshold from 0.2 to 0.7.
    Sweep sweep(testConfig());
    std::vector<float> excitation(sweep.lengthSamples());
    sweep.process(excitation);
    const auto delay = (std::size_t) std::llround(0.003 * 48000.0);
    std::vector<float> driven(excitation.size(), 0.0f);
    for (std::size_t i = 0; i < excitation.size(); ++i) {
        driven[i] += excitation[i];
        if (i + delay < driven.size()) driven[i + delay] += -1.5f * excitation[i];
    }
    auto chain = makeBandpass(60.0, 15000.0);
    for (auto biquad : chain) for (auto& s : driven) s = biquad.processSample(s);

    const auto inverse = sweep.buildInverseFilter();
    const auto result = rta::ir::deconvolve(driven, inverse,
                                            {48000.0, sweep.lengthConstantL(), 1.0});
    CHECK(rta::ir::findPolarity(result, {}).sign == rta::ir::Sign::Positive);
}
```

`makeBandpass(lowHz, highHz)` is a file-local helper returning a
`std::vector<rta::dsp::Biquad>` built with `rta::dsp::ButterworthDesign`. Read
that header before writing it.

- [ ] **Step 2: Run to verify it fails**

Expected: `Cannot open include file: 'rta/ir/Polarity.h'`.

- [ ] **Step 3: Implement**

```cpp
PolarityResult findPolarity(const Deconvolution& source, const PolarityConfig& config) {
    if (!(source.sampleRate > 0.0))
        throw std::invalid_argument("findPolarity: sampleRate must be positive");
    if (!(config.arrivalFraction > 0.0) || !(config.arrivalFraction <= 1.0))
        throw std::invalid_argument("findPolarity: arrivalFraction must be in (0, 1]");

    const auto span = static_cast<std::size_t>(
        std::llround(config.searchSeconds * source.sampleRate));
    const std::size_t last = std::min(source.originIndex + span, source.samples.size());

    double largest = 0.0, mostPositive = 0.0, mostNegative = 0.0;
    for (std::size_t i = source.originIndex; i < last; ++i) {
        const double v = source.samples[i];
        largest = std::max(largest, std::abs(v));
        mostPositive = std::max(mostPositive, v);
        mostNegative = std::min(mostNegative, v);
    }

    PolarityResult out;
    if (largest <= 0.0) return out;                 // Unknown

    // Noise window: from the second-harmonic packet up to t=0. By the closed
    // form -L*ln(N), H2 is the FIRST packet, so everything between it and the
    // origin is sidelobes and noise only. The window is computed, not chosen.
    const auto h2 = static_cast<std::size_t>(std::llround(-source.harmonicOffsetSamples(2)));
    const std::size_t noiseFrom = h2 < source.originIndex ? source.originIndex - h2 : 0;
    double sumSq = 0.0;
    std::size_t count = 0;
    for (std::size_t i = noiseFrom; i < source.originIndex; ++i) {
        sumSq += (double) source.samples[i] * (double) source.samples[i];
        ++count;
    }
    const double noiseRms = count > 0 ? std::sqrt(sumSq / (double) count) : 0.0;
    out.confidenceDb = noiseRms > 0.0 ? 20.0 * std::log10(largest / noiseRms) : 200.0;

    // Bandwidth of the arrival itself, in octaves between its -10 dB points.
    // This is the figure that decides whether an ABSOLUTE polarity verdict is a
    // fact about the device at all: for a band-limited system the sign of the
    // first arrival is set by that system's phase response, not its wiring.
    // Confidence cannot stand in for it -- confidence reads its HIGHEST value
    // on exactly the system that answers backwards, because signal-to-noise is
    // excellent there and beside the point.
    out.bandwidthOctaves = arrivalBandwidthOctaves(source, source.originIndex, last);

    for (std::size_t i = source.originIndex; i < last; ++i) {
        if (std::abs((double) source.samples[i]) >= config.arrivalFraction * largest) {
            out.arrivalIndex = i;
            out.sign = source.samples[i] > 0.0f ? Sign::Positive : Sign::Negative;
            break;
        }
    }
    if (out.confidenceDb < config.minConfidenceDb
        || out.bandwidthOctaves < config.minBandwidthOctaves)
        out.sign = Sign::Unknown;
    return out;
}
```

with, in the anonymous namespace above it:

```cpp
/// Octaves between the -10 dB points of the arrival window's own spectrum.
/// Self-contained on purpose: the bandwidth that governs the first arrival's
/// shape is the bandwidth of the arrival, so this needs no band edges from the
/// sweep and no dependency on IrSpectrum.
double arrivalBandwidthOctaves(const Deconvolution& source,
                               std::size_t from, std::size_t to) {
    if (to <= from) return 0.0;
    const std::size_t fftSize = nextPowerOfTwo(to - from);
    dsp::RealFft fft(fftSize);
    std::vector<float> padded(fftSize, 0.0f);
    std::copy(source.samples.begin() + from, source.samples.begin() + to, padded.begin());
    std::vector<std::complex<float>> bins(fft.numBins());
    fft.forward(padded, bins);

    double peak = 0.0;
    for (const auto& bin : bins) peak = std::max(peak, (double) std::abs(bin));
    if (peak <= 0.0) return 0.0;

    const double threshold = peak * 0.31622776601683794;   // -10 dB
    std::size_t lowBin = 0, highBin = 0;
    bool found = false;
    for (std::size_t k = 1; k < bins.size(); ++k) {        // skip DC
        if ((double) std::abs(bins[k]) < threshold) continue;
        if (!found) { lowBin = k; found = true; }
        highBin = k;
    }
    if (!found || lowBin == 0 || highBin <= lowBin) return 0.0;
    return std::log2((double) highBin / (double) lowBin);
}
```

- [ ] **Step 4: Run to verify it passes**

```
ctest --test-dir build -C Release -R "polarity" --output-on-failure
```
Expected: four cases PASS. **Read the count** — a `-R` that matches nothing
reports success.

- [ ] **Step 5: Commit**

```bash
git add core/include/rta/ir/Polarity.h core/src/ir/Polarity.cpp core/tests/test_ir_polarity.cpp core/CMakeLists.txt core/tests/CMakeLists.txt
git commit -m "feat(core): a polarity verdict that knows when it has none"
```

---

## Task 6: the golden vector, the guards, and the documents this made false

**Files:**
- Modify: `tools/gen_golden.py` (new `ir_deconv` case)
- Create/modify: `core/tests/golden/` output file
- Modify: `core/tests/test_ir_deconvolver.cpp` (golden comparison)
- Modify: `docs/HANDOFF.md`, `docs/plans/MASTER-EXECUTION-PLAN.md`,
  `memory/MEMORY.md`, `docs/dsp/2026-08-30-sweep-ir-l4a.md`

- [ ] **Step 1: Add the golden case**

Generate, in NumPy, the deconvolution of a fixed 3-tap response by a sweep built
from the closed forms — **not** by calling anything this lane wrote. Store
`origin_index`, the recovered tap amplitudes, and the in-band flatness. Read
`tools/gen_generator.py`'s existing `sweep_deconv` case first: it is the same
shape and its conventions (`case`/`size`/rows/`end`) must be matched exactly.

- [ ] **Step 2: Compare in C++ with a stated tolerance**

The existing `sweep_deconv` comparison uses ±2 dB and explains why: two
independent pipelines (float32 `RealFft` here, float64 NumPy there) measuring a
noise-floor ratio do not converge tighter. Reuse that reasoning and that
tolerance for any dB-domain row; use `WithinRel(…, 1e-3)` for amplitudes and
exact equality for indices.

- [ ] **Step 3: Read the guard counts — do not assume them**

```
ctest --test-dir build -C Release -R "framework_deps" -V
```
Expected: `core_has_no_framework_deps` reports **more than 70 files**, by the
number of files this lane added to `core/`. If the count is unchanged, the glob
missed `core/src/ir/` and the guard is passing while watching nothing — HANDOFF
trap #1. Fix the guard, not the number.

- [ ] **Step 4: Full clean build and suite**

```
cmake --build build --config Release --parallel --clean-first
```
```
ctest --test-dir build -C Release --output-on-failure
```
Record the total. Compare against Task 1 Step 9's baseline; the delta must equal
the number of cases this lane added.

- [ ] **Step 5: Fix the statements this lane made false**

Grep for them rather than remembering them (HANDOFF trap #10 — "numbers rot
faster than we think, even for the person who just wrote the warning"):

```
grep -rn "L4\b\|not started\|chưa có decision record" docs/ memory/
```

At minimum:
- `docs/plans/MASTER-EXECUTION-PLAN.md` — the L4 row and "Suggested opening
  order" item 3, which says L4 "starts at station 1" and has no decision record.
  Both are now false.
- `docs/HANDOFF.md` — "Lane tiếp theo" names L4 as next; the baseline block's
  test totals; the note that L4 has no record.
- `memory/MEMORY.md` — add a line for the fade-clamp lesson if one is written.
- `docs/dsp/2026-08-30-sweep-ir-l4a.md` — the preamble says "nothing in `core/`
  implements any of this yet". After this lane that is false; mark it as built
  and point at the station-5 report rather than deleting the sentence.

**Numbers live in ONE place.** The suite totals belong in `docs/HANDOFF.md`'s
baseline block and nowhere else.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "docs(L4a): the lane is built, and the statements it made false are fixed"
```

---

## Station 5: adversarial verification

Not optional. Dispatch a reviewer with **no Write tools** that reads the real
files and tries to refute the claim that this plan was implemented. Specifically
ask it to check:

1. That `core_has_no_framework_deps` actually scans `core/src/ir/` — by adding a
   JUCE include there temporarily and confirming the guard goes RED.
2. That the `IrSpectrum` lead-in is load-bearing — set it to zero and confirm the
   highpass test fails.
3. That the polarity narrowband case fails if `minBandwidthOctaves` is set to
   0.0 — and that no symmetry gate crept back in: `grep -rn "symmetry" core/`
   should return nothing. The record withdrew that idea after measuring it, and
   an implementer who read only the code would reinvent it.
4. That the regenerated `sweep_deconv` golden's `peak_index` is still 96999.
5. That no file exceeds 400 lines.

A reviewer that reports "all tests pass" has not done this. **The question is
never "is it green", it is "what wrong implementation would make it red".**
