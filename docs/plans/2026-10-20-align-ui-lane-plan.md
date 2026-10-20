# ALIGN-UI lane (H5, `app/align-ui`) — stations 1–3 research and plan

*2026-10-20. Read at `origin/main` `65ee5f6` (H4 SWEEP merged, #69). Fifth
sequential hot lane of plan review #2; opens after H4. Every task names its
production caller and its operator path (CLAUDE.md rules 5 and 6). Governing
record: `docs/dsp/2026-09-06-l7-alignment-wizard.md` (the "record"). Planner:
Opus 5.5 (Plan agent, read-only); written down by the orchestrator. Owner
defaults adopted under plan review #2's "execute autonomously" instruction and
the owner's 2026-10-19 "do the rest until done"; every row can be overridden
before its task is built. Rows the planner flagged as genuinely the owner's are
marked **(owner)** — they are defaulted, built, and listed on the decision
board so the owner can reverse them.*

## Station 1 — literature, other tools, three findings that change the obvious plan

**Literature.** Linkwitz, "Active Crossover Networks for Noncoincident
Drivers", JAES 24(1), 1976. Rane Notes 107 and 160 (the record's §3 table,
`arg H − arg L = N·90°`, CI-locked in `core/tests/test_align_order4_identity.cpp`).
McCarthy, *Sound Systems: Design and Optimization*: match phase over the
crossover *band*, not at one point (record §2 rejects the single-bin read).
Farina 2000: an ESS IR gives an unambiguous coarse arrival — the record's §4
source for the cycle integer.

**Other tools** (station-1 research, second-hand via Sound Design Live):
- Smaart / SysTune: store a live transfer trace of each source with the other
  muted, seed delay from the IR delay finder, match phase in the overlap, use
  "+6 dB says aligned" as a check afterwards.
- Open Sound Meter: same shape, alignment by hand.
- REW alignment tool: two stored sweeps, delay/invert controls, and a "find
  optimal delay" that maximises the summed response.
- **Where they disagree:** REW uses the *sum as the objective*; Smaart/OSM use
  it as a *check*. Record §0 ruling 1 and §6 forbid the objective, so REW's
  optimiser is the one thing not copied. All take the coarse delay from an IR
  and the fine alignment from coherent transfer phase — the split adopted here.

**F1 — a SWEEP trace cannot be fitted.** `traceFromSweepIr` stores phase and
no coherence (SWEEP decision 4). Fit and crossover search treat absent
coherence as absent (`CrossoverFit.cpp:13-16`, `CrossoverBandFit.cpp:88`), so
two SWEEP traces give `AllBinsAbsent`. The fit runs on coherent TRANSFER
captures; SWEEP supplies only the IR witnesses — the wizard's own split
(`submitCapture(Trace)` vs `supplyImpulseResponses`).

**F2 — defect in shipped code: the cycle integer comes from the wrong number.**
`AlignmentWizard.cpp:282-283` sets `cycleOffset_ = irLow.originIndex −
irHigh.originIndex`. `originIndex` is t = 0: in core the end of the inverse
filter (`Deconvolver.h:34`), in SWEEP the reference peak
(`SweepIrPipeline.cpp:151`). Neither is the arrival. For two SWEEP presses the
difference is pre-roll timing jitter (0 to ~500 ms) — plausible-looking and
wrong. The arrival relative to t = 0 is `measPeak − refPeak`
(`SweepIrOutcome::delaySamples`). The record's §4 sentence carries the same
misreading; no test catches it. Fixed by T8; record amended after the code.
(Orchestrator confirmed by reading `origin/main` 2026-10-20.)

**F3 — SYNTHETIC renders one output channel.** `prepare(fs, 1)` /
`render(&out, 1, n)` (`SyntheticInput.cpp:73,118`); the wizard solos
`subOutputChannel = 1`, so the sub step plays silence. No device-free test of
the real sequence is possible without T0.

## The design (wave A: live wizard; wave B: IR witnesses)

1. Operator presses **ALIGN**, a sixth pane-selector button (`PaneView::Align`, `"align"`).
2. In the pane they answer: MAIN and SUB output channels; (a) which side is
   high-pass; (b) topology; (c) inversion; (d) seed ("NO SEED" is an explicit
   answer). Every combo starts "not asked" except the two output channels.
3. **MEASURE** → `AlignRunner::press()`: checks blockers (decision 11 + busy),
   builds a fresh `AlignmentWizard`, replays the stored answers, `arm()`:
   pink noise at −12 dBFS, strict solo of the high-pass side's channel.
4. The 2 Hz timer calls `poll()`. Once `captureWindowOpen()` holds and a dwell
   has passed (dwell = `transferFifoDepth·fftSize/fs` of samples rendered
   *since this step's solo*, 1.365 s at defaults), the runner freezes
   `latest()`'s TRANSFER block (STORE's `freezeSnapshot`), requires coherence,
   submits it, and adds it to the library as `ALIGN MAIN @ HH:MM:SS` /
   `ALIGN SUB @ …`, group `ALIGN`.
5. Wizard re-solos to the low-pass side; repeat 4; then disarms and computes.
6. Wave A (no IRs): `AskingCycle` — operator types a coarse offset in ms →
   samples. Wave B (IRs bound): `Computed` or `AskingPolarity`, with the
   polarity signals and their reasons listed.
7. Pane shows the verdict (crossover, τ\*, the candidate chosen by the
   integer, R, Δ vs the named topology, inversion sentence) and the
   `CrossoverSurface` with the suggested delay as a pending G11 op.
8. Operator sets the rack and presses **SUM**: `beginMeasuredSum()` routes both
   outputs; after the dwell `ALIGN SUM @ …` is frozen and drawn via
   `CrossoverSurface::setMeasuredSum` against the predicted sum.
9. Wave B: **SWEEP MAIN** / **SWEEP SUB** run H4's sweep on that output; per-side
   IR pickers list library SWEEP traces whose IR is still in memory
   (`IrStore::find`); bound IRs go to the wizard keyed by *source*, each
   carrying its arrival (T8).

**Against the obvious approaches.**
- (i) Run SWEEP on main then sub and feed the SWEEP traces to the wizard — F1
  makes the fit refuse, F2 makes the integer garbage. Rejected.
- (ii) Pick two STORE traces in an XOVER-like pane and press COMPUTE — the
  wizard has no engine-free entry (`arm()` solos and plays); a bypass or dummy
  engine proves nothing about which channel the operator named actually played
  (the show-time hazard), and SUM has nothing to route. Not built here.
- (iii) Fold ALIGN into XOVER — XOVER's subject is already-captured traces
  (`PaneFactory.cpp`), `CrossoverPaneView.cpp` is 322 lines, and wizard
  answers are run state that must survive pane rebuilds. Cost of a separate
  pane: topology asked in two places; mitigated by one shared list (T5).
- (iv) REW's "find optimal delay" — forbidden (record §6); the I4 member
  whitelist on `CrossoverSurface` stays untouched.

## Owner-default decisions

| # | decision | default | why / what the alternative costs |
|---|---|---|---|
| 1 | pane **(owner)** | sixth selector button `ALIGN`, factory seam (`PaneFactory.cpp`); model owned by `MainComponentAlign` so it survives pane rebuilds (EQ pattern) | folding into XOVER splits a 322-line file and mixes viewer with producer |
| 2 | excitation | `gen::PinkNoise` at −12 dBFS RMS (VERIFY/LOCATE level) | a sweep per step does not fit the wizard's single `armSource` and yields no coherence (F1) |
| 3 | capture timing **(owner)** | automatic after window open + dwell + coherence present | matches `EqVerifyRunner`, deterministic in tests; a CAPTURE button (Smaart-like) adds a press per side and a "never pressed" timeout path |
| 4 | output channels | MAIN/SUB preselected to out 1/out 2 (`Config` 0/1); equal channels refused | routing facts, not designer intent, so the ASK ruling does not cover them |
| 5 | fit options **(owner)** | `BandFitOptions{}` as shipped (±1 oct, 20 ms, 1 µs grid); `matchedPairAgreement` 0.5; `minimumGatedCoherence` 0.0; pane prints "fit window ±1 oct, R bar 0.5: unmeasured defaults" | record §4/§12 want these measured on real pairs first (QA §13.2); blocking leaves row 17 CORE ONLY |
| 6 | cycle integer | wave A: operator types ms, samples = round(ms·fs/1000); wave B: from IR arrivals. τ shown = fit candidate nearest integer/fs, τ\* also shown | record §4's own design; `Verdict` gains the candidate list (T1); τ\* alone would silently pick a cycle |
| 7 | inversion sentence **(owner)** | shown when \|Δ\| > 90° (nearer the inverted line); Δ always shown in degrees; unknown (c) shows both deltas | record §2 asks for "a stated tolerance" never measured; the nearest-line rule needs none |
| 8 | captures | library group `ALIGN`, `ALIGN MAIN/SUB/SUM @ HH:MM:SS`, persisted by SAVE | lets XOVER/EQ reuse them |
| 9 | IR source | SWEEP MAIN / SWEEP SUB in the ALIGN pane run H4's sweep on that output; pickers use `IrStore::find` | IR and transfer capture are separate moments; the pane prints both times |
| 10 | SYNTHETIC **(owner)** | loopback renders 2 outputs, each with its own measurement path; ch 0 unchanged (40 Hz–16 kHz BP, delay 4); ch 1 default LR4 LP at 100 Hz, delay 100; reference = lowest routed output's block; tests override both | changing ch 0 would re-break every SYNTHETIC magnitude test and snapshot H4 just settled |
| 11 | refusals before press | each named: Exponential averaging (dwell undefined), no reference (`NoTransfer`), device stopped and not SYNTHETIC, LOCATE/CAL/SWEEP/VERIFY busy, fs mismatch, MAIN == SUB | same set as `EqVerifyRunner::blocker()`; ALIGN *allows* SYNTHETIC |
| 12 | QA rows Z2, Z14 | not absorbed; ALIGN's surface draws one run's captures, which share `appliedDelaySamples` because APPLY is refused while ALIGN is busy (T6) | absorbing Z2 touches `VirtualTrace`/`CrossoverSurface` (I4 whitelist) for a defect ALIGN cannot trigger |
| 13 | wizard API | wave A: `Verdict::candidateTausSeconds`; wave B: `supplyImpulseResponse(Source, ImpulseWitness{Deconvolution, arrivalSamples})` replaces the high/low pair | keyed by source because question (a) decides high/low inside the wizard (ALIGN-R14) |
| 14 | F2 fix **(owner)** | take the cycle integer from IR *arrivals*, not `originIndex`; amend record §4 after the code | current code returns pre-roll jitter; there is no correct use of the `originIndex` difference |

## Sequencing and mutual exclusion

- **Run state.** `AlignRunner`: `Idle → Measuring(High|Low) →
  Computed/AskingCycle/AskingPolarity → MeasuringSum → Done`. `busy()` only
  while a wizard step is `Measuring*` or `MeasuringSum`.
- **One fresh wizard per MEASURE** (`arm()` refuses unless `Answering`); the
  runner keeps the answers and replays them.
- **Guards both ways.** `captureBusy()` (`MainComponent.h`) gains
  `|| align_.busy()` — read by LOCATE (`MainComponentDelay.cpp:24`), CAL
  (`MainComponentCalibration.cpp:90,105`), SWEEP's `outputBusy` host and
  VERIFY's `env.captureBusy` (`MainComponentEqVerify.cpp:26`). `applyClicked`
  (`MainComponentDelay.cpp:118`) refuses while ALIGN is busy. ALIGN refuses
  while any of those is busy.
- **Aborts** (each disarms and says why): `outputEpoch()` moved; SYNTHETIC
  toggle moved; device stopped; no usable snapshot within dwell + 5000 ms
  (`captureTimedOut`); transfer grid or averaging mode changed mid-run.
- **Polling.** `align_.poll()` in `timerCallback` before the SYNTHETIC early
  return (`MainComponent.cpp:242-247`).
- **Threads.** `compute()` on the message thread; in wave B T11 prints its
  time, > 100 ms moves to a worker (EQ risk-2 precedent). `AlignRunner.h`
  holds the wizard behind a pimpl so no message-thread header includes
  `rta/ir/*` (H4 isolation rule).

## Device-free acceptance (argued)

**Option A, chosen:** SYNTHETIC with N = 2 outputs and a per-channel path.
Reference = lowest routed output's block (`role()` is a relaxed atomic,
`OutputRoutingConfig.h:58`); measurement = Σ_c path_c(out_c) + noise. Proves the
wiring CI otherwise cannot see: the named channel is the one that played, each
capture lands in the right slot, the dwell flushes the previous step, SUM
routes both outputs.

**Option B, rejected:** drive the wizard only from stored traces — cheaper, but
proves nothing about `MainComponent`'s channel mapping and cannot exercise SUM.

**Fixture (T6):** noise off; ch 0 = LR4 HP 100 Hz, delay 4; ch 1 = LR4 LP
100 Hz, delay 100, so τ0 = +96 samples = +2.000 ms (LP later); HP = MAIN,
LR-4, NOT INVERTED, NO SEED. Each tolerance is a **discrimination bound** —
half the gap between the right answer and the named mutant's answer (accuracy
is core C1's job):
- swapped solo channels give τ ≈ −2 ms → assert |τ − 2 ms| < 2 ms;
- inverted ch 1 path gives |Δ| ≈ 180° → assert |Δ| < 90°, no inversion sentence;
- SUM routing main only reads 6.02 dB below the predicted sum at fc → assert the
  gap at fc < 3 dB.
The builder prints τ, Δ and the gap beside their bounds and states any
windowing bias observed. Do not tighten the bounds.

## Station-3 task table

| task | files | called from | operator path | acceptance (mutant that must go RED) |
|---|---|---|---|---|
| **T0** SYNTHETIC with 2 outputs | `measure/SyntheticInput.h/.cpp`, `MainComponentSynthetic.cpp` | `SyntheticInput::runBody`; `setSyntheticMode` | SYNTHETIC → ALIGN MEASURE | `test_synthetic_loopback.cpp` (+ `_multi.cpp` if > 400): ch 1 soloed + Oscillator armed → measurement == path1(reference) bit-exact, reference == ch 1 render; ch 0 soloed → H4 behaviour bit-identical. RED: render 1 channel; apply path0 to ch 1 |
| **T1** wizard candidates + dwell extraction | `AlignmentWizard.h/.cpp` (`Verdict::candidateTausSeconds`); new `measure/TransferDwell.h` (moves `EqVerifyRunner::dwellSeconds`) | `AlignReadout::resolveCycle` (T3); `AlignRunner::poll` (T2); `EqVerifyRunner` | MEASURE | C5 fixture (40–160 Hz, τ0 = 9 ms): list contains τ\* and the 11.82 ms competitor (C5b); `test_eq_verify_runner*` green. RED: empty candidate list |
| **T2** `AlignRunner` (JUCE-free; add to `measure_has_no_framework_deps` GLOBS) | new `measure/AlignRunner.h/.cpp` (`AlignHost`, `AlignEnvironment`, `AlignBlock`) | `MainComponentAlign::poll`, pane MEASURE/SUM | MEASURE, SUM | `test_eq_verify_runner_flow.cpp` pattern: (a) each decision-11 refusal by name; (b) only `mainCh` Routed when HP = MAIN, only `subCh` when HP = SUB; (c) no submit earlier than the dwell since *this step's* solo; (d) captures in group `ALIGN` with names; (e) epoch change → disarm, `busy()` false; (f) second MEASURE keeps answers; (g) `answerCycleMs(2.0)` @48 kHz → 96 → `Computed`. RED: submit first snapshot; dwell from arm not per solo; fixed solo channel |
| **T3** readout | new `measure/AlignReadout.h/.cpp` | `AlignPaneView` (T5) | MEASURE → readout | τ = +2 ms, HP = MAIN → "delay MAIN by 2.00 ms (96 samples)"; HP = SUB → "delay SUB"; candidates {τ\*, τ\*±8.26 ms} with integer 9 ms → τ\* + 8.26; |Δ| = 100° → inversion sentence, 80° → none; ambiguous → both deltas. RED: names swapped; always τ\*; a 45° rule |
| **T4** pane vocabulary | `PaneRegistry.h`, `PaneSelectorDecision.h`, `PaneFactory.h/.cpp` (+`AlignPaneBinding`), `MainComponentPanes.cpp` (6th cell, STORE disabled), `MainComponent.h` | `selectPaneView`, `restoreWorkspaceFromSession` | ALIGN button; OPEN of a saved `align` pane | round trip over six enumerators. RED: delete the `"align"` arm |
| **T5** pane view | new `view/AlignPaneView.h/.cpp`; new `view/TopologyChoices.h` (7-row list moved from `CrossoverPaneView.cpp`) | `makePaneFactory` Align branch | ALIGN → answer → MEASURE | every question combo starts "not asked"; MEASURE disabled with blocker text until all answered; `setPendingOps`/`setMeasuredSum` get production callers; XOVER tests green. RED: a pre-selected topology |
| **T6** controller + wiring + guards | new `MainComponentAlign.h/.cpp`; `MainComponent.h/.cpp`; `MainComponentDelay.cpp` (APPLY guard); `MainComponentTestAccess.h`; both source lists | ctor, `timerCallback` | **ALIGN pane MEASURE and SUM** | `tests_juce/test_main_component_align.cpp`, fixture above through real combos/buttons: library +2 then +1 after SUM; τ/Δ/gap bounds hold; LOCATE/CAL/SWEEP/APPLY refused mid-run, ALIGN refused during SWEEP; SYNTHETIC off mid-run → aborted, disarmed; switch away/back keeps verdict. RED: remove `|| align_.busy()`; swap solo channels; SUM without additive toggle |
| **T7** specimen | new `tools/snapshot_align.h/.cpp`, `tools/snapshot.cpp` (+2) | `rtatool_snapshot` | — | `main-live-align.png` through the real buttons in SYNTHETIC; prints FAILED if no verdict |
| **T8** wizard arrival fix (wave B) | `AlignmentWizard.h/.cpp`, `AlignmentWizardSignals.cpp`, signals test call sites; new `app/tests/test_alignment_wizard_ir.cpp` | `AlignRunner` IR binding (T11) | SWEEP MAIN/SUB → MEASURE | origins 12345 apart, arrivals 40 (main) and 136 (sub): HP = MAIN → +96, HP = SUB → −96; H8 invariance and H7 green; record §4 amendment after the code. RED: `originIndex` difference; fixed high/low mapping |
| **T9** arrival field + lookup | `SweepIrOutcome.h` (`SweepIr::arrivalSamples`), `SweepIrPipeline.cpp:161`, `trace/IrStore.h` (`find`) | `AlignRunner` binding; pane IR pickers | SWEEP → ALIGN IR picker | `arrivalSamples == delaySamples` on pure delay 37; `find` hit, miss, nullptr after the 9th add. RED: `find` returns the first entry |
| **T10** SWEEP on a named output | `MainComponentSweep.h/.cpp` (`clickedOnOutput(ch)`, `swept(id, ch)` callback, name `SWEEP out<N> @`) | ALIGN pane SWEEP MAIN/SUB via `MainComponentAlign` | SWEEP MAIN, SWEEP SUB | SYNTHETIC 2 outputs: ch 1 sweep arrival == 100 + argmax of path1's `BiquadCascade` IR (same float ops), exact integer; plain SWEEP still uses out 1. RED: hard-coded `soloOutput(…, 0)` (`MainComponentSweep.cpp:91`) |
| **T11** IR binding | `AlignRunner`, `AlignPaneView`, `MainComponentAlign` | pickers, `swept` callback | SWEEP MAIN → SWEEP SUB → MEASURE | through buttons: `Computed` with no AskingCycle; integer == sub arrival − main arrival; rho listed; sub `findPolarity` listed as refused (BandTooHigh); prints `compute()` time. RED: drop the supply |
| **T12** close each wave | — | — | — | `tools/orphan_check.py --base <wave start>` exit 0; `AlignmentWizard.cpp`, `AlignmentWizardSignals.cpp`, `IrStore::find`, `setPendingOps`, `setMeasuredSum` reachable from `rtatool`; every file within budget |
| **T13** human try | — | — | see below | a human sees it |

**Waves.** A = T0–T7 + T12 (flips status row 17 to BUILT, partial: no IRs).
B = T8–T11 + T12 + T13.

## File-length budget (hard cap 400; counts at `65ee5f6`)

| file | now | after |
|---|---|---|
| `MainComponent.h` | 353 | ≤ 360 |
| `MainComponent.cpp` | 311 | +14 |
| `MainComponentPanes.cpp` | 159 | +12 |
| `MainComponentDelay.cpp` | 128 | +4 |
| `MainComponentSynthetic.cpp` | 122 | +14 |
| `MainComponentTestAccess.h` | 230 | +12 |
| `MainComponentSweep.h/.cpp` | 116 / 197 | +12 / +22 |
| `measure/AlignmentWizard.h/.cpp` | 299 / 318 | +20 / +12 (split cycle logic into `AlignmentWizardSignals.cpp` if `.cpp` passes 340) |
| `measure/SyntheticInput.h/.cpp` | 139 / 164 | +18 / +35 |
| `measure/EqVerifyRunner.h/.cpp` | 170 / 303 | −4 / −8 |
| `measure/SweepIrOutcome.h`, `SweepIrPipeline.cpp` | 95, 192 | +4, +1 |
| `trace/IrStore.h` | 59 | +10 |
| `view/CrossoverPaneView.cpp` | 322 | −22 |
| `view/PaneRegistry.h`, `PaneSelectorDecision.h` | 105, 64 | +4, +2 |
| `PaneFactory.h/.cpp` | 36 / 34 | +3 / +7 |
| `tools/snapshot.cpp` | 342 | 344 |
| `app/tests/test_alignment_wizard_signals.cpp` | 358 | call sites only |
| `app/tests_juce/test_synthetic_loopback.cpp` | 271 | ≤ 400, else `_multi.cpp` |
| `measure/AnalysisThread.h` | 398 | **0 — do not touch** |

New: `AlignRunner` ~160/300; `AlignReadout` ~60/160; `TransferDwell.h` ~30;
`AlignPaneView` ~140/300; `TopologyChoices.h` ~40; `MainComponentAlign`
~100/220; `snapshot_align` ~30/200; each test file ≤ 350.

## Hot files (serialise with H6)

`MainComponent.h/.cpp`, `MainComponentPanes.cpp`, `MainComponentDelay.cpp`,
`MainComponentSynthetic.cpp`, `MainComponentSweep.*`, `MainComponentTestAccess.h`;
`PaneFactory.*`, `PaneRegistry.h`, `PaneSelectorDecision.h`;
`measure/SyntheticInput.*`; both `app/cmake/*_sources.cmake`,
`app/tests/CMakeLists.txt`, `app/tests/cmake/impl_test_sources.cmake`,
`app/tests_juce/CMakeLists.txt`, `main_component_tests.cmake`; `tools/snapshot.cpp`.

## Human-try row (real hardware)

1. Out 1 → main amp, out 2 → sub amp; loop from the console split to in 2
   (REF); microphone on in 1 (MEAS).
2. Open ALIGN; answer from the processor's preset (topology and inversion come
   from the preset, not the measurement).
3. SWEEP MAIN, then SWEEP SUB: each IR delay ≈ distance/343·fs.
4. MEASURE: pink noise on main only, then sub only. Read crossover Hz,
   "delay X by n ms", R (reported as an unmeasured bar), Δ.
5. Dial the delay into the processor, press SUM: measured sum lies on the
   predicted sum near fc.
6. Move the sub 1 m back and repeat: suggested delay grows by ~2.9 ms.
7. No hardware: SYNTHETIC → ALIGN → answer HP = MAIN, LR-4, NOT INVERTED, NO
   SEED → MEASURE → type 2 ms → SET OFFSET. **Wave B (no typed offset):**
   press SWEEP MAIN, wait for its `IR +n samples` line, press SWEEP SUB, then
   MEASURE: the pane's right-hand block lists `IR MAIN` / `IR SUB` with their
   arrivals, the verdict carries no "type the coarse offset" prompt (the integer
   is sub arrival − main arrival less the named topology's low-pass peak lag; SET
   OFFSET stays usable and a typed offset overrides it, the status saying so; if
   the integer fits no candidate clearly the pane shows "IRs suggest about X ms"
   and asks instead), and the signal
   lines read `rho: …` and `findPolarity, low-pass side: refused (BandTooHigh)`
   (`main-live-align-ir.png`). Picking `NO IR (TYPE THE OFFSET)` in either
   picker and pressing MEASURE brings the offset prompt back. With the SHIPPED defaults (ch 0 the
   H4 40 Hz–16 kHz band-pass +4, ch 1 the LR4 low-pass at 100 Hz +100, decision
   10) the pane reads a crossover between the two corners (about 55 Hz), a delay
   on MAIN of a few ms (the 96-sample offset plus the skirts' group delay), a Δ
   beyond 90° and the opposite-polarity sentence. That is the right reading of
   THAT pair, not a fault: a 2nd-order band-pass skirt against a 4th-order
   low-pass is not an LR-4 crossover (`test_main_component_align_defaults.cpp`
   locks these structural facts; `main-live-align.png` shows them). The LR4
   high-pass/low-pass pair the T6 bounds come from (τ ≈ 2 ms, Δ ≈ 0, no
   inversion sentence; `main-live-align-lr4.png`) is reachable through the test
   hook `restartSyntheticForTest` only, not from a button.

SYNTHETIC cannot show reflections, drift or real driver band edges.

## Not built here

REW-style optimal-delay / maximise-sum search (forbidden, record §6). An
engine-free "align two stored traces" entry. Question (d)'s second half
("which output can be delayed") — the suggestion is always "delay the earlier
source". Persisting the wizard's output (schema 4, record §12); MTW vs fixed LF
fit choice. Measuring fit window / τ range / R bar on real pairs (QA §13.2);
the ρ threshold. XOVER `appliedDelaySamples` (Z2), multi-pane selector (Z14),
generator/output control beyond SWEEP's output parameter (Z16). VERIFY in
SYNTHETIC. `CaptureSequencer`. Bessel. IR persistence.
