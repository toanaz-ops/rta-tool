# L7 Wave 0 — the shared core foundation (MinimumPhase, FilterSpec, BiquadDesign, BiquadResponse)

> **For agentic workers:** REQUIRED SUB-SKILL `superpowers:test-driven-development` and `superpowers:executing-plans`. Steps are checkboxes; the failing test is written and *seen to fail* before the code. Station 5 (adversarial verify, a reviewer with **no Write tools**) is not optional.

*2026-09-06, lane L7, Wave 0, station 3. Written from the three L7 records — `docs/dsp/2026-09-06-l7-auto-eq.md` (EQ), `docs/dsp/2026-09-06-l7-fir-export.md` (FIR), `docs/dsp/2026-09-06-l7-alignment-wizard.md` (ALIGN) — after reading the real `core/` files, not the records' descriptions of them. Worktree `.claude\worktrees\continue-pending-work-f8aa84`, HEAD `a5463ec`. Every acceptance number below is a closed-form identity; Wave 0 needs **no golden vector**.*

## 0. What Wave 0 is, and why it is built once

Three L7 sub-lanes — **L7-EQ** (auto-EQ + G24 dip classifier), **L7-FIR** (G10 coefficient export), **L7-ALIGN** (G17 wizard + G11 virtual processor + G18 crossover) — each need pieces of biquad design and minimum-phase machinery. Building them per sub-lane would duplicate the cepstral kernel (the EQ record §1.3 caught it being specified twice) and split the RBJ cookbook across two files. Wave 0 lands the shared primitives **once**, behind one `core/CMakeLists.txt` + one `core/tests/CMakeLists.txt` edit, provable entirely by closed-form identity so no sub-lane inherits an unproven dependency.

**Wave 0 ships exactly four things and nothing else:**

| # | Component | Header / impl | Namespace | Consumed by |
|---|---|---|---|---|
| 1 | Minimum-phase cepstral kernel | `core/include/rta/dsp/MinimumPhase.{h}` + `core/src/dsp/MinimumPhase.cpp` | `rta::dsp` | L7-EQ (G24 excess phase), L7-FIR (min-phase taps), later L4c |
| 2 | Filter descriptor | `core/include/rta/eq/FilterSpec.h` (header-only) | `rta::eq` | L7-EQ, L7-ALIGN, L7-FIR (as the shared vocabulary) |
| 3 | RBJ cookbook synthesis | `core/include/rta/eq/BiquadDesign.h` + `core/src/eq/BiquadDesign.cpp` | `rta::eq` | L7-EQ, L7-ALIGN |
| 4 | Complex biquad response | `core/include/rta/dsp/BiquadResponse.h` + `core/src/dsp/BiquadResponse.cpp` | `rta::dsp` | L7-ALIGN (G11/G18) |

## Global constraints

- SPDX header `// SPDX-License-Identifier: AGPL-3.0-or-later` on every new file; `core/` never includes JUCE/Qt/a device API (guard `core_has_no_framework_deps`). Comments explain *why a formula is that formula* — a reader checks the DSP against a textbook from the comment.
- **Hard cap 400 lines, aim 300**, headers too. Every file is budgeted below; all sit well under 200.
- **`core/include/rta/dsp/Biquad.h` MUST NOT be touched.** Wave 0 *adds* design and complex-response code; the apply-only `Biquad`/`BiquadCascade` (DF2T, no `<complex>`) is reused verbatim. Prove it at the end: `git diff --stat main -- core/include/rta/dsp/Biquad.h` is empty.
- **Never assert a value the implementation produced.** Closed form, published standard, or golden-with-a-second-author — a regression lock only if labelled one. Wave 0 uses closed forms only.
- **Tolerance shapes.** Coefficient algebra done in `double` (cookbook synthesis, biquad response) → `WithinAbs`/`WithinRel(…, 1e-12)`. Anything through `Fft`/`RealFft` (the cepstral kernel) → **Shape A**, the two-term float32 form of `memory/float32-fft-precision.md`: `tol_k = 1e-6·|X_k| + 2e-7·peak`, with the measured residual **printed beside the tolerance** and `2e-7` tightened if the fixture reads an order of magnitude under it (`memory/a-fixture-can-be-too-well-behaved-to-fail.md`).
- Build dirs **`build-l7w0`** (OFF) / **`build-l7w0-on`** (ON), Visual Studio generator, never Ninja (`memory/a-misconfigured-build-goes-99-percent-of-the-way.md`). Ignore IDE clang diagnostics; MSVC `/W4` is the truth and must be **0 warnings**.

```
cmake -S . -B build-l7w0 -G "Visual Studio 18 2026" -A x64
cmake --build build-l7w0 --config Release --parallel
ctest --test-dir build-l7w0 -C Release --output-on-failure
```

## Reconciliations made while planning (take to the orchestrator; do not silently re-decide in code)

- **W0-R1 — the kernel has two entry points; the shared piece is the middle.** FIR §4 enters from a time-domain linear-phase FIR `h_lin` (compute `A = |FFT(h_lin)|` first); EQ §4.3 enters from a magnitude spectrum `|H|` and wants `arg h_min`. *Reconciled:* the Wave 0 kernel is the **magnitude-spectrum → minimum-phase complex spectrum** core on a full `nFft`-point grid. L7-FIR wraps it (obtain `A`, then `IFFT` the result to taps, and choose the oversampling factor); L7-EQ wraps it (mirror its half-grid `|h|` to a full grid, then read `arg`). The fold/floor/log spec is identical in both records — see §1.
- **W0-R2 — namespace split, kept from the records.** MinimumPhase and BiquadResponse are pure DSP primitives beside `Fft`/`RealFft`/`Biquad`, so `rta::dsp` (EQ §4.3, ALIGN §5). FilterSpec + `designBiquad` + `responseDb` are the EQ record's own `rta::eq` area (EQ §7); this creates the first `core/include/rta/eq/` and `core/src/eq/` files. `rta::eq` depends down on `rta::dsp`; never the reverse.
- **W0-R3 — the BiquadResponse consistency-lock sign.** ALIGN §5 writes "`20log10|H_i|` equals `sectionAttenuationDb`". `Biquad.h:75` defines `attenuationDb` as decibels of **attenuation, positive = down** (`= −20log10|H|`). The correct lock is therefore `20·log10|biquadResponse(c,ω)| == −BiquadCascade::attenuationDb({c}, ω)` to 1e-12. Planned with the sign corrected.
- **W0-R4 — excess phase and the dip classifier are NOT Wave 0.** EQ §4.3 names the *kernel* "shared with G10"; `excessPhase(TransferSnapshot,…)` and `classifyDip(…)` (EQ §7) are consumed only by L7-EQ among the three sub-lanes (L4c reuses them later, §4.5). Pulling `TransferSnapshot` fill/trust semantics into the shared foundation for one consumer is over-scoping. *Decision:* the kernel is Wave 0; excess phase and the classifier land in L7-EQ.
- **W0-R5 — the cepstral oversampling factor is not chosen here.** FIR §4 requires it *measured* by `gen_fir.py`; the Wave 0 kernel takes `nFft` as a caller argument. Choosing it is L7-FIR's.
- **W0-R6 — no golden vectors, and none of the L7 gen scripts are Wave 0.** Every Wave 0 acceptance is a closed-form identity. `autoeq.txt`/`fir.txt` and their `gen_*.py` (argparse-guarded, avoiding the polynomial guard's blunt patterns — EQ §6, FIR §8) belong to the consuming sub-lanes.

---

## 1. The single minimum-phase spec (records reconciled)

Both records describe the homomorphic real-cepstrum method; they agree bit for bit once the entry point (W0-R1) is fixed. THE spec the kernel implements:

1. **Floor.** `A_k = max(|H|_k, 10^(floorDb/20))`, `floorDb = kMinPhaseFloorDb = −120.0f` — the same −120 dB as `TransferSnapshot::kMagnitudeFloorDb` (`TransferEstimator.h:70`), one constant for one physical reason (a second floor is the drift `memory/` exists to prevent). *Open question OQ-A, deferred to the owner (EQ §12.5):* −120 was chosen for a designed correction filter; both records reuse it for a measured `|H|`. Ship −120; flag it.
2. **Log, NOT halved.** `L_k = ln A_k`. scipy's `minimum_phase` default `half=True` returns the *square root* magnitude; this project uses the `half=False` configuration so `|H_min| == |H|` exactly (FIR §1, §4).
3. **Real cepstrum.** `c = Re(IFFT(L))` over the full `nFft` grid (`Fft`, complex, `L` loaded as the real part). `nFft` a power of two, `≥ 4`.
4. **Fold** (even `nFft`, always true — `Fft` is power-of-two): `win[0] = 1`; `win[k] = 2` for `1 ≤ k ≤ nFft/2 − 1`; `win[nFft/2] = 1` (Nyquist); `win[k] = 0` for `k > nFft/2`. `c_min = c · win`. This is FIR §4's `[1, 2, …, 2, 1 + (nFft%2)]` with `nFft%2 == 0`.
5. **Exponentiate to the spectrum.** `H_min = exp(FFT(c_min))` — the complex minimum-phase spectrum, `nFft` bins. (L7-FIR later takes `Re(IFFT(H_min))` for taps; L7-EQ reads `arg(H_min)`.)

---

## Task W0-1 — `BiquadResponse` (ALIGN §5, §9, §10.3)

Smallest and most independent (pure algebra on `Biquad::Coeffs`, no new namespace, no FFT), so it goes first.

**Files.** Create `core/include/rta/dsp/BiquadResponse.h` (≤ 60), `core/src/dsp/BiquadResponse.cpp` (≤ 60), `core/tests/test_biquad_response.cpp` (≤ 200); modify `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`.

```cpp
// core/include/rta/dsp/BiquadResponse.h  --  namespace rta::dsp
// Biquad.h avoids <complex> on purpose (Biquad.h:107); this is the header that
// adds it, so a caller who needs the complex response never re-derives the
// numerator/denominator (a second spelling of one formula -- W0-R3).
#include "rta/dsp/Biquad.h"
#include <complex>
#include <span>

[[nodiscard]] std::complex<double>
biquadResponse(const Biquad::Coeffs& c, double omegaRadiansPerSample) noexcept;
//   H(e^{jw}) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2), z = e^{jw}

[[nodiscard]] std::complex<double>
cascadeResponse(std::span<const Biquad::Coeffs> sections, double omegaRadiansPerSample) noexcept;
//   product of the section responses -- the G11 biquad-cascade op (ALIGN §5)
```

**RED first.** `test_biquad_response.cpp` opens with `#include "rta/dsp/BiquadResponse.h"`; the build fails at the include. Paste that. **First failing assertion** once the header exists — the DC endpoint (ALIGN §5 row "biquad cascade"):

| # | case | closed-form acceptance (source) |
|---|---|---|
| **T1 (first)** | endpoints are real | `biquadResponse(c, 0) == (b0+b1+b2)/(1+a1+a2)`; `biquadResponse(c, π) == (b0−b1+b2)/(1−a1+a2)`; both `Im == 0` within `1e-12` (evaluate `z=+1`, `z=−1`) |
| T2 | passthrough | `c = {1,0,0,0,0}` → `H ≡ 1+0j` at 64 ω over `(0, π)`, `1e-12` |
| T3 | **consistency lock (labelled)** | for coeffs from `ButterworthDesign::bandPass` sections and from `designBiquad` (W0-3): `20·log10|biquadResponse(c,ω)| == −BiquadCascade::attenuationDb({c}, ω)` to `1e-12` at 64 log-spaced ω — the sign corrected per **W0-R3**. Two spellings of one formula; a lock, not a proof |
| T4 | cascade = product | `cascadeResponse({c1,c2}, ω) == biquadResponse(c1,ω)·biquadResponse(c2,ω)` to `1e-12` |
| T5 | pole near the circle stays finite | the lowest 48 kHz band's section (radius `1 − 8.7e-5`, `Biquad.h:17`): `H` finite, no NaN, at ω on its resonance |

- [ ] **Accept:** OFF ctest **`base+5`/`base+5`** (base = the measured HEAD count; §Numbers). Zero `/W4` warnings.
- [ ] **Mutation:** flip the denominator sign (`1 − a1 z^-1 …`) → T3 fails at every bin; revert.
- [ ] **Commit:** `git add core/include/rta/dsp/BiquadResponse.h core/src/dsp/BiquadResponse.cpp core/tests/test_biquad_response.cpp core/CMakeLists.txt core/tests/CMakeLists.txt` → `feat(core): the complex biquad response, the header Biquad.h deliberately is not`

## Task W0-2 — `MinimumPhase` (EQ §4.3, FIR §4; the §1 spec)

**Files.** Create `core/include/rta/dsp/MinimumPhase.h` (≤ 75), `core/src/dsp/MinimumPhase.cpp` (≤ 120), `core/tests/test_minimum_phase.cpp` (≤ 230); modify both `CMakeLists.txt`.

```cpp
// core/include/rta/dsp/MinimumPhase.h  --  namespace rta::dsp
#include <complex>
#include <span>
#include <vector>

inline constexpr float kMinPhaseFloorDb = -120.0f;   // == TransferSnapshot::kMagnitudeFloorDb (W0-R1 step 1)

struct MinimumPhaseResult {
    std::vector<std::complex<float>> spectrum;   // nFft bins: H_min, |H_min| == floored |H|
};

// magnitudeFullGrid: |H| on a full nFft-point DFT grid (nFft a power of two, >= 4).
// Returns the minimum-phase spectrum via the homomorphic real-cepstrum method
// of the plan's SECTION 1 (log NOT halved; fold [1,2,...,2,1]; floor floorDb).
// A caller with an N/2+1 half-grid mirrors to nFft first (L7-EQ); a caller with
// a time-domain FIR takes |FFT(h)| first and IFFTs the result (L7-FIR).
[[nodiscard]] MinimumPhaseResult
minimumPhaseFromMagnitude(std::span<const float> magnitudeFullGrid,
                          float floorDb = kMinPhaseFloorDb);
// throws std::invalid_argument if size() is not a power of two or < 4.
```

**RED first.** Missing-header build failure, then the flat-spectrum identity — a flat magnitude is already its own minimum phase (zero excess phase), the cleanest closed form:

| # | case | closed-form acceptance (source) |
|---|---|---|
| **T1 (first)** | flat `|H| ≡ 1` → `H_min ≡ 1` | every `arg(spectrum[k]) == 0` and `|spectrum[k]| == 1` within Shape A; `Re(IFFT(spectrum))` is `δ[0]` (`1.0`, rest `≤` Shape A). `nFft = 4096` |
| T2 | magnitude identity | `|spectrum[k]| == floored magnitudeFullGrid[k]` bin for bin, Shape A — FIR §4 "verification that does not depend on the derivation being right" |
| T3 | first-order min-phase system reproduced | input `= |1 − 0.5·e^{−jω}|` on the grid → `arg(spectrum)` equals the analytic `arg(1 − 0.5 e^{−jω})` within Shape A, and `Re(IFFT(spectrum)) ≈ {1, −0.5, 0, …}` — a known minimum-phase impulse |
| T4 | a non-min-phase magnitude yields the **min-phase** counterpart | input `= |1 − 2·e^{−jω}|` (zero *outside* the circle). `|1 − 2e^{−jω}| = 2·|1 − 0.5 e^{−jω}|`, so the kernel returns the min-phase system of that magnitude: `arg(spectrum) == arg(2·(1 − 0.5 e^{−jω}))` (i.e. the *reflected* zero at 0.5, not 2), Shape A. This is §1.2's two-path essence, analytic |
| T5 | floor | one bin at `−200 dB` → no NaN/Inf anywhere; `20log10|spectrum|` at that bin `== −120` within Shape A |
| T6 | refusals | `nFft ∈ {3, 6, 1000}` and empty span → `REQUIRE_THROWS_AS(…, std::invalid_argument)` |

Shape A residual printed beside every tolerance (Global constraints).

- [ ] **Accept:** OFF **`base+11`** (T1–T6 across one test file adds 6 `TEST_CASE`s; count read from ctest). Zero `/W4`.
- [ ] **Mutation 1:** halve the log (`L *= 0.5`, scipy's `half=True`) → T2 fails (`|H_min| == √|H|`); revert. **Mutation 2:** set the Nyquist fold weight to `2` → T3's impulse drifts; revert.
- [ ] **Commit:** `git add core/include/rta/dsp/MinimumPhase.h core/src/dsp/MinimumPhase.cpp core/tests/test_minimum_phase.cpp core/CMakeLists.txt core/tests/CMakeLists.txt` → `feat(core): the cepstral minimum-phase kernel, log not halved, shared by G10 and G24`

## Task W0-3 — `FilterSpec` + `BiquadDesign` (EQ §6, §7, §9)

Creates the first `rta::eq` area. Depends on W0-1 (T7 cross-check) and W0-2 (T8 cross-check), so it is last.

**Files.** Create `core/include/rta/eq/FilterSpec.h` (≤ 45, header-only), `core/include/rta/eq/BiquadDesign.h` (≤ 70), `core/src/eq/BiquadDesign.cpp` (≤ 160), `core/tests/test_biquad_design.cpp` (≤ 260); modify both `CMakeLists.txt`.

```cpp
// core/include/rta/eq/FilterSpec.h  --  namespace rta::eq
enum class FilterType { Peaking, LowShelf, HighShelf };   // no all-pass (EQ §3: zero column in S)
struct FilterSpec { FilterType type = FilterType::Peaking; double fcHz = 1000.0, q = 1.0, gainDb = 0.0; };

// core/include/rta/eq/BiquadDesign.h  --  namespace rta::eq
#include "rta/dsp/Biquad.h"
#include "rta/eq/FilterSpec.h"
[[nodiscard]] rta::dsp::Biquad::Coeffs designBiquad(const FilterSpec&, double sampleRate);
[[nodiscard]] double responseDb(const FilterSpec&, double sampleRate, double hz);
//   exact magnitude response in dB (+ = boost) = -BiquadCascade::attenuationDb({designBiquad(...)}, w)
// both throw std::invalid_argument on sampleRate <= 0, fcHz <= 0, fcHz >= sampleRate/2, q <= 0.
```

**Body — RBJ Audio EQ Cookbook, Q parameterisation.** `A = 10^{G/40}`, `ω0 = 2π·fc/fs`, `α = sin ω0/(2Q)`. Peaking: `b = {1+αA, −2cos ω0, 1−αA}`, `a = {1+α/A, −2cos ω0, 1−α/A}`, normalised by `a0`. **Re-read the cookbook text for the `√A` shelf forms** rather than trusting any transcription (EQ §6) — the §9 identities below are what catch a transcription slip.

**RED first.** Missing-header build failure, then the exact peaking gain at `fc` (EQ §6: `|H(e^{jω0})| = A²` → `40·log10 A = G` dB exactly):

| # | case | closed-form acceptance (source) |
|---|---|---|
| **T1 (first)** | peaking gain at `fc` | `responseDb({Peaking,fc,Q,G}, fs, fc) == G` within `1e-12`, over grid `fc ∈ {40,100,1000,4000}`, `Q ∈ {0.5,1,2,8}`, `G ∈ {−15,−6,+3,+6}`, `fs ∈ {44100,48000,96000}` |
| T2 | 0 dB at DC and Nyquist | `responseDb(…, fs, ε)` and `responseDb(…, fs, fs/2·0.999…)` → `0.0` within `1e-12` (`Σb = Σa` at `z = ±1`) |
| T3 | **half-gain bandwidth** (load-bearing, EQ §3/§6) | at `ω_{1,2} = 2·arctan(w_{1,2}·tan(ω0/2))`, `w_{1,2} = ∓1/(2Q) + √(1 + 1/(4Q²))`: `responseDb == G/2` within `1e-12` — the fact §3's Q-from-half-depth rests on. Builder computes both sides in double |
| T4 | inverse cascade is flat | `BiquadCascade::attenuationDb({designBiquad(G), designBiquad(−G)}, ω) ≡ 0` within `1e-12` at 64 ω |
| T5 | shelves | LowShelf/HighShelf: `G` dB at the shelved end, `0` at the other, **`G/2` at `fc`** (`|H(jω0)| = A`), each within `1e-12` |
| T6 | pole radius < 1 | `BiquadCascade({designBiquad(spec)}).maxPoleRadius() < 1` for `G ∈ [−30,+30]`, `Q ∈ [0.1,20]`, `fc ∈ (0, fs/2)` — the invariant CI asserts (`Biquad.h:69`) |
| T7 | **cross-lock: a peaking section is minimum-phase** | `minimumPhaseFromMagnitude(|H|)` (W0-2) reproduces the designed peaking section's own phase within Shape A (EQ §9.3). Ties Wave 0's two DSP pieces on one fixture |
| T8 | refusals | `sampleRate ≤ 0`, `fcHz ≤ 0`, `fcHz ≥ fs/2`, `q ≤ 0` → `std::invalid_argument` (four cases) |

- [ ] **Accept:** OFF **`base+19`** (T1–T8 add 8 `TEST_CASE`s; count read from ctest). Zero `/W4`.
- [ ] **Mutation 1:** invert `A` in the peaking numerator vs denominator (`1+α/A` ↔ `1+αA`) → T1 reads `−G`; revert. **Mutation 2:** drop the `a0` normalisation → T2 fails; revert.
- [ ] **Commit:** `git add core/include/rta/eq/FilterSpec.h core/include/rta/eq/BiquadDesign.h core/src/eq/BiquadDesign.cpp core/tests/test_biquad_design.cpp core/CMakeLists.txt core/tests/CMakeLists.txt` → `feat(core): the RBJ cookbook design and the FilterSpec three lanes share`

## Task W0-4 — prove the guards still GUARD (EQ §9.10, FIR §7.8, ALIGN §10.11)

No files; a procedure whose output goes in the report. Every count is **read from the guard's own `(N files scanned)` line**, never from this plan — a mis-globbed guard passes while watching nothing.

- [ ] **RED, framework deps.** Add `#include <juce_core/juce_core.h>` atop `core/include/rta/dsp/MinimumPhase.h`; reconfigure; `ctest -R core_has_no_framework_deps` → **paste the failure naming MinimumPhase.h**; remove the line.
- [ ] **RED, polynomial form.** Add a comment line `// signal.lfilter` inside `core/src/eq/BiquadDesign.cpp` (the guard reads `core/**/*.cpp`, not only `tools/*.py`); `ctest -R filter_design_has_no_polynomial_form` → paste the failure naming BiquadDesign.cpp; remove it. This proves the new `rta::eq` source is inside the polynomial guard's scan set.
- [ ] **GREEN, both guards, with the new files scanned.** Re-run each; paste the `(N files scanned)` line. Predictions to falsify: framework `107 → ~117`, polynomial `125 → ~135` (10 new files each: 4 headers + 3 cpp + 3 tests, no new `.py`). If a number comes in *below* the prediction, a new file fell outside a glob — stop and report.
- [ ] **GREEN, the other two guards** (`coherence_gate_is_not_bypassed`, `core_makes_no_class_1_claim`) still pass with their own risen scanned counts — no Wave 0 file names a `coherence` field or a Class-1 claim.
- [ ] **Lengths + frozen file.** `wc -l` over every new file (all < 400); `git diff --stat main -- core/include/rta/dsp/Biquad.h` is empty.
- [ ] **Accept:** a `--clean-first` OFF build → **`base+19`/`base+19`**, 0 `/W4` warnings. **Commit:** `git add docs/reports` → `test(core): the L7 Wave 0 guards, made red once before they were believed`

---

## Numbers the builder must measure, not copy

Every figure below is a prediction. Measure `base` on HEAD `a5463ec` **before** W0-1, and each Accept line as it is reached; if a measurement disagrees, the plan is wrong and the orchestrator hears about it — do not bend the code to hit it.

| quantity | plan says | how |
|---|---|---|
| ctest OFF baseline | **`base`** (measure) | `--clean-first` build of `a5463ec`, `build-l7w0` |
| ctest OFF after W0-1 / W0-2 / W0-3 | `base+5` / `base+11` / `base+19` | derived from the `TEST_CASE` lists; re-derive if a list changes |
| `core_has_no_framework_deps` scanned | 107 → **~117** | the guard prints `(N files scanned)` |
| `filter_design_has_no_polynomial_form` scanned | 125 → **~135** | as above; no new `.py` |
| `coherence_gate_is_not_bypassed` / `core_makes_no_class_1_claim` | green, counts rise | as above |
| MSVC `/W4` warnings | **0** | grep the build log; a warning is a defect, not noise |

## What Wave 0 does NOT include (deferred to the consuming sub-lane)

- **L7-EQ:** `excessPhase(TransferSnapshot,…)`, `classifyDip(…)` and the half→full-grid mirror that feeds the kernel (W0-R4); the `EqAllocator` (placement, `γ²/f` weights, ridge solve, ranking); the linearisation `s_i(f) = R_i(f;1dB)` and its measured error (EQ §9.2); `gen_autoeq.py` + `autoeq.txt`; the target types (§5).
- **L7-FIR:** `FirDesign` (frequency sampling, the IFFT-to-taps wrap of the kernel, truncation loss); the **cepstral oversampling factor** `nFft/N`, measured by `gen_fir.py` (W0-R5); `gen_fir.py` + `fir.txt`; the text/WAV writers (`app/`).
- **L7-ALIGN:** the five G11 virtual-processor ops, `spectralCrossover()`, `crossoverBandFit()`, `relativePolarity()`, `VirtualTrace`, the topology table (`app/`).
- Any golden vector (W0-R6). Any change to `Biquad.h`, `RealFft`, `Fft`, `GroupDelay`, `TransferEstimator`.

## For the station-4 builder, first read

1. The three records are binding; this plan implements only their **shared** primitives and flags W0-R1..R6, which the orchestrator amends first.
2. Order is fixed by dependency: **W0-1 (BiquadResponse) → W0-2 (MinimumPhase) → W0-3 (FilterSpec+BiquadDesign)**; W0-3's T7/T8 cross-lock the earlier two. One commit per task.
3. The failing test first, seen to fail (the missing `#include`), then the header, then the body. Paste the command and its output for every "done" — a green build proves it compiles, not that the numbers are right (`CLAUDE.md`).
4. `Biquad.h` is frozen; you *add* beside it, never edit it. Prove it with the diff.
5. W0-4's deliberate REDs are not optional — a guard nobody has seen fail is a guard nobody knows is watching.
6. Build in `build-l7w0` / `build-l7w0-on`, Visual Studio, never Ninja. Every count here is a prediction you are expected to falsify if it is wrong.
