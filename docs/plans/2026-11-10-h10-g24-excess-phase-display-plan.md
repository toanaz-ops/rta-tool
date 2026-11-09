# H10 -- G24 min / excess-phase display on the gated response (master-plan row 5 gap)

*2026-11-09 (late). Follows H9 (`docs/plans/2026-11-09-h9-l4c-ir-gate-plan.md`).
Code read at `origin/main` `b756649`. Unmeasured until HW-175.*

## Why now, and why on the gated response

`core/include/rta/dsp/ExcessPhase.h` already ships `rta::dsp::excessPhase()` --
built in L7-EQ and declared "shared with L4c's display feature (EQ-R1)". No
`app/src` file includes it. An ungated room IR has no meaningful excess phase
(every reflection adds non-minimum-phase zeros); the H9 gated response is the
first input in the app for which the decomposition answers the operator's
question: "is this dip/phase turn the loudspeaker's own minimum-phase
behaviour (EQ can fix it) or excess phase (delay, crossover, reflection --
EQ cannot)?".

## Decisions (record: `docs/dsp/2026-11-09-l4c-ir-gate.md` §7, written with the code)

1. **Input:** the gated spectrum (16384-point, 8193 bins = 2^13 + 1, the
   kernel's size contract). `trusted` mask = bins with `fMin <= f <=
   trustedHighHz` of the source SWEEP. Phase convention unchanged (arrival-
   referenced, as `gateSpectrum` returns it).
2. **Cost:** `kExcessPhaseOversamplingFactor` = 128 -> a 2^21-point transform
   per call. It must NOT run per drag event. It runs when a gate is settled
   (mouse-up / a refusal cleared) and only while the phase view is on; the
   builder measures one call's wall time on this machine and, if it exceeds
   50 ms, moves it to a worker thread with a revision counter (the RT60 worker
   pattern), never onto the audio thread.
3. **Display:** the IR pane's lower plot gets a three-way selector
   `MAG | PHASE | EXCESS`: PHASE draws measured phase and minimum phase
   (unwrapped, degrees); EXCESS draws `excessPhaseRad` (degrees) and excess
   group delay (ms) over the trusted band only; the readout adds
   `tau0 X.XX ms` (broadband delay removed). `valid == false` -> the plot says
   "too few trusted bins for excess phase" (the kernel's own refusal), never
   an empty curve.
4. **No new persisted state**: the selector is not saved with a session.

## Tasks

| id | task | called from (production caller) | operator path | proof |
|---|---|---|---|---|
| C1 | `IrGateModel::excessPhaseFor(gated, fMin, trustedHigh)` -> `rta::dsp::excessPhase` + `minimumPhase` phase for display | `IrPaneView` (C2) via `MainComponentIr` | IR pane: settle a gate, choose PHASE or EXCESS | `app/tests/test_ir_gate_model.cpp`: closed-form fixtures -- a pure delay D -> excess phase ~ 0 after tau0 removal and tau0 == D/fs; a direct + reflection (a < 1) comb is MINIMUM phase (|a| < 1 zeros inside) -> excess ~ 0; H = 1 + a*e^{-jwd} with a > 1 (zeros outside the unit circle) -> the all-pass factor is e^{-jwd}(1 + e^{jwd}/a)/(1 + e^{-jwd}/a), so after tau0 removal the excess phase is the closed form 2*atan2(sin(wd)/a, 1 + cos(wd)/a) plus the residual of (d/fs - tau0)*w (derive and assert both, tolerance derived in the test); valid == false with an all-untrusted mask |
| C2 | `MAG / PHASE / EXCESS` selector + plots + tau0 readout in `IrPaneView` / `IrPlots`; computed on settle, not per drag | `IrPaneView` | as above | JUCE view test: selector switches plots, readout carries tau0, no recompute during a drag (counter), refusal text when invalid; snapshot `ir-view-excess.png` |

Every new `.cpp` in every source list (incl. `app/tests_juce/CMakeLists.txt`).
Row 5 stays BUILT (partial) -- G22 remains; the G24 half of its gap closes.
