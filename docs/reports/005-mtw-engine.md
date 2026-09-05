# 005 — The multi-time-window engine (lane L3)

*2026-09-05 → 2026-09-06. All five stations of lane L3 in one orchestrator
session: three station-1 research agents, the station-2 record
`docs/dsp/2026-09-05-mtw-l3.md`, the station-3 plan
`docs/plans/2026-09-05-L3-mtw-impl-plan.md`, two Sonnet build passes and one
fix pass, and four adversarial verifiers — one on the record itself before
anything was built, three on the code.*

## The numbers, from a clean rebuild

Measured by the final station-5 verifier on `55d7314`, in build directories no
builder had touched, `--clean-first`, Visual Studio generator (never Ninja),
MSVC 14.51.36231:

```
cmake -S . -B build-final-on  -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=ON -DRTA_JUCE_PATH=...
cmake --build build-final-on  --config Release --parallel --clean-first   -> 0 "warning C"
ctest --test-dir build-final-on  -C Release                              -> 100% tests passed, 0 tests failed out of 436

cmake -S . -B build-final-off -G "Visual Studio 18 2026" -A x64
cmake --build build-final-off --config Release --parallel --clean-first  -> 0 "warning C"
ctest --test-dir build-final-off -C Release                              -> 100% tests passed, 0 tests failed out of 390
```

Baseline at the start of the lane: **362 (OFF), 402 (ON)**, both measured on
`7e10eb6` in this session. The lane is +28 core and JUCE-free app tests, +34 in the ON
configuration. The one place the verified figures live is `docs/HANDOFF.md`'s
baseline block; this report does not copy them twice.

Guards after the lane, all proven RED-then-GREEN by inserting the forbidden
thing and watching them fail:

```
core_has_no_framework_deps             OK, 96 files   (before: 87)
coherence_gate_is_not_bypassed         OK, 62 files   (before: 57)
filter_design_has_no_polynomial_form   OK, 113 files  (before: 103)
measure_has_no_framework_deps          OK, 30 files   (before: 29)
platform_types_has_no_framework_deps   OK, 5 files
```

## What was built

**`core/`** — `rta::dsp::MtwLayout` (the band table as pure functions),
`MtwEngine` (seven `DualFftEngine`s at full rate, FFT size doubling per octave
downward, one delay per band, one stitch), `MtwResult` (frequency vector, a
`(band, bin)` index per point, and the per-band `TransferSnapshot`s — it copies
no coherence array, on purpose). Defaults `N_0 = 1024`, `K = 6`: 1281 points,
0.73 Hz bins below 187.5 Hz, 89–177 points per octave elsewhere, longest
window 1.365 s. Golden `core/tests/golden/mtw.txt` from `tools/gen_mtw.py`
(`scipy.signal.csd` per band, sliced to the owned bins).

**`app/`** — `Snapshot::mtw` (`std::optional<MtwBlock>` with its own frequency
vector and per-band descriptors), `Analyser` hosting the MTW engine beside the
fixed one and feeding both the same pair per hop (gap **G2**), `TransferView`
drawing from an explicit frequency vector through the same `xForHz` geometry as
stored traces, seam hairlines at the six band boundaries on all three panes, a
per-plot source toggle defaulting to MTW, and a readout strip that prints each
band's integration time.

`DualFftEngine.cpp` and `test_dualfft.cpp` were not edited (321 and 400 lines,
`git diff main` empty for both).

## What the research changed about the plan

Three things, each of which the roadmap had assumed the other way.

1. **No decimation.** The master plan's L3 row said "decimation cascade"; so
   does Smaart's own manual. The research found that a 1024-point FFT on a
   stream decimated by `2^k` and a `1024·2^k`-point FFT at full rate cover the
   same seconds, the same bin width, the same Hann leakage and the same PSD, and
   that the anti-alias filter cancels in `H1` and `γ²` while aliasing does not.
   The full-rate design costs about 44 MFLOP/s per channel — the number the
   filter-bank record already called irrelevant when it removed *its*
   decimation cascade with the rule "decimation returns only with a profiler
   result in hand". No profiler result exists. Record §2.
2. **The seam is physics, not a defect, so it is shown and not blended.** A
   band admits a reflection at lag τ in proportion to its window's
   autocorrelation `r_w(τ)`, which is the same `overlapCorrelation()` L2 uses
   for `Neff`. A 30 ms reflection is a comb in the 1.365 s band and absent from
   the 21 ms band, where it lowers coherence to `1/(1+a²)` instead. That is a
   closed-form test (T4), and it is the test that proves MTW does the thing it
   exists for. Record §4.
3. **Averaging is frames, not seconds — reversed after the first draft.** The
   record's first §5 specified seconds, uniform across bands. The planner ran
   the inherited 0.5 s default through the real `exponentialEffectiveAverages`
   and found three bands could never reach the coherence gate of 8; the
   smallest uniform value that could was 3.5 s, at which the top band averages
   682 looks. Uniform confidence is uniform frames. The record was reversed and
   the seconds are now *reported* per band instead — 0.09 s at the top, 5.5 s
   below 187.5 Hz at depth 16. Record §5; memory
   `a-default-must-be-run-through-the-gate-it-feeds.md`.

## What the verifiers refuted

**On the record, before any code.** Memory estimate wrong by an order of
magnitude (rings are `4·fftSize`, FIFO storage is allocated unconditionally);
point count 1282 was 1281, and the 1282 reading implied a duplicated 187.5 Hz
at one seam; the owned-bin formula was written with `N_k` where only `N_0`
holds; and the coherence-gate guard is a name-based tripwire that a stitched
`coherence` member would trip or evade, so the core result must not copy one.
All four corrected in the record before station 3 read it.

**On the plan.** `exponentialEffectiveAverages` does not reach the header's
`(2−a)/a`; the returned value is `1 + (Neff_raw − 1)/D` with `D = 1.9246` at
75 % Hann overlap, identical in every FFT size. Quoting the ceiling overstates
reachable averages by nearly 2×. Memory
`a-header-ceiling-is-not-the-reachable-average-count.md`.

**On L3a (core).** Nothing. 383/383, four mutations caught, guards RED→GREEN.

**On L3b (app), first pass.** Two findings went back to station 4 and were
fixed in `55d7314`: the record required the view to *show* each band's
integration seconds and nothing did; and no test asserted seam marks are
*absent* when a pane shows the fixed FFT — the verifier removed the gate and
all tests stayed green. Both now have tests, and the coherence pane draws seams
too.

**Two builder findings worth keeping.** The plan's mutation "rotate
`firstIndex` by one band" is invisible to the pure-delay tests, because every
point reads 1.0 whichever band it came from; only the frequency-varying Task 3
tests catch it. And the plan's compensated-delay fixture produces phase exactly
0.0, so it cannot catch a missing radians→degrees conversion; the test gained
an uncompensated backstop.

## Known gaps, stated rather than hidden

- **MTW traces are live-only.** `Trace` derives its axis from `fftSize` on
  purpose; storing an MTW trace is an L5 amendment with its own record.
- **The per-plot source toggle has no UI control yet** — it exists as an API
  the tests and a future control can call. The default (MTW everywhere) is what
  a user sees.
- **No committed snapshot fixture carries an `MtwBlock`.** The only rendered
  evidence is from a temporary patch to `tools/snapshot.cpp`, applied and
  reverted three times (two builders, one verifier). A committed fixture is a
  small follow-up.
- **5.5 s of fill below 187.5 Hz at the default depth** is the operator-facing
  cost of frames-uniform averaging. Whether a coherence trace filling in from
  the top reads as a fault on a real stage is a question for a person with a
  system, not for arithmetic; the readout strip exists so that person has the
  number in front of them.
- Cross-fade at seams, FTW/TFC-style post-windowing, MTW-driven RTA banding and
  MTW coherence weighting for L6b are all listed in record §8 as not decided.

## How this landed

Branch `claude_desk/fable-orchestration-planning-bebcb3`, 11 commits ahead of
`main` at the time of writing (`git rev-list --count main..HEAD` — measure, do
not copy). Not merged; merge is the owner's word.
