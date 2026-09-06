# L3 — multi-time-window transfer function: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: use `superpowers:subagent-driven-development`
> (recommended) or `superpowers:executing-plans` to implement this plan task by
> task. Steps use checkbox (`- [ ]`) syntax for tracking. Station 5 (adversarial
> verify, reviewer with **no Write tools**) is not optional — see
> `docs/reports/README.md` and the refutation list at the end of this file.

*2026-09-05, lane L3, station 3. Written from `docs/dsp/2026-09-05-mtw-l3.md`
(station 2) after reading the real files, not the record's description of them.
Worktree `.claude\worktrees\continue-6bacde`, HEAD `7e10eb6`.*

**Goal:** every frequency region gets its own analysis window — long at the
bottom, short at the top — stitched into one curve with an explicit frequency
vector and visible seams, provable on CI with no sound card.

**Split:** **L3a** is `core/` only and must build and pass with
`RTA_BUILD_APP=OFF`. **L3b** is `app/` only. L3b depends on L3a; L3a depends on
nothing.

---

## Record conflicts found

The decision record is binding. Four of its claims were refuted by the real
files, and **all four have since been corrected in the record itself** — C1, C2
and C4 in commit `6b3fbad`, and **C3 by a reversal of §5 from seconds-uniform to
frames-uniform averaging**, which is a design change and not a digit. All four
are kept here with their evidence so the wrong form cannot come back; nothing
below is a plan-side workaround for a record that still says something else.
Each entry carries `file:line` evidence and the smallest resolution.

**One consequence of the §5 reversal runs through the whole plan and is worth
stating once:** averaging is now `fifoDepth` / `timeConstantFrames` in
**frames**, uniform across bands, so **every band reports the same
`effectiveAverages`** — 8.5866271 at the default depth 16 — and each reports its
own `integrationSeconds = depth * hop_k / fs`. The words
`timeConstantSeconds`, `fifoSeconds` and `coherenceUnlockTimeConstantSeconds`
appear nowhere in this lane's interfaces, and the defaults 3.5 s / 4.0 s that an
earlier draft derived are **refuted, not superseded**: they were the least-bad
answer to a question §5 no longer asks.

### C1 — the owned-bin formula — **RESOLVED IN RECORD (`6b3fbad`)**

**Status: fixed in `docs/dsp/2026-09-05-mtw-l3.md` §3, which now reads `N_0/8 ..
N_0/4 - 1 = 128 .. 255` in every band and says in parentheses that the first
draft's `N_k` was "wrong by a factor `2^k`". Evidence kept below; no plan change
follows from it.**

The record's superseded draft said: *"Owned bins in a middle
band are `N_k/8 .. N_k/4 - 1`"*. For `k = 1` that reads bins 256..511 of a
2048-point transform — 3000–5977 Hz, which happens to land on the right
frequencies — but the count is 256, not the 128 that the same sentence and the
§3 table both assert. At `k = 5` the two forms diverge outright.

The owned octave is `[fs/(8·2^k), fs/(4·2^k))` (§2), so the lower bin index is
`f_lo · N_k / fs = N_k / (8 · 2^k) = N_0 / 8`, **independent of `k`**. Written
correctly the range is `N_0/8 .. N_0/4 - 1` — bins **128..255 in every band**,
128 bins wide, exactly as the table's `points` column says.

**Resolution:** implement `N_0/8 .. N_0/4 - 1`, which is what the corrected
record now specifies. `MtwLayout`'s header carries the one-line derivation above
so the wrong form cannot come back.

### C2 — the point count — **RESOLVED IN RECORD (`6b3fbad`)**

**Status: fixed. The record's §3 table now gives the bottom band 256 points and
the total as 1281, and states that the bottom band owns bins 0..255. Evidence
kept below; the corrected table stays because every number in this plan is read
off it.**

The record's superseded draft gave the bottom band 257 points and the total as
1282. 257 points means bins 0..256 of a
65536-point transform, and bin 256 sits at `256 · 48000/65536 = 187.5 Hz` —
**the same frequency as band 5's first owned bin** (`128 · 48000/32768 =
187.5 Hz`). The stitched vector would repeat 187.5 Hz, which T1 forbids
(record §7, T1) and which §4's "each output point comes
from exactly one band" forbids too.

The half-open convention in §2 (`[fs/(8·2^k), fs/(4·2^k))`) already settles it:
187.5 Hz belongs to band 5. The bottom band owns bins **0..255 — 256 points**.

**Corrected table for the defaults (`N_0 = 1024`, `K = 6`, `fs = 48000`):**

| k | `N_k` | hop | Δf (Hz) | window | owned bins | points | first index | frequency range (Hz) |
|---|---|---|---|---|---|---|---|---|
| 6 | 65536 | 16384 | 0.732422 | 1.365 s | 0 – 255 | **256** | 0 | 0 – 186.768 |
| 5 | 32768 | 8192 | 1.464844 | 682.7 ms | 128 – 255 | 128 | 256 | 187.5 – 373.535 |
| 4 | 16384 | 4096 | 2.929688 | 341.3 ms | 128 – 255 | 128 | 384 | 375 – 747.070 |
| 3 | 8192 | 2048 | 5.859375 | 170.7 ms | 128 – 255 | 128 | 512 | 750 – 1494.141 |
| 2 | 4096 | 1024 | 11.71875 | 85.33 ms | 128 – 255 | 128 | 640 | 1500 – 2988.281 |
| 1 | 2048 | 512 | 23.4375 | 42.67 ms | 128 – 255 | 128 | 768 | 3000 – 5976.563 |
| 0 | 1024 | 256 | 46.875 | 21.33 ms | 128 – 512 | **385** | 896 | 6000 – 24000 |

**Total 1281 points**, indices 0..1280. Closed form:

```
pointCount(N0, K) = (N0/2 - N0/8 + 1)      // top band, up to and including Nyquist
                  + (K - 1) * (N0/8)        // middle bands
                  + (N0/4)                  // bottom band, DC up to its edge
```

Checks: `(1024, 6) = 385 + 640 + 256 = 1281`; `(1024, 7) = 1409`;
`(512, 6) = 193 + 320 + 128 = 641`; `(512, 3) = 193 + 128 + 128 = 449`;
`(2048, 6) = 769 + 1280 + 512 = 2561`.

Nothing else in §3 moves: the PPO figures (57 at 60 Hz, 38 at 40 Hz, 88.7–177.4
inside a middle band) and the `N_0 >= 554` derivation are all unaffected, and
1281 still clears the spec's "~800+ points".

**Resolution:** 1281, not 1282 — as the corrected record now says. Every test
number in this plan uses 1281, and refutation R7 greps for the old figures.

### C3 — averaging in seconds — **RESOLVED IN RECORD (§5 reversed to frames-uniform)**

**Status: the record's §5 was REVERSED after this plan's analysis. It no longer
specifies seconds at all.** The first draft of §5 said "averaging is specified in
seconds, uniform across bands"; the arithmetic below — computed from the real
`AverageCount` functions rather than from the header's stated ceiling — showed
that uniform seconds cannot give uniform confidence, and §5 now reads
*"Averaging is specified in frames, uniform across bands; seconds are reported
per band"*. **The analysis that forced the reversal is kept in the record**
(§5's "Why, and why the first draft said the opposite"). The two evidence tables
are kept here so the seconds form cannot come back by accident.

**Evidence table 1 — asymptotic exponential `Neff` under UNIFORM SECONDS**, from
the real `exponentialEffectiveAverages` (`core/src/dsp/AverageCount.cpp:67-113`),
`frames = 10000`, fs = 48000, Hann, `hop = N/4`, in ascending-frequency vector
order. `Neff = 1 + (Neff_raw - 1)/D`, `Neff_raw -> (2-a)/a`,
`D = 1 + 2*sum_{m>=1} c(m*hop)^2`:

| idx | k | `N_k` | hop | tau=0.5 | tau=1.0 | tau=2.0 | tau=3.0 | tau=3.5 | tau=4.0 |
|---|---|---|---|---|---|---|---|---|---|
| 0 | 6 | 65536 | 16384 | **2.061** | **3.554** | **6.584** | 9.623 | 11.144 | 12.665 |
| 1 | 5 | 32768 | 8192 | **3.554** | **6.584** | 12.665 | 18.752 | 21.795 | 24.839 |
| 2 | 4 | 16384 | 4096 | **6.584** | 12.665 | 24.839 | 37.016 | 43.104 | 49.193 |
| 3 | 3 | 8192 | 2048 | 12.665 | 24.839 | 49.193 | 73.547 | 85.725 | 97.902 |
| 4 | 2 | 4096 | 1024 | 24.839 | 49.193 | 97.902 | 146.612 | 170.967 | 195.323 |
| 5 | 1 | 2048 | 512 | 49.193 | 97.902 | 195.323 | 292.743 | 341.454 | 390.164 |
| 6 | 0 | 1024 | 256 | 97.902 | 195.323 | 390.164 | 585.006 | 682.427 | 779.848 |

(bold = below the gate of 8.) At the inherited `tau = 0.5 s` **three** bands are
gated for ever, and the smallest uniform value that opens the bottom band with a
25 % margin buys the top band 682 averages and a lag of seconds at exactly the
frequencies where an operator makes EQ moves. Two rows of one table differing by
a factor of 331 is the whole refutation.

**Evidence table 2 — `Fifo` under UNIFORM SECONDS**, with
`depth_k = max(1, llround(fifoSeconds * fs / hop_k))` and the real
`fifoEffectiveAverages`, bottom band only:

| `fifoSeconds` | 1.0 | 2.0 | 3.0 | 4.0 | 6.5 |
|---|---|---|---|---|---|
| bottom-band depth | 3 | 6 | 9 | 12 | 19 |
| bottom-band `Neff` | 1.878 | 3.407 | 4.957 | 6.511 | 10.144 |

`depth_k * numBins_k` is the **same** for every band under uniform seconds, so a
seconds-based FIFO costs about 21.5 MB per second across the seven bands and
*still* leaves the bottom band gated at any depth a memory cap allows. Both
halves of that are why the cap is now expressed in frames.

**What the record now specifies, and every number it implies.** `MtwConfig`
carries the fixed engine's own vocabulary and nothing new: `TransferAveraging
{Fifo, Exponential}`, `fifoDepth` in **frames** (default 16, `validate()` rejects
> 32), `timeConstantFrames` in **frames** (default 16). There is **no**
`timeConstantSeconds`, no `fifoSeconds`, and no
`coherenceUnlockTimeConstantSeconds` anywhere in this lane.

1. **One alpha in every band, and the mechanism that makes it one.** `MtwEngine`
   gives band `k` a `DualFftEngine::Config` with
   `timeConstantSeconds_k = timeConstantFrames * hop_k / fs`. The engine then
   computes `alpha_k = 1 - exp(-(hop_k/fs) / timeConstantSeconds_k)`
   (`core/src/dsp/DualFftEngine.cpp:65-67`), and `hop_k/fs` cancels:

   ```
   alpha_k = 1 - exp(-1 / timeConstantFrames)      for every k
   ```

   At the default 16 that is **`alpha = 0.060586937`** in all seven bands. The
   cancellation IS the design, and it belongs in `MtwLayout.h`'s comment: a
   builder who passes `timeConstantFrames` through as seconds gets seven
   different alphas, and nothing catches it except task 1 case 8.
2. **One asymptotic `Neff` in every band.** `D = 1 + 2*sum c(m*hop)^2` is
   **1.9246389** in every band (`hop/N = 1/4` everywhere and the periodic Hann
   shape does not depend on `N`), so
   `Neff_inf = 1 + ((2-a)/a - 1)/D` = **17.1123296** at `alpha = 0.060586937`,
   identical to 8 significant figures in all seven.
3. **`Fifo` depth 16 gives `Neff = 8.5866271` in every band.**
   `DualFftEngine::effectiveAverages()` calls
   `fifoEffectiveAverages(window, hop, min(frameCount, fifoDepth))`
   (`core/src/dsp/DualFftEngine.cpp:141-142`), and with `hop = N/4` only three
   lags survive: `c(N/4) = 0.659154943`, `c(N/2) = 0.166666667`,
   `c(3N/4) = 0.007511724`. So

   ```
   Neff(16) = 16 / (1 + 2*[(1 - 1/16)c1^2 + (1 - 2/16)c2^2 + (1 - 3/16)c3^2])
            = 8.58662709
   ```

   **Record §5 says "about 8.8"; the exact value from the real function is
   8.5866271.** Every assertion in this plan uses 8.5866271, and that round
   figure is the one digit §5 still owes a correction (task 8 step 2).
4. **The FIFO fill, and where the gate really opens.** `Neff` depends only on
   `min(frameCount, 16)`, so the fill is the **same sequence in every band**:

   | frames | 12 | 13 | 14 | **15** | **16** | 17+ |
   |---|---|---|---|---|---|---|
   | `Neff` | 6.5113 | 7.0300 | 7.5488 | **8.0677** | **8.5866** | 8.5866 |

   The first frame clearing the gate of 8 is **15**, by 0.85 %; frame 16 is where
   `Neff` **saturates**, 7.3 % over. Record §5's "the gate opens at frame 16 in
   every band" is right about the design point and loose about the arithmetic, so
   every test below requires **16** frames and nothing rests on an 0.85 % margin.
5. **Exponential mode needs 27 frames, not 16.** At `alpha = 0.060586937` the
   real `exponentialEffectiveAverages` reads 7.5204 at frame 26 and 8.0321 at
   frame 27, so the gate opens at **frame 27** in every band — 491520 samples in
   the 65536-point band. That is why every engine test below runs in the
   **default `Fifo` mode**: 16 frames instead of 27, and the test length is 60 %
   of what Exponential would need.
6. **`integrationSeconds_k = fifoDepth * hop_k / fs`**, reported per band and
   shown by the view (record §5, §6). At the defaults, ascending-frequency order:

   | idx | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
   |---|---|---|---|---|---|---|---|
   | `N_k` | 65536 | 32768 | 16384 | 8192 | 4096 | 2048 | 1024 |
   | `integrationSeconds` | 5.4613333 | 2.7306667 | 1.3653333 | 0.6826667 | 0.3413333 | 0.1706667 | 0.0853333 |

   Uniform confidence is bought with non-uniform seconds, and the operator is
   told which: 85 ms at the top, **5.461 s below 187.5 Hz**. A coherence trace
   filling in from the top over five seconds is otherwise read as a fault.

**Resolution (what the code does):**

- `MtwConfig` carries `averaging = Fifo`, `fifoDepth = 16`,
  `timeConstantFrames = 16`. `validate()` rejects `fifoDepth == 0`,
  `fifoDepth > 32` (the memory cap below) and `timeConstantFrames <= 0`.
- `MtwEngine` converts to the band engine's seconds by
  `timeConstantSeconds_k = timeConstantFrames * hop_k / fs` — the only place
  seconds appear in this lane, with the header saying why the conversion is
  exactly the inverse of `DualFftEngine`'s own.
- `MtwBand` carries `integrationSeconds`, and `mtwIntegrationSeconds(cfg, band)`
  is the pure function task 1 tests.
- **The negative case lives in task 1, expressed through
  `exponentialEffectiveAverages` directly** — `MtwConfig` has no per-band tau to
  express it with, which is the point. A uniform `tau = 0.5 s` written as
  per-band frames, `timeConstantFrames_k = 0.5 * fs / hop_k`, gives
  `{1.46484, 2.92969, 5.85938, 11.71875, 23.4375, 46.875, 93.75}` in
  ascending-frequency order, hence alphas
  `{0.494732, 0.289178, 0.156897, 0.081794, 0.041769, 0.021107, 0.010610}` and
  asymptotic `Neff` `{2.061, 3.554, 6.584, 12.665, 24.839, 49.193, 97.902}` — the
  bottom band at **2.061**, permanently under the gate. That assertion exists so
  nobody re-introduces seconds.

### C4 — a stitched `coherence` member in `core/` — **RESOLVED IN RECORD (`6b3fbad`)**

**Status: fixed. The record's §5 now says `MtwResult` holds the per-band
`TransferSnapshot`s plus a frequency vector and a `(band, bin)` index, copies no
coherence, and states explicitly that the guard is name-based ("a field of any
other name would evade it"), with the flattening done in `app/`. Evidence kept
below; the resolution is unchanged and is what task 2 implements.**

`core/tests/check_coherence_gate.cmake:41-49` fails any file under
`core/src/*.cpp` or `core/include/*.h` other than `TransferEstimator.cpp` that
matches `(\.|->)coherence[ \t]*=`, `coherence[ \t]*=[ \t]*std::vector`, or
`coherence[ \t]*\.[ \t]*emplace`. A stitched result type with an
`std::optional<std::vector<float>> coherence` that `MtwEngine.cpp` fills matches
on the first line that fills it — including a designated initialiser, as the
guard's own header comment warns.

Routing each band through `makeSnapshot()` therefore keeps the guard's one
exception file the only place coherence is assigned in `core/` under exactly one
design, which is the one the record now carries:

**Resolution:** `MtwResult` **owns no coherence array**. It holds the per-band
`TransferSnapshot`s produced by `makeSnapshot()` (each carrying its own gated
`std::optional`) plus the layout, and exposes
`std::optional<float> coherenceAt(const MtwResult&, std::size_t index)` which
indexes into the owning band's snapshot. Nothing in `core/` writes a coherence
field outside `TransferEstimator.cpp`. Flattening for display happens in `app/`,
where `Analyser.cpp:180` already does the same thing for the fixed block and the
guard does not reach.

### Non-conflicts, checked and cleared (do not re-check)

- **`hopSize = fftSize/4` is legal.** `DualFftEngine::validate()` requires only
  `1 <= hopSize <= fftSize` (`core/src/dsp/DualFftEngine.cpp:93-95`).
- **`RealFft` supports 65536.** The only constraint is power-of-two and >= 4
  (`core/src/dsp/RealFft.cpp:18-20`); `Fft` requires >= 2
  (`core/src/dsp/Fft.cpp:17-19`). No upper bound anywhere.
- **`overlapCorrelation`'s lag is in SAMPLES and does not wrap**
  (`core/src/dsp/AverageCount.cpp:8-38`): `1.0` at lag 0, exactly `0.0` at
  `lag >= window.size()`. That is the `r_w` §4 defines, unchanged.
- **`makeSnapshot()` takes a `const DualFftEngine&`**
  (`core/include/rta/dsp/TransferEstimator.h:85`), so composing engines needs no
  change to it.

### Memory: what the record now says, and what the code must do about it

Record §2 carries the corrected memory arithmetic and this plan implements it.
Every figure below is derived from a member declaration, in decimal MB to match
the record. `sum_k numBins_k = sum_k (N_k/2 + 1) = 65024 + 7 = **65031**` for the
default `K = 6` table, and `sum_k N_k = 1024*(2^7 - 1)` = **130048**.

- **Rings are `bit_ceil(4*fftSize + skip)` floats per channel**
  (`core/src/dsp/DualFftEngine.cpp:23-25`). With `skip = 0`, `4*N` is already a
  power of two, so the seven bands hold `2 * 4 * 130048` floats =
  **4.16 MB** — 2.10 MB of it in the 65536 band alone.
- **Per-bin fixed cost, whatever the averaging mode**: `referenceBins_` and
  `measurementBins_` are `complex<float>` (8 B each), and `runningSum{Sxx,Syy}` +
  `{sxx,syy}Mean_` are `double` (8 B each) with `runningSumSxy_` and `sxyMean_`
  `complex<double>` (16 B each) — **80 B per bin**, so
  `65031 * 80` = **5.20 MB**.
- **Per-sample scratch**: four `float` frame/windowed vectors of `N` per band,
  `16 * 130048` = **2.08 MB**, plus the window coefficients `4 * 130048` =
  **0.52 MB**.
- **The FIFO storage is allocated unconditionally, whatever the averaging mode**
  (`core/src/dsp/DualFftEngine.cpp:76-78`), at
  `fifoDepth * numBins * (8 + 8 + 16) B` per band. With **one depth in every
  band** (§5, frames-uniform) that is `65031 * 32` = **2.08 MB per frame of
  depth**, the record's figure exactly.

So the whole engine costs `11.96 MB + 2.08 MB * fifoDepth`:

| mode | `fifoDepth` | FIFO | total |
|---|---|---|---|
| `Exponential` (with rule 1 below) | 1 | 2.08 MB | **14.0 MB** |
| `Fifo`, default | 16 | 33.3 MB | **45.3 MB** |
| `Fifo`, the `validate()` cap | 32 | 66.6 MB | **78.6 MB** |
| `Exponential` **without** rule 1 | 16 | 33.3 MB | 45.3 MB |

Two rules follow, both of them the record's:

1. **`MtwEngine` sets `fifoDepth = 1` on every band Config when
   `averaging == Exponential`**, because the engine allocates the FIFO whether it
   uses it or not. With the rule, exponential mode costs 14.0 MB; without it,
   45.3 MB — 31 MB of storage that is written by nothing and read by nothing.
   Task 2 case 6 asserts `band(i).config().fifoDepth == 1`, and step 5(c) mutates
   it, because a memory claim no test can fail is not a claim.
2. **`fifoDepth` is capped at 32 in `MtwLayout::validate()`**, so the worst case
   stays under 80 MB. Task 1 case 7 asserts that `validate()` throws
   `std::invalid_argument` at `fifoDepth = 33` and accepts 32. Unlike the
   seconds-based cap this replaces, the cap now has **no measurement price**:
   depth 16 already gives `Neff = 8.5866271` in every band, over the gate of 8,
   and depth 32 gives 16.8955 — so `Fifo` publishes coherence in every band
   including the 65536-point one. That is the whole gain of the frames-uniform
   reversal, and the header comment says so beside the cap.

## Global constraints

- `// SPDX-License-Identifier: AGPL-3.0-or-later` on every new file.
- `core/` never includes JUCE, Qt, or an audio-device API.
- **Hard cap 400 lines, aim 300.** Headers too. Every new file is budgeted below.
- **`core/src/dsp/DualFftEngine.cpp` (321 lines) and `core/tests/test_dualfft.cpp`
  (400 lines) MUST NOT be edited.** Neither may `core/include/rta/dsp/DualFftEngine.h`.
- Never assert a value the implementation produced. Closed form, published
  standard, or golden — and a regression lock only if labelled as one.
- Frequency reads as a whole number of hertz; dB one decimal; coherence two.
- Comments explain *why a formula is that formula*.
- The venv is at the **main checkout**, not this worktree:
  `D:\DEV CAVE EP3\PRJ010-RTA-TOOL\.venv\Scripts\python.exe`. Absolute path, no
  second venv.
- Build directory is **`build-l3`**, Visual Studio generator, never Ninja.

```
cmake -S . -B build-l3 -G "Visual Studio 18 2026" -A x64
cmake --build build-l3 --config Release --parallel
ctest --test-dir build-l3 -C Release --output-on-failure
```

L3b additionally needs:

```
cmake -S . -B build-l3 -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=ON -DRTA_JUCE_PATH="D:/DEV CAVE EP3/PROJECT005-AZ-handsfree/external/JUCE"
```

### The test signal length, derived once and used by every engine test

Every engine test needs the bottom band past its gate, and under the
frames-uniform default (`Fifo`, `fifoDepth = 16`) that means **16 frames in the
65536-point band** — not 15, whose 0.85 % margin is a flake waiting for a
tolerance change (C3 point 4). `DualFftEngine` emits a frame at every hop once
the first full window exists, so

```
frames_k(n) = 1 + (n - N_k) / hop_k          for n >= N_k
n_min       = N_6 + 15 * hop_6 = 65536 + 245760 = 311296 samples (6.485 s)
```

**Chosen: `n = 327680` samples** — 6.827 s at 48 kHz, i.e. `5 * 65536`. Two
reasons for the round number over the bare minimum: `n - N_k` is an exact
multiple of `hop_k` in **every** band, so each band's frame count is a stated
integer rather than a floor that a reader has to trust; and it puts the bottom
band at 17 frames, one clear of the 16 the FIFO holds, which proves the FIFO
actually drops a frame rather than merely filling. Frame counts, ascending in
frequency:

| idx | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|---|
| `N_k` | 65536 | 32768 | 16384 | 8192 | 4096 | 2048 | 1024 |
| frames at `n = 327680` | **17** | 37 | 77 | 157 | 317 | 637 | 1277 |

`min(frames, 16) == 16` in every band, so **every band reads
`effectiveAverages() == 8.5866271`** and every band publishes coherence. That
one number replaces the seven different `Neff` values the seconds-based draft
had to carry, and it is why the tolerances below are one derivation rather than
seven.

**The short feed, for the per-band gate test: `n = 180224`** (3.755 s,
`65536 + 7 * 16384`). Frame counts `{8, 19, 41, 85, 173, 349, 701}`, again exact
in every band, so the bottom band sits at `Neff = 4.4393` — under the gate —
while all six others are saturated at 8.5866271. That is the per-band gate,
demonstrated by a length rather than asserted by a comment.

**If a test ever needs `Exponential` instead**, the gate opens at frame 27
(C3 point 5), so `n >= 65536 + 26 * 16384 = 491520`. No test below needs it.

**Baselines, measured 2026-09-05 on HEAD `7e10eb6`:** `RTA_BUILD_APP=OFF` →
**362/362**; `RTA_BUILD_APP=ON` → **402/402**. Note that `app/tests` is
registered *outside* the `RTA_BUILD_APP` guard (`CMakeLists.txt:44-46`), so the
JUCE-free app tests L3b adds land in the OFF count too. Only `app/tests_juce`
is ON-only.

**Guard file counts today** (each guard prints its own count; **read it, do not
assume it** — HANDOFF trap #1 is a mis-globbed guard that passes while watching
nothing):

| ctest | globs | today | after L3a | after L3b |
|---|---|---|---|---|
| `core_has_no_framework_deps` | core `include/*.h` + `include/*.hpp` + `src/*.h` + `src/*.cpp` + `tests/*.cpp` | **87** | **96** (+3 h, +2 cpp, +4 test cpp) | 96 |
| `coherence_gate_is_not_bypassed` | core `src/*.cpp` + `include/*.h` | **57** | **62** (+3 h, +2 cpp) | 62 |
| `filter_design_has_no_polynomial_form` | core `include/*.h` + `src/*.cpp` + `tests/*.cpp` + `tools/*.py` | **103** | **113** (+3 h, +2 cpp, +4 test cpp, +1 py) | 113 |
| `measure_has_no_framework_deps` | explicit GLOBS list, `app/tests/CMakeLists.txt:70` | **29** | 29 | **30** (+`view/ColumnMap.h`) |

---

# L3a — `core/`

## Task 1 — `MtwLayout`: the band table as pure functions

**Files:**
- Create `core/include/rta/dsp/MtwLayout.h` (<= 130 lines)
- Create `core/src/dsp/MtwLayout.cpp` (<= 140 lines)
- Create `core/tests/test_mtw_layout.cpp` (<= 300 lines)
- Modify `core/CMakeLists.txt` (add the .cpp), `core/tests/CMakeLists.txt`

**Interfaces produced:**

```cpp
namespace rta::dsp {

struct MtwConfig {
    std::size_t topFftSize = 1024;   ///< N0, restricted to {512, 1024, 2048}
    std::size_t octaveCount = 6;     ///< K; bands are k = 0..K, so K+1 of them
    double sampleRate = 48000.0;
    WindowType window = WindowType::Hann;

    /// Record §5: averaging is specified in FRAMES, uniform across bands, and
    /// the SECONDS are reported per band (MtwBand::integrationSeconds). Both
    /// knobs below are frame counts and neither has a seconds twin: uniform
    /// seconds cannot give uniform Neff, because Neff per second of
    /// integration scales as 1/T_window (conflict C3's two evidence tables).
    TransferAveraging averaging = TransferAveraging::Fifo;
    std::size_t fifoDepth = 16;         ///< frames, 1..32. Neff = 8.5866271 at 16.
    double timeConstantFrames = 16.0;   ///< frames, > 0. alpha = 1 - exp(-1/this).

    int referenceDelaySamples = 0;
    double minimumEffectiveAverages = 8.0;
};

struct MtwBand {
    std::size_t fftSize = 0;
    std::size_t hopSize = 0;        ///< fftSize / 4
    std::size_t firstBin = 0;       ///< inclusive
    std::size_t lastBin = 0;        ///< inclusive
    std::size_t firstIndex = 0;     ///< where this band starts in the stitched vector
    double lowerEdgeHz = 0.0;       ///< fs / (8 * 2^k); 0.0 for the bottom band
    double windowSeconds = 0.0;         ///< fftSize / fs
    double integrationSeconds = 0.0;    ///< fifoDepth * hopSize / fs -- what the view prints
};

[[nodiscard]] std::vector<MtwBand> mtwBands(const MtwConfig&);          // ascending in frequency
[[nodiscard]] std::size_t mtwPointCount(const MtwConfig&);
[[nodiscard]] std::vector<double> mtwFrequencies(const MtwConfig&);
/// 1 - exp(-1/timeConstantFrames). Takes a band index and IGNORES it: the value
/// is band-independent by construction, and the parameter is kept so a caller
/// cannot silently start assuming otherwise. Task 1 case 8 asserts the equality.
[[nodiscard]] double mtwAlpha(const MtwConfig&, std::size_t band);
[[nodiscard]] double mtwIntegrationSeconds(const MtwConfig&, std::size_t band);
void validate(const MtwConfig&);   // throws std::invalid_argument

}  // namespace rta::dsp
```

`mtwBands` returns bands ordered **ascending in frequency**, so vector index 0 is
the record's `k = K` (the 65536-point band) and `k = octaveCount - vectorIndex`.
Say that in the header: the two orderings are the single most likely place for an
off-by-one, and every number in this plan is in vector order.

- [ ] **Step 1: Write the failing tests** — `core/tests/test_mtw_layout.cpp`

  1. `TEST_CASE("the MTW band table doubles the FFT size once per octave downward", "[mtw][layout]")`
     — for the defaults the seven `fftSize` values in ascending-frequency order
     are `{65536, 32768, 16384, 8192, 4096, 2048, 1024}` and every `hopSize` is
     `fftSize/4`. `windowSeconds[0]` is `65536/48000 = 1.36533333…` within
     `1e-12`; `windowSeconds[6]` is `0.02133333…`.
  2. `TEST_CASE("every band owns bins 128..255 except the top and the bottom", "[mtw][layout]")`
     — conflict C1. Vector indices 1..5 have `firstBin == 128` and
     `lastBin == 255`. Index 0 (the bottom band) has `firstBin == 0`,
     `lastBin == 255`. Index 6 (the top band) has `firstBin == 128`,
     `lastBin == 512 == topFftSize/2`, Nyquist included. Assert the general form
     too: `firstBin == topFftSize/8` for every band but the bottom, at
     `topFftSize` 512 and 2048 as well.
  3. `TEST_CASE("the stitched frequency vector is strictly increasing and 1281 points long", "[mtw][layout]")`
     — conflict C2. `mtwPointCount(defaults) == 1281`;
     `mtwFrequencies(defaults).size() == 1281`; `f[0] == 0.0` exactly;
     `f[i] > f[i-1]` for every `i >= 1`; `f[1280] == 24000.0` within `1e-9`.
  4. `TEST_CASE("each stitched point's frequency is its owning band's bin frequency", "[mtw][layout]")`
     — for every band and every owned bin,
     `f[band.firstIndex + (bin - band.firstBin)] == bin * fs / band.fftSize`
     within `1e-9`. Spot values that must appear verbatim in the test:
     `f[255] == 255 * 48000.0 / 65536.0` (186.7676 Hz), `f[256] == 187.5`,
     `f[384] == 375.0`, `f[512] == 750.0`, `f[640] == 1500.0`,
     `f[768] == 3000.0`, `f[896] == 6000.0`.
  5. `TEST_CASE("the owned boundaries are exactly fs / (8 * 2^k)", "[mtw][layout]")`
     — `lowerEdgeHz` in ascending order is `{0, 187.5, 375, 750, 1500, 3000, 6000}`
     within `1e-12`, and equals `firstBin * fs / fftSize` for every band but the
     bottom.
  6. `TEST_CASE("point count follows the closed form when N0 or K changes", "[mtw][layout]")`
     — `(1024, 7) == 1409`, `(512, 6) == 641`, `(512, 3) == 449`,
     `(2048, 6) == 2561`. Each must also equal `mtwFrequencies().size()`, and
     each vector must be strictly increasing.
  7. `TEST_CASE("the layout refuses a table it cannot express", "[mtw][layout]")`
     — `validate` throws `std::invalid_argument` for `topFftSize` outside
     `{512, 1024, 2048}`, for `octaveCount == 0`, for `octaveCount > 10`
     (`N0 · 2^K` must stay a transform the machine will allocate — at
     `N0 = 2048, K = 10` the bottom band is 2 Mi points and one ring is 32 MB),
     for `sampleRate <= 0`, for `timeConstantFrames <= 0`, and for
     `minimumEffectiveAverages < 1.0` (the same floor `DualFftEngine::validate`
     enforces at `core/src/dsp/DualFftEngine.cpp:112-114`). Also **record §5's
     memory cap, now in frames**: `fifoDepth = 33` throws, `fifoDepth = 32` does
     not, and `fifoDepth = 0` throws. The cap is 32 inclusive, and asserting both
     sides is what stops it drifting to 33 the first time a test wants more
     averages. 32 frames is 66.6 MB of FIFO across the seven bands (memory
     section); 33 is not a measurement decision, it is 2.08 MB more.
  8. `TEST_CASE("one alpha and one depth in every band, and seconds only as a report", "[mtw][layout]")`
     — record §5 and T5, and the case that catches a re-introduction of seconds.
     For the defaults, `mtwAlpha(cfg, k)` is **bit-identical** for every `k`
     (`REQUIRE(mtwAlpha(cfg,k) == mtwAlpha(cfg,0))`, `==` not `Approx` — it is
     one expression evaluated seven times, not seven that agree) and equals
     `1 - std::exp(-1.0/16.0) = 0.060586937` within `1e-9`. Repeat at
     `timeConstantFrames = 4` and `= 64`, and at `topFftSize` 512 and 2048, so
     the equality is a property of the design and not of one number.
     `mtwIntegrationSeconds(cfg, k)` in ascending-frequency order is
     `{5.4613333, 2.7306667, 1.3653333, 0.6826667, 0.3413333, 0.1706667,
     0.0853333}` within `1e-7` and equals `fifoDepth * hop_k / fs` exactly;
     `mtwBands(cfg)[k].integrationSeconds` carries the same value. Assert the
     ratio too: consecutive entries differ by exactly a factor of two, which is
     the visible consequence of frames being uniform.
  9. `TEST_CASE("the default depth clears the coherence gate in every band, and uniform seconds does not", "[mtw][layout][averaging]")`
     — record T5, and the case that makes conflict C3 a property rather than a
     paragraph. Build the Hann coefficients for each band with the real
     `rta::dsp::Window` and call the **real** `AverageCount` functions — never a
     re-derivation in the test, which would only prove the test agrees with
     itself.

     **The positive half, `Fifo`.**
     `fifoEffectiveAverages(window_k, hop_k, std::min(M, cfg.fifoDepth))` with
     `M = 16`, for every band, is **8.5866271** within `1e-6` — the SAME value in
     all seven, asserted as `Approx(neff[0])` against each other as well as
     against the constant. It clears `cfg.minimumEffectiveAverages = 8` by 7.3 %.
     Assert the fill on the way there, once (it is band-independent, so once is
     the honest number of times): frames 14, 15 and 16 read **7.5488**,
     **8.0677**, **8.5866** within `1e-4`, so 14 is under the gate and 15 is over
     it by 0.85 %. That pins why the plan's test length covers 16 frames and not
     15. At `fifoDepth = 32` the value is **16.8955**; at 8 it is **4.4393**,
     under the gate — the cap and the floor both asserted.

     **The positive half, `Exponential`.**
     `exponentialEffectiveAverages(window_k, hop_k, mtwAlpha(cfg, k), 10000)` is
     **17.1123296** within `1e-4` in every band at `timeConstantFrames = 16`, and
     again identical across bands. At `frames = 26` it is **7.5204** and at 27
     **8.0321**, so the gate opens at frame 27 — the fact that decides which
     averaging mode the engine tests use.

     **`D` is the same in every band.**
     `D = 1 + 2·Σ_{m≥1} c(m·hop)²` computed from `overlapCorrelation` is
     **1.9246389** within `1e-6` for **every** band, `N0 = 512` and `2048`
     included, because `hop/N = 1/4` in all of them. Assert the three surviving
     lags too: `c(N/4) = 0.659154943`, `c(N/2) = 0.166666667`,
     `c(3N/4) = 0.007511724` within `1e-8`, and `c(N) == 0.0` exactly. That
     equality is what makes one alpha legitimate for the whole table.

     **The negative half — uniform SECONDS, and why the Config cannot express
     it.** `MtwConfig` has no per-band time constant, so this case is asserted
     directly on `exponentialEffectiveAverages` with a hand-built per-band alpha,
     never through `MtwEngine`. For `tau = 0.5 s`,
     `timeConstantFrames_k = 0.5 * fs / hop_k` gives
     `{1.46484, 2.92969, 5.85938, 11.71875, 23.4375, 46.875, 93.75}` and
     `alpha_k = 1 - exp(-1/timeConstantFrames_k)` gives
     `{0.494732, 0.289178, 0.156897, 0.081794, 0.041769, 0.021107, 0.010610}`
     within `1e-6`, whose asymptotic `Neff` is
     `{2.061, 3.554, 6.584, 12.665, 24.839, 49.193, 97.902}` within `1e-3`.
     `REQUIRE(neff[0] < cfg.minimumEffectiveAverages)` — the 65536 band sits at
     **2.061** for ever — while `REQUIRE(neff[6] > 90.0)`. A comment on the case
     says what it is for: seconds were tried, and this is the arithmetic that
     refused them.

- [ ] **Step 2: Run and watch it fail** — header not found.
- [ ] **Step 3: Implement.** Pure arithmetic, no state, no FFT. The header
      comment must carry the C1 derivation, the ascending-vs-`k` ordering
      warning, and the `hop_k/fs` cancellation of C3 point 1 — *why* one
      `timeConstantFrames` becomes one alpha once `DualFftEngine` divides by the
      hop again.
- [ ] **Step 4: Run.** Expected **371/371** with `RTA_BUILD_APP=OFF` (362 + 9).
- [ ] **Step 5: Prove the tests can fail.** Four mutations, each reverted:
      (a) `firstBin = fftSize/8` instead of `topFftSize/8` → cases 2 and 4;
      (b) bottom band `lastBin = topFftSize/4` (the record's 257) → case 3's
      strictly-increasing assertion and the 1281 count;
      (c) `mtwAlpha` returns `1 - exp(-(hop_k/fs) / timeConstantFrames)` — i.e.
      treats the frame count as seconds → case 8's bit-identity, where the seven
      values then spread over a factor of 47. **This is the mutation that
      re-introduces seconds, and it is the one worth being able to catch;**
      (d) `mtwIntegrationSeconds` returns `fifoDepth * fftSize / fs` (the window,
      not the hop) → case 8's table, which would read 4x too large.

- [ ] **Step 6: Commit** — `feat(core): the MTW band table, with the octave that owns each bin`

**Acceptance.** `ctest --test-dir build-l3 -C Release --output-on-failure` with
`RTA_BUILD_APP=OFF` reports **371 tests passed, 0 failed**.
`core_has_no_framework_deps` prints **90 files scanned**;
`coherence_gate_is_not_bypassed` prints **59**;
`filter_design_has_no_polynomial_form` prints **106**.

---

## Task 2 — `MtwEngine`: seven engines, one delay, one stitch

**Files:**
- Create `core/include/rta/dsp/MtwResult.h` (<= 90 lines)
- Create `core/include/rta/dsp/MtwEngine.h` (<= 120 lines)
- Create `core/src/dsp/MtwEngine.cpp` (<= 190 lines)
- Create `core/tests/test_mtw_engine.cpp` (<= 300 lines)
- Modify `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`

**Interfaces produced:**

```cpp
struct MtwResult {
    MtwConfig config;
    std::vector<MtwBand> bands;                  ///< ascending in frequency
    std::vector<double> frequencyHz;             ///< mtwPointCount entries, strictly increasing
    std::vector<std::complex<double>> h;
    std::vector<float> magnitudeDb;              ///< floored at TransferSnapshot::kMagnitudeFloorDb
    std::vector<float> phaseRadians;             ///< (-pi, pi]
    std::vector<TransferSnapshot> bandSnapshots; ///< one per band, straight from makeSnapshot()
};

/// Conflict C4: MtwResult owns NO coherence array. Coherence lives in each
/// band's own TransferSnapshot, where makeSnapshot() gated it, and is read
/// through here. std::nullopt means that band has not passed its gate.
[[nodiscard]] std::optional<float> coherenceAt(const MtwResult&, std::size_t index);
[[nodiscard]] std::size_t bandForIndex(const MtwResult&, std::size_t index);

class MtwEngine {
public:
    explicit MtwEngine(const MtwConfig&);
    [[nodiscard]] const MtwConfig& config() const noexcept;
    [[nodiscard]] std::size_t bandCount() const noexcept;
    [[nodiscard]] const DualFftEngine& band(std::size_t) const noexcept;
    void process(std::span<const float> reference, std::span<const float> measurement);
    void reset() noexcept;
private:
    MtwConfig config_;
    std::vector<MtwBand> bands_;
    std::vector<DualFftEngine> engines_;   ///< constructed once, never resized
};

[[nodiscard]] MtwResult makeMtwResult(const MtwEngine&, Estimator);
```

**The reference delay: each band engine carries it itself. No new helper.**
This is an **implementation of record §2's rule**, not a departure from it. §2
says the reference delay is "applied once, at full rate, upstream of all bands,
in integer samples". *At full rate* and *in integer samples* are the measurement
content, and they are satisfied exactly: every band runs at full rate, so the
same integer `D` is the same integer in every band — there is nothing to convert
and no fractional path to invent (`DelayFinder::subSample` stays the documented
gap it already is). *Upstream of all bands* is the record describing a
mechanism, and the mechanism that already exists inside `DualFftEngine` is
better than a new one for three reasons:

1. `DualFftEngine::reset()` re-arms the skip from its **own** config
   (`core/src/dsp/DualFftEngine.cpp:308-312`: "a skip counter left mid-count from
   before the reset would mis-align the next stream"). An upstream skipper would
   have to duplicate that re-arm and stay in step with seven engines' resets — a
   second place to get alignment wrong, which is exactly what the dual-FFT record
   §4 exists to refuse.
2. The skip logic lives inside `DualFftEngine::process`'s prologue
   (`core/src/dsp/DualFftEngine.cpp:155-200`), in a file this lane may not edit.
   A helper would be a hand copy of it.
3. The cost is `bandCount() × |D|` sample copies, **once, ever**: 28,693 copies
   at `D = 4099`. The rings are already sized for it —
   `bufferCapacity(fftSize, skipMagnitude)` folds `|D|` straight in
   (`core/src/dsp/DualFftEngine.cpp:23-25`).

**The record has been amended to say so.** §2 no longer says "upstream of all
bands"; it says the delay is applied "at full rate in integer samples" and then
records this as the station-3 implementation choice, with the `reset()` re-arm
and the repeated one-time skip as its stated cost. Nothing about the measurement
changed; if it had, this would be a conflict entry and not a paragraph. Task 8
step 2 verifies that amendment is committed rather than only in the tree.

`MtwEngine`'s constructor therefore copies `referenceDelaySamples` into each
band's `DualFftEngine::Config` unchanged, and the `|D| <= 1<<20` bound is
enforced by the first band's construction throwing
(`core/src/dsp/DualFftEngine.cpp:40-44`) before the vector is complete — RAII
cleans up the bands already built.

**Real-time contract.** All allocation is in the constructor: `engines_` is
`reserve`d to `bandCount()` and emplaced once, and each `DualFftEngine` allocates
in its own constructor. **`MtwEngine::process` allocates nothing** — it is
`for (auto& e : engines_) e.process(reference, measurement);` and nothing else.
Stitching allocates, so stitching is `makeMtwResult()`, a free function called
from the publish path, never from `process()` — the same split `makeSnapshot()`
already has.

**The frames-to-seconds conversion, in exactly one place.** Band `k`'s
`DualFftEngine::Config` gets
`timeConstantSeconds = config_.timeConstantFrames * hop_k / sampleRate`, so the
engine's own `alpha = 1 - exp(-(hop_k/fs)/tau_k)` collapses to
`1 - exp(-1/timeConstantFrames)` — identical in every band (C3 point 1). Set it
**whatever the mode**, so a later switch to `Exponential` is not a silent zero.
`fifoDepth` is copied through unchanged when `averaging == Fifo`, and forced to
**1** when `Exponential`, because the engine allocates the FIFO either way
(memory rule 1).

- [ ] **Step 1: Write the failing tests** — `core/tests/test_mtw_engine.cpp`

  1. `TEST_CASE("a compensated pure delay reads unity across every band", "[mtw][engine]")`
     — T2. `x` = float32 white noise, `n = 327680` samples (the derived length
     above: 17 frames in the bottom band, `Neff = 8.5866271` in every band, clear
     of the gate of 8 by 7.3 %); `y[i] = x[i - D]` with the leading `D` samples
     zero; `D = 137`; `referenceDelaySamples = D`; otherwise the shipping
     defaults (`Fifo`, `fifoDepth = 16`). After the skip the two streams are the
     SAME samples, and `analyseFramePair()` promotes both to `double` from the
     same `float` bins (`core/src/dsp/DualFftEngine.cpp:236-238`), so
     `Sxy == Sxx` **bit-exactly**. Assert, for every stitched index `i` whose
     owning band has `Sxx[bin] > 0`: `h[i].real()` within `1e-12` of `1.0`,
     `h[i].imag()` exactly `0.0`, `magnitudeDb[i]` within `1e-9` of `0.0`,
     `phaseRadians[i]` exactly `0.0`, and `coherenceAt(result, i)` present and
     within `1e-12` of `1.0`. This is an identity, not a tolerance: **do not
     loosen it.** If it fails, the stitch is reading the wrong band's bins.
  2. `TEST_CASE("a delay divisible by no power of two still reads unity", "[mtw][engine]")`
     — the same, with `D = 4099` (odd, and larger than the hop of bands 0–3).
     Record §7 T2: "the case a decimated design could not have passed without a
     fractional delay". Same assertions, same tolerances.
  3. `TEST_CASE("coherence is withheld per band until that band has its own 16 frames", "[mtw][engine]")`
     — record §5. Under frames-uniform averaging the gate is no longer a
     permanent exclusion of the low bands; it is a **fill time that differs per
     band**, `fifoDepth * hop_k / fs`, and this case measures exactly that.
     Feed the **short** derived length, `n = 180224`: the bottom band has 8
     frames (`Neff = 4.4393`, under 8) while every other band is saturated at
     8.5866271. Assert `bandSnapshots[0].coherence == std::nullopt` and
     `coherenceAt(result, i) == std::nullopt` for every `i` in `[0, 256)`, and a
     value present for every `i` in `[256, 1281)`. Then feed the same engine on
     to the full `n = 327680` and assert **every** band now has coherence,
     bottom included, with `bandSnapshots[0].effectiveAverages` within `1e-6` of
     **8.5866271**. The negative and the positive are the same engine at two
     lengths, which is what makes the negative mean "not yet" rather than
     "never" — and "not yet" is the whole difference the §5 reversal bought.
  4. `TEST_CASE("every band advances by its own hop, and reports its own integration seconds", "[mtw][engine]")`
     — T5's runtime half. After `n = 327680`, `band(i).frameCount()` equals
     `1 + (n - N_i)/hop_i` exactly, i.e. `{17, 37, 77, 157, 317, 637, 1277}` in
     ascending-frequency order (the length section derives these; recompute, do
     not copy). Then the property the frames-uniform record exists for:
     `band(i).effectiveAverages()` is **the same number in every band**, 8.5866271
     within `1e-6` — assert it against `band(0).effectiveAverages()` as well as
     against the constant. And the seconds that are *not* the same:
     `bands[i].integrationSeconds` is `{5.4613, 2.7307, 1.3653, 0.6827, 0.3413,
     0.1707, 0.0853}` within `1e-4`, each exactly twice its neighbour. Uniform
     averages, non-uniform seconds, both asserted in one case — that pair IS the
     decision §5 records.
  5. `TEST_CASE("reset re-arms the reference delay in every band", "[mtw][engine]")`
     — feed 2 s of the `D = 137` pair, `reset()`, feed the **whole**
     `n = 327680` pair from the top again, and assert case 1's unity identity
     still holds. The full length after the reset is not decoration: `reset()`
     zeroes every band's `frameCount_` (`core/src/dsp/DualFftEngine.cpp:313`), so
     the bottom band needs its 16 frames again — 6.485 s — before its gate
     reopens, and a shorter re-feed would assert `coherenceAt() == 1.0` on a band
     that is legitimately `nullopt`. A skip that forgot to re-arm shows up at
     high frequency first.
  6. `TEST_CASE("the stitched result exposes one snapshot per band, in order", "[mtw][engine]")`
     — `bandSnapshots.size() == bands.size() == 7`;
     `bandSnapshots[i].binWidthHz == fs / bands[i].fftSize` within `1e-12`;
     `bandForIndex(result, 0) == 0`, `(…, 255) == 0`, `(…, 256) == 1`,
     `(…, 1280) == 6`; `band(i).config().fifoDepth == 16` in the default `Fifo`
     mode and `== 1` for every band in `Exponential` mode (the memory claim, made
     falsifiable — see step 5c); `band(i).config().timeConstantSeconds` equals
     `16 * hop_i / fs` so that `mtwAlpha` and the engine's own alpha are the same
     number (C3 point 1, and the assertion that catches a builder passing frames
     through as seconds); and `MtwEngine::process` throws `std::invalid_argument`
     on spans of different lengths (delegated, but assert it — a caller must not
     discover that in only one band).

- [ ] **Step 2: Run and watch it fail.**
- [ ] **Step 3: Implement** `MtwResult.h`, `MtwEngine.h`, `MtwEngine.cpp`.
- [ ] **Step 4: Run.** Expected **377/377** with `RTA_BUILD_APP=OFF`.
- [ ] **Step 5: Prove the tests can fail.** Mutations, each reverted:
      (a) drop `referenceDelaySamples` from the band Config → cases 1, 2, 5;
      (b) offset `firstIndex` by one band's point count → cases 1 and 6;
      (c) leave `fifoDepth` at the `DualFftEngine` default 16 in exponential mode
      → case 6's `fifoDepth == 1` assertion. A memory claim no test can fail is
      not a claim, which is why that assertion is in case 6 rather than a comment;
      (d) pass `timeConstantFrames` straight into `timeConstantSeconds` without
      the `* hop_k / fs` → case 6's `timeConstantSeconds` assertion in every band
      but whichever one happens to have `hop_k == fs`, i.e. all seven.
- [ ] **Step 6: Commit** — `feat(core): seven windows, one curve, and a delay applied once per band`

**Acceptance.** OFF build reports **377 tests passed, 0 failed**.
`core_has_no_framework_deps` prints **94**; `coherence_gate_is_not_bypassed`
prints **62** *and still passes* — if it fails, conflict C4's resolution was not
followed. `filter_design_has_no_polynomial_form` prints **110**.

---

## Task 3 — What MTW is FOR: the biquad across every seam, and the reflection

No production code. This task proves the lane did something a fixed FFT cannot,
and it is separated from task 2 so `test_mtw_engine.cpp` stays under the
400-line cap — the split file name is declared here rather than improvised
later, the way `test_transfer_view_unwrap.cpp` had to be.

**Files:** create `core/tests/test_mtw_response.cpp` (<= 300 lines); modify
`core/tests/CMakeLists.txt`.

- [ ] **Step 1: Write the failing tests.**

**Every tolerance in this task comes from one `Neff`.** Under frames-uniform
averaging every band reads `Neff = 8.5866271` at the derived test length
(C3 point 3), so the estimator's own random error is a single derivation instead
of seven. Bendat & Piersol's normalised random error for `H1`, and the standard
deviation and small-sample bias of the coherence estimate, are

```
eps_r(|H1|) = sqrt(1 - g2) / ( sqrt(g2) * sqrt(2 * Neff) )
sd(g2_hat)  = sqrt(2) * g2 * (1 - g2) / sqrt(Neff)
bias(g2_hat) ~ (1 - g2) / Neff                       (upward, Carter)
```

At `Neff = 8.5866271` those are **12.1 % per bin** on `|H1|` where `g2 = 0.8`,
`sd = 0.077` and `bias = +0.023` on coherence. **That is the honest per-bin
figure, and it is why the reflection cases below assert band STATISTICS — a mean
over a band's owned points, and a standard deviation across them — rather than a
tight per-bin bound that no amount of tightening could make true.** The noiseless
biquad of case 2 is the exception: `g2 = 1` there, so `eps_r = 0` and its
per-point bounds stay as tight as float32 and Hann leakage allow.

- [ ] **Step 1: Write the failing tests.**

  1. `TEST_CASE("overlapCorrelation on a Hann window matches (2 + cos u)/3", "[mtw][window]")`
     — an independent closed form for `r_w`, so T4 is not checking the engine
     against the same function twice. For periodic Hann the **circular**
     autocorrelation is exactly `c(m) = (2 + cos(2πm/N)) / 3`: expand
     `w[n] = 0.5 − 0.5 cos θ_n`, use `Σ cos θ_n = 0` and
     `Σ cos θ_n cos(θ_n+u) = (N/2) cos u`, and divide by `Σ w² = 3N/8`. The
     non-wrapping form `overlapCorrelation` computes is that minus a tail whose
     terms are all `O((m/N)⁴)` near the window's zeros — 6e-8 at
     `m/N = 1440/65536`. Assert
     `overlapCorrelation(hann(65536), 1440) == 0.99682836` within `1e-6`
     (the value of `(2 + cos(2π·1440/65536))/3`), and
     `overlapCorrelation(hann(1024), 1440) == 0.0` **exactly** — the
     "lag beyond the window" identity, and the reason band 0 has no comb.
     Assert the same closed form at `hop` lags too, where it is the `D` of
     C3 point 2: `c(N/4) = 0.659154943` against `(2 + cos(π/2))/3 = 2/3` minus
     its tail, so the two forms are compared, not conflated.
  2. `TEST_CASE("a biquad reads back its closed form across every seam", "[mtw][response]")`
     — T3. `rta::dsp::Biquad::Coeffs{b0 = 0.3, b1 = −0.2, b2 = 0.1, a1 = −0.5,
     a2 = 0.2}` — the same section `tools/gen_transfer.py:118-120` already uses,
     so two lanes agree on one system. White float32 noise, **`n = 327680`**, and
     the **shipping defaults** — `Fifo`, `fifoDepth = 16`, which put every band
     at `Neff = 8.5866271`, over the gate of 8, so the coherence assertion below
     runs in all seven. (The first draft said `Fifo, fifoSeconds = 8.0` and the
     second `Exponential, timeConstantSeconds = 3.5`; §5 now has neither knob.)
     Expected
     `H(e^{jw}) = (b0 + b1 e^{−jw} + b2 e^{−2jw}) / (1 + a1 e^{−jw} + a2 e^{−2jw})`
     at `w = 2π f[i] / fs`. Assert for every stitched index `1 <= i < 1280`
     (skipping DC, whose phase is degenerate and which no log axis can draw, and
     the Nyquist point): `| |H_meas| / |H_exact| − 1 | < 5e-3` (±0.043 dB) and
     `|arg H_meas − arg H_exact| < 0.01 rad` (0.57°), wrapped.
     Those stay per-point and stay tight **because this system is noiseless and
     LTI**: `g2 = 1`, so `eps_r = 0` and the only error is Hann leakage on a
     smooth response plus float32. Neither depends on `Neff`, which is why
     lowering the averaging from the old draft's 190 effective averages to 8.59
     does not move these two numbers.
     **There is no separate seam test:** a band that stitched at the wrong
     offset, or normalised differently, fails here at the 128-point boundaries.
     Also assert `coherenceAt(result, i) > 0.99` at every index — a noiseless LTI
     system has unit coherence, and this catches a per-band PSD scaling error
     that a magnitude ratio would hide. (0.99, not 0.999: the upward-biased
     estimator sits at `1 − O(1/Neff)` from leakage alone at `Neff = 8.59`.)
  3. `TEST_CASE("a reflection is admitted in proportion to the window autocorrelation", "[mtw][response]")`
     — T4, the test that proves MTW does the thing it is for.
     `y[i] = x[i] + a·x[i − τ]`, `a = 0.5`, `τ = 1440` samples (30 ms at 48 kHz),
     white float32 `x`, `n = 327680`, shipping defaults (`Fifo`,
     `fifoDepth = 16`).
     For each band compute `r_k = overlapCorrelation(hannCoefficients(N_k), 1440)`
     — `overlapCorrelation` takes the window's coefficient span and a lag in
     **samples** (`core/include/rta/dsp/AverageCount.h:20-21`) — giving, in
     ascending-frequency order,
     `{0.9968283, 0.9873724, 0.9504070, 0.8151663, 0.4313682, 0.0168888, 0.0}`,
     and expect `H = 1 + a·r_k·e^{−jwτ}` at every owned bin. Three assertions,
     each with its derivation:

     - **Per band, the RMS residual.**
       `rms_k = sqrt(mean_i (|H_meas,i| − |H_exact,i|)²)` over that band's owned
       points. Its expected value is `eps_r(g2_k) · rms|H|`, and `g2_k` is
       `|1 + a r_k e^{−jwτ}|² / (1 + a² + 2 a r_k cos wτ)` at its worst point:
       `{0.0191, 0.0378, 0.0715, 0.1180, 0.1388, 0.1217, 0.1207}` for
       `eps_r`, ascending in frequency. Assert `rms_k < 0.20` in every band —
       comfortably above the largest expectation (0.139) and far below the
       **0.372** an expectation that dropped the `r_k` factor would produce
       (`sqrt(0.121² + 0.352²)`). That factor-of-two-and-a-half gap is the whole
       discriminating power of this case, and it is why the assertion is an RMS
       over a band rather than a bound per bin: at 12 % per bin a per-bin bound
       loose enough to pass would be loose enough to miss the mutation.
     - **Band 0 is the anchor and is exact in form.** `N_0 = 1024 < 1440`, so
       `r_0 = 0` **exactly** and `|H| = 1` at every one of its 385 points. Assert
       `|mean_i |H_meas,i| − 1| < 0.03` — the standard deviation of that mean is
       `0.1207/sqrt(385) = 0.0061`, so 0.03 is five of them — and that the band
       carries **no comb**: `stddev_i |H_meas,i| < 0.20`, against an expectation
       of 0.121 from estimator noise alone.
     - **The bottom band carries the comb.** `r_6 = 0.9968283`, so `|H|` sweeps
       **0.5015858 … 1.4984142**. Assert `max|H| > 1.35` and `min|H| < 0.65`
       over band 6 (expected extremes minus four standard deviations of
       `eps_r = 0.0191`, i.e. 1.384 and 0.540), and
       `stddev_i |H_meas,i| > 0.25` against an expectation of **0.344** — the
       comb's own `a·r/sqrt(2) = 0.352` slightly reduced by `|·|` being a
       magnitude. Band 0 under 0.20 and band 6 over 0.25 in the same run is the
       measurement, stated as one inequality apiece.
  4. `TEST_CASE("the reflection lands in the top band's coherence, not its magnitude", "[mtw][response]")`
     — the other half of T4, and the sign of the seam step.
     `γ² = |1 + a·r·e^{−jwτ}|² / (1 + a² + 2·a·r·cos wτ)`.
     **Band 0** (`r = 0`): `γ² = 1/(1 + a²) = 0.80` exactly, at every point. At
     `Neff = 8.5866271` the estimate has `sd = 0.0772` per point and a `+0.0233`
     upward bias, so a per-point ±0.03 bound is arithmetically impossible;
     assert instead `|mean_i γ²_i − 0.80| < 0.05`, whose own standard deviation
     is `0.0772/sqrt(385) = 0.0039` and whose expected centre is 0.823.
     **Band 6** (`r = 0.9968283`): `γ²` stays in `[0.993746, 0.999295]`, where
     `sd = 0.0030` per point; assert **every** owned point above `0.97`
     (0.9937 minus four standard deviations is 0.982) and
     `mean_i γ²_i > 0.99`.
     **And the separation, which is the actual claim:**
     `mean(γ² over band 6) − mean(γ² over band 0) > 0.10`, against an expected
     0.171. Two bands, the same room, opposite readings: that is the measurement
     MTW exists to make, and it is why the 187.5 Hz seam legitimately steps by
     the comb depth rather than being cross-faded away (§4).

- [ ] **Step 2: Fail.** — [ ] **Step 3:** no implementation; if these do not pass
      against task 2's code, task 2 is wrong. — [ ] **Step 4: Run.** Expected
      **381/381** OFF.
- [ ] **Step 5: Prove the tests can fail.** (a) Build every band at the same
      `fftSize` (refutation R1) → cases 3 and 4 must both fail; if they pass, the
      tests are not reading per-band data. (b) Drop the `a·r` factor to a bare `a`
      → case 3's `rms_k` at bands 4–6 (ascending order), where the expectation
      moves by 0.35 against a 0.20 bound. (c) Use the circular (wrapping)
      autocorrelation → case 1 at `lag = 1440, N = 1024`, which must be exactly 0.
      (d) Set `fifoDepth = 1` in these cases → `Neff = 1`, every band gated, case
      2's and case 4's coherence assertions dead — the check that the tolerance
      derivation above is actually load-bearing.
- [ ] **Step 6: Commit** — `test(core): the reflection the short window refuses and the long one shows`

**Acceptance.** OFF build reports **381 tests passed, 0 failed**.
`core_has_no_framework_deps` prints **95**;
`filter_design_has_no_polynomial_form` prints **111**.

---

## Task 4 — `tools/gen_mtw.py` and the golden

**Files:**
- Create `tools/gen_mtw.py` (<= 190 lines)
- Create `core/tests/golden/mtw.txt` (generated, committed, ≈ 1.2 MB)
- Create `core/tests/test_mtw_golden.cpp` (<= 200 lines)
- Modify `core/tests/CMakeLists.txt`

**The generator must take an argument.**
`memory/a-gen-script-runs-the-moment-you-invoke-it.md`: `tools/gen_transfer.py`
has no argparse, so `--help` runs the whole pipeline and rewrites the golden.
`gen_mtw.py` uses `argparse` with `--out PATH` (a default is fine; *parsing* is
not optional), so `--help` exits before any write. Add `--check`, which
regenerates into a temporary path and diffs, so the determinism property that
turned that accident into a no-op is tested deliberately.

**No `signal.lfilter`, no `output='ba'`.** `filter_design_has_no_polynomial_form`
scans `tools/*.py` (`core/tests/check_no_polynomial_form.cmake:17-21`). If a
filter is needed at all, use `signal.sosfilt` with an explicit SOS row, exactly
as `tools/gen_transfer.py:119-120` does.

**Golden table: `N0 = 512`, `K = 3`, `fs = 48000`, `n = 19456`.** Not the default
table — the default's bottom band is 65536 points and would need ≈ 330 k samples
of committed float text to reach its 16 frames, i.e. a 6 MB golden. The scaled
table exercises the identical layout, stitch and scaling code (four bands: 512,
1024, 2048, 4096; 449 points; owned bins 64..256 / 64..127 / 64..127 / 0..127) at
about 1.2 MB — the same order as the existing `transfer.txt` (1.06 MB). The
**default** table is covered by task 1's closed forms and by the layout block
below.

**Why `n = 19456` and not the old draft's 16384.** `19456 = 4096 + 15 * 1024` is
the smallest length that gives the **bottom** band its 16 frames, and `n - N_k`
is an exact multiple of `hop_k` in all four bands, so every band's frame count is
a stated integer:

| `N_k` | 4096 | 2048 | 1024 | 512 |
|---|---|---|---|---|
| `hop_k` | 1024 | 512 | 256 | 128 |
| frames at `n = 19456` | 16 | 35 | 73 | 149 |
| frames the FIFO holds (`min(frames, 16)`) | 16 | 16 | 16 | 16 |
| `Neff` | 8.5866271 | 8.5866271 | 8.5866271 | 8.5866271 |

Every band holds exactly 16 frames and every band clears the gate, so the
golden's coherence rows are comparable in all four — which the old 16384 length
would not have been (its bottom band had 13 frames, `Neff = 7.030`, gated).

**The generator matches the FIFO's window, not the whole file.** The old draft
made the FIFO mean equal scipy's plain mean by choosing `fifoSeconds` so large
that no band ever dropped a frame. **That is no longer available**: with a
frames-uniform `fifoDepth` capped at 32, and band frame counts spread 16:149 by
the octave doubling, no legal depth exceeds every band's frame count. So the
generator slices instead. Band `k`'s FIFO holds frames
`F_k - 16 .. F_k - 1`, which span the samples
`[(F_k - 16) * hop_k, n)` — a tail of `N_k + 15 * hop_k` samples:

| `N_k` | 4096 | 2048 | 1024 | 512 |
|---|---|---|---|---|
| tail start | 0 | 9728 | 14592 | 17024 |
| tail length | 19456 | 9728 | 4864 | 2432 |
| segments `1 + (len - N)/hop` | 16 | 16 | 16 | 16 |

`scipy.signal.csd` / `welch` on `x[start:n]` with `nperseg = N_k`,
`noverlap = 3*N_k/4`, `window="hann"`, `detrend=False`, `average="mean"` then
produces **exactly the 16 frames the engine's FIFO holds, at the same sample
offsets**, because both start a frame every `hop_k` samples from their own
origin and `start` is a multiple of `hop_k`. This is a better golden than the
old premise, not a weaker one: it exercises the FIFO's drop-off, which the
"never drop a frame" trick specifically avoided.

Two blocks, in the repo's existing line-based golden format:

```
case mtw_layout_default
n0 1024
octaves 6
point_count 1281
first_index 0 256 384 512 640 768 896
fft_size 65536 32768 16384 8192 4096 2048 1024
first_bin 0 128 128 128 128 128 128
last_bin 255 255 255 255 255 255 512
integration_seconds 5.4613333 2.7306667 1.3653333 0.6826667 0.3413333 0.1706667 0.0853333
freq <1281 values>
end

case mtw_stitched_512_3
n0 512
octaves 3
fifo_depth 16
size 19456
input_x <19456 values>   # generated in float32, widened to float64 exactly
input_y <19456 values>
freq <449 values>
pxx <449>  pyy <449>  pxy_real <449>  pxy_imag <449>  coherence <449>
end
```

The stitched rows are built in Python by calling `scipy.signal.csd` / `welch`
**once per band on that band's own tail slice**, then slicing each band's owned
bins and concatenating; `coherence` is formed as `|Sxy|²/(Sxx·Syy)` from those
same three rows rather than by a fourth scipy call, so the golden cannot disagree
with itself. The layout block is computed from §3's formulas in Python, so
task 1's C++ closed forms face a second author, and `integration_seconds` puts
the frames-uniform decision of §5 in the committed file where a reviewer can see
it.

- [ ] **Step 1: Write `test_mtw_golden.cpp` against a file that does not exist yet.**

  1. `TEST_CASE("the golden's layout block matches MtwLayout for the default table", "[mtw][golden]")`
     — `mtwPointCount == 1281`, the four index/bin/size rows and all 1281
     frequencies match within `1e-9`, and the seven `integration_seconds` match
     `mtwIntegrationSeconds(defaults, k)` within `1e-7`.
  2. `TEST_CASE("the MTW golden from scipy matches the stitched Sxx, Syy, Sxy and coherence", "[mtw][golden]")`
     — build `MtwEngine` with `N0 = 512, K = 3, fs = 48000`,
     `averaging = Fifo`, **`fifoDepth = 16`** (the shipping default), and feed
     the golden's 19456 samples. `REQUIRE(band(i).frameCount() >= 16)` and
     `REQUIRE(band(i).effectiveAverages() == Approx(8.5866271).epsilon(1e-6))`
     for every band — the premise that the engine averaged the same 16 frames the
     generator did, stated as an assertion the way
     `core/tests/test_transfer_golden.cpp:87` states its own.
     **Do not reach for a huge `fifoDepth` the way that test does**: `validate()`
     caps it at 32, and even at 32 the default table would hold 66.6 MB of FIFO.
     The FIFO's depth is real memory in this engine, which is the whole reason
     the cap is in `validate()` and not in a comment.
     Tolerance is the two-term float32 form, **per band**
     (`memory/float32-fft-precision.md`):
     `tolerance(expected, bandPeak) = |expected| · 1e-5 + bandPeak · 1e-6`, with
     the peak taken over that band's own owned slice, never globally — a
     4096-point band's rounding floor and a 512-point band's are different
     numbers, and a global peak silently hands the tight band the loose one's
     tolerance.

- [ ] **Step 2: Fail** — `loadGolden` throws, which is the point of it throwing.
- [ ] **Step 3: Write `gen_mtw.py`, run it, commit the .txt.**

```
"D:/DEV CAVE EP3/PRJ010-RTA-TOOL/.venv/Scripts/python.exe" tools/gen_mtw.py --out core/tests/golden/mtw.txt
```

      From the **main checkout's** venv by absolute path — there is none in this
      worktree, and building a second one is HANDOFF trap #8. Then run it again
      with `--check`, confirm byte-identity, and record that in the report.
- [ ] **Step 4: Run.** Expected **383/383** OFF.
- [ ] **Step 5: Prove the golden can fail.** Perturb one `pxy_imag` value in the
      committed file by `1e-3`, watch case 2 fail at that index, revert. Then
      rename `mtw.txt` and confirm the test throws rather than passing on an
      empty case list — `REQUIRE(checked == 2)`, the same shape
      `test_transfer_golden.cpp:130` uses.
- [ ] **Step 6: Commit** — `test(core): a scipy golden for the stitch, one band at a time`

**Acceptance.** OFF build reports **383 tests passed, 0 failed**.
`filter_design_has_no_polynomial_form` prints **113** and passes — the new
`gen_mtw.py` is inside its scan. `core_has_no_framework_deps` prints **96**.

---

## Task 5 — Prove the guards are watching

No files created. This is a procedure, and its output goes in the report.

- [ ] **Step 1: RED for `core_has_no_framework_deps`.** Insert
      `#include <juce_core/juce_core.h>` at the top of
      `core/include/rta/dsp/MtwEngine.h`, reconfigure, run
      `ctest --test-dir build-l3 -C Release -R core_has_no_framework_deps` and
      **paste the failure**, which must name `MtwEngine.h`. Remove the include.
      This is the L4a/L4b procedure and it exists because a guard whose new files
      fall outside its glob passes while watching nothing.
- [ ] **Step 2: GREEN.** Re-run and paste `OK (96 files scanned)`. **96, read
      from the output, not from this plan.** If it still says 87, the new files
      landed somewhere the glob does not reach.
- [ ] **Step 3: RED for `coherence_gate_is_not_bypassed`.** Add
      `result.coherence = std::vector<float>(n);` to `MtwEngine.cpp`, confirm the
      guard fails naming that file, remove it. Confirm the count is **62**.
- [ ] **Step 4: RED for `filter_design_has_no_polynomial_form`.** Put
      `signal.lfilter` inside a comment in `gen_mtw.py`, confirm failure, remove.
      Count **113**.
- [ ] **Step 5: File lengths.**
      `wc -l core/include/rta/dsp/Mtw*.h core/src/dsp/Mtw*.cpp core/tests/test_mtw_*.cpp`
      — every one under 400. Confirm
      `wc -l core/src/dsp/DualFftEngine.cpp core/tests/test_dualfft.cpp` still
      reads **321** and **400**, and that
      `git diff --stat main -- core/src/dsp/DualFftEngine.cpp core/include/rta/dsp/DualFftEngine.h core/tests/test_dualfft.cpp`
      is empty.
- [ ] **Step 6: Commit** — `test(core): the MTW guards, made red once before they were believed`

**Acceptance.** A clean OFF build — `--clean-first` before any number reported
(HANDOFF trap #11) — gives **383/383**.
**L3a test-count delta: +21 (362 → 383).**

---

# L3b — `app/`

## Task 6 — `MtwBlock` in the snapshot, `MtwEngine` in the analyser

**Files:**
- Modify `app/src/measure/Snapshot.h` (109 → ≈ 160 lines)
- Modify `app/src/measure/Analyser.h` (142 → ≈ 165), `Analyser.cpp` (205 → ≈ 250)
- Create `app/tests/test_analyser_mtw.cpp` (<= 250 lines)
- Modify `app/tests/CMakeLists.txt`

**Snapshot additions:**

```cpp
struct MtwBandDescriptor {
    std::size_t firstIndex = 0;
    std::size_t pointCount = 0;
    std::size_t fftSize = 0;
    float windowSeconds = 0.0f;        ///< fftSize / fs -- the window's own length
    /// fifoDepth * hopSize / fs. NOT windowSeconds, and NOT the same in every
    /// band: record §5 buys uniform effectiveAverages with non-uniform seconds,
    /// 85 ms at the top and 5.461 s below 187.5 Hz, and requires the view to
    /// print which. A coherence trace that fills in from the top over five
    /// seconds is read as a fault unless the number is on screen.
    float integrationSeconds = 0.0f;
    double effectiveAverages = 0.0;    ///< 8.5866271 in EVERY band once filled
    /// The lower edge of this band's owned octave, and therefore the seam the
    /// view draws. 0 for the bottom band, which has no lower neighbour.
    float seamHz = 0.0f;
    /// False when this band has not passed its own gate. Absence of coherence
    /// is PER BAND here, not per block: the bottom band is still filling while
    /// the top has been measuring for seconds (record §5, conflict C3). A
    /// single std::optional on the block would force the whole curve to be
    /// coherence-less because of the slowest band.
    bool coherenceAvailable = false;
};

struct MtwBlock {
    std::vector<double> frequencyHz;   ///< 1281 by default; strictly increasing; [0] is DC
    std::vector<float> magnitudeDb;
    std::vector<float> phaseDeg;       ///< the same radians->degrees crossing Analyser already owns
    std::vector<float> coherence;      ///< meaningful only inside bands whose descriptor says so
    std::vector<MtwBandDescriptor> bands;  ///< ascending in frequency
    int appliedDelaySamples = 0;
};

// in Snapshot:
std::optional<MtwBlock> mtw;
```

`Analyser::Config` gains `bool mtwEnabled = true`, `std::size_t mtwTopFftSize = 1024`,
`std::size_t mtwOctaveCount = 6`, `TransferAveraging mtwAveraging = Fifo`,
`std::size_t mtwFifoDepth = 16` and `double mtwTimeConstantFrames = 16.0` — all
in **frames** (record §5). Deliberately not named after the fixed engines'
`timeConstantSeconds`, which stays at 0.5 s and means something else: the fixed
engine has one hop, so its seconds and its frames are interchangeable; MTW has
seven hops and they are not. The `MtwEngine` is a member `mtw_`,
constructed **in the constructor's member-initialiser list** beside `dual_`
(`app/src/measure/Analyser.cpp:86`) — it has no default constructor, same as
`DualFftEngine`, so there is nowhere else to build it. `pushPair` feeds it after
`dual_`; `reset()` resets it; `publish()` calls `makeMtwResult()` and flattens,
reading coherence through `coherenceAt()` and setting `coherenceAvailable` from
whether the band's snapshot has a value.

**Sample-rate change: nothing new is needed.**
`AnalysisThread::rebuildAnalyserIfEpochChanged()` already builds a **fresh
`Analyser`** at `bus.sampleRate()` on every epoch change
(`app/src/measure/AnalysisThread.cpp:87-107`), so the `MtwEngine` is rebuilt with
it and no MTW-specific path exists to get wrong. The one thing to record,
because it is new and real: that construction now allocates about **45.3 MB**
at the `Fifo` default, or **14.0 MB** in `Exponential` mode (memory section),
on the analysis thread. That thread is not the audio callback, so
it is legal, but it is a multi-millisecond stall on a device change and it
belongs in the header comment beside the existing note about paying for the dual
engine's buffers (`app/src/measure/Analyser.h:52-56`).

- [ ] **Step 1: Write the failing tests** — `app/tests/test_analyser_mtw.cpp`
  1. `TEST_CASE("no MTW block before the first pushPair", "[analyser][mtw]")` —
     `pushMeasurement` alone leaves `snapshot->mtw` empty. A single-channel
     capture has no transfer function; it does not have a flat one.
  2. `TEST_CASE("Analyser publishes an MTW block once a pair has been pushed", "[analyser][mtw]")`
     — `mtw->frequencyHz.size() == 1281`, and `magnitudeDb`, `phaseDeg` and
     `coherence` are all that same length.
  3. `TEST_CASE("the MTW block carries an explicit frequency vector, not a bin width", "[analyser][mtw]")`
     — `frequencyHz[256] == 187.5`, `[384] == 375.0`, `[896] == 6000.0`,
     `[1280] == 24000.0`, strictly increasing throughout; and
     `snapshot->fftSize` is still the **fixed** engine's size, untouched, so
     `TransferBlock` consumers cannot be confused by the new block.
  4. `TEST_CASE("the MTW block's descriptors mark every band boundary", "[analyser][mtw]")`
     — `bands.size() == 7`; `seamHz` in order is
     `{0, 187.5, 375, 750, 1500, 3000, 6000}`; `firstIndex` is
     `{0, 256, 384, 512, 640, 768, 896}`; `pointCount` sums to 1281;
     `windowSeconds[0]` is 1.3653 within `1e-4`. And the pair record §5 exists
     for: `effectiveAverages` is **8.5866271 in every band** within `1e-6` once
     every band has 16 frames, while `integrationSeconds` is
     `{5.4613, 2.7307, 1.3653, 0.6827, 0.3413, 0.1707, 0.0853}` within `1e-4`,
     each exactly twice its neighbour. Uniform averages, non-uniform seconds,
     carried all the way to the struct the view reads.
  5. `TEST_CASE("phase crosses to degrees exactly once, in the same place the fixed block does", "[analyser][mtw]")`
     — feed task 2 case 1's compensated-delay pair; assert every `phaseDeg` is
     `0.0f` and every `magnitudeDb` matches a directly constructed `MtwEngine`'s
     bit for bit. This is what catches a stray radians/degrees or dB/linear
     conversion in the flattening.

- [ ] **Step 2: Fail.** — [ ] **Step 3: Implement.** — [ ] **Step 4: Run.**
      OFF expected **388/388**; ON expected **428/428**.
- [ ] **Step 5: Prove they can fail.** (a) Publish the MTW block unconditionally
      → case 1; (b) fill `frequencyHz` with `i · binHz` from `fftSize` → cases 3
      and 4; (c) skip the degrees conversion → case 5; (d) copy `windowSeconds`
      into `integrationSeconds` → case 4, where the two differ by exactly the
      factor `fifoDepth/4`.
- [ ] **Step 6: Commit** — `feat(app): the snapshot learns a frequency vector of its own`

**Acceptance.** `ctest` OFF **388/388**, ON **428/428**.
`measure_has_no_framework_deps` still prints **29** (Snapshot.h and Analyser.*
are already in its list; no new file yet) and passes.

---

## Task 7 — Drawing it: an explicit frequency vector, seams, and a per-plot source

**Files:**
- Create `app/src/view/ColumnMap.h` (<= 120 lines, JUCE-free, pure)
- Create `app/src/view/MtwLayer.h` + `MtwLayer.cpp` (<= 130 / <= 220 lines, JUCE)
- Modify `app/src/view/TransferView.h` (101 → ≈ 125), `TransferView.cpp`
  (328 → **must stay under 400**)
- Create `app/tests/test_column_map.cpp` (<= 150),
  `app/tests_juce/test_transfer_view_mtw.cpp` (<= 250)
- Modify `app/tests/CMakeLists.txt` (source list **and** the `GLOBS` guard list),
  `app/tests_juce/CMakeLists.txt`

**Why three files and not one edit.** `TransferView.cpp` is at 328 of a 400-line
cap. `absoluteColumnsForBins` (`app/src/view/TransferView.cpp:99-124`) moves into
`ColumnMap.h` unchanged and gains a sibling that takes an explicit frequency
span:

```cpp
[[nodiscard]] std::vector<int> absoluteColumnsForBins(const PlotGeometry&, double binHz,
                                                      std::size_t bins, std::size_t columnCount);
[[nodiscard]] std::vector<int> absoluteColumnsForFrequencies(const PlotGeometry&,
                                                             std::span<const double> hz,
                                                             std::size_t columnCount);
```

Both delegate to one inner loop, so the mapping cannot drift between the fixed
and MTW paths. **`hz[0]` is exactly 0 for the MTW block**, and
`PlotGeometry::xForHz(0.0)` computes `log(0)` — non-finite
(`app/src/view/PlotGeometry.h:40-42`). The shared loop must reject a non-finite
column the same way the existing one skips out-of-range bins; assert it,
because the alternative is a NaN reaching a `juce::Path`.

`ColumnMap.h` is pure and JUCE-free, so it is **added to
`measure_has_no_framework_deps`' GLOBS list** (`app/tests/CMakeLists.txt:70`),
taking it 29 → 30. `MtwLayer.{h,cpp}` uses JUCE and is **not** added — the same
call `StoredTraceLayer` already makes.

`TransferView` gains, per pane:

```cpp
enum class TransferSource { Mtw, Fixed };
void setSource(TransferPane pane, TransferSource source);   // Magnitude, Phase, Coherence
[[nodiscard]] TransferSource source(TransferPane pane) const noexcept;
```

**Default `Mtw` for all three panes** (record §6). The fixed `TransferBlock` is
untouched and stays selectable — Smaart's v9 dual-dataset behaviour, and what
makes the MTW path falsifiable by eye against the engine this project already
proved.

Seams: `MtwLayer` draws a hairline at `geometry.xForHz(descriptor.seamHz)` for
every band but the bottom, in a colour taken from `app/src/view/MeasureColours.h`
where `trace`, `coherence` and `target` already live — **no measurement
vocabulary enters `ui/`**; `az_ui` supplies the palette only.

- [ ] **Step 1: Write the failing tests.**

  `app/tests/test_column_map.cpp` (JUCE-free, counted in OFF):
  1. `TEST_CASE("an explicit uniform frequency vector maps to the same columns as a bin width", "[columnmap]")`
     — build `hz[i] = i · 46.875` for 513 entries and assert
     `absoluteColumnsForFrequencies` equals
     `absoluteColumnsForBins(g, 46.875, 513, n)` element for element. This is the
     refactor's own safety net: the fixed path must not change behaviour.
  2. `TEST_CASE("a zero-hertz point produces no column", "[columnmap]")` —
     `hz[0] == 0.0` yields the same sentinel the existing loop uses for an
     unmappable bin, and `std::isfinite` holds for every produced column.

  `app/tests_juce/test_transfer_view_mtw.cpp` (ON only), using
  `test_transfer_view_helpers.h`:
  3. `TEST_CASE("the transfer view draws the MTW block by default", "[transferview][mtw]")`
     — a snapshot carrying both blocks renders, and the magnitude path's extent
     count matches the MTW block's column coverage, not the fixed block's. Make
     the two blocks **deliberately different** (fixed flat at 0 dB, MTW a ramp)
     so "it drew something" cannot pass for "it drew the right one".
  4. `TEST_CASE("the per-plot source toggle switches magnitude back to the fixed FFT", "[transferview][mtw]")`
     — `setSource(Magnitude, Fixed)` renders the flat curve while phase, still
     `Mtw`, is unchanged.
  5. `TEST_CASE("seam marks are drawn once per band boundary", "[transferview][mtw]")`
     — six marks for a seven-band block (the bottom band has no lower
     neighbour), at the columns of `{187.5, 375, 750, 1500, 3000, 6000}` Hz.

- [ ] **Step 2: Fail.** — [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** OFF expected **390/390**; ON expected **433/433**.
- [ ] **Step 5: Prove they can fail.** (a) Draw the fixed block regardless of the
      toggle → cases 3 and 4; (b) draw a seam for the bottom band too → case 5
      (seven marks); (c) hand-copy the column loop instead of sharing it and
      change one rounding step → case 1.
- [ ] **Step 6: Render the specimen** and read it, per CLAUDE.md — never a screen
      capture:

```
cmake --build build-l3 --config Release --target rtatool_snapshot --parallel
```

      then run `rtatool_snapshot.exe shots 1100 760` through `cmd //c` and read
      `shots/specimen.png`. The seams must be visible and the low end must carry
      visibly more detail than the fixed trace.
- [ ] **Step 7: Commit** — `feat(app): a curve that knows its own frequencies, and shows where the windows change`

**Acceptance.** OFF **390/390**; ON **433/433**.
`measure_has_no_framework_deps` prints **30 files scanned** and passes.
`wc -l app/src/view/TransferView.cpp` reads **under 400**.

---

## Task 8 — Close the lane in public

Rule 12. Not optional, and not batched to the end of the project.

- [ ] **Step 1: Re-measure from a clean build.** `--clean-first` before any
      number reported. Paste both `ctest` summaries and every guard's own count.
- [ ] **Step 2: Correct the record.** C1, C2 and C4 are **already corrected**
      in `docs/dsp/2026-09-05-mtw-l3.md` (commit `6b3fbad`); do not re-amend
      them. Two things still owe the record an edit, and both are drafted in the
      station-3 report ready to paste:
      **(a) §5's "about 8.8"**, which is the one number the reversed §5 still
      carries loose. The exact value of
      `fifoEffectiveAverages(hann(N), N/4, 16)` from the real function is
      **8.5866271**, identical in every band; §5 should read that, and should say
      that the first frame clearing the gate of 8 is 15 (by 0.85 %) while 16 is
      where `Neff` saturates. Amend the digits, keep the paragraph.
      **(b) §2's reference-delay sentence**, which has **already been amended**
      to the per-band skip this plan implements (task 2) — here the job is to
      confirm it is committed and not merely sitting in the working tree.
      Amend, do not rewrite: the reasoning that was there stays, with the
      correction beside it.
- [ ] **Step 3: Grep for statements this lane made false.**
      `docs/plans/MASTER-EXECUTION-PLAN.md`'s L3 row still says "decimation
      cascade", which record §2 rejected; `docs/HANDOFF.md`; anything claiming
      `core/` has no multi-resolution transfer path. And anything anywhere — an
      earlier station report, a queue entry, a handoff — still describing MTW
      averaging in **seconds**, a `timeConstantSeconds` of 3.5, a `fifoSeconds`
      of 4.0, or a `coherenceUnlockTimeConstantSeconds`. Record §5 was reversed;
      those are the vocabulary of the superseded draft and a session that finds
      one will implement it.
- [ ] **Step 4: Memory.** Two `memory/` entries for the two findings that
      actually cost something, both indexed in `memory/MEMORY.md`:
      **(a) `exponentialEffectiveAverages` does not reach `(2−a)/a`.** That
      ceiling is the pre-overlap `Neff_raw`; the returned value is
      `1 + (Neff_raw − 1)/D` with `D = 1 + 2·Σ c(m·hop)² = 1.9246389` for Hann at
      75 % overlap, **the same in every FFT size** because `hop/N` is what
      decides it. Quoting the ceiling overstates the reachable averages by nearly
      2×. Carry `fifoEffectiveAverages(hann, N/4, 16) = 8.5866271` and
      `exponentialEffectiveAverages` at `alpha = 1 − exp(−1/16)` = 17.1123296
      beside it, since those are the two numbers every MTW test asserts.
      **(b) Averaging a multi-resolution analyser in SECONDS cannot give uniform
      confidence.** `Neff` per second of integration scales as `1/T_window`, so
      one `tau` across bands whose windows differ by 64× gives `Neff` differing by
      64× — 2.061 in a 65536-point band where a 1024-point band reads 97.902 at
      the same 0.5 s. What the coherence gate, the coherence value and the
      variance of `|H|` all measure is `Neff`, and uniform `Neff` is uniform
      **frames**. Record the reversal itself as the lesson: the first draft of
      §5 chose seconds because an operator turning a knob means seconds, and the
      arithmetic refused it. The seconds are still reported, per band.
- [ ] **Step 5: What the human can run.** Name the commands and what they should
      SEE: the two `ctest` lines, and the snapshot command with "seams at 187.5,
      375, 750, 1500, 3000 and 6000 Hz, and a low end resolved to 0.73 Hz".
- [ ] **Step 6: Handoff for the next lane**, written now, per rule 8 —
      `D:\DEV CAVE EP3\shared\handoff\handoff-<YYYYMMDD>-L3-mtw.md`. It must carry
      the one operator-facing consequence of §5 that a human should see on a
      real system before it is treated as settled (**5.461 s** of fill below
      187.5 Hz at the default depth 16 — uniform confidence bought with
      non-uniform latency; whether an operator reads a coherence trace filling
      in from the top as a fault is a stage question, not an arithmetic one) and
      the L5 amendment MTW traces need (`Trace` derives its
      axis from `fftSize` on purpose, `app/src/trace/Trace.h:91-93`).
- [ ] **Step 7: Commit** — `docs(L3): multi-time-window, and the time constant the gate actually needs`

**Acceptance.** OFF **390/390**, ON **433/433**, from a `--clean-first` build,
pasted.

---

## Expected test-count ledger

| after | OFF | ON | delta |
|---|---|---|---|
| baseline `7e10eb6` | 362 | 402 | — |
| task 1 | 371 | 411 | +9 |
| task 2 | 377 | 417 | +6 |
| task 3 | 381 | 421 | +4 |
| task 4 | 383 | 423 | +2 |
| task 5 | 383 | 423 | 0 (guards; no new ctest is registered) |
| task 6 | 388 | 428 | +5 |
| task 7 | 390 | 433 | +2 OFF, +3 ON-only |

**L3a delta: +21 (362 → 383 OFF).** **L3 total: +28 OFF, +31 ON.**

---

## Station 5 — what the verifier should try to refute

A reviewer with **no Write tools**, reading the real files, never the builder's
report. Each item is a way this plan could be satisfied by wrong code.

- **R1 — the seam tests pass because both bands are the same engine.** If
  `MtwEngine` built `bandCount()` engines all at the same `fftSize`, T2 and T3
  would still pass: a uniform table stitches to a perfectly valid curve. Check
  `band(i).config().fftSize` for each `i` directly, and check that task 3 case 3
  reports a comb in band 6 and none in band 0 — identical engines cannot do both.
- **R2 — T2 passes trivially because the delay is 0.** Confirm both `D = 137` and
  `D = 4099` actually reach `DualFftEngine::Config::referenceDelaySamples` in
  *every* band, not just band 0, and that removing the delay makes the test fail.
  A unity test on a `D = 0` pair is a test of nothing.
- **R3 — the golden was regenerated from the C++ output.** Read `gen_mtw.py` and
  confirm the stitched rows come from `scipy.signal.csd/welch/coherence`, not
  from a file the C++ wrote. Confirm `git log --follow core/tests/golden/mtw.txt`
  shows it committed with the generator and untouched afterwards. Run the
  generator with `--check` and confirm byte-identity.
- **R4 — the golden's tolerance was widened until it passed.** Compare the
  two-term constants against `core/tests/test_transfer_golden.cpp:52` (`1e-5` /
  `1e-6`). Anything looser needs a written reason. Confirm the peak is taken
  **per band**, not globally — a global peak hands the tight band the loose
  band's tolerance and hides real error.
- **R5 — `coherence_gate_is_not_bypassed` passes because it stopped watching.**
  Read its printed count (62 after L3a), then confirm `MtwResult` genuinely has
  no coherence member and that `coherenceAt()` reads
  `bandSnapshots[b].coherence`, i.e. the gate `makeSnapshot()` applied.
- **R6 — the seconds came back.** Record §5 was **reversed** to
  frames-uniform after this plan's first draft. Grep `core/`, `app/` and
  `tools/` for `timeConstantSeconds` outside `DualFftEngine`'s own Config and
  the one conversion line in `MtwEngine.cpp`; for `fifoSeconds` and
  `coherenceUnlockTimeConstantSeconds`, which must not exist at all; and for the
  refuted constants `3.5`, `4.0` and `2.9530` in an averaging context. Then
  confirm task 1 case 8 asserts `mtwAlpha` is **bit-identical** across bands and
  case 9 asserts the negative half through the **real**
  `exponentialEffectiveAverages` with a hand-built per-band alpha — the 65536
  band at **2.061**, under the gate for ever. A plan that dropped the seconds
  without keeping a test that says why has hidden the finding rather than
  recorded it. Second check: grep for a hard-coded `1.9246` or `8.5866` in
  *production* code. `D` and `Neff` must be computed from `overlapCorrelation`
  and `fifoEffectiveAverages`; those digits belong only in tests and comments.
- **R7 — the point count is 1282 somewhere.** Grep the tree for `1282`, `257` in
  an MTW context, and `N_k/8`. All three are the record's **superseded** first
  draft (corrected in `6b3fbad`), and any one of them reappearing means the
  stitch duplicates 187.5 Hz.
- **R8 — the untouchable files were touched.**
  `git diff --stat main -- core/src/dsp/DualFftEngine.cpp core/include/rta/dsp/DualFftEngine.h core/tests/test_dualfft.cpp`
  must be empty, and `wc -l` must still read 321 / 400.
- **R9 — `process()` allocates.** Read `MtwEngine::process` and confirm it does
  nothing but forward to each band. Any `resize`, `assign` or `push_back` there
  is a dropout at a live show, which is the exact situation this tool exists for.
- **R10 — the exponential mode still allocates the FIFO.** Confirm
  `band(i).config().fifoDepth == 1` in exponential mode, per task 2 case 6.
  Without it exponential mode carries **45.3 MB** where 14.0 MB is read — 31 MB
  of storage nothing writes and nothing reads (memory section: 2.08 MB per frame
  of depth, and the `DualFftEngine` default depth is 16).
- **R11 — the view draws the fixed block and calls it MTW.** Task 7 case 3 must
  use two *visibly different* blocks. If both are flat, the test proves nothing.
- **R12 — a claimed test count was assumed, not read.** Re-run both `ctest`
  invocations from a `--clean-first` build and compare against the ledger above.
  A guard count copied from this plan instead of from the guard's own output is
  the failure mode this project already has a trap number for.
- **R13 — T5 passes because all bands share one `DualFftEngine` config object
  rather than because `Neff` was checked per band.** This is the specific way the
  frames-uniform record can be satisfied by wrong code: if `MtwEngine` built its
  bands from a single `DualFftEngine::Config` that it mutated only `fftSize` and
  `hopSize` on, every band would trivially report the same `alpha`, the same
  `fifoDepth` and the same `effectiveAverages()` — and task 1 case 8 and task 2
  case 4 would pass while proving nothing, because they would be reading one
  object seven times. Three checks, in this order. **(1)** Read `MtwEngine`'s
  constructor and confirm each band gets its **own** `Config` with its own
  `timeConstantSeconds = timeConstantFrames * hop_k / fs` — a *different* number
  in every band (`{5.4613, 2.7307, …, 0.0853}` s), which is the whole point: the
  alphas come out equal because the hops cancel, not because the seconds were
  copied. **(2)** Confirm task 2 case 6 asserts those seven `timeConstantSeconds`
  values are **different from each other** and match `16 * hop_i / fs`; equal
  seconds across bands is the bug this refutation names. **(3)** Confirm task 1
  case 9 calls `fifoEffectiveAverages` / `exponentialEffectiveAverages` with each
  band's **own** window coefficients and **own** hop, not with band 0's reused
  seven times — the equality is a derived result about Hann at `hop/N = 1/4`, and
  a test that assumes it cannot also prove it.
- **R14 — a test length that does not actually fill the bottom band.** The
  derived length is `n = 327680` (17 frames at `N = 65536`) and the short one is
  `n = 180224` (8 frames). Confirm both appear as written, that `n - N_k` is an
  exact multiple of `hop_k` in every band, and that task 2 case 3's negative half
  really is the short length and its positive half the full one — a `nullopt`
  asserted at a length where every band is filled proves nothing, and one
  asserted at a length no band fills proves nothing either.
