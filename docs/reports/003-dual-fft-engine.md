# 003 — The dual-FFT transfer-function engine (lane L2)

*2026-08-29. Stations 3, 4 and 5 of lane L2. Station 1 and 2 landed the day
before as `docs/dsp/2026-08-28-dual-fft.md`, so this lane began at the
implementation plan.*

## The numbers, from a clean rebuild

Incremental builds can silently reuse a stale `.obj`, so nothing below comes
from one. Measured on the branch as it stands, uncommitted:

```
cmake -S . -B build-l2 -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=OFF
cmake --build build-l2 --config Release --clean-first --parallel
   -> exit 0, grep -ci warning = 0

ctest --test-dir build-l2 -C Release
   -> 100% tests passed, 0 tests failed out of 261
   -> Total Test time (real) = 7.28 sec
```

**Baseline at the start of this lane was 208.** The whole suite is +53 tests.

That 208 is the `RTA_BUILD_APP=OFF` count. `docs/HANDOFF.md`'s 226 is the
`RTA_BUILD_APP=ON` count, which adds the two JUCE-linked targets. **The ON
configuration was not re-measured this session** — no work in this lane touched
JUCE, `platform/`, or `ui/`, and the app-side file that was touched
(`app/src/measure/PhaseUnwrap.*`) compiles into `rtatool_analysis_tests`, which
is built in the OFF configuration too. A number nobody measured does not go in
a report, so this one says so instead of estimating.

There are now **five** framework/representation guards, not three:

```
core_has_no_framework_deps              filter_design_has_no_polynomial_form
measure_has_no_framework_deps           core_makes_no_class_1_claim
platform_types_has_no_framework_deps    coherence_gate_is_not_bypassed   <- new
```

## What was built

Eight tasks, each built by one agent and refuted by a fresh one that read the
real files. Plan: `docs/plans/2026-08-29-L2-dual-fft-impl-plan.md`.

| # | Deliverable | Files |
|---|---|---|
| 1 | Harris 1978 overlap correlation; **effective** independent averages for both averaging modes | `core/{include/rta,src}/dsp/AverageCount.*` |
| 2 | The engine: two rings, one-time delay skip, three accumulators, FIFO + exponential. Plus `PsdScaling.h`, now the single definition of `2/(fs·Σw²)` — `SpectrumEngine` was rewired to it | `core/include/rta/dsp/PsdScaling.h`, `core/{include/rta,src}/dsp/DualFftEngine.*` |
| 3 | H1, H2, Hv (total least squares), magnitude-squared coherence, and the gated snapshot | `core/{include/rta,src}/dsp/TransferEstimator.*` |
| 4 | Group delay from the closed form, with its smoothing width stated | `core/{include/rta,src}/dsp/GroupDelay.*` |
| 5 | GCC-PHAT: flat weighting, relative floor, sub-sample peak, polarity | `core/{include/rta,src}/dsp/DelayFinder.*` |
| 6 | Coherence-gated phase unwrap, in the VIEW layer | `app/src/measure/PhaseUnwrap.*` |
| 7 | scipy goldens + a build guard on the coherence gate | `tools/gen_transfer.py`, `core/tests/golden/transfer.txt`, `core/tests/check_coherence_gate.cmake` |
| 8 | This report, the docs sync, and two memory files | — |

## What the research changed about the plan

The decision record claimed `y[n] = g·x[n−D]` makes `H` exactly
`g·e^{−j2πkD/N}`. **True only under a rectangular window.** A frame of a
periodic signal is a circular rotation of the block, but `w[n]·x[(n−D) mod N]`
is not the rotation of `w[n]·x[n]`, so under Hann the identity is approximate.
A worker writing that test with Hann would have loosened the tolerance until it
went green and destroyed the one test pinning magnitude, phase and delay at
once. The plan states this before any test code; the exact-identity tests use
`Rectangular`, and Hann appears in the statistical tests where it belongs.

## What the verifiers refuted

Nine holes, across seven tasks. **Not one came from a failing test.** Every one
came from deliberately breaking working code and noticing nothing went red.

| Where | The hole |
|---|---|
| T1 | The 75 %-overlap test's `[0.50, 0.56]` band also accepted the 0.5351 produced by truncating the lag sum after m=1. Now asserts the formula against correlations pinned elsewhere. |
| T1 | `exponentialEffectiveAverages` **hung for ever** at `hop == 0` — a `noexcept` function in an infinite loop. Proven by a harness compiled against the real `.cpp`, killed at a 10 s timeout. The FIFO twin survived the same input only by accident, via a second loop bound. |
| T2 | Both delay tests leave the frames bit-identical (`X == Y`), and `conj(X)·Y == conj(X·conj(Y))` is real either way — so conjugating the wrong channel was invisible. The sign convention is load-bearing: it feeds straight into `referenceDelaySamples`. |
| T2 | The reset test fed 4096 samples against a 40-sample delay, so the skip counters were already spent — "forgetting to re-arm from zero" was indistinguishable from re-arming. Now resets mid-skip. |
| T2 | `TransferAveraging::Exponential` appeared in **zero tests**. The whole branch could be deleted silently. |
| T2 | The `min(frameCount, fifoDepth)` clamp was dead: every test that queried `effectiveAverages()` used a depth larger than its frame count. That mutation overstates independence exactly once the FIFO saturates — what the coherence gate exists to prevent. |
| T3 | `makeSnapshot` was never called with `H2` or `Hv`; the enum dispatch was unproven. `phaseRadians` was pinned by nothing but `isfinite` on silence. |
| T4 | Doubling the one-sided difference's denominator at both edge fallbacks left **all 2618 assertions passing**. The constant-response test touches the edges only where `diff == 0`, where any denominator gives zero. |
| T5 | Deleting the band limit entirely passed all 9 delay-finder fixtures — the case asserted only the delay, which the default path already proved. |

Two more that are not test holes but belong here:

- **`DualFftEngine::validate()` did not check `minimumEffectiveAverages`.** With
  it set to 0, the engine emitted coherence at frame one — and the agent
  demonstrated the record's exact prediction rather than merely asserting a
  throw: 516 bins, every one reading exactly 1.0. Record §3 says the value is a
  judgement but the floor's existence is not; now `validate()` enforces it.
- **`measure_has_no_framework_deps` was not watching the new files.** It uses an
  explicit file list, and `PhaseUnwrap.*` was never added. The guard passed
  perfectly while scanning twenty files that were not the ones in question.

## Two facts that cost time twice, now in `memory/`

- **`RealFft` transforms in `std::complex<float>`** — and its *absolute* error
  floor is set by the largest bins, not the bin under test. A
  parts-per-million-of-this-bin tolerance is unattainable at the top of a
  decaying spectrum. Two agents hit this independently, in different tasks.
  `memory/float32-fft-precision.md`.
- **Four conventions that look arbitrary and are not** — the conjugation side,
  the delay sign, magnitude-squared coherence (ours reads *lower* than Open
  Sound Meter's, which is a different quantity and not a bug), and
  absence-not-zero. `memory/dual-fft-conventions.md`.

## Known gaps, stated rather than hidden

- **The sub-sample parabola's sign handling is untested by construction.** The
  ratio `(r₋₁−r₊₁)/(r₋₁−2r₀+r₊₁)` is invariant under negating all three
  samples, so fitting to `polarity·r` and to `|r|` elementwise agree whenever
  the triple shares a sign — which is every case a smooth PHAT peak produces.
  They diverge only on an opposite-sign neighbour, and then by a fraction of a
  sample. A discriminating fixture needs a ~0.02-sample margin, and a test that
  fragile fails later for unrelated reasons and teaches people to ignore red.
  Documented in `DelayFinder.h` and `.cpp` instead.
- **`coherence_gate_is_not_bypassed` is convention enforcement, not a fence.**
  A verifier demonstrated three bypasses in a sandbox — a pointer write, a
  reference alias, and `.emplace()` — all of which the first version passed with
  `guard OK (4 files scanned)`, exit 0. The patterns were widened and all three
  now fire; re-run against the real tree it reports `OK (46 files scanned)` with
  no false positives. A multi-statement alias construction, a pointer-to-member,
  and an aggregate initialisation of `TransferSnapshot` can still slip past. The
  guard's own header comment names those, because a guard that overstates its
  reach stops the next reader looking.
- `Estimator::Hv`'s branch selection is proven correct (the `+` root is the one
  bracketed by |H1| and |H2|, by Cauchy-Schwarz) but only across the bins of one
  fixture.

## Nothing here is committed

Every file above is untracked or modified-unstaged. That is deliberate — this
session had no instruction to commit, and the project rule requires approval
naming the action. Two consequences a reader should know:

1. The plan's per-task step "paste the red output into the commit message body"
   did not happen. **This report is where that evidence lives instead**, and the
   verifiers independently reproduced the mutations they could.
2. `git status --porcelain | wc -l` is **39**: 28 new, 11 modified — across
   `core/` (22), `docs/` (8), `memory/` (4), `app/` (4) and `tools/` (1). One
   lane's worth, not a partial state. (An earlier draft of this line guessed
   "~25" from memory rather than measuring. Pitfall #10 does not spare the
   person writing the warning.)
