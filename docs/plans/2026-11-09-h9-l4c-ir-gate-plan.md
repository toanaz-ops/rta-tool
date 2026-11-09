# H9 -- L4c IR gate lane (master-plan row 5)

*2026-11-09. Owner lifted the pause and asked for the next phase without
per-step questions. Code read at `origin/main` `2321e51`. Decision record:
`docs/dsp/2026-11-09-l4c-ir-gate.md`. Everything here is **unmeasured** until
the hardware session (HW-175).*

## Scope

In: G25 IR gate (core + operator path), the owner-decided polarity refusal
sentences. Out: G22 offline WAV (own lane), G24 min/excess-phase display
(after the gate), L4d STI (purchase), MTW storage v4, `CaptureSequencer`.

## Tasks

| id | task | called from (production caller) | operator path | proof |
|---|---|---|---|---|
| A1 | `rta::ir::GateSpec`, `gateSpectrum()` in `core/include/rta/ir/IrGate.h` + `core/src/ir/IrGate.cpp`: 4-point Tukey gate relative to the arrival, lead-in rotation through `analyseSpectrum`, `fMinHz`, refusals | `app/src/measure/IrGateModel.cpp` (A2) | n/a (engine) | `core/tests/test_ir_gate.cpp`: closed-form direct+reflection fixtures (gate before the reflection -> flat to float tolerance; reflection under the fade -> `|1 + a w(d) e^{-jwd}|` with `w` from the half-Hann formula); phase referenced to the arrival; each refusal |
| A2 | `IrGateModel` (app/measure): IR from `IrStore` -> ETC points (dB envelope, ms after arrival) + `gateSpectrum` result + a `Trace` for the library | `IrPaneView` (A3) via `MainComponentIr` | choose an IR in the IR pane | `app/tests/test_ir_gate_model.cpp` |
| A3 | `IR` pane: `PaneView::Ir` (string `ir`), selector cell, `IrPaneView` (picker, ETC, two draggable markers, gated magnitude, readout `GATE a..b ms . valid above N Hz`, `STORE GATED`) | `PaneFactory.cpp`, `MainComponentPanes.cpp`; `MainComponentIr` wires `IrStore` + `TraceLibrary` | press SWEEP (or RT60 SWEEP) to produce an IR, press `IR`, drag the markers, press `STORE GATED`, see the trace in TRANSFER | `app/tests_juce/` view test (drag moves marker, readout, store adds a trace), pane round-trip test over every `PaneView` |
| B1 | Polarity refusal sentences with measured Hz (`BandTooLow`, `BandTooHigh`, `SweepBandInsufficient`) replacing enum names in the ALIGN readout | `AlignRunnerIr.cpp` / `AlignmentWizardSignals.cpp` | ALIGN pane, SWEEP MAIN / SWEEP SUB on a horn / sub / narrow sweep | text tests: each sentence carries the number from `PolarityResult` |

## Rules

- PR A = A1+A2+A3 (+ this plan and the record). PR B = B1.
- Every new `.cpp` in every source list, including `app/tests_juce/CMakeLists.txt`.
- Lane closes when `tools/orphan_check.py --base 2321e51` exits 0 and both PRs
  are merged with 6/6 CI.
- Row 5 becomes BUILT (partial: G22 and the G24 display deferred).
