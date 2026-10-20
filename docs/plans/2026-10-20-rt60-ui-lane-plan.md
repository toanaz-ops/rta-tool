# RT60-UI lane (H6, `app/rt60-ui`) — stations 1–3 research and plan

**Status, 2026-10-21: BUILT, both waves, operator-reachable; lane closed.**
Wave A (T0–T8) PR #74 `17f3010`; wave B (T9–T12) PR #77 `7b3375f`; audit fix
(core decay truncation and clarity, `docs/dsp/2026-08-30-ir-decay-l4b.md` §11)
PR #76 `df3d444`; closeout PR `app/rt60-ui-closeout` (status rows, QA queue
"H6 RT60-UI", handoff, LOW batch). Row 4 of the master plan flipped after wave
A. The real-hardware try (human-try steps 1–6) is unmeasured and stays in the
QA queue as "later".

*2026-10-20. Read at `origin/main` `148b784` (H4 SWEEP and H5 ALIGN-UI wave A
merged, #71). H5 wave B (`app/align-ui-b`: `SweepIr::arrivalSamples`,
`IrStore::find`, SWEEP on a named output + `swept` callback) **must merge
before H6 builds**; every H6 task naming those symbols adapts to the names wave
B actually ships. Sixth and last hot lane of plan review #2; flips status row 4.
Every task names its production caller and operator path (CLAUDE.md rules 5,
6). Governing record: `docs/dsp/2026-08-30-ir-decay-l4b.md` (the "record").
Planner: Opus 5.5 (Plan agent, read-only); written down by the orchestrator.
Owner defaults adopted under plan review #2's "execute autonomously" and the
owner's 2026-10-19 "do the rest until done"; rows marked **(owner)** are
defaulted, built, and listed on the decision board so the owner can reverse them.*

## Station 1 — literature, other tools, six findings

**Provenance rule (binding, inherited).** The record: "No document produced by
this lane may say 'per ISO 3382-1'". `docs/HUMAN-QA-QUEUE.md` (standards
purchase): a standard not bought may be named, not quoted. ISO 3382-1:2009,
ISO 3382-2:2008 and ISO 18233:2006 are **named, not cited by clause**; the
owner deferred the purchase on 2026-08-30. Figures below come from open
literature and tools that restate the standards, attributed to their source.

**Literature.**
- Schroeder, JASA 37 (1965) 409–412: the backward integral of the squared IR
  equals the ensemble average of interrupted-noise decays. For
  h(t) = n(t)·e^{−t/τ} it is a straight line in dB, slope −60/T,
  T = 3·ln10·τ — the closed form behind every device-free bound below.
- Lundeby, Vigran, Bietz, Vorländer, Acustica 81 (1995) 344–355: iterative
  crossing search + a correction that reads the regression line at t_c (record §11a; it was one sample, `p²(t_c)·τ`, until the audit). Derived in core (`DecayLundeby.cpp`;
  record §3 lists every constant).
- Chu, JASA 63 (1978) 1444–1450: subtract mean noise power. Not adopted (decision 6).
- Jacobsen 1987 / Jacobsen & Rindel 1987: band-passed decay valid for
  B·T > 16; time-reversed filtering lowers it to ~4. Core measured the
  inflation itself and gates at **6** on the reported T30 (record §4c-bis/ter);
  B·T = 16 gates nothing (§4e).
- IEC 61260-1:2014: base-ten octave/third-octave edges (`OctaveBands.h`);
  magnitude only, nothing on phase/direction — core chose zero-phase by
  measurement (record §4d).
- Restated (not quoted) from the unbought standards: evaluation ranges EDT
  0/−10, T20 −5/−25, T30 −5/−35 dB (REW help, pyrato, python-acoustics;
  record §1a); lowest band 63 Hz octave / 50 Hz third (REW help); bottom of the
  evaluation range ≥ 10 dB above noise (Rational Acoustics RT60 article ⇒
  ≥ 35 dB for T20, ≥ 45 dB for T30).

**Other tools.**

| tool | noise / truncation | band filter direction | figures |
|---|---|---|---|
| python-acoustics `room.t60_impulse` (`master`) | none: `cumsum` over the whole record | forward (`zero_phase=False`, order 8) | regression × 2/3/6 |
| pyrato (pyfar) | truncation, Lundeby, Chu, Chu+Lundeby; `intersection_time_lundeby` | caller's choice (forward default) | `reverberation_time_linear_regression` T20…EDT, LDT |
| REW (help; closed) | manual IR truncation; Topt searches end point in 1 dB steps | forward / time-reversed / zero-phase; order 6 | EDT, T20, T30, Topt; octave/third |
| Smaart (Rational Acoustics) | manual Ln marker at the noise saddle; Lr2 ≥ 10 dB above noise | not stated | EDT, RT60 from 30 dB span (20 if limited) |
| Open Sound Meter | no RT60 readout found; not verified at a commit, no claim rests on it | — | — |

Agree: Schroeder + least-squares over the same spans. Split: noise handling and
filter direction. Core already decided both by measurement (Lundeby with
correction; zero-phase). H6 surfaces those decisions; it does not reopen them.

**F1 — a SWEEP IR cannot be handed to the decay code as stored.**
`SweepIr::samples` is the whole linear deconvolution. Past
`originIndex + tailSamples` the overlap with the inverse filter shrinks into a
sweep-length decaying, low-passed artefact with apparent RT60 ≈
3·T/log10(f2/f1) = **10.0 s** for the shipped sweep (L4a record, "noise tail").
Lundeby's first noise estimate is the last tenth of its input
(`DecayLundeby.cpp:76`) — uncropped it sits in the artefact and T reads long.
H6 crops at t = 0 + tail; `tailSamples` is a local in `analyseChecked`, so T0
adds the field.

**F2 — negative time holds the harmonic packets; zero-phase needs a lead-in.**
Order-k packet at −L·ln k; shipped sweep L = 10/ln 1000 = 1.4476 s, 2nd
harmonic at −1.003 s. Core refuses a curve with no lead-in (`NoLeadIn`; a
missing lead-in fabricates 38.7 dB, record §5a). Lead-in = min(200 ms,
½·L·ln 2): 9600 samples at 48 kHz (shipped sweep); 1 s test sweep
L = 0.1448 s → 0.0502 s = 2409 samples.

**F3 — the shipped sweep cannot answer the 63 Hz octave.** `validBandLowHz` =
20·2² = 80 Hz (two-octave fade-in, `Sweep.h:139-143`); the 63 Hz octave spans
44.7–89.1 Hz. Core does not know the sweep's band, so the app refuses with
"63 Hz: below this sweep's trusted band (80 Hz)". 125 Hz (89.1–177, base-ten grid) and 8 kHz
(upper edge 11.2 kHz < 19.7 kHz) are inside.

**F4 — core's T30 bar is lower than the restated 45 dB.** `fitOne` refuses T30
only when the Lundeby-corrected curve never reaches −35 dB (`Decay.cpp:43-48`),
defensible with compensation (record §2: +3.6 % at 55 dB SNR). An app-side
second gate would create two truths; the pane shows the decay range reached
(−`db.back()`, the exact quantity core gates on) beside every figure. Adopting
45/35 dB is decision 5 and would be a core `DecayConfig` change.

**F5 — EDT has no validity envelope; the ensemble has no caller.** EDT reads
+22 to +24 % inside the region the B·T gate admits; record §9: not to be
presented as equal in quality to T20/T30. `decayTimesAcross`
(`DecayEnsemble.h`) is the record's answer to EDT variance; no `app/src` caller.

**F6 — tail knob and T30 requirement are separate questions.** Tail does not
change the IR's noise level (set by sweep energy). Tail decides whether Lundeby
finds a noise region past the crossing + 10 dB margin, roughly
T·(INR + 10)/60 / 0.9. A too-short tail gives a **refusal** (`NoNoiseEstimate`
or `RangeTooSmall`), not a biased number — provided F1's crop is honoured. The
L4a bound (tail ≥ 1.5 × RT60, record §9) governs frequency response, not decay times.

## The design

1. Operator presses **RT60**, a seventh pane-selector button (`PaneView::Rt60`, `"rt60"`).
2. **IR picker**: every library trace in group `SWEEP` (and ALIGN's
   `SWEEP out<N>`), newest first, tagged "IR in memory" or "no IR: opened from
   file or dropped from memory (8 kept); sweep again". Picking a tagged one
   shows only that refusal.
3. **Producing an IR from the pane**: output combo (out 1…N, default out 1),
   **TAIL** combo (3 / 6 / 10 s, default 3 s), **SWEEP** button → H4's sweep on
   that output with that tail through wave B's named-output entry extended by a
   per-press tail. Rail SWEEP keeps 3 s. The new trace is auto-selected only for
   sweeps the RT60 pane requested.
4. **Analysis** (`Rt60Analysis`, JUCE-free): window = [arrival − leadIn,
   origin + tail) (F1, F2); per octave band 63–8000 Hz: refuse if outside
   [trustedLow, trustedHigh] (F3); else `bandFilterZeroPhase` →
   `energyDecayCurve(band, leadIn, fs, B)` → `decayTimes`; each band carries
   T30, T20, EDT (each with its refusal), range reached, B·T gated, crossing time.
5. **Table**, one row per band: `Hz | T30 | T20 | EDT* | range | note`. Header
   names the IR, the tail, the trusted band and the method ("Lundeby
   truncation, zero-phase octave filters"); EDT footnote "EDT: no validity check
   (record §9)"; a refusal fills the note cell with its reason and numbers.
6. **Advice line** when any band refused `NoNoiseEstimate`, or 1.5 × the longest
   reported T30/T20 exceeds the tail: e.g. "longest decay 2.40 s needs a tail
   ≥ 3.6 s: choose TAIL 6 s and sweep again". Never acted on automatically.
7. **Wave B**: multi-select up to 8 IR'd traces → `decayTimesAcross` median +
   IQR %, spread shown only at ≥ 3 captures (F5); Schroeder-curve plot for the
   selected band with fit spans; OCTAVE / THIRD toggle.

**Against the obvious approaches.**
- (i) A section in the SWEEP readout — one `juce::Label` on the rail; 8 bands ×
  4 figures + refusals do not fit; it would re-analyse every press and cannot
  pick an older IR. Rejected.
- (ii) Feed `SweepIr::samples` straight to `energyDecayCurve` — F1, F2. Rejected;
  T1's crop test is the guard.
- (iii) Auto-resweep with a longer tail — plays a 10 s sweep nobody pressed for,
  a show-time hazard; tail is refusal-vs-number (F6), so advice suffices.
- (iv) Chu or REW's Topt — Topt's curvature-type confidence was measured
  worthless (`DecayEnsemble.h`, |corr| ≤ 0.16).

## Owner-default decisions

| # | decision | default | why / what the alternative costs |
|---|---|---|---|
| 1 | where RT60 lives **(owner)** | 7th selector button `RT60`, model owned by `MainComponentRt60` (EQ/ALIGN pattern); a future L4c ETC/gate view could share the pane | SWEEP readout cannot hold a table; costs a 7th selector cell and ~5 lines of `MainComponent.h` (360) |
| 2 | tail knob | TAIL combo 3 / 6 / 10 s in the RT60 pane, default 3 s, per press; rail SWEEP unchanged; advice line | fixed 3 s refuses halls with no way out; free entry invites 0.2 s; auto-resweep rejected |
| 3 | band set **(owner)** | octave 63–8000 Hz in wave A (63 refused under the shipped sweep); third-octave behind a toggle in wave B | record §9 leaves thirds open; offering them in wave A doubles rows and shows routine B·T refusals before the octave table is proven |
| 4 | filtering | core's zero-phase, unchanged | measured (record §4b/§4d) |
| 5 | dynamic-range rule **(owner)** | no app gate; show "range X dB" per band; refusal quotes core's bar ("T30 needs the decay to reach 35 dB") | an app gate at 45/35 dB disagrees with core's tests; adopting it is a core `DecayConfig` change + record amendment after measuring |
| 6 | noise handling | Lundeby + correction (core); no Chu | core's measured choice |
| 7 | EDT **(owner)** | shown, footnoted "no validity check"; wave B adds spread at ≥ 3 captures | hiding it removes a figure REW and Smaart both show |
| 8 | units | seconds two decimals ("0.80 s"); Hz whole using IEC 61260-1 nominal labels (63, 125 … 8000); dB one decimal; B·T one decimal; spread whole %; tail one decimal | an RT difference becomes audible at ~5 % (literature): at 0.30 s that is 15 ms, so 0.01 s resolves it; a third decimal is far below the measured spread; one decimal is a 33 % step at 0.3 s |
| 9 | missing IR | listed with its reason, never hidden; result held only for the selected trace; RT60 results not persisted (pane choice round-trips as `"rt60"`) | hiding traces after OPEN reads as "the app lost my sweeps"; IR persistence out of scope |
| 10 | SYNTHETIC room **(owner)** | inert feedback comb in output 0's path (`roomCombDelaySamples`, `roomCombGain`; gain 0 is bit-identical), reachable only via `restartSyntheticForTest` | a default room changes every SYNTHETIC magnitude test H4/H5 settled; cost: in plain SYNTHETIC every band refuses (no room) |
| 11 | thread | message thread in wave A (one IR, 7 bands, ~154k samples, est. < 50 ms); T1 prints the time; wave B moves ensemble/thirds to a worker if > 100 ms | ALIGN precedent; `Rt60Analysis.h` carries no `rta/ir/*` |

## Sequencing and mutual exclusion

- Prerequisite: H5 wave B merged; H6 rebases on it and re-measures rows marked †.
- The RT60 pane never owns the output: its SWEEP goes through
  `MainComponentSweep`, already inside `captureBusy()`, so LOCATE/CAL/ALIGN/
  VERIFY refuse while it runs and it refuses while they run — no new guard.
- One sweep-result dispatcher: a press carries a requester tag
  (`Rail | Align | Rt60`); `swept(id, ch, requester)` auto-selects only for `Rt60`.
- Analysis runs once per selection change and is cached on `MainComponentRt60`
  (a pane rebuild keeps the table). If the trace leaves the library or its IR is
  evicted, the result stays shown with "IR no longer in memory" appended.

## Device-free acceptance (argued)

**Option A, chosen: SYNTHETIC + a feedback comb through the pane's real SWEEP
button.** y[n] = x[n] + g·y[n−D]; each trip loses 20·log10(1/g) dB, so
**T = 3·D/(fs·log10(1/g)) exactly, in every band** (Schroeder on a pulse
train). D = 480 @ 48 kHz, T = 0.80 s → g = 10^(−0.0375) = 0.91728 (0.75 dB per
trip, 40 steps across the T30 span). Measurement noise on (−30 dB) so Lundeby
has a floor. Proves button → named output → tail override → IR → crop → table.

**Option B, also built (JUCE-free):** synthetic `SweepIr` objects into
`Rt60Analysis` — exact control of trusted band, tail, artefact region and
per-band T; cannot prove the wiring.

**Bounds** — discrimination bounds, half the gap between truth and the named
mutant's answer (accuracy is core's job, `test_ir_decay.cpp` ±10 %):
- ×2 span multiplier (record's double-count) reads 1.60 s → |T30 − 0.80| < 0.40
  and |T20 − 0.80| < 0.40 in 125–8000 Hz.
- Label shift (B): IR = decay(1.20 s, 125 Hz octave) + decay(0.40 s, 2 kHz
  octave) → |T₁₂₅ − 1.20| < 0.40, |T₂ₖ − 0.40| < 0.40.
- Crop (F1), exact: `analysedSamples == leadIn + tailSamples`; filling past the
  tail with a +40 dB decaying ramp leaves every band bit-identical.
- Lead-in (F2), exact: 9600 (shipped sweep) and 2409 (1 s sweep) at 48 kHz.
- Trusted band (F3): 63 Hz refuses with "80 Hz" in the text.
- Advice (F6) at T = 0.80 s: tail 1.0 s → advice present (1.2 > 1.0 or a
  refusal); tail 2.0 s → absent (1.5·T < 1.8 < 2.0 by the bound above).

The builder prints T30, T20, range and B·T per band beside each bound. Do not
tighten the bounds.

## Station-3 task table

| task | files | called from | operator path | acceptance (mutant that must go RED) |
|---|---|---|---|---|
| **T0** tail on the IR | `measure/SweepIrOutcome.h` (`SweepIr::tailSamples`), `SweepIrPipeline.cpp` | `Rt60Analysis` (T1) | SWEEP → RT60 | `test_sweep_ir_pipeline.cpp`: `tailSamples == capture − (refArrival + sweepSamples)` exact. RED: field left 0 |
| **T1** analysis (JUCE-free; add to `measure_has_no_framework_deps`) | new `measure/Rt60Analysis.h/.cpp` (`Rt60Band`, `Rt60Result`, mirrored `Rt60Refusal` + `OutsideTrustedBand`, `NoIr`, `NoTail`), new `measure/Rt60Bands.h` | `MainComponentRt60::analyse` (T5) | pick an IR in RT60 | new `app/tests/test_rt60_analysis.cpp` + `Rt60Fixture.h`: all Option B checks (crop bit-identity, lead-in integers, 63 Hz refusal, label-shift bound, `NoIr`, `NoTail`); prints wall time. RED: no crop; lead-in 0; trusted-band check removed; bands reversed |
| **T2** readout text | new `measure/Rt60Readout.h/.cpp` | `Rt60PaneView` (T4) | RT60 table | `test_rt60_readout.cpp`: 0.8 → "0.80 s"; the 125.89 Hz octave's NOMINAL label → "125" (table rows use nominal labels; `rt60Hertz(125.89)` is plain rounding, "126 Hz", and is for band edges such as 80 Hz); RangeTooSmall at −28.4 → "decay reaches 28.4 dB; T30 needs 35 dB"; B·T 5.43 → "band too narrow for a decay this short (B·T 5.4 < 6)"; NoNoiseEstimate names the tail; advice per F6; every refusal has non-empty text. RED: one decimal; "126 Hz"; empty refusal text |
| **T3** pane vocabulary | `PaneRegistry.h`, `PaneSelectorDecision.h`, `PaneFactory.h/.cpp` (+`Rt60PaneBinding`), `MainComponentPanes.cpp` (7th cell), `MainComponent.h` | `selectPaneView`, `restoreWorkspaceFromSession` | RT60 button; OPEN of a saved `rt60` pane | round trip over seven enumerators. RED: delete the `"rt60"` arm |
| **T4** pane view | new `view/Rt60PaneView.h/.cpp` | `makePaneFactory` Rt60 branch | RT60 → pick → table; output/TAIL/SWEEP | `tests_juce/test_main_component_panes_rt60.cpp`: picker lists a trace without IR with the reason; TAIL default 3 s; switch away/back keeps the table. RED: hide IR-less traces |
| **T5** controller + sweep override | new `MainComponentRt60.h/.cpp`; `MainComponentSweep.h/.cpp` (per-press `tailSeconds` + requester on wave B's entry); `MainComponent.h/.cpp`; `MainComponentTestAccess.h`; both source lists | ctor; `swept` dispatcher | **RT60 pane SWEEP** | `tests_juce/test_main_component_rt60.cpp`, Option A through real buttons (1 s sweep): T30/T20 bounds 125–8000 Hz; 63 Hz refused; tail 1.0 → advice, 2.0 → none; rail SWEEP afterwards still 3 s; ALIGN-requested sweep not auto-selected; after `fillIrStoreForTest(8)` the earlier trace reads "no IR". RED: tail override ignored; auto-select every sweep; analysis on uncropped samples |
| **T6** SYNTHETIC room | `measure/SyntheticImpairment.h` (comb in `MeasurementPath`), `SyntheticInput.h/.cpp` | `SyntheticInput::runBody` | test hook only (decision 10) | gain 0 → bit-identical to today; impulse → pulses at 4+480k with amplitude g^k (closed form). RED: comb applied at gain 0; delay off by one |
| **T7** specimen | new `tools/snapshot_rt60.h/.cpp`, `tools/snapshot.cpp` (+2) | `rtatool_snapshot` | — | `main-live-rt60.png` through the real buttons with the comb; prints FAILED if no T30 |
| **T8** close wave A | `docs/plans/MASTER-EXECUTION-PLAN.md` row 4; `docs/dsp/2026-08-30-ir-decay-l4b.md` amendment (crop, lead-in, trusted band — after the code) | — | — | `tools/orphan_check.py --base <wave start>` exit 0; `energyDecayCurve`, `decayTimes`, `bandFilterZeroPhase` reachable from `rtatool`; row 4 → BUILT |
| **T9** ensemble (wave B) | `Rt60Analysis` (+`decayTimesAcross`), `Rt60PaneView` multi-select | pane | sweep 3×, select all | 3 comb IRs (different seeds): median within the bound, spread shown; 2 captures → value only. RED: spread shown at 2; same IR counted 3× |
| **T10** EDC plot (wave B) | `Rt60PaneView`, maybe new `view/Rt60Chart.cpp` | pane | click a band row | comb curve linear: residual of the −5/−35 fit < 1.5 dB (staircase step 0.75 dB). RED: plot of an unintegrated ETC |
| **T11** third-octave (wave B) | `Rt60Analysis`, `Rt60Bands.h`, pane toggle | pane | OCTAVE/THIRD | 50–10000 Hz thirds; 63 and 80 Hz refused under the shipped sweep; 40 Hz third at T = 0.4 s refuses on B·T. RED: toggle ignored |
| **T12** close wave B + human try | — | — | see below | orphan_check exit 0; a human sees it |

**Waves.** A = T0–T8 (flips row 4). B = T9–T12.

## File-length budget (cap 400; counts at `148b784`; † = H5 wave B also edits)

| file | now | after |
|---|---|---|
| `MainComponent.h` | 360 | ≤ 366 |
| `MainComponent.cpp` | 313 | +8 |
| `MainComponentPanes.cpp` | 167 | +10 |
| `MainComponentTestAccess.h` | 249 | +10 |
| `MainComponentSweep.h/.cpp` † | 116 / 197 | +6 / +10 on top of wave B |
| `measure/SweepIrOutcome.h` †, `SweepIrPipeline.cpp` | 95, 192 | +5, +1 |
| `measure/SyntheticImpairment.h` | 118 | +28 |
| `measure/SyntheticInput.h/.cpp` | 162 / 201 | +8 / +4 |
| `view/PaneRegistry.h`, `PaneSelectorDecision.h` | 113, 67 | +5, +2 |
| `PaneFactory.h/.cpp` | 43 / 40 | +3 / +6 |
| `tools/snapshot.cpp` | 344 | 346 |
| `app/tests/test_sweep_ir_pipeline.cpp` | 338 | +12 |
| `app/tests_juce/CMakeLists.txt` | 369 | +3 (else `main_component_tests.cmake`, 127) |
| `measure/AnalysisThread.h` | 398 | **0 — do not touch** |
| `measure/AlignRunner.cpp` | 392 | **0 — do not touch** |
| `core/include/rta/ir/Decay.h` | 347 | 0 (no core change in H6) |

New: `Rt60Analysis` ~80/190; `Rt60Bands.h` ~45; `Rt60Readout` ~50/130;
`Rt60PaneView` ~100/280; `MainComponentRt60` ~60/130; `snapshot_rt60` ~30/150;
`Rt60Fixture.h` ~60; each test file ≤ 350.

## Hot files

`MainComponent.h/.cpp`, `MainComponentPanes.cpp`, `MainComponentSweep.*`,
`MainComponentTestAccess.h`, `PaneFactory.*`, `PaneRegistry.h`,
`PaneSelectorDecision.h`, `measure/SweepIrOutcome.h`,
`measure/SyntheticImpairment.h`, `measure/SyntheticInput.*`, both
`app/cmake/*_sources.cmake`, `app/tests/CMakeLists.txt`,
`app/tests/cmake/impl_test_sources.cmake`, `app/tests_juce/CMakeLists.txt`,
`main_component_tests.cmake`, `tools/snapshot.cpp`,
`docs/plans/MASTER-EXECUTION-PLAN.md`.

## Human-try row

1. Real hardware: out 1 → loudspeaker in the room; REF loop on in 2; mic on
   in 1 at or beyond critical distance.
2. RT60: output out 1, TAIL 3 s, SWEEP. In ~15 s the table fills: 63 Hz refused
   (80 Hz trusted band), 125–8000 Hz with T30/T20/EDT* and a range.
3. Compare with REW at the same position: T30 within core's spread, a few % in
   mid bands; a larger gap at 125 Hz in a dry room is expected (record §6).
4. Move the mic 2 m: T30/T20 barely move; EDT may move a lot (the footnote's point).
5. In a hall (T > 2 s): TAIL 3 s → `NoNoiseEstimate` or the advice line; TAIL 6 s
   and a second press → numbers.
6. Pick a SWEEP trace after SAVE/OPEN: "no IR: … sweep again".
7. No hardware: SYNTHETIC → RT60 → SWEEP. Every band **refuses** (the synthetic
   path is a band-pass + 4-sample delay, no room): "band too narrow for a decay
   this short (B·T … < 6)" or "no noise floor found" — the right reading, not a
   fault. The comb room (T = 0.80 s) is reachable only via
   `restartSyntheticForTest`; `main-live-rt60.png` shows it.

**Added by wave B (what a human sees now; record section 12):**

8. Click a table row: the chart under the table draws that band's decay curve
   (dB down, seconds across) with the EDT, T20 and T30 fit spans over it and
   their figures in the legend. A straight fall means the T30 is one slope; a
   bend means it averages two. A refused band's chart says why it has no curve.
   With no click it shows the band nearest 1 kHz.
9. Sweep the same spot three times (SWEEP, SWEEP, SWEEP, each a new trace),
   then ENSEMBLE and tick all three. The table now shows the median across the
   captures and, from three, "IQR n%" beside each figure; with two ticked it
   shows the median alone and the header says "IQR needs 3 captures". More
   sweeps tighten it (1/sqrt(N), record `DecayEnsemble.h`); up to eight.
10. OCTAVE / THIRD button: the table becomes 24 third-octave rows, 50 Hz to
    10 kHz. Under the shipped sweep 50, 63 and 80 Hz are refused (80 Hz trusted
    band) and low thirds in a dry room may refuse on "band too narrow for a
    decay this short". The pane says "analysing in the background" for a
    moment (the worker thread); it never freezes.
11. In SYNTHETIC (no room) every band still refuses, as in step 7; the comb
    room's chart and thirds are in `main-live-rt60.png` and
    `main-live-rt60-third.png` (`rtatool_snapshot`).

Wave B deviation from the T11 row: the table starts at the 50 Hz third, so the
"40 Hz third at T = 0.4 s refuses on B*T" case is tested through
`analyseRt60Bands` with edges computed from `OctaveBands` (B*T 3.7), and the
50 Hz third (5.4) is checked in the table itself.

SYNTHETIC cannot show two-slope rooms, coupled spaces, noise spectra or drift.

## Not built here

C50/C80/D50 in the UI (no tolerance measured, record §9). Topt, curvature, Chu,
nonlinear (Xiang) fits. A 45/35 dB app gate. An EDT validity envelope. IR or
RT60 persistence. Third-octave in wave A. Spectrograph / decay waterfall (L5c).
ETC, gate, min-phase views (L4c). STI (L4d, blocked on IEC 60268-16). Changing
the sweep's start frequency to make 63 Hz answerable. Multi-slope detection.

Sources for the other-tools section: REW RT60 help; python-acoustics
`acoustics/room.py`, `acoustics/signal.py` at `master`; pyrato docs
(`_modules/pyrato/rap.html`, `roomacoustics.html`); Rational Acoustics,
"Reverberation Time: Spilling the 'T' on RT60". Open Sound Meter: nothing found.
