# XOVER and the applied delay (QA queue Z2)

Owner authorised 2026-11-09 (card HW-176 = A). Written after the code it
describes: `VirtualTrace::fromTraceRestoringAppliedDelay`
(`app/src/trace/VirtualTrace.cpp`), called from
`CrossoverPaneView::refreshFromLibrary` for both traces.

## The defect

`CaptureMeta::appliedDelaySamples` was stored and never read by XOVER. A
TRANSFER trace stored after APPLY (D = N) and one stored before it (D = 0) sit
on different time axes, so their complex sum has a wrong relative delay.

## T0 - the sign, measured on the real engine

`core/tests/test_dualfft_applied_delay_sign.cpp` feeds a kN-periodic signal
(rectangular window, exact closed form) through `DualFftEngine` with lag L and
applied D. Result, 3 cases / 3066 assertions:

    H_stored[k] = e^{-j 2 pi k (L - D) / N} = H_true[k] * e^{+j 2 pi k D / N}

for D > 0, D < 0 and D = L. Positive D discards the first D measurement samples
(an advance), so the stored phase carries `e^{+j w D}`. This matches
`memory/dual-fft-conventions.md` item 2.

## What shipped

`fromTraceRestoringAppliedDelay` = `fromTrace`, then one `applyDelay` rotation
with tau = +D / that trace's own `sampleRate`
(`applyDelay` multiplies by `e^{-j 2 pi f tau}`, cancelling the stored factor).
Absolute per trace, once, at construction. D = 0 skips the rotation, so the
result is bit-identical to `fromTrace` (test asserts exact equality).

Plain `fromTrace` is unchanged and must stay so: ALIGN passes D_B - D_A to
`crossoverBandFit`, which owns that correction; a rotating `fromTrace` would
correct twice (comment at `AlignmentWizard.cpp`, test
"plain fromTrace does not rotate").

## Rejected options

- **Rotate inside `sumOf`.** Hits ALIGN and every other consumer of `sumOf`;
  needs the per-source D, which a `VirtualTrace` deliberately does not carry
  (no `CaptureMeta`). Cost: a metadata field on a type whose point is having none.
- **A `DelayOp` in the chain.** `CrossoverSurface::setPendingOps` calls
  `setChain`, which overwrites the chain, so the restoration would vanish the
  moment the operator proposes a delay. Cost: a silent regression on first use.
- **Relative delta on one side only (D_B - D_A).** Wrong when both D are
  non-zero and sample rates differ per trace; needs a choice of reference side.
  The absolute form needs no such choice and is the same arithmetic.

## ALIGN's sign (T4, NOT fixed here)

`core/src/dsp/CrossoverBandFit.cpp:79-95` pre-rotates hB by
`e^{+j 2 pi f (D_B - D_A)/fs}`, and `CrossoverFit.h:112` / the fixture assume a
capture with D "carries `e^{-j w D}`". T0 shows the stored factor is `e^{+j w D}`,
so the correction should be `e^{-j w (D_B - D_A)/fs}`: ALIGN's sign appears
reversed. Latent: APPLY is refused while ALIGN runs and the difference is
normally 0. Its tests use a hand-built fixture with the same assumed sign, so
they cannot catch it. To be scheduled as its own change.

## Verification

Mutations (each rebuilt from a deleted exe, restored, tree clean):
sign flip -> `test_virtual_trace_applied_delay.cpp:70` and
`test_main_component_panes_xover_applied_delay.cpp:89`; fixed 48 kHz instead of
the trace's own rate -> `...applied_delay.cpp:70`; never rotate ->
`...applied_delay.cpp:92` and pane `:89`; pane using plain `fromTrace` -> pane
`:89`.
