# H8 review (Fable)

Date 2026-11-09 · model claude-fable-5-1 · commit 8fce235 (`6f1417f..8fce235`, PRs #96–#99) · method read-only (nothing built or run; "would fail" claims are by reasoning).

## Findings

### BLOCKER / HIGH
None.

### MEDIUM
- **M-1 — M1 prominence gate uses whole-signal rms.** `app/src/measure/SweepIrPipeline.cpp:47-61` computes `peakToRms` over all samples; `:140-141` requires `windowed.peakToRms >= 20`. A loud out-of-window echo (≈26 dB above the arrival, less for a band-limited sub pulse) defeats the gate and M1 silently falls back to global argmax → `ArrivalOutOfWindow` with no message. Fix: rms over the window (or pre-arrival region `[winLo, refPeak)`); test with a 30 dB-louder echo. Agent-safe: yes.
- **M-2 — `RTA_API_ENABLED=1` with a port that fails to bind is silent.** `app/src/api/ApiServer.cpp:367-375` sets `boundPort = -1`; `app/src/MainComponent.cpp:230-232` never reads `running()`/`refusal()`. Fix: one readout/log line when `enabled && !running()`. Agent-safe: yes.
- **M-3 — Enabled without `RTA_API_TOKEN` = unauthenticated local read access, undocumented.** `ApiPolicy.cpp:126-128`; `docs/HANDOFF.md` H8 step (2) and `MASTER-EXECUTION-PLAN.md` row 12 call the token "optional" without the consequence. Fix: one sentence in both docs. Agent-safe: yes.
- **M-4 — M1 can turn a geometry refusal into an accepted wrong arrival.** `SweepIrPipeline.cpp:132-145`: true arrival past `winHi` (>0.56 s) + an interior secondary peak (leak, early reflection, sidelobe) clearing 20× rms → adopted as `delaySamples`; fit check `:148-150` passes. Pre-H8 this was `ArrivalOutOfWindow`. Fix: require the windowed peak within ~12 dB of the global peak, or flag `arrivalAmbiguous`. Agent-safe: yes, but the threshold is an owner decision.

### LOW
- **L-1** N1 hint keys on level, not status: `MainComponentSweep.cpp:236-241` appends "raise GEN" to `ArrivalOutOfWindow` too; `-12.0` duplicates `shippedSweepConfig` (`SweepIrPipeline.h:33`); no negative test at ≥ −12.
- **L-2** Token compare not constant-time (`ApiPolicy.cpp:142`). Token never logged.
- **L-3** `std::tolower`/`std::isdigit` in `ApiSettingsFromEnv.h:30-36` vs the project's locale rule (`ApiPolicy.cpp:17-20`).
- **L-4** Silent fallbacks in env parsing (bad `RTA_API_PORT` keeps 4736; trailing space disables). Doc note or readout.
- **L-5** `MASTER-EXECUTION-PLAN.md:139` says "(a) and (b-strict) unchanged (12/19)"; (a) is 15/19 = 78.9 %.
- **L-6** H8 plan c1 promises "venue noise in pre-roll/tail" but the test (`test_sweep_ir_pipeline.cpp:358-379`) uses a clean echo; `2026-09-29-sweep-ir-lane-plan.md:264` line refs stale.
- **L-7** Torn role/tf pair for one plan rebuild (`AnalysisThread.cpp:142,331`; `RoutingMatrix.cpp:117-126`). Benign, note only.
- **L-8** Picker id 0 → `setActiveTransferFunction(-1)` keeps stale slot silently (`MainComponentRail.cpp:47-49`); clamp. "REF TF8" fit in ROLE cell unverified.

## Checked and clean
Audio callback reads only role snapshot; `activeTfIndex_` message-thread only; member order (picker before rail viewport) holds; bind address not env-derivable, loopback check unchanged, port 1..65535 enforced; `MainComponent.cpp` 376 lines (cap 400); new tests discriminate (by reading); M1 window range matches fit check; operator paths in the plan match the UI; flipped rows 2/11/12 accurate with "unmeasured" labels.

## Verdict
Mergeable as shipped. Before the hardware session: M-1 and M-2; M-3 docs; M-4 needs an owner threshold decision.
