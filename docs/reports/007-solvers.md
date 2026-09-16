# 007 — Solvers (lane L7)

*2026-09-16, lane closeout. L7 ran as five sub-lanes over ten days:
station 1+2 for all five in one orchestrator session
(2026-09-06), then Wave 0 → Wave 1 → Wave 2 → Wave 3, each wave built by a
Sonnet-class builder against an Opus plan and refuted by an independent
verifier with no `Write`/`Edit`. Records
`docs/dsp/2026-09-06-l7-{output-path,fir-export,auto-delay,auto-eq,alignment-wizard}.md`,
research `docs/research/2026-09-06-l7-*-station1-research.md` plus
`docs/research/2026-09-15-l7-align-order4-probe.md`, plans
`docs/plans/2026-09-06-L7-wave0-impl-plan.md`,
`docs/plans/2026-09-07-L7-{out,fir,delay,eq}-impl-plan.md` and
`docs/plans/2026-09-15-L7-align-impl-plan.md`.*

**Lane state: BUILT and merged.** The last piece, ALIGN Wave 3b, merged as
PR #9 at `6d9a53d`. Nothing in L7 is now waiting on an agent; what is left is
waiting on a person, and §"Open, and it is the owner's" says what.

---

## 1. How it landed — nine PRs, five of them L7's own

Halfway through the lane the owner replaced local `--no-ff` merges with
GitHub pull requests (`docs/GIT-WORKFLOW.md`, PR #1). L7 is therefore split
across two régimes, and both are recorded here because the handoffs cite
hashes from each.

| PR | branch | merge commit | what |
|---|---|---|---|
| #2 | `l7/align-impl-plan` | `1852dff` | ALIGN station-3 plan, tasks A–J, ALIGN-R1..R14 |
| #3 | `l7/align-order4-probe` | `0855e8f` | the order-4 phase-sign probe that settled ALIGN §13.1 |
| #4 | `l7/eq-app-session-verify` | `a2de06e` | EQ tasks E/F/G + the oversampling precision fix |
| #8 | `l7/align-wave3a-core` | `02bd02a` | ALIGN Wave 3a, tasks A–F (core) |
| #9 | `l7/align-wave3b-app` | `6d9a53d` | ALIGN Wave 3b, tasks G–J (app) |

Wave 0, Wave 1 (OUT + FIR) and Wave 2 (DELAY + EQ core A–D) predate the
workflow change: they were merged into the local `main` at `a937a98`
(`--no-ff`, 2026-09-07) and reached `origin/main` when the 36-commit backlog
was pushed `4b05049..23b7ea0` on 2026-09-15. Three PRs that are not L7's own
but that L7 had to wait on or work around: #5 (`00d9571`, the three-OS CI
matrix), #7 (`be52a62`, the `atomic<shared_ptr>` guard re-lexing the files it
scans) and #1 (`1b0d133`, the workflow document itself).

**CI was green for exactly one merge.** From after #5, GitHub Actions is
blocked at the account level — every run dies in about three seconds with
"recent account payments have failed or your spending limit needs to be
increased". PRs #1/#2/#3/#4/#7/#8/#9 were merged on two locally-run
configurations plus an independent verifier, not on a green matrix. That is a
documented departure from `docs/GIT-WORKFLOW.md` rule 3 and it is the owner's
to lift (`docs/HUMAN-QA-QUEUE.md`, first `[!]`).

## 2. The numbers

**The figures at `6d9a53d` are verifier-measured**, by an independent rebuild
in a scratch worktree run in parallel with this closeout — not the builder's
own tallies, which they confirm. Generator `Visual Studio 18 2026`, MSVC 14.51,
JUCE 9.0.1 via `RTA_JUCE_PATH`. There is no CI behind them: GitHub Actions is
billing-blocked, so three local configurations plus a verifier is the whole of
the evidence and this report does not pretend otherwise.

```
INDEPENDENT REBUILD AT 6d9a53d  (scratch worktree, verifier-measured)
  RTA_BUILD_APP=OFF                                  -> 649/649, 0 failed
  RTA_BUILD_APP=ON                                   -> 717/717, 0 failed
  forced fallback (-DRTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK=ON) -> 649/649, 0 failed
  "warning C" across the build logs                  -> 0
  guards green                                       -> 11 of 11
  rtatool_snapshot                                   -> 8 PNGs, named with their
                                                        byte sizes below
```

The per-wave figures below are the **builder's**, pasted from `ctest` at each
commit named, and are the record of how the lane got there:

```
Wave 0 tip   (08723bd..c61b5dc)   OFF 470/470
Wave 1 FIR                        OFF 489     ON 553
Wave 1 OUT                        OFF 505/505 ON 571/571
Wave 2 DELAY                      OFF 529/529 ON 597/597
Wave 2 EQ core A-D                OFF 551/551 ON not measured (E/F unbuilt)
EQ E/F/G  base 23b7ea0            OFF 551     ON 619
EQ E/F/G  tip                     OFF 564/564 ON 632/632
after the five-PR merge a2de06e   OFF 591/591 ON 659/659
ALIGN Wave 3a  02bd02a            OFF 624     ON 692/692   (see note)
ALIGN Wave 3b  branch tip         OFF 649/649 ON 717/717
warning C in every build log      -> 0
```

Three honesty notes the next session should not have to re-derive:

- **`6d9a53d`'s tree is byte-identical to the Wave 3b branch tip.**
  `git diff --stat 6d9a53d^2 6d9a53d` is empty, so **OFF 649 / ON 717** is a
  claim about the merge commit and not only about the branch — and the
  independent rebuild read the same two numbers off the merge commit itself.
- **OFF 624 is the one figure nobody measured directly.** The ON baseline 692
  *was* measured, by PR #9's verifier, on a clean tree at `02bd02a`; 624 closes
  arithmetically, since `717 − 692 = 25 = 649 − 624`. It is arithmetic, not a
  reading, and it is labelled as such here for the same reason PR #9 labelled
  it.
- **Both configurations rise on app work.** `app/tests` is
  `add_subdirectory`-ed *outside* the `RTA_BUILD_APP` guard, so the JUCE-free
  analysis tests compile in OFF too. Three separate plans predicted "ON only"
  and were wrong; a fourth should not be.

### Guards, at `6d9a53d`

Each was made RED once in the shape that actually trips it and GREEN again
after revert — the shapes are in PR #9's table and in `docs/HANDOFF.md`.

| guard | scanned at `6d9a53d` | before L7 |
|---|---|---|
| `core_has_no_framework_deps` | 157 files | 96 (end of L6b: 107) |
| `measure_has_no_framework_deps` | 64 files | 57 at `02bd02a`, 42 at end of L6b |
| `coherence_gate_is_not_bypassed` | 96 files | 68 |
| `filter_design_has_no_polynomial_form` | 183 files | 125 |
| `output_render_has_no_rt_hazards` | lines 126–210 of `OutputEngine.cpp` | **new in L7** (Wave 1 OUT) |
| `audioio_callback_has_no_rt_hazards` (ON) | lines 119–146 of `AudioIo.cpp` | new in L7 |
| `audioio_scoped_no_denormals_is_first` (ON) | OK | pre-existing, unchanged |
| `platform_types_has_no_framework_deps` | green; last counted at 8 during EQ task G | 5 |

`git diff --stat origin/main -- platform/` was empty for the whole of Wave 3,
so the three RT-hazard guards are green by construction there rather than by
mutation. The independent rebuild counts **11 guards green**, which is the
eight above plus `core_makes_no_class_1_claim`, `no_std_atomic_over_shared_ptr`
and `test_names_are_ascii`.

**One hole in a guard, found while writing this report and not by it.**
`core/tests/check_no_framework_deps.cmake:37-43` globs
`${CORE_DIR}/include/*.h`, `*.hpp`, `${CORE_DIR}/src/*.h`, `*.cpp` and
`${CORE_DIR}/tests/*.cpp` — **but not `${CORE_DIR}/tests/*.h`**. Four test
headers are therefore never scanned: `core/tests/CrossoverBandFixture.h`,
`core/tests/DelayFilterFixtures.h`, `core/tests/support/Golden.h` and
`core/tests/guard_fixtures/hex_escape_comment.h`. A `#include <juce…>` in any
of them would compile into `rta_core_tests` and the guard would still print OK.
None of them contains one today; the hole is in the scan, not in the tree. A
one-line fix is in flight on branch **`ci/guard-scan-test-headers`** and is not
part of this closeout — see `docs/HUMAN-QA-QUEUE.md`.

### The eight snapshot PNGs, at `6d9a53d`

Rendered by the independent rebuild with
`rtatool_snapshot.exe shots 1100 760`, byte sizes as written:

| file | bytes | what it is |
|---|---|---|
| `preview-phase.png` | 45851 | **L7-ALIGN's visible evidence** — the G18 surface over a synthetic BW4 pair |
| `preview-target.png` | 46908 | dev-preview specimen, target/corridor mockup |
| `preview-tf.png` | 45311 | dev-preview specimen, transfer function |
| `transfer.png` | 42213 | the Bode composite over a deterministic fixture — **this is where measured evidence lives** |
| `workspace.png` | 55099 | the 1–3 pane vertical stack (L5c decision 6) |
| `main-live.png` | 39853 | the real measurement window, `MainComponent`, 1280×800 |
| `specimen.png` | 42023 | the `az_ui` design swatch — **not** a `Snapshot`, and not measured data |
| `rta-view.png` | 21384 | the RTA plot alone, byte-for-byte diffable |

`shots/` is gitignored; nothing under it is committed.

## 3. What was built, per sub-lane

### L7-OUT — the output path (Q3-folded prerequisite)

Record `docs/dsp/2026-09-06-l7-output-path.md`. Commits `881862b..16ec825`.

Every solver has to play a signal, so the owner folded G20's generator
solo/mute out of L6b §8 and into L7 as a *prerequisite*, not a sibling: the
lock-free output path is designed once and serves both solver excitation and
auto solo/mute. Shipped: `RampedGain::prepare`, an `OutputEngine` built on
`platform/types` and free of JUCE, a callback that renders in **one line after
`pushFromCallback`** with `ScopedNoDenormals` still the first statement, and an
`OutputPolicy` with strict-solo and additive modes. `kRequestedOutputChannels`
went from 2 to `kMaxChannels` without over-reading.

The verifier proved the render path RT-safe by reading it: no allocation, no
lock, no I/O, no FFT — scratch is preallocated in the constructor,
`std::visit` runs over a trivially-copyable source, and
`Sweep::buildInverseFilter` is unreachable from it. That reading is now held by
a test rather than by the reading: `output_render_has_no_rt_hazards`.

Two things the next session must not overstate. **G20 auto solo/mute is a
capability plus a binding pattern, not a live UI** — no `CaptureSequencer` has
a UI owner today, and wiring `onStep` into `MainComponent` means building the
sequencer panel that record §12 puts out of scope. And the "complementary
handover" test proves *reversal continuity*, not the ramp's **shape**: every
odd-symmetric curve satisfies `g(p) + g(1−p) = 1`. The shape is caught by two
sibling tests (raised-cosine closed form, ramped sine), both red under
mutation.

### L7-FIR — FIR export (G10)

Record `docs/dsp/2026-09-06-l7-fir-export.md`. Commits `4facbd8..74bd03a`
plus `d0246fe`. Frequency-sampling linear phase and a minimum-phase variant
that reuses `minimumPhaseFromMagnitude` verbatim, log-frequency target
interpolation, text and 32-bit-float WAV export under `app/src/export/`
(`FirExport.h`, `FirTextWriter.cpp`, `FirWavExport.h`, `FirWavWriter.cpp`),
golden `core/tests/golden/fir.txt` from `tools/gen_fir.py` with scipy as second
author and `--check` proving byte-identical regeneration.

Two premises the station-1 research carried were refuted by reading before any
code: Toeplitz + Hankel is not Levinson, and a WAV sample-rate field is **not**
safe to rely on — CamillaDSP ignores it.

**The G24 caveat that this lane created and EQ paid.** FIR ships cepstral
oversampling at **8×**, and the first builder's evidence for it — "the
magnitude-identity residual is flat at every factor" — is an algebraic
tautology (`Re(FFT(fold(c))) == FFT(c)` cannot distinguish an adequate factor
from an inadequate one). The comment was corrected in `d0246fe`. The real
evidence is convergence of the minimum-phase impulse, and 8× suffices **only
because** `designLinearPhaseCore` windows the target first, which bounds how
sharp a notch can be. That is why EQ had to re-derive its own factor rather
than inherit this one — see `memory/a-shared-kernels-numeric-factor-is-not-transferable.md`.

### L7-DELAY — auto-delay

Record `docs/dsp/2026-09-06-l7-auto-delay.md`. Commits `7e1c209..753d522`.
`suggestDelay` is a policy layer over `findDelayPhat`, refactored bit-for-bit
through a private `PhatCorrelation.h` (the verifier diffed it verbatim against
`6d22342^`). Trust is `peak / f_band` against a null floor `√(ln m / M_in)`
with `c = 4`, from a 105-trial survey with zero wrong answers.
`ResidualDelayTracker` reads `γ²` and never writes it, and its zero-allocation
claim is proven by a real counting allocator rather than asserted.
`RawCaptureBuffer` and `DelayLocator` refuse `Mls` and drive strict solo
through `OutputEngine`.

**DELAY is the one L7 solver an operator can actually reach.** `MainComponent`
owns a `LOCATE` button (its hint reads "pink noise, output ch 1, one-shot"), a
delay readout that shows `delay: locating…` then the samples and milliseconds,
and an `APPLY` button enabled **only** when the verdict is
`DelayVerdict::Accepted` — `MainComponentDelay.cpp`, task F2. The record's §8
ordering is respected in the wiring: `disarmSource()` happens **before**
`suggestDelay`.

The sub-sample gap at fractions 0.3/0.7 of about 0.19 samples is the known bias
of three-point parabolic interpolation (≈ 4 µs at 48 kHz), measured
independently by the verifier — not a defect.

The two modes are forced by the mathematics, not chosen by taste: **Locate**
(linear correlation, its own span, no coherence) and **Track** (circular
correlation on an averaged `Sxy`, coherence as the weight). First-arrival-
fraction was rejected because PHAT manufactures a phase-inverted ghost at
`D₁ − Δ` of height ≈ a/2, and phase-slope was rejected because it needs unwrap,
which L2 §6 forbids in `core/`.

### L7-EQ — auto-EQ, with G24 folded in

Record `docs/dsp/2026-09-06-l7-auto-eq.md`. Core A–D in `7756da9..6940f92`;
app E/F/G in `1a5df1a`, `976f0e3`, `c8e1769` (PR #4, `a2de06e`).

Core: `rta::dsp::excessPhase` (EQ-R1), `rta::eq::classifyDip` with
`S* = 2·asin r_D`, `solveGains` (ridge Cholesky, the golden carries the
condition number) and `EqAllocator` (greedy peaking placement plus `autoEq`).
App: `EqTrustMask.h` with `kEqTrustFloor = 0.7` and the rule that **absent
coherence means wholly untrusted, not a pass**; `EqSession` (committed set,
`applied` mark, exclusion mask, Auto EQ, Suggest accept/decline/re-rank, an
exact dB-add ghost); `EqTextExport.h`; and `EqVerify` with `h1SigmaDb` from
Bendat & Piersol's H1 (the equation number is still marked UNVERIFIED, exactly
as L6b §1 carried it) plus `compareToPrediction`, which flags a bin only when
it exceeds **both** the corridor **and** 3σ, and never flags an untrusted bin.

The sign convention is load-bearing and is written where it is enforced:
`EqSession::workingResidualDb` returns `m − t + Σ Rᵢ` **raw**, and the
auto-offset `c` and the negation belong to `EqAllocator`
(`EqAllocator.cpp:203` `negated()`, `:32` `autoOffset()`). Doing either again
in `app/` would double it and flip the sign.

**G24's oversampling factor, and what it is actually driven by.** Raw measured
magnitude needs a minimum of **64×**; `kExcessPhaseOversamplingFactor` ships
**128×**. The lane's first framing said 64× is the smallest factor at which the
excess-phase *swing* converges. Measured again through the pure functions of
`tools/gen_autoeq_algo.py`, that is **wrong**: swing alone converges at 8×
across every fixture, and the criterion that demands 64× is **tail energy** — a
proxy for cepstral aliasing. 128× ships unchanged, because tail energy *is*
aliasing and aliasing corrupts the kernel whether the swing metric has noticed
or not. The framing matters because a reader who believed swing drove it could
cut the factor eightfold and see nothing go red until a real measurement with a
sharp notch arrived. Corrected in three places: the `gen_autoeq_algo.py`
docstring, the constant's comment in `core/include/rta/dsp/ExcessPhase.h`, and
a dated **§4.3 amendment** in the record — an addition, not an edit, because
the record was written at station 2 before Task A measured anything and
contains the word "oversampling" nowhere. No golden was regenerated;
`gen_autoeq.py --check` reports byte-identical afterwards and the SHA-256 of
`core/tests/golden/autoeq.txt` is unchanged either side.

### L7-ALIGN — G11 + G17 + G18 with ρ

Record `docs/dsp/2026-09-06-l7-alignment-wizard.md`, plan
`docs/plans/2026-09-15-L7-align-impl-plan.md`.

**Wave 3a, tasks A–F (core), PR #8 at `02bd02a`.** `37d8e5b` the five G11 ops
in `rta::dsp` — `applyDelay` / `applyPolarity` / `applyGain` / `applyBiquads`
and `sumResponses` returning `summationTrust` (deliberately **not** named
`coherence`); `1e5bbc4` `spectralCrossover`, the `|H_A| = |H_B|` crossings with
a coherence gate, two-point interpolation, and **every** crossing listed rather
than one picked; `f6b9e52` `crossoverBandFit`, τ found on the complex plane
with the intercept as a circular mean, a bounded `R`, and the summation
cross-term as the weight; `8d8e733` `expectedOffset` in `app/src/measure/` —
record §3's topology table as **one closed form**; `5a05904`
`rta::ir::relativePolarity`, bounded, **with no verdict**; `2659ee7` the two ρ
surveys, which adopt nothing.

**Wave 3b, tasks G–J (app), PR #9 at `6d9a53d`.** `VirtualTrace` as the single
dB↔complex conversion point, with four of its seven cases negative and each
carrying a positive control on `Trace` itself (a `VirtualTrace` is not
constructible as a `Trace`, `TraceLibrary::add` on one is ill-formed, it has no
`meta()`, and the sum's per-bin trust lives on a **separate return type** so
the sum has nowhere to put a `coherence` even by accident). `AlignmentWizard`
plus `AlignmentWizardSignals.cpp`: four **asked** questions (high-pass side,
topology, "has the processor already inverted one output?", crossover seed),
the L7-OUT §6 solo sequence over a real `OutputEngine` with no device, strict
solo as the sequence default, nine named refusals, and a polarity-signal table
that asks rather than picks. `CrossoverSurface` as the G18 model, JUCE-free and
proven in the OFF configuration, with `PhaseAlignPreview` repointed from canned
arrays onto it and checked by the offscreen snapshot.

**What a user of `rtatool.exe` can actually see of ALIGN today: nothing.**
(DELAY is the one solver with a real UI — see §7. ALIGN has none.) This is the
sentence most easily left out of a closeout, so it is stated first and measured
rather than remembered.

- **`rta::view::CrossoverSurface` is a headless model.** It is JUCE-free and
  proven in the OFF configuration, and its pixels come from exactly one place:
  `app/src/dev/preview/PhaseAlignPreview.{h,cpp}`, rendered offscreen by
  `tools/snapshot.cpp` into `shots/preview-phase.png`. **It is not wired into
  `MainComponent`** — `grep -c CrossoverSurface app/src/MainComponent.cpp
  app/src/MainComponent.h app/src/MainComponentDelay.cpp` is **0, 0, 0**.
  That is not an oversight: it is decision **ALIGN-R8**, "G18 ships as the
  existing dev-preview specimen driven by a real model, not as `MainComponent`
  wiring", which ends with the instruction "nobody should hunt for a
  `MainComponent` hook". So the lane state is **BUILT (model + specimen)**, and
  **`MainComponent` wiring is a follow-up that has not started.**
- **`AlignmentWizard` has less than that — it has no UI at all.** Its only
  references anywhere in `app/src` are its own three files
  (`measure/AlignmentWizard.h`, `AlignmentWizard.cpp`,
  `AlignmentWizardSignals.cpp`); there is no view, no panel, and no
  dev-preview specimen. It is reachable **only from `app/tests/`** — four test
  files plus `AlignmentWizardFixture.h`. The four questions it asks, the solo
  sequence and the nine refusals are all exercised by ctest and by nothing
  else. Nothing in the running application can start a wizard today.

Neither of these weakens the ctest evidence — a JUCE-free model proven in both
configurations is exactly what the architecture asks for — but a reader must
not come away thinking an operator can open a pane and see it.

**G17 is a phase question, and the wizard asks it.** The correct crossover
phase offset is topology-defined — BW-N high-pass leads low-pass by `N·90°` at
every frequency, LR-N inherits the identity at the same N — and it is *not*
recoverable from measurement, because 180° of wiring and 180° of topology are
indistinguishable to a measurement. "Maximise the measured sum" was rejected
for exactly that reason. What `family` changes is the **designed sum**, not the
offset.

## 4. What ships with NO threshold, and why

`relativePolarity` returns a bounded ρ with **no verdict and no threshold**,
and that is the lane's most deliberate omission.

Two independent grids were run (`tools/probe_rho_a.py`,
`tools/probe_rho_b.py`) and **they did not agree**:

- **Grid A** — 2 wrong-sign cells out of 43,200, which puts a floor at
  ρ > 0.0640. But a **correctly**-signed cell sits at 0.0520. The two
  distributions overlap, so no scalar cut separates them.
- **Grid B** — zero wrong-sign cells in 2,000, so no floor can be placed
  at all.

This is precisely the failure
`memory/a-threshold-read-off-a-grid-is-that-grids-floor.md` was written about:
a threshold read off one grid is that grid's floor and nothing more. Test E6
(`core/tests/test_relative_polarity_guard.cpp`) holds the absence structurally
and leads with a positive control on `DelayPolicy.h` so that "no verdict here"
is a measurement and not an assumption.

Two documented failures ship beside it, as tests with `DOCUMENTED FAILURE` in
their names: **ρ reads negative across a correctly wired BW2 crossover**, and
**two band-pass boxes move the peak off lag 0 at order 4**. So ρ is a witness
the UI may show beside `findPolarity`, with its reasons — never a verdict, and
never a topology sign.

## 5. Corrected claims — the decision records and their amendments

A record that is never amended is a record nobody checked. Five corrections
landed as dated amendments; a sixth is still owed.

1. **ALIGN §4 — the competing delays do NOT sit at `τ* ± n/f̄`.**
   `|S(τ)|` is a magnitude, so writing `f_k = f̄ + δ_k` factors the mean out as
   a rotation of modulus 1 that `|·|` discards: the answer depends on the
   spread alone and `f̄` **cannot appear in it**. The spacing comes from the
   band's **width**, `1/(N·Δ)`. Measured by case C5b, which moves the window's
   centre and holds its width: 40–160 Hz and 240–360 Hz, both 121 bins of 1 Hz,
   give a first competitor at **0.0118209 s in both**, while `1/f̄` would change
   threefold. **Amended in place in §4, 2026-09-16.** The paragraph's
   conclusion survives; only its mechanism was wrong.
2. **ALIGN §7 — ρ across a BW2 crossover is LOW, not "high".** Measured
   **0.0676** on a correctly wired BW2 pair at fc = 100 Hz / 48 kHz, sign
   negative, lag 0; the Parseval closed form in the fixture predicts
   **0.0675611** — six figures. `|L|` and `|H|` barely share a band, so ρ is a
   Cauchy–Schwarz overlap ratio of order `√(fc/(fs/2)) = 0.065`. This
   **strengthens** the record: across a crossover ρ is both low *and*
   confidently wrong about the sign. **Amended in place in §7, 2026-09-16**
   (case E8).
3. **ALIGN §13.1 — the order-4 wrong-sign attribution, closed without owner
   input.** An independent probe (`tools/probe_align_order4.py`,
   `docs/research/2026-09-15-l7-align-order4-probe.md`, PR #3) measured the
   `N·90°` identity exact to machine precision — analog `0.00e+00°`, digital
   `1.16e-11°`, BW and LR, orders 1–8, every frequency. L4a's wrong sign at
   order 4 comes from applying decision 6b's **un-whitened correlation
   peak-sign rule (ρ)** to a fixture that is **two band-pass boxes** rather
   than a matched-cutoff crossover pair: the offset stops being constant
   (62.9° spread at order 4) and the cross-correlation peak leaves lag 0 for an
   opposite-signed lobe, while the value *at* lag 0 stays correctly positive.
   Convention is not and cannot be the culprit — conjugation flips the offset's
   sign, and `−0° = 0°`, `−180° = 180°`, so no flip can reach an **even** order.
   The shipped PHAT correlator (`DelayFinder.cpp:34`) does not reproduce L4a
   either: it reads the mirror, right at order 4 and wrong at order 8. Two
   correlators disagreeing on one unchanged pair of loudspeakers is the whole
   argument for the ruling that followed: **never read a topology sign off any
   correlation peak, whitened or not.** Locked by
   `core/tests/test_align_order4_identity.cpp`; §3 carries the SETTLED note and
   §13.1 is struck through as closed.
4. **EQ §4.3 — 64× is a tail-energy figure, not a swing figure.** See §3 above.
   Added, not edited, because the record had no oversampling paragraph.
5. **EQ §7 — the ghost identity is over the NOT-applied filters**, and
   `EqTextExport` writes a fifth, optional `applied` column the plan's Task E
   row did not have. Round-2 verification showed why: a list without the bit is
   unsafe to load back into the processor that produced the measurement — the
   applied rows land twice, and −7.1 dB becomes −14.2 dB on an 8 dB bump. The
   column is written only for applied rows and is optional on read, so a
   four-column file still imports and an unflagged row reads as *not* applied.
   Amended in §7.
6. **Still owed: ALIGN-R1's `1e-12`.** It is a property of the Butterworth-SOS
   fixture it was measured on, not of the lock. On a −9 dB Q = 4 peaking
   evaluated at its own corner, the two spellings cannot agree better than
   **1.23e-12**; task A6 asserts a **derived** per-ω conditioning bound instead,
   which at ω = 3.14 is 130× *tighter* than 1e-12. The amendment belongs in §5
   and in the Wave 0 plan and has not been written. Memory:
   `a-tolerance-inherited-from-a-plan-is-that-plans-fixture.md`.

A seventh, smaller one: **record §10.11 understates the coherence-gate
mutation.** Renaming `summationTrust` to `coherence` alone would **not** go red,
because `check_coherence_gate.cmake:44-47` matches *assignments*, not
declarations; the mutation has to do both. Confirmed by making it red.

### Two plan rows amended in place

- **D6 (Wave 3a).** The plan's word-grep does **not** return "only the
  pre-existing `ButterworthDesign` hits" on an untouched tree. Measured:
  `BandWeights.h:41`, `EqGainSolve.cpp:20`, `Decay.cpp:8` and `:86`, and four
  in `FilterBank.cpp` — it matches prose. D6 and E6 ship instead as
  **declaration-shaped scans run from the tests**, so they execute in CI on all
  three operating systems; both test targets gained `RTA_REPO_ROOT`.
- **I4 (Wave 3b).** The plan's word-grep does not catch the very mutation the
  plan prescribes: `bestDelayForLoudestSum()` contains none of the listed
  words, and the only line that does is the shipped header's own comment
  arguing against the objective. Shipped instead: the class's member functions
  are **enumerated** and must match a whitelist exactly. That whitelist then
  failed four times in a row, each time to a form the previous round's
  narrower claim had not covered — class-body-only (a free function slipped),
  `=`-line-skipping (a lambda object slipped), line-based (the same lambda
  wrapped over two lines slipped, which is how the scan's own comment wrote the
  example), and a comment stripper with no string-literal state (a URL's `//`
  ate a closing `";` and swallowed the next declaration). It is no longer
  line-based: the file is stripped of comments with literal state tracked,
  joined into one string, and cut into declaration units at `;`/`{` with
  function bodies skipped by brace depth. **Eight** forms now go red with the
  name present in the enumeration, and the scan's **scope — including what it
  does not cover** (raw string literals, preprocessor conditionals, another
  translation unit) — is written into the test rather than claimed around it.
  Memory: `a-prescribed-mutation-is-not-proof-the-check-catches-it.md`.

## 6. What the verifiers refuted

Wave by wave, the findings that changed shipped code or shipped claims:

- **Wave 0.** A real bug: `designBiquad` on a high-Q large-cut shelf (Q = 8,
  −15 dB) drove the radicand `(A + 1/A)(1/Q − 1) + 2` negative, so alpha and
  every coefficient came out NaN. `validate()` now throws
  `std::invalid_argument` for a shelf outside the domain (`c61b5dc`). Peaking
  is unaffected — its alpha carries no gain term. The verifier also confirmed
  that loosening the tolerances still killed the mutations (perturb the `A`
  divisor → 192/192 red at 1e-9; drop the fold-doubling → 2060/4096 red at
  3e-7), which is what makes the tolerance a bound and not a fit.
- **Wave 1 FIR.** The magnitude-identity residual argument for 8× was refuted
  as a tautology (above), and with it the temptation for EQ to inherit the
  factor.
- **Wave 2 EQ.** Every load-bearing claim was mutation-tested red: dropping the
  negation made the residual **increase** and D5 go red; the `S*` threshold and
  both guards went red on demand. The Wave 0 kernel diff is empty — it was
  reused verbatim, which is what EQ-R5 asked for.
- **Wave 3a.** Three record corrections (§4, §7, ALIGN-R1) and four plan
  corrections (B1's `0.7`/`12.4` are not representable in `float`, which is
  what `magnitudeDb` is — the rounding alone puts the crossing 3.75e-6 Hz off
  124.0, 3700× the plan's own 1e-9, so the fixture uses the binary-exact 0.75
  and 12.25 and asserts at 1e-12; C6 pins τ at 0 because a free search slides
  it to 2.96e-5 s; C1's intercept uses the derived bound `2π·f̄·dτ_max`; D6
  above). Plus one live defect: `crossoverBandFit`'s agreement had a lower
  bound but no **upper** one where it is tight, so scaling the R denominator
  gave `R = 2` with the whole suite green. `R ≤ 1` by the triangle inequality
  is now asserted in C1 and C5 (`778a305`).
- **Wave 3b, four rounds.** Round 1: three of nine `WizardRefusal`
  enumerators had no test, including the live `TraceNotUsable` branch (case H4b
  now drives all nine and asserts it saw nine), and four numeric expectations
  had no derivation. Round 2 **refuted the fix**: "a `switch` with no
  `default:` makes MSVC emit C4062 at /W4" is false — C4062 is off by default
  on 14.51 and `/W4` does not enable it, and a tenth enumerator built clean
  with H4b still green. It now ships as a `WizardRefusal::Count` sentinel plus
  `kWizardRefusalCount`, which needs no warning flag and behaves identically on
  all three CI operating systems; the missing `default:` is kept for the reason
  that **is** true, GCC/Clang's `-Wswitch`. Rounds 2–4 also walked the I4
  whitelist through the four escapes described above. And the verifier measured
  a **false PASS**: deleting the test executable is not sufficient for a
  mutation in a header guarded by a compile-time assertion, because MSBuild
  relinked without recompiling the one translation unit that holds the
  `static_assert`s. The recipe is delete the exe **and force the TU that holds
  the assertion** — now a section in
  `memory/mutation-testing-needs-the-exe-deleted-first.md`.

## 7. Known gaps, stated rather than hidden

- **Shelves are not placed.** `EqSession` creates no shelf; every `FilterSpec`
  comes from `EqAllocator`, which is peaking-only this pass. `EqTextExport`
  reads and writes all three types because a hand-edited file may contain them,
  but nothing in the app generates a shelf — so the `(Q, gainDb)` domain clamp
  (EQ-R5/D6) is still **unenforced** and belongs to whoever adds shelves.
- **`app/src/dev/preview/EqPreview.*`** — the specimen EQ-R4 describes — was
  not built. Only the ctest-provable half of Task E/F shipped.
- **Exactly one solver is reachable from the running application, and it is
  DELAY.** `MainComponent` owns a `LOCATE` button (hint: "pink noise, output
  ch 1, one-shot"), a delay readout and an `APPLY` button that is enabled only
  when the verdict is `DelayVerdict::Accepted` — `MainComponentDelay.cpp`,
  task F2. Every other solver has **no `MainComponent` owner**, measured:
  `EqSession`, `EqVerify`, `RawCaptureBuffer`, `CaptureSequencer`,
  `AlignmentWizard` and `CrossoverSurface` all read **0** references across
  `MainComponent.{h,cpp}` and `MainComponentDelay.cpp`. G18 reaches pixels only
  through the dev-preview specimen and the offscreen snapshot (ALIGN-R8); the
  wizard reaches nothing outside ctest; no `CaptureSequencer` has a UI owner
  (the OUT note that G20 is a capability plus a binding pattern). **`MainComponent`
  wiring for the rest of L7 is a follow-up that has not started**, and each of
  these was a named decision in its record rather than a drift.
- **ALIGN-R7 is a string proxy.** `ReferenceMismatch` compares
  `CaptureMeta::channelRoles` for string equality. Making it structural needs a
  `CaptureMeta` field and a session-schema bump, which this lane did not do.
- **`SignalStanding::Authoritative` exists and nothing produces it.** Without
  the value, "across a crossover no time-domain sign is authoritative" is
  unfalsifiable; with it, the enumeration says so.
- **`Biquad.h::maxPoleRadius` uses `std::max(0.0, NaN)` = 0.0**, so a NaN filter
  reads as maximally stable. Moot on the shelf path after `c61b5dc`, but a trap
  for every other NaN source. Not fixed: `Biquad.h` is frozen and this needs
  its own task.
- **H7's disagreement cell is a regression lock**, labelled as one in the test
  name. ρ's sign and the product of two `findPolarity` readings agree on every
  ordinary pair (five constructions probed); they part at `Polarity.h`'s
  documented limit 1 — a two-way box with an inverted tweeter — and it is
  crossover-dependent: 800 Hz and 1200 Hz agree, **2000 Hz disagrees**
  (findPolarity negative, margin 1.00; ρ +0.7986), 3500 Hz agrees again. The
  fixture takes the 2 kHz cell. It will go red if either internal moves for an
  unrelated reason.
- **Three files exceed the plan's per-file budgets** (none exceeds CLAUDE.md's
  hard 400): `AlignmentWizard.h` 300 against a planned 180,
  `AlignmentWizard.cpp` 318 against 300, `test_virtual_trace.cpp` 330 against
  280.

## 8. Open, and it is the owner's

None of these is an agent's to close.

1. **A real sub/main pair** (ALIGN record §13.2). Every check in §10 is
   synthetic. Whether `R` stays high enough on a real room capture for the
   intercept to be readable, and whether ±1 octave is the right window on a
   real 24 dB/oct pair, need a person with a rack and a microphone.
2. **Question (c)'s "unknown" branch** (ALIGN record §13.3). When the operator
   does not know whether the processor inverts, the G18 surface now shows both
   candidate lines (`targetAmbiguous()` / `alternativeTargetRadians()`) with
   nothing choosing between them. The record's reading is that showing both is
   not inferring — the operator still chooses which line to believe and the
   wizard says so — but it is the one place the "never infer topology" ruling's
   edge is close, and the owner should look before it goes further.
3. **The `core/tests/test_weighting.cpp` assertion review** deferred from PR #5
   on 2026-09-16. Old: `isinf(|H(Nyquist)| dB)`. New: zero-DC asserted on the
   coefficients `b0 − b1 + b2 = 0` (an exact identity, Sterbenz) plus `> 200 dB`
   at Nyquist with a bound argued from `d²`. Structurally stronger, weaker at
   exactly one value. Merged as-is on the owner's instruction, logged in
   `docs/HUMAN-QA-QUEUE.md`.
4. **GitHub Actions is billing-blocked**, so `docs/GIT-WORKFLOW.md` rule 3's
   merge gate cannot be satisfied at all until the owner restores it.
5. **The L7 judgement defaults await sign-off** — chosen and named, not silent,
   and none of them blocks: `G_cap +6 dB` / `Q_max` (EQ §12.2), the N cap
   (§12.3), `NotMinimumPhase → V2` (§12.4), a −120 dB floor on measured `|H|`
   (§12.5), the Locate plausibility window (DELAY §14.1), tracker on by default
   (§14.3), the 64-output hardware check (OUT §13.1), and strict solo as the
   sequence default (OUT §13.2 / DELAY §14.2, owner decision 4 of 2026-09-06).
6. **The ρ fold-in ruling was relayed, not recorded** (ALIGN record §13.4).
   This closeout writes the line into `docs/HUMAN-QA-QUEUE.md`; the owner
   confirms it says what was meant.

## 9. What a human can run

In `docs/HANDOFF.md`, the top section **"2026-09-16 — L7 (Solvers) CLOSED
OUT"**: every runnable thing L7 produced, with the exact PowerShell 7 command,
one command per block, and what should appear on screen. It opens with the
section a closeout is most tempted to skip — **what of L7 is visible in
`rtatool.exe` at all** (DELAY's `LOCATE`/`APPLY`, and nothing else) — then
covers both ctest configurations, the per-solver test listing (and why a
`ctest -R` word filter misses the ρ cases), the eleven guards with their
scanned counts and the one hole in `check_no_framework_deps.cmake`, the
offscreen snapshot with all eight PNGs by byte size and what
`shots/preview-phase.png` shows, the two ρ surveys and the order-4 probe, the
FIR text and WAV export paths, and the EQ text format with its `applied`
column.

That section also carries rule 12's third item: the handoff for the next lane,
**L6a (SPL-pro)** — what to read first, what is blocked on the owner, and the
fact that it starts at **station 1**, because SPL-pro has no decision record
yet.
