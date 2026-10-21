# H7 — Generator / output control lane: stations 1–3, research and plan

*2026-10-21. Code read at `origin/main` `ea539fc`. Planner: Opus 5.5 (Plan agent, read-only); owner decisions recorded the same day by the orchestrator session (section "Owner decisions" — it SUPERSEDES the planner's D1). Recommended by `docs/proposals/2026-10-21-fable-options.md` (§4, §5 week 1 item 2, §6 question 2). Closes master-plan gap 3 and QA-queue row Z16; flips status rows 1 and 13 from "BUILT (partial)" to BUILT. Every task names its production caller and its operator path (CLAUDE.md rules 5 and 6).*

## Owner decisions — 2026-10-21 (binding; supersede the planner defaults below)

| # | decision | owner answer |
|---|---|---|
| D1 | do measurements follow the generator's level and output? | **Both modes, "follow GEN" first.** A measurement-level setting with two modes: **FOLLOW GEN** (default) — LOCATE, VERIFY, SWEEP and ALIGN play at the generator's level (each in its own convention: noise RMS, sweep peak) and, where the measurement does not choose its output by design, on the generator's outputs; **OWN** — each measurement keeps its record-designed −12 dBFS and its own output (the planner's original D1). FOLLOW GEN is built first (wave B1), OWN right after in the same lane (wave B2). |
| D2 | level numbers | **Accepted:** default −30.0, floor −60.0, hard ceiling −12.0 dBFS, step 1.0 dB. The ceiling also bounds FOLLOW GEN measurements: a measurement is never louder than today. |
| D3 | persistence | **None.** Every launch: pink, −30.0 dBFS, out 1, stopped, measurement mode FOLLOW GEN. |
| — | sequential-PR rule | **Narrowed** to `MainComponent.h/.cpp` (+ shared lists), recorded in `docs/GIT-WORKFLOW.md`. |

Consequences of D1 the lane must honour:
- Lowering the level in FOLLOW GEN lowers every measurement's SNR (finding F2). Each measurement readout prints the level and output it actually played (e.g. `SWEEP @ -30.0 dBFS peak, out 1`), and the stored trace metadata carries it, so a quiet capture is visibly quiet, not mysteriously refused.
- Outputs: LOCATE, VERIFY and the rail SWEEP (today out 1) follow the generator's selected outputs in FOLLOW GEN. ALIGN (solo main/sub) and RT60 (pane-chosen output) keep choosing their output by design in both modes; only their level follows.
- Existing H3–H6 tests were settled at −12 dBFS. They must pin their mode explicitly (OWN, or FOLLOW GEN with GEN at −12) so they keep testing what they tested; new tests cover both modes. Never loosen a settled tolerance to make FOLLOW GEN at −30 pass — if a measurement cannot work at the operator's level, it refuses with a readout naming the level.
- Mutual exclusion is unchanged: measurements still refuse while GEN is *playing*; following its *settings* does not require it to play.

## Station 1: what exists, what other tools do, findings

### What the repo already has

- **Generators in core** (`core/include/rta/gen/`): `Oscillator` (sine, peak dBFS), `DualSine`, `WhiteNoise` and `PinkNoise` (RMS dBFS; Kellett 7-term; `kDefaultNoiseLevelDbFsRms` = −12 is the cap that keeps pink's ~12 dB crest out of clipping, `Noise.h:12-16`), `Sweep` (Farina ESS, peak dBFS, plays once: `Sweep.cpp:180`), `Mls`, `RampedGain` (10 ms raised cosine).
- **Output engine** (`platform/types/.../OutputEngine.h`, record `docs/dsp/2026-09-06-l7-output-path.md`): one source slot (`SourceVariant`), master `RampedGain`, 64 per-output gates; `setSource` only while quiescent; `armSource`/`disarmSource`; `routeOutput(ch, on)`; `prepare()` bumps `outputEpoch`, disarms, renders silence for a stale slot. **No level control of its own** — level is baked into the source; record §4 "parameter changes are swaps"; §12 leaves "UI for the generator panel" and "headroom/clipping policy" to `app/`.
- **Who drives the output today:**

  | consumer | where | source | output |
  |---|---|---|---|
  | LOCATE | `MainComponentDelay.cpp:33-36` | pink −12 dBFS RMS | strict solo, out 1 |
  | VERIFY | `EqVerify.cpp:143-149` | pink −12 dBFS RMS | out 1 |
  | SWEEP | `MainComponentSweep.cpp:106-110`, `SweepIrPipeline.h:31` | sweep −12 dBFS peak | out 1, or a named output for ALIGN/RT60 |
  | ALIGN | `AlignRunnerState.h:91`, `AlignRunner.h:46` | pink −12 dBFS | solo main/sub |
  | RT60 | through SWEEP | sweep | pane-chosen output |

- **Mutual exclusion** (`MainComponent.h:92-96`, PR #69): `captureBusy()` = LOCATE settle/armed, CAL armed, `sweep_.busy()`, `align_.busy()`; `outputBusy()` = `captureBusy() || eq_.verifyBusy()`. SWEEP host, ALIGN env, LOCATE, CAL read `outputBusy()`; VERIFY env reads `captureBusy()`. LOCATE **ignores `setSource`'s return** (`MainComponentDelay.cpp:34`).
- **SYNTHETIC since H5:** renders the engine for 2 outputs (out 1 = 4-sample delay + 40 Hz–16 kHz band-pass; out 2 = LR4 100 Hz + 100 samples). Measurement noise fixed at **−50 dBFS RMS** (`SyntheticInput.cpp:113-114`). Entering SYNTHETIC calls `engine.prepare()`; leaving it does not.
- **AudioIo** requests 64 outputs (`AudioIo.cpp:28`); real count is `currentState().numOutputChannels`.
- **No app preferences store exists** (no `PropertiesFile`/`ApplicationProperties` in `app/src`).
- **`MainComponent.h` is 366 lines, 191 of them `//` comments**, mostly per-method rationale for functions defined in sibling `.cpp` files.

### Other tools (web read 2026-10-21; ARTA not read — closed source)

| tool | signals | level | ceiling / protection | output choice | start ramp | persisted | measurement vs generator |
|---|---|---|---|---|---|---|---|
| REW | tones, multitone, pink/white, sweeps | RMS dBFS, 0.01 dB | preview warning above FS; stops on SPL limit / >30 % input clipping | either/both channels | sweeps only | not read | **separate**: Measure dialog level, default −12, max −3 dBFS |
| Smaart v9 | noise, pink sweep, sine, file, SMPTE | master + per-output trim; 1.0/0.5 dB steps | not found | mono / multi-mono / stereo | not found | not found | live TF **uses** the running generator |
| Open Sound Meter | pink, white, sine, sweep… | gain, default −6, no clamp | none | multi-channel set | none | gain/type/channels yes, enabled no | live TF uses the generator |

### Findings

- **F1.** Level lives in the source and swaps only while quiescent, so an app-only design steps the level by swap: hole ≤ `2·ramp + one block + one poll` ≈ **41 ms** at 48 kHz / 512 / 10 ms, click-free.
- **F2.** Measurement levels are designed and tested; SYNTHETIC noise is fixed −50 dBFS RMS, so lowering drive lowers every settled SNR (now material because D1 = FOLLOW GEN by default).
- **F3.** Live TRANSFER on hardware has no excitation today; the generator enables the pink-on-a-box + loopback REF workflow — the lane's main value.
- **F4.** A device restart already silences the engine (`prepare()` + epoch). The UI must only **not re-arm**.
- **F5.** A stopped LIVE device never renders, so `sourceIsQuiescent()` never becomes true: stopping needs a timeout; starting refuses with no device running.
- **F6.** Sweep and MLS are not continuous here; GEN offers pink/white/sine.
- **F7.** LOCATE ignores `setSource`'s result: exclusion must be "generator counts in `outputBusy()`"; measurements refuse rather than pre-empt.

## Candidate designs, argued

- **A. App-only output client, level steps by swap** — JUCE-free `GeneratorModel` + `GeneratorRunner` over the existing engine API, JUCE controller on the rail. No core/platform/callback change; ceiling enforced in `makeSource`. Cost: ≤ ~41 ms dip per step, no slow fade-in.
- **B. Master trim stage in `OutputEngine`** (the obvious one) — gapless live level, but changes the hard-real-time path every measurement relies on, the trim is global and leaks into measurements (or every client must reset it), and its only gain is a gapless drag that Smaart itself does not offer (it steps).
- **C. One drive level for everything** — originally rejected; the owner's D1 adopts it as the FOLLOW GEN *mode* alongside OWN, implemented in `app/` by passing the generator's level/outputs into each measurement's source construction (not by an engine trim), so design A's no-RT-change property holds.

**Chosen: A** for the generator, plus the D1 measurement-level mode built on the same `GeneratorModel` (`makeSource`-style constructors with the shared clamp). B stays the escalation if hardware finds the step dip unacceptable.

Placement: **rail rows** (not a pane) so STOP is visible from every pane.

## The design

1. **Rail rows under SWEEP.** Row 1: `GEN` toggle (reads `STOP` while playing), signal `PINK / WHITE / SINE`, frequency (SINE only; 31 ISO R10 nominals 20–20000 Hz, default 1000, whole Hz). Row 2: `−` | level readout | `+` | `OUT ▾` | `MEAS: FOLLOW GEN / OWN` (wave B). Readout e.g. `GEN: playing pink -30.0 dBFS RMS on out 1`; dB one decimal; RMS for noise, peak for sine.
2. **Level** per D2; `makeSource` clamps (0 dBFS request builds −12). A step while playing is a swap (F1).
3. **Start/stop.** Start refused while another client holds the output, or in LIVE with no device running; then `setSource`, route selected outputs (Additive), `armSource`. Stop: `disarmSource`, Stopping until quiescent, 500 ms timeout (F5).
4. **Outputs.** `OUT ▾` popup, one tick per device output; Additive (record §13.2); live `routeOutput` while playing; last tick cannot be removed; prune on output-count change (empty → out 1).
5. **Never re-arms by itself** after epoch change, input-mode change (explicit cancel — leaving SYNTHETIC bumps no epoch), or device stop.
6. **Nothing persisted** (D3).
7. **Mutual exclusion.** `generator_.active()` joins `outputBusy()` and VERIFY's env, not `captureBusy()`. GEN start reads `captureBusy() || eq_.verifyBusy()`. Measurements refuse naming GEN (`sweep: refused -- GEN is playing; press STOP`).
8. **Measurement level mode (D1).** FOLLOW GEN (default): each measurement builds its source at `generator.settings().levelDb` in its own convention and, for LOCATE/VERIFY/rail SWEEP, routes the generator's selected outputs; ALIGN and RT60 keep their output choice. OWN: today's −12 dBFS and own outputs. Every measurement readout and stored metadata names the level and output used.

## Header split (task 0)

Moves (a) per-method `///` blocks from `MainComponent.h` verbatim into the defining `.cpp` (one-line pointer left), and (b) the seven pane-selector buttons with wiring/layout into `MainComponentPaneSelector` (owned member; buttons still children of `MainComponent`). `MainComponent.h` 366 → ~215–225. With the narrowed sequential rule, later pane lanes that only add sibling files no longer serialise.

## Waves

- **A1** (`app/main-header-split`): T0 alone, pure refactor.
- **A2** (`app/generator-a`): T1–T5 — model, runner, controller with the minimum safe operator path (GEN, −/+, readout, pink on out 1) and all safety/exclusion.
- **B1** (`app/generator-b`): T6–T9 + **T13** (FOLLOW GEN mode, default) — signal/frequency, outputs, measurement level following, hints, specimen.
- **B2** (`app/generator-own-mode`): **T14** (OWN mode + the `MEAS` toggle), tests in both modes.
- **C** (`app/generator-closeout`, docs): T11–T12 record amendments after code, status rows, QA queue, handoff.

No `core/` or `platform/` changes in any wave.

## Station-3 task table

| id | task | files | called from | operator path | acceptance | mutant that must go RED |
|---|---|---|---|---|---|---|
| **T0** | Header split, no behaviour change | `MainComponent.h`; `MainComponentCalibration/Spl/Delay/Store.cpp`, `MainComponent.cpp` (comments in, verbatim); new `MainComponentPaneSelector.h/.cpp`; `MainComponentPanes.cpp`; both `app/cmake/*sources.cmake` | `MainComponent` ctor (`paneSelector_.attach`), `resized` | every existing button, unchanged | `MainComponent.h` ≤ 230 lines; `//`-line multiset differs only by pointer lines; snapshot PNGs byte-identical to base; ctest OFF/ON tallies equal base; orphan_check 0 | swap two buttons' `onClick` in `MainComponentPaneSelector.cpp` → `test_main_component_panes*` RED |
| **T1** | `GeneratorModel` (JUCE-free; in `measure_has_no_framework_deps`) | new `measure/GeneratorModel.h/.cpp`: `GeneratorSignal`, `GeneratorSettings`, level constants, `clampLevelDb`, `stepLevelDb`, `makeSource(settings, fs, seed)` (clamps inside), `kSineFrequenciesHz` (31), `pruneOutputs`, `levelText`, `frequencyText` | `GeneratorRunner` (T2), measurement source builders (T13) | GEN (T3) | sine 1000 Hz −30 at 48 kHz: max \|x\| over 48 samples = 10^(−30/20) ± 1e-6; 0 dBFS request → peak 10^(−12/20); pink/white −30 RMS over 2^18 within ±0.2 dB; clamp/step; texts; 31 freqs; prune | clamp moved out of `makeSource`; RMS/peak text swapped; floor −90 |
| **T2** | `GeneratorRunner` state machine Idle/Playing/Swapping/Stopping | new `measure/GeneratorRunner.h/.cpp` | `MainComponentGenerator` (T3), `timerCallback` (T4) | GEN, −/+ | real `OutputEngine`: start ramp follows 0.5(1−cos πn/480); ±1 dB step hole ≤ 960 + block then steady at new level; `prepare()` → Idle with reason, never re-arms; refusals (busy, no device); stop timeout 500 ms; outputs change without swap; last output untick refused; above-ceiling request plays −12 | auto re-arm after epoch; no stop timeout; `soloOutput` instead of `routeOutput`; swap skipped |
| **T3** | `MainComponentGenerator` controller (JUCE) + 100 Hz timer while `needsFastPoll()` | new `MainComponentGenerator.h/.cpp` | ctor → `wireGenerator()`; `resized` | **GEN, −, +** | SYNTHETIC: GEN arms engine, routes out 1, readout "playing pink -30.0 dBFS RMS on out 1"; +×20 → "-12.0 … (ceiling)"; −×40 → "-60.0" | fast timer never started |
| **T4** | Wiring into the composition root | `MainComponent.h/.cpp`, `MainComponentLayout.cpp`, `MainComponentSynthetic.cpp` (cancel on mode change), `MainComponentEqVerify.cpp`, `MainComponentDelay/Calibration/Sweep.cpp` (refusal text), `MainComponentTestAccess.h`, cmake lists, `main_component_tests.cmake` (not `tests_juce/CMakeLists.txt`, 395 lines) | ctor, `timerCallback`, `setSyntheticMode`, busy guards | GEN; SWEEP/LOCATE/CAL/ALIGN while GEN plays | GEN playing → each measurement refuses naming GEN; SWEEP running → GEN refused; leave SYNTHETIC → Idle with reason, stays Idle on re-enter; STOP then SWEEP arms; `captureBusy()` unchanged by GEN | `generator_.active()` out of `outputBusy()`; cancel dropped from `setSyntheticMode`; generator added to `captureBusy()` |
| **T5** | Close wave A2 | `docs/HANDOFF.md` short entry | — | — | orphan_check `--base <A2 start>` 0; tallies in PR | — |
| **T6** | Signal and frequency choice | `MainComponentGenerator.*` | `attach` | PINK/WHITE/SINE, frequency | SINE 1000 Hz: snapshot peak within one bin of 1000 Hz; −×6 lowers it 6.0 ± 0.1 dB; signal change = one swap | frequency ignored; signal change without swap |
| **T7** | Output selection | `MainComponentGenerator.cpp`, `GeneratorRunner` (prune) | `attach`; `poll` | OUT ▾ | SYNTHETIC: out 2 only → TRANSFER at 1 kHz ≥ 20 dB below 50 Hz (LR4 100 Hz); out 1 only → within 3 dB; list has 2 entries | `soloOutput` instead of Additive; prune skipped |
| **T13** | **FOLLOW GEN measurement level (D1, default)** | measurement source builders: `MainComponentDelay.cpp` (LOCATE), `EqVerify.cpp`/`MainComponentEqVerify.cpp` (VERIFY), `MainComponentSweep.cpp`/`SweepIrPipeline.h` (SWEEP), `AlignRunner*`-adjacent env (ALIGN — avoid `AlignRunner.cpp` at 392 lines, pass the level through the env/state), trace metadata | each measurement's start path | LOCATE, VERIFY, SWEEP, ALIGN, RT60 presses | in SYNTHETIC with GEN at −30 (not playing): LOCATE/VERIFY/ALIGN sources at −30 dBFS RMS, SWEEP at −30 dBFS peak (closed form from `makeSource` conventions); LOCATE/VERIFY/rail SWEEP route GEN's outputs; ALIGN/RT60 keep their outputs; every readout and stored metadata names level + output; measurements still refuse while GEN plays; existing H3–H6 tests pin their mode explicitly and stay green with tolerances unchanged | a measurement ignores the GEN level (stays −12) → RED; readout omits the level → RED; LOCATE routes out 1 regardless of GEN outputs → RED |
| **T8** | Measurement hints show their level | `MainComponent.cpp`, `MainComponentSweep.cpp`, `AlignRunnerText.cpp` | ctor | hover | hint names the level actually used in the current mode | — (text) |
| **T9** | Specimen | new `tools/snapshot_generator.h/.cpp`; `tools/snapshot.cpp` (+2); snapshot sources | `rtatool_snapshot` | — | `main-live-gen.png`: SYNTHETIC, GEN SINE 1000 Hz −30.0 out 1 via real buttons; FAILED if not "playing" | — |
| **T14** | **OWN mode + `MEAS: FOLLOW GEN / OWN` toggle (wave B2)** | `MainComponentGenerator.*`, the T13 builders | `attach`; measurement start paths | MEAS toggle | OWN: every measurement back to −12 dBFS and its own output regardless of GEN; toggling while a measurement runs is refused or applies to the next press (decide, test); default each launch = FOLLOW GEN (D3); tests run representative H3–H6 measurements in both modes | OWN ignored (still follows GEN) → RED; default OWN at launch → RED |
| **T10** | Close waves B1/B2, human try | — | — | see below | orphan_check per wave; tallies | — |
| **T11** | Record amendments after code (rule 6) | `docs/dsp/2026-09-06-l7-output-path.md` §14 "H7: the operator generator as an output client"; `docs/dsp/2026-08-27-generator.md` | — | — | verifier checks each sentence vs code: §12 UI + headroom policy resolved (ceiling −12 in `makeSource`), measured step hole, §13.3 drop-never-resume as shipped, B as escalation, D1 two modes with FOLLOW GEN default | — |
| **T12** | Status and queue | `MASTER-EXECUTION-PLAN.md` rows 1, 13 → BUILT; `HUMAN-QA-QUEUE.md` Z16 closed + H7 try block; `HANDOFF.md` | — | — | (b-strict) 10/19 → **12/19 = 63.2 %**; (b) 14/19; (a) 15/19 — arithmetic written out | — |

## File-length budget (cap 400; at `ea539fc`)

`MainComponent.h` 366 → ≤ 230 (T0) → ≤ 240 (T4, T14); `MainComponent.cpp` 332 → ≤ 350; `MainComponentCalibration.cpp` 324 → ≤ 358 (watch); `MainComponentTestAccess.h` 292 → ≤ 310. **Do not touch:** `app/tests_juce/CMakeLists.txt` (395), `measure/AnalysisThread.h` (399), `measure/AlignRunner.cpp` (392), anything in `platform/` or `core/`. New files each ≤ 250; test files ≤ 350.

## Hot files and ordering

With the narrowed rule, only PRs touching `MainComponent.h/.cpp` or the shared lists (`app/cmake/*.cmake`, `main_component_tests.cmake`, `tools/snapshot.cpp`) serialise. Every H7 wave touches `MainComponent.h/.cpp` or the cmake lists, so H7's own PRs stay in order: A1 → A2 → B1 → B2 → C.

## Human try

**SYNTHETIC (no hardware):**
1. SYNTHETIC on: rail shows `GEN: stopped -- pink -30.0 dBFS RMS, out 1`, `MEAS: FOLLOW GEN`.
2. GEN: button reads STOP, readout "playing pink -30.0 dBFS RMS on out 1"; RTA shows the generator.
3. `+` stops at −12.0 "(ceiling)"; `−` stops at −60.0; each step a brief dip, never above −12.
4. SINE 1000 Hz: single peak at 1 kHz; six `−` lower it 6 dB.
5. TRANSFER, `OUT ▾`: out 2 only → 100 Hz low-pass with the larger delay; both → the sum.
6. PINK −60 on TRANSFER: coherence falls (S/N ≈ −10 dB vs SYNTHETIC's −50 dBFS noise, γ² ≈ 0.09) — correct, not a fault.
7. While GEN plays: SWEEP, LOCATE, CAL START, ALIGN MEASURE each refuse naming GEN. STOP, then SWEEP works.
8. During SWEEP, GEN is refused naming SWEEP.
9. GEN playing, SYNTHETIC off → GEN stops with a reason; on again → stays stopped.
10. STOP, GEN at −30, press SWEEP (FOLLOW GEN): readout `SWEEP @ -30.0 dBFS peak, out 1`; MEAS → OWN, SWEEP again: `@ -12.0`.
11. Relaunch: pink −30.0, out 1, stopped, FOLLOW GEN — nothing remembered.

**Needs hardware (owner):** level tracking on a loopback REF (one `+` = 1.0 dB), no click on start/stop/step and an acceptable step dip at a PA (else design B lane), live TRANSFER with GEN pink, device unplug while playing (silence, reason, no resume), multi-output interface listing (record §13.1), LIVE with device stopped refuses, sine 20 Hz / 20 kHz at −12 as a sanity bound for D2, and FOLLOW GEN measurements at a realistic operator level vs OWN. Depends on rebuilding `build-asio/`; joins the single hardware QA sheet.

## Not built here

Live gapless level (design B); looping pink sweep, MLS, dual-sine, file playback, SMPTE; decorrelated per-output noise; per-output trim; SPL-limit / clipping auto-stop; persistence; generator control through the remote API; a keyboard panic key.

Sources: [REW Signal Generator](https://www.roomeqwizard.com/help/help_en-GB/html/siggen.html), [REW Making Measurements](https://www.roomeqwizard.com/help/help_en-GB/html/makingmeasurements.html), [Smaart multi-output generator](https://rationalacoustics.com/blogs/smaart-v9-info-blog/multi-output-signal-generator), [Smaart generator fly-up](https://support.rationalacoustics.com/support/solutions/articles/150000187340-introducing-the-new-signal-generator-fly-up-menu), [Smaart 9.2 release overview](https://downloads.rationalacoustics.com/documentation/smaart-v9/ReleaseOverview/Smaart9.2ReleaseOverview.pdf), OSM [generator.h](https://raw.githubusercontent.com/psmokotnin/osm/master/src/generator/generator.h) / [generator.cpp](https://raw.githubusercontent.com/psmokotnin/osm/master/src/generator/generator.cpp) / [generatorthread.cpp](https://raw.githubusercontent.com/psmokotnin/osm/master/src/generator/generatorthread.cpp).
