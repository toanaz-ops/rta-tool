# L7-EQ — auto-EQ, ranked suggestions, and the min/excess-phase null test (lane L7, sub-lane L7-EQ, with G24 folded in)

> **For agentic workers:** REQUIRED SUB-SKILL `superpowers:test-driven-development` and `superpowers:executing-plans`. Steps are checkboxes; the failing test is written and *seen to fail* before the code. Station 5 (adversarial verify, a reviewer with **no Write tools**) is not optional.

*2026-09-07, lane L7, sub-lane L7-EQ, station 3. Written from the decision record `docs/dsp/2026-09-06-l7-auto-eq.md` after reading the real files it cites and the Wave 0 code it reuses — `core/include/rta/eq/{FilterSpec,BiquadDesign}.h`, `core/src/eq/BiquadDesign.cpp`, `core/include/rta/dsp/{MinimumPhase,GroupDelay,TransferEstimator}.h`, `core/src/dsp/{MinimumPhase,FirDesign}.cpp`, `tools/gen_fir_algo.py`, `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`, `app/src/measure/OutputPolicy.h`, `platform/types/include/rta/platform/OutputEngine.h` — not the record's description of them. Worktree `.claude\worktrees\continue-pending-work-f8aa84`, HEAD `1ad48da` (Wave 0 + Wave 1 built and verified on this branch; measure the real baseline, §"Numbers"). Every acceptance number is a closed-form identity or a golden with `cond` written into it; the gain solve is the only golden vector (record §9).*

## 0. What L7-EQ is, and the one hazard that shapes the whole plan

The operator has a measured transfer function `H` + coherence (L2), a target (§5), and a parametric EQ downstream. L7-EQ turns the deviation into a small biquad set, shows the predicted result as a ghost before anything commits, and **refuses to boost where boosting cannot work**. That last clause is G24 — the minimum/excess-phase null test, **folded into this sub-lane** by owner ruling (2026-09-06, HANDOFF §"Ba quyết định") — and it is the reason this plan is not just a curve-fitter.

**The load-bearing hazard (HANDOFF §"G24 CAVEAT", verifier Wave 1).** The shared kernel `rta::dsp::minimumPhaseFromMagnitude` (Wave 0, BUILT) does **no oversampling of its own** — it is exact on the `n`-point grid it is handed (`MinimumPhase.cpp`: floor → log → IFFT → fold → exp, all at length `n`). Oversampling is the **caller's** job: FIR (`FirDesign.cpp:36`) ships `kCepstralOversamplingFactor = 8`, adequate **only because `designLinearPhaseCore` windows its target first and caps notch sharpness**. G24 feeds **raw measured `|H|`** — a transfer function with sharp comb notches, never windowed — so 8× may time-alias the min-phase impulse and corrupt the excess phase G24 gates on. **Task A measures G24's own factor by impulse/swing convergence, and MUST NOT reuse FIR's 8× or FIR's magnitude-identity evidence** (that residual is `Re(FFT(fold(c)))==FFT(c)`, an algebraic tautology flat at every factor — `gen_fir_algo.sweep_oversampling_factor`'s own docstring says so).

## Global constraints

- SPDX header `// SPDX-License-Identifier: AGPL-3.0-or-later` on every new file.
- **`core/` never includes JUCE/Qt/a device API** (guard `core_has_no_framework_deps`). The allocator and phase tests are `rta::eq`/`rta::dsp`, spans in, numbers out. **`app/src/measure/` is guarded too** (`measure_has_no_framework_deps` — the session model stays framework-free where it can; JUCE lives in `view/`).
- **Never assert a value the implementation produced** (`CLAUDE.md`). Tier-1 acceptance is closed-form; the one golden (`autoeq.txt`) carries its own `cond(SᵀWS+λI)` so Shape B is self-describing.
- **Float32-aware tolerances** (`memory/float32-fft-precision.md`): anything through `Fft` uses **Shape A** `tol_k = 1e-6·|X_k| + 2e-7·peak`, residual printed beside the tolerance; the double solve uses **Shape B** `|g−g_gold| ≤ cond·1e-15·max|g_gold| + 1e-12`; coefficient algebra in double is plain `1e-12` relative.
- **Hard cap 400 lines, aim 300**, headers too. Budgets per file below; `EqAllocator.{h,cpp}` is the one near the aim and is pre-split at the placement/solve seam.
- **Core is compiled in both configs** (no per-file toggle exists — `core/CMakeLists.txt` lists every `.cpp` unconditionally); "OFF" builds `rta_core` + tests, "ON" adds `-DRTA_BUILD_APP=ON` (JUCE via `RTA_JUCE_PATH`). New core work is proven in **OFF**; app session/verify need **ON**.
- Build dirs **`build-l7eq`** (OFF) / **`build-l7eq-on`** (ON), Visual Studio generator, **never Ninja** (`memory/a-misconfigured-build-goes-99-percent-of-the-way.md`). MSVC `/W4`: **0 warnings**.

```
cmake -S . -B build-l7eq -G "Visual Studio 18 2026" -A x64
cmake --build build-l7eq --config Release --parallel
ctest --test-dir build-l7eq -C Release --output-on-failure
```

For ON append `-DRTA_BUILD_APP=ON -DRTA_JUCE_PATH=<path>` and use `build-l7eq-on`.

## Reconciliations made while planning (take to the orchestrator; do not silently re-decide in code)

- **EQ-R1 — `excessPhase` is placed in `rta::dsp`, not `rta::eq`.** Record §7 files `excessPhase` under `rta::eq`, but §4.5 requires the **L4c display feature to reuse it**, and `minimumPhaseFromMagnitude` already set the precedent of a shared `rta::dsp` kernel. Putting `excessPhase` in `rta::dsp` (beside `MinimumPhase.h`, `GroupDelay.h`) lets L4c call it without dragging in `rta::eq`; only the EQ **verdict** (`classifyDip`) is EQ-specific and stays `rta::eq`. Flagged because it moves one file the record named for `eq/`.
- **EQ-R2 — EQ ships its own data-quality coherence threshold; it does NOT block on L5b.** The record (§1.1, §4.1) leaves `θ` to L5b, which has no record; the only threshold in `app/` is a mockup constant (`dev/preview/TargetMatchPreview.cpp`). *Decision:* the **core allocator owns no threshold** (it takes a caller-supplied `trusted` mask, record §4.1 verbatim). The **app** builds that mask with a stated default `kEqTrustFloor = 0.7` (`γ² ≥ 0.7`), a plain data-quality gate — **not** the ISO-2969 X-curve tolerance table (that block, §5, is about *named-preset conformance* only, not about having a coherence floor). When L5b lands its threshold, the app swaps the source of `θ`; nothing in core changes. `0.7` is a labelled judgement (open question 12.? / §"Open"); below it the H1 variance `σ` of §8 diverges, which is the physical reason a data-quality floor exists at all.
- **EQ-R3 — "apply to the virtual processor" ships as a FilterSpec set + exact ghost, not live filtering.** The virtual EQ DSP (G11) is **L7-ALIGN, Wave 3, not built** (record §7, §11; HANDOFF wave plan). *Decision:* L7-EQ's "apply" writes the accepted `std::vector<FilterSpec>` into an app-owned session model and draws the ghost `ghost_k = m_k + Σ_i responseDb(spec_i, fs, f_k)` (exact dB add, via Wave 0 `responseDb`). It does **not** filter audio. G11 later consumes the same `FilterSpec` list. This is the seam the record drew; EQ builds up to it and no further.
- **EQ-R4 — the interactive chip/ghost UI is a dev-preview specimen, not MainComponent wiring.** Like L7-OUT left G20's panel unwired (HANDOFF §"Hai ghi chú OUT"), the live JUCE panel depends on host panels that do not exist. *Decision:* all EQ logic (session, ranking, ghost, verify, export) is device-free and **ctest-provable** in `app/tests`; the visual chip surface is a `app/src/dev/preview/EqPreview.*` specimen rendered by the existing snapshot path (`CLAUDE.md §"Seeing the GUI"`), not a `MainComponent` binding. Flagged so nobody hunts for a MainComponent hook.
- **EQ-R5 — `responseDb` and `designBiquad` are reused verbatim from Wave 0.** `core/include/rta/eq/BiquadDesign.h` already declares both; `responseDb` is `−attenuationDb` sign-corrected (record §7). The allocator's linearised column `s_i(f) = responseDb({type,fc,Q,gain=1dB}, fs, f)` and the ghost both call it; no second response path is written. `designBiquad` throws `std::invalid_argument` on out-of-domain shelf `(Q,gainDb)` (`c61b5dc`) — the allocator clamps into domain **before** calling it (Task D) and the refusal test (§9.9) covers the throw.

---

## The core API this lane builds (record §7, reconciled)

```cpp
// core/include/rta/dsp/ExcessPhase.h   (rta_core, EQ-R1)     -- shared with L4c
namespace rta::dsp {
struct ExcessPhaseResult {
    std::vector<float> excessPhaseRad;   // phi_x per measurement bin, delay removed
    std::vector<double> excessGroupDelay;// tau_x per bin (on h/h_min, |.|==1)
    double broadbandDelaySec = 0.0;      // tau_0, the median removed
    std::size_t oversampleFactor = 0;    // the factor actually used (Task A)
    bool valid = false;                  // false if too few trusted bins
};
// magnitudeHalfGrid/hHalfGrid: DC..Nyquist (the fixed-engine TransferSnapshot grid);
// trusted: same length. Mirrors to the full circle and oversamples internally.
[[nodiscard]] ExcessPhaseResult
excessPhase(std::span<const float> magnitudeHalfGrid,
            std::span<const std::complex<double>> hHalfGrid,
            std::span<const std::uint8_t> trusted,
            double sampleRate,
            std::size_t oversampleFactor);   // from kExcessPhaseOversamplingFactor
}

// core/include/rta/eq/DipClassifier.h   (rta_core)
namespace rta::eq {
enum class DipVerdict { Boostable, NotMinimumPhase, Untrusted };
struct DipClassification { DipVerdict verdict; double depthDb; double swingRad; double thresholdRad; };
// S* = 2*asin(r_D), r_D = (10^(D/20)-1)/(10^(D/20)+1); boosts only (§4.3.7).
[[nodiscard]] DipClassification
classifyDip(const dsp::ExcessPhaseResult& xp, std::span<const float> hz,
            std::size_t fL, std::size_t fStar, std::size_t fR,
            std::span<const float> residualDb, bool isBoost);
}

// core/include/rta/eq/EqGainSolve.h + EqAllocator.h   (rta_core)
namespace rta::eq {
struct EqInput {   // spans, all same length M unless noted
    std::span<const float> hz, residualDb, coherence;
    std::span<const std::uint8_t> trusted, excluded;
    double sampleRate; int maxFilters; double gCapDb; double qMaxBoost, qMaxCut;
    std::optional<double> roomT60Sec;   // ties Q_max when present (§3)
};
struct GainSolve { std::vector<double> gainsDb; double conditionNumber; };
[[nodiscard]] GainSolve solveGains(std::span<const FilterSpec> placed, const EqInput&); // ridge Cholesky
struct Candidate { FilterSpec spec; double scoreDb; DipClassification dip; };
[[nodiscard]] std::vector<Candidate> rankCandidates(const EqInput&, int maxCandidates); // Suggest: one greedy step
[[nodiscard]] std::vector<FilterSpec>  autoEq(const EqInput&);                          // greedy to N, then one solve
}
```

---

## Task A — `excessPhase` + the oversampling-factor MEASUREMENT (core, OFF) — the G24 CAVEAT task

First, most independent, and everything else in G24 rests on the factor it establishes.

**Files.** Create `core/include/rta/dsp/ExcessPhase.h` (≤ 110), `core/src/dsp/ExcessPhase.cpp` (≤ 200), `core/tests/test_excess_phase.cpp` (≤ 260); create `tools/gen_autoeq.py` (argparse, `--out`, `--check`, `--help` exits before any write — the `gen_fir.py`/`gen_mtw.py` precedent, `memory/a-gen-script-runs-the-moment-you-invoke-it.md`) with a `tools/gen_autoeq_algo.py` companion (mirrors `gen_fir_algo.py`); create golden `core/tests/golden/autoeq.txt` (oversampling section only in this task). Modify `core/CMakeLists.txt` (add `src/dsp/ExcessPhase.cpp`) and `core/tests/CMakeLists.txt` (add `test_excess_phase.cpp`).

**The measurement (how the factor is found for RAW measured magnitude — the caveat's core).** `gen_autoeq_algo.py` builds the analytic two-path comb `H(θ) = 1 + a·e^{−jθD}` (the sharpest notch G24 will meet, and **not windowed**), for `a ∈ {1.25, 2, 4}` (the NMP side, sharpest for large `a`) at several `D`. For each factor `F ∈ {1,2,4,8,16,32,64,128,256}` it evaluates `|H|` **analytically** on the `F·nFft` grid (no interpolation error, isolating cepstral aliasing), reconstructs the min-phase impulse by the same homomorphic steps as the kernel, and measures **two convergence metrics against the `F=256` reference — never the magnitude-identity residual**:
1. **Reconstructed excess-phase swing** `S(F)` over the notch region — the quantity G24 actually gates on. Converged when `|S(F) − S(256)| < 0.5°`.
2. **Min-phase impulse tail energy** past `nFft/2` (aliasing wraps the tail into the causal window) as an energy ratio in dB — the operational analogue of `truncationLossDb`, converged when it stops falling with `F`.

The chosen factor is the smallest `F` where **both** converge; ship **one step of margin** above it (`factor_with_margin`, `gen_fir_algo.py:239`). The golden records the per-`F` `S` table, the per-`F` tail-energy table, and `chosen_oversampling_factor`. The C++ constant `inline constexpr std::size_t kExcessPhaseOversamplingFactor` (in `ExcessPhase.h`) is set to that value; a test reads the golden and asserts `kExcessPhaseOversamplingFactor >= measured_minimum`. **If the measured minimum exceeds 8, that alone proves the caveat and the plan was right to demand this task.** The C++ path interpolates measured `ln|H|` (log-f, linear-dB — the FIR §6 rule) onto the `F·nFft` grid; the golden also reports the interpolation error at the chosen `F` for a realistic bin count, flagged as a separate quantity for the orchestrator.

**RED first.** `test_excess_phase.cpp` opens `#include "rta/dsp/ExcessPhase.h"`; the build fails at the include. Paste it.

| # | case | closed-form acceptance |
|---|---|---|
| **A1 (first)** | pure delay has flat excess phase | input `h_k = e^{−jω_k D}`, `|H|≡1` (all trusted): reconstructed `φ_x` is `0` within Shape A after `τ_0 = D/fs` is removed; `broadbandDelaySec == D/fs` within `1/fs` |
| A2 | a min-phase section reconstructs itself | `|H|` of a cookbook peaking section (min-phase by construction, record §9.3): `arg h_min` matches its own `phaseRadians` within Shape A; excess swing `≈ 0` |
| A3 | oversampling factor clears the sweep | read `autoeq.txt`: `kExcessPhaseOversamplingFactor >= measured_minimum_factor`, and it is **not** silently 8 unless the sweep independently chose 8; the `S`-convergence table is monotone toward the reference |
| A4 | too few trusted bins refuse | a notch whose region + one-region margin is untrusted → `valid == false` (never a curve, `memory/a-fixed-defect-returns-through-the-silent-fallback.md`) |

- [ ] **Accept:** OFF ctest `base_core + N_A` (count read from ctest, not predicted). Zero `/W4`. `gen_autoeq.py --check` byte-identical.
- [ ] **Mutation:** hard-code `oversampleFactor = 1` in the caller → A3's convergence assertion or the two-path classify (Task B) fails; revert.
- [ ] **Commit:** `feat(core): excessPhase (G24 kernel) + its own oversampling factor, measured by impulse/swing convergence on raw comb magnitude — not FIR's windowed 8x`

## Task B — `classifyDip`: Boostable vs NotMinimumPhase vs Untrusted (core, OFF)

The test this record exists for. Depends on Task A.

**Files.** Create `core/include/rta/eq/DipClassifier.h` (≤ 90), `core/src/eq/DipClassifier.cpp` (≤ 120), add cases to `test_excess_phase.cpp` (or `test_dip_classifier.cpp`, ≤ 180 — one file if the pair stays under 400). Modify `core/CMakeLists.txt` (+`src/eq/DipClassifier.cpp`), and tests CMakeLists if a new file.

`r_D = (10^{D/20}−1)/(10^{D/20}+1)`; NMP swing predicted `4·asin r_D`, MP predicted `0`, boundary `S* = 2·asin r_D` (record §4.3.6, max-margin midpoint from the dip's own depth — **not a grid floor**, `memory/a-threshold-read-off-a-grid-is-that-grids-floor.md`). Depth `D = min(r(f_L), r(f_R)) − r(f*)` (peak-to-notch, conservative). `S = max φ_x − min φ_x` over `[f_L,f_R]` at bin resolution (no smoothing — the swing concentrates at the notch). Boosts gated; cuts report the margin but are never refused (§4.3.7).

**RED first.** `#include "rta/eq/DipClassifier.h"` fails to build. Paste it.

| # | case (record §9.4) | closed-form acceptance |
|---|---|---|
| **B1 (first)** | the two-path pair, identical magnitude | `a=0.8` and `a=1.25`, `D=144` at 48 kHz: magnitudes coincide bin-for-bin after offset; `a=0.8` → **Boostable**, `S` within Shape A of `0`; `a=1.25` → **NotMinimumPhase**, `S` within Shape A of `4·asin 0.8 = 3.71 rad` against `S* = 1.85 rad` at `D≈19.1 dB` |
| B2 | shallow-dip coverage | `a∈{0.5,2}` (`S=120°`) and `a∈{0.25,4}` (`S=58°`, `D≈4.4 dB`, `S*=29°`) classify MP/NMP correctly — the depth-derived `S*` is why the shallow ones separate, where a fixed 90° would miss them (record §4.4) |
| B3 | verdict fields carried, not re-gated | `DipClassification` carries `D`, `S`, `S*`, and `S/S*` is reportable; the classifier does not itself drop the candidate (`Polarity.h:103-110` lesson — display, never re-gate) |
| B4 | cuts not gated | a NMP-shaped **cut** returns its margin but `verdict` never blocks it (isBoost=false path) |

- [ ] **Accept:** OFF ctest `base_core + N_A + N_B`. Zero `/W4`.
- [ ] **Mutation 1:** use a fixed `S* = π/2` → B2 fails on the shallow pair; revert. **Mutation 2:** drop the delay removal (`τ_0`) → B1's `a=0.8` swing is no longer ≈0; revert.
- [ ] **Commit:** `feat(core): classifyDip — depth-derived S*=2*asin(r_D) splits boostable dips from non-minimum-phase nulls`

## Task C — `solveGains`: weighted ridge least squares in dB (core, OFF)

The joint gain solve, the one golden vector. Independent of A/B; do it before D so D can call it.

**Files.** Create `core/include/rta/eq/EqGainSolve.h` (≤ 80), `core/src/eq/EqGainSolve.cpp` (≤ 180), `core/tests/test_eq_allocator.cpp` (≤ 340 — shared with Task D; solve cases first); extend golden `autoeq.txt` (gain section, with `cond` per case) via `gen_autoeq.py`. Modify `core/CMakeLists.txt` (+`src/eq/EqGainSolve.cpp`).

`g = argmin Σ_k w_k (r_k − Σ_i s_i(f_k) g_i)² + λ‖g‖²`, solved `(SᵀWS + λI)g = SᵀWr` by Cholesky in **double** (`N ≤ 16`, one solve, no iteration). `s_i(f) = responseDb({type_i,fc_i,Q_i,1dB}, fs, f)` (EQ-R5, exact). Weights `w_k = γ²_k/f_k` on trusted bins, `0` elsewhere (record §3; `γ²` is L6b's bounded trust, `1/f` the log-axis Jacobian — `memory/positions-are-not-replicates.md` is why inverse-variance was rejected). `λ = 0.1·min_i(s_iᵀWs_i)/(G_cap − 0.1)` (record §3, the isolated-filter shrinkage is exactly 0.1 dB at the cap). `cond(SᵀWS+λI)` returned and written to the golden.

**RED first.** `#include "rta/eq/EqGainSolve.h"` fails. Paste it.

| # | case (record §9.7) | acceptance |
|---|---|---|
| **C1 (first)** | matches the numpy solve | fixed `(fc,Q)` set + synthetic `r`: `g_core` vs `numpy.linalg.solve(SᵀWS+λI, SᵀWr)` golden, **Shape B** with `cond` from the golden |
| C2 | collinear pair splits evenly | two filters at identical `(fc,Q)`: each gets exactly half the unregularised single-filter gain within Shape B — ridge keeps the near-singular solve finite (record §3) |
| C3 | ridge shrinkage is 0.1 dB at the cap | one isolated filter driven to `G_cap`: shrinks by `0.1 dB ± 1e-9` vs the unregularised gain — the derived `λ`, not a chosen one |
| C4 | linearisation exact at five points | `responseDb(g)` vs `g·s(f)` at `fc` (`=g`), the two `G/2` midpoints (`=g/2`, §6), DC and Nyquist (`=0`) for `g∈{−15,−6,+3,+6}`; the max error **elsewhere** is printed and amended into the record (§9.2), not asserted |

- [ ] **Accept:** OFF ctest `base_core + … + N_C`. Zero `/W4`. `gen_autoeq.py --check` byte-identical.
- [ ] **Mutation:** perturb `λ`'s numerator to `0.2` → C3 reads 0.2 dB shrinkage and fails; revert.
- [ ] **Commit:** `feat(core): solveGains — weighted ridge Cholesky in dB, lambda derived to 0.1 dB shrinkage at the cap`

## Task D — `rankCandidates` + `autoEq`: greedy placement, ranking, whole pipeline (core, OFF)

Placement, auto-offset, exclusion, and the two stopping points. Depends on B (classify) and C (solve).

**Files.** Create `core/include/rta/eq/EqAllocator.h` (≤ 120), `core/src/eq/EqAllocator.cpp` (≤ 260 — placement + ranking + `autoEq`; the solve lives in Task C's file, so this stays under the aim), add cases to `core/tests/test_eq_allocator.cpp`. Modify `core/CMakeLists.txt` (+`src/eq/EqAllocator.cpp`).

Residual `r_k = m_k − t_k − c`, `c` the `γ²/f`-weighted mean of `m−t` over the anchor band (closed form). Placement: trusted extremum of largest `|r|`; region `[f_L,f_R]` bounded by flanking opposite-sense extrema; `Q = fc/(f_2−f_1)` at the half-depth crossings (record §3, the cookbook's own bandwidth definition), clamped `[Q_min, Q_max]`. `Q_max` tied to the L4b T60 when present (`t_60 = 2.2·Q_p/fc`, `Q_p = A·Q`), else fallback `Q≤10` boost / `Q≤20` cut. No new filter within half a bandwidth of an existing one (anti-collinearity placement rule). Shelves only where the residual keeps one sign across the outermost half-octave. `G_cap = +6 dB` default (labelled judgement). Boost candidates run through `classifyDip`; a NotMinimumPhase boost is **not placed as a boost** (routed to V2 per §4.3), cuts always allowed.

**RED first.** `#include "rta/eq/EqAllocator.h"` fails. Paste it.

| # | case (record §9) | closed-form / labelled acceptance |
|---|---|---|
| **D1 (first)** | greedy places on the feature | a single Gaussian bump in `r` of known centre + half-depth width: candidate lands on the centre bin, `Q = fc/(f_2−f_1)` to the grid's resolution (§9.6) |
| D2 | exclusion mask respected | a bump inside `excluded` is not proposed (decline-a-chip path, record §2) |
| D3 | auto-offset is the weighted mean | `c` equals the closed-form `γ²/f`-weighted mean of `m−t` over the anchor band |
| D4 | NMP boost refused, cut allowed | at an `a>1` notch: `autoEq` places **no boost** there (routed out), but the same shape as a cut is placed — the two gates independent on one fixture (record §9.5 pairs this with a coherent-notch coherence read `γ²>0.99`) |
| D5 | whole pipeline (**labelled regression lock**) | three-peak fixture: `γ²/f`-weighted RMS residual after `autoEq(N=3)` is below the input's; ghost `= m + attenuation`-summed cascade **exactly**. Labelled a lock, not a proof (`CLAUDE.md`) |
| D6 | refusals | `fs≤0`, `fc≥fs/2`, `Q≤0`, mismatched span lengths, `N>trusted extrema`, every-bin-untrusted all throw or return empty **with a reason** — never a curve; the out-of-domain shelf `(Q,gainDb)` is clamped before `designBiquad` or the throw is caught (EQ-R5) |

- [ ] **Accept:** OFF ctest `base_core + … + N_D`. Zero `/W4`.
- [ ] **Mutation 1:** skip the exclusion check → D2 proposes the masked bump; revert. **Mutation 2:** drop the NMP refusal in placement → D4 places a boost into the null; revert.
- [ ] **Commit:** `feat(core): EqAllocator — greedy placement (Q from half-depth), auto-offset, exclusion, autoEq + rankCandidates`

## Task E — app session model, trust mask, text export (app, ON)

The two modes as operator-facing state, device-free and ctest-provable. Depends on core (A–D).

**Files.** Create `app/src/measure/EqSession.h` (≤ 160) + `app/src/measure/EqSession.cpp` (≤ 200), `app/src/measure/EqTrustMask.h` (≤ 60, `kEqTrustFloor = 0.7`, EQ-R2), `app/src/export/EqTextExport.h` (≤ 90, header-only), `app/tests/test_eq_session.cpp` (≤ 260); modify `app/tests/CMakeLists.txt` (add the test). Session builds `EqInput` from a fixed-engine `TransferSnapshot` + target vector; holds the committed `std::vector<FilterSpec>`, per-filter *applied* marks, and the session exclusion mask.

- *Auto EQ:* one call runs `autoEq` to `N`, lands the whole set, ghost drawn.
- *Suggest:* `rankCandidates` returns top 1–3; accept-one recomputes the residual and re-ranks; **decline** adds the region to `excluded` (record §2).
- *Residual after acceptance* = `measured − target + Σ R_i` over committed filters **not yet marked applied** (record §2); an *applied* filter leaves the sum (else counted twice — in the room and in the prediction).
- *Ghost* = `m_k + Σ_i responseDb(spec_i, fs, f_k)` (EQ-R3/R5, exact dB add).

**RED first.** `#include "rta/measure/EqSession.h"` fails. Paste it.

| # | case | acceptance |
|---|---|---|
| **E1 (first)** | ghost is the exact dB sum | after `autoEq(N=3)`, `ghost_k == m_k + Σ responseDb(...)` within `1e-9` (double dB add) |
| E2 | decline excludes the region | declining the top chip → its region is in `excluded`, and the next `rankCandidates` omits it |
| E3 | accept re-bases the residual | accept one chip → residual for the next rank is `measured − target + R_accepted`; an *applied* mark drops that term from the sum |
| E4 | trust mask is a plain floor | `EqTrustMask` marks `γ²≥0.7` trusted; a bin with absent coherence is untrusted; **no ISO-2969 table involved** (EQ-R2) |
| E5 | text export round-trips the specs | `EqTextExport` writes `type, fc(Hz whole number), Q(2dp), gain(0.1 dB)` per `CLAUDE.md §"Reading out numbers"`; a re-parse yields the same `FilterSpec`s |

- [ ] **Accept:** ON ctest `base_on + N_E`. Zero `/W4`. `measure_has_no_framework_deps` still green (EqSession/EqTrustMask framework-free).
- [ ] **Mutation:** make the residual keep *applied* filters in the sum → E3 double-counts and fails; revert.
- [ ] **Commit:** `feat(app): EqSession — Auto-EQ one-shot + Suggest accept/decline/re-rank, exact ghost, FilterSpec text export`

## Task F — VERIFY mode over the output path (app, ON)

Re-measure after the operator applies, and flag honest disagreement. Depends on E and Wave 1 `OutputEngine` (BUILT).

**Files.** Create `app/src/measure/EqVerify.h` (≤ 120) + `.cpp` (≤ 140), `app/tests/test_eq_verify.cpp` (≤ 180); modify `app/tests/CMakeLists.txt`. Excitation rides `docs/dsp/2026-09-06-l7-output-path.md` §6 **verbatim** — `setSource(PinkNoise)` → `routeOutput(ch,true)` → `armSource()` → wait the coherence gate → snapshot → `disarmSource()` → wait `sourceIsQuiescent()`. Nothing here touches a gate or the slot (record §8).

"Disagrees beyond tolerance" (V1 honesty rule) per bin where `|measured − predicted|` exceeds **both** the operator's corridor half-width **and** `3σ_k`, `σ_k ≈ (20/ln10)·√((1−γ²_k)/(2·n_d·γ²_k))` (Bendat & Piersol H1, the form L6b §1 carried — **equation number unverified**, same caveat). Provable device-free by driving `OutputEngine.render` as `test_output_engine.cpp` does.

**RED first.** `#include "rta/measure/EqVerify.h"` fails. Paste it.

| # | case | acceptance |
|---|---|---|
| **F1 (first)** | excitation rides the contract | `EqVerify` drives a stub `OutputEngine`: `setSource`→`routeOutput`→`armSource` in the §6 order; `renderedSamples()` advances; `disarmSource` reaches `sourceIsQuiescent()` — asserted through the engine, no device |
| F2 | disagreement needs BOTH terms | a bin `0.2 dB` off predicted with a wide corridor and low `σ` is **not** flagged; the same off with a tight corridor **and** `>3σ` **is** — neither term alone flags (record §8) |
| F3 | σ from coherence | `σ_k` matches the closed form for given `γ²_k`, `n_d` within `1e-9` |

- [ ] **Accept:** ON ctest `base_on + N_E + N_F`. Zero `/W4`.
- [ ] **Mutation:** flag on the corridor term alone (drop `3σ`) → F2's noise bin flags and fails; revert.
- [ ] **Commit:** `feat(app): EqVerify — re-measure over the output path, flag disagreement past corridor AND 3-sigma`

## Task G — prove the guards still GUARD, both configs (record §9.10)

No new logic; a procedure whose output goes in the report. Every count is **read from the guard's own `(N files scanned)` / STATUS line**, never predicted here.

- [ ] **GREEN, framework guards, new files scanned.** `core_has_no_framework_deps` green with `ExcessPhase`, `DipClassifier`, `EqGainSolve`, `EqAllocator` in scope; `measure_has_no_framework_deps` green with `EqSession`, `EqTrustMask`, `EqVerify`. Paste each `(N files scanned)`; both rise. **RED once:** add `#include <juce_core/juce_core.h>` atop `ExcessPhase.h`, reconfigure, `ctest -R core_has_no_framework_deps` → paste the failure naming the file → remove it.
- [ ] **GREEN, polynomial guard scans the gen script.** `filter_design_has_no_polynomial_form` green with `gen_autoeq.py` in `TOOLS_DIR`; the script uses `sosfreqz(sos`/`sosfilt`, no array named `b`, no `freqz(b`/`signal.lfilter` (record §6). Paste STATUS.
- [ ] **Lengths + no stray edits.** `wc -l` over every new file (all < 400; `EqAllocator.{h,cpp}` < 300); `git diff --stat main -- core/include/rta/dsp/MinimumPhase.h core/src/dsp/MinimumPhase.cpp core/include/rta/eq/BiquadDesign.h` is empty — Wave 0 kernels reused verbatim (EQ-R5), never edited.

---

## Numbers the builder must measure, not copy

Every figure is a prediction to falsify. Measure baselines on the current tip **before** Task A; if a measurement disagrees, the plan is wrong and the orchestrator hears about it — do not bend the code to hit it. **Do NOT copy the Wave-1 tip counts (OFF 505 / ON 571)** — Wave 2 may have added DELAY tests in parallel; re-derive `base_core` / `base_on` on the actual tip you branch from.

| quantity | how |
|---|---|
| ctest OFF baseline `base_core` | `--clean-first` build of the tip, `build-l7eq` |
| ctest ON baseline `base_on` | `--clean-first` ON build, `build-l7eq-on` |
| OFF after A/B/C/D | `base_core + N_A/N_B/N_C/N_D` (read each from ctest; a `TEST_CASE`/`SECTION` list can change the count) |
| ON after E/F | `base_on + N_E/N_F` |
| `core_has_no_framework_deps` / `measure_has_no_framework_deps` scanned | each guard prints `(N files scanned)`; both rise |
| G24 measured oversampling factor | `autoeq.txt` `chosen_oversampling_factor` — **the number that proves or refutes the caveat**; report it explicitly |
| linearisation max error off the 5 points | printed by Task C's C4; amend into record §9.2 |
| MSVC `/W4` warnings | grep the build log; a warning is a defect |

## Build sequence and acceptance gate

1. **Task A** (`excessPhase` + oversampling measurement, OFF) — the caveat task, everything in G24 rests on its factor.
2. **Task B** (`classifyDip`, OFF) — the two-path test.
3. **Task C** (`solveGains`, OFF) — the one golden.
4. **Task D** (`EqAllocator`, OFF) — placement, ranking, `autoEq`.
5. **Task E** (app session + trust mask + export, ON).
6. **Task F** (app VERIFY, ON).
7. **Task G** (guards shown to guard, both configs).

**Acceptance gate:** OFF and ON ctest both green at the measured counts; **0 `/W4`** in both; `autoeq.txt` regenerated by `gen_autoeq.py --check` byte-identical; the G24 oversampling factor **measured, reported, and `>=` the sweep minimum** (not silently 8); both framework guards green with risen scanned counts and one shown red once; the polynomial guard green with `gen_autoeq.py` scanned; every new file < 400 lines (`EqAllocator.{h,cpp}` < 300); Wave 0 kernels untouched (`git diff --stat`).

## What L7-EQ does NOT include (deferred; record §11)

- The virtual EQ / virtual processor model that actually filters audio, and multi-source summation — **G11 / L7-ALIGN, Wave 3** (EQ-R3). EQ ships the `FilterSpec` set + exact ghost; G11 consumes it.
- The coherence **threshold** `θ` as a product decision — L5b's; EQ ships a labelled data-quality default `0.7` consumed as a mask (EQ-R2).
- The minimum/excess-phase **impulse responses** and their pane — the L4c display half of G24 (record §4.5); L4c reuses `excessPhase` (EQ-R1) and must still design the time-domain IRs, their windowing against L4a `Deconvolution`, and the draggable gate G25.
- The live JUCE chip/ghost panel wired into `MainComponent` — EQ ships a dev-preview specimen (EQ-R4); the panel is a later UI task.
- Re-linearising the gain solve at the solved gains (a second bounded pass) — scheduled only if C4's error exceeds 0.1 dB at the cap (record §11).
- Named **X-curve** / **Harman** presets — blocked on the purchased ISO 2969 tolerance table / verified AES source (record §5); user-drawn + text-import is the escape hatch and ships in Task E's target vector.
- FIR export of the result — that is G10 (`app/src/export/Fir*`, BUILT); EQ hands G10 a per-bin `Σ R_i` curve, not breakpoints (record §7 amends FIR §11 by one overload — a Wave-3/FIR follow-up, not this lane).

## Open questions for a human (do NOT block station 4; the record's §12 + HANDOFF judgements)

1. **Record the G24 fold** in `MASTER-EXECUTION-PLAN.md:120` (still lists G24 under L4c) and the parity row (says P4), plus the QA queue — one line each (record §12.1).
2. **`kEqTrustFloor = 0.7`** (EQ-R2, data-quality gate) — is this the interim default until L5b, and should it be a UI setting or a constant?
3. **`G_cap = +6 dB`, fallback `Q_max` 10/20, `N` cap default 6** — labelled judgements (record §12.2/§12.3); is the T60-tied `Q_max` the product?
4. **Should a NotMinimumPhase chip open V2 directly**, or only annotate (record §12.4)?
5. **`−120 dB` floor for measured `|H|`** — same constant as G10's designed-filter case, on purpose; confirm it is right for raw measured data (record §12.5).
6. **EQ-R1** (excessPhase in `rta::dsp` not `rta::eq`) and **EQ-R3/R4** (apply = specs+ghost, UI = specimen) — confirm before station 4 builds.

## For the station-4 builder, first read

1. The record `docs/dsp/2026-09-06-l7-auto-eq.md` is binding; this plan implements it and flags EQ-R1..R5, which the orchestrator amends first.
2. Order is fixed by dependency: **A → B → C → D → E → F → G**. One commit per task.
3. The failing test first, seen to fail (the missing `#include`), then the header, then the body. Paste the command and its output for every "done".
4. **The G24 CAVEAT (Task A) is the reason this sub-lane is not a curve-fitter.** Do not reuse FIR's 8× or its magnitude-identity residual; measure the factor by impulse/swing convergence on the raw two-path comb. Read `FirDesign.cpp:16-36` and `gen_fir_algo.py:196-247` to see exactly what evidence NOT to trust.
5. Wave 0's `MinimumPhase`, `designBiquad`, `responseDb` are reused verbatim (EQ-R5); prove them untouched with the diff (Task G). Every count here is a prediction you are expected to falsify.
