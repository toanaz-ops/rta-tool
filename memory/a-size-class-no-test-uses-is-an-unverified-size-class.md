# A size class no test uses is an unverified size class

**2026-09-29, L7-EQ UI wave A (T3).** Found by deriving the expected value
independently while writing the FIR export's test, not by any existing test.
Nothing in `core/tests` was red.

`designFir` (`core/src/dsp/FirDesign.cpp`, `designLinearPhaseCore`) builds
`taps[0..half]`, `half = (N-1)/2`, and mirrors. For odd `N` that is exact. For
even `N` the zero-phase impulse's centre sample lands on **both** central taps,
so `h[0]` is counted twice (`FirDesign.cpp:129-139` writes `hZero[0]` to both
`taps[half]` and `taps[N-1-half]`). Measured by a direct DFT of the taps: a flat
0 dB target at `N = 4096` reads `|H(0)| = 2.00000` (both centre taps 1.0),
against `1.00001` for `N = 4095`; a +6 dB peaking filter reads `3.0195` (even)
versus `1.9945` (odd) for a target of `1.99526`. (2.04, in an earlier draft of
this note, was wrong for the flat target.) The FIR record (Sec.4) said "even `N` is accepted ... documented as a
half-sample group delay"; the *magnitude* was never checked at any even `N`.

The suite could not see it: every magnitude assertion in `test_fir_design.cpp`,
`test_fir_design_minphase.cpp` and the golden uses 1023, 511 or 255. The even-`N`
cases (1024, 100) check bitwise symmetry, metadata and phase linearity, all of
which the defect leaves intact. A function with a parity branch and a test suite
that only ever exercises one parity is two functions, one of them unproved.

## The check this costs

For any function whose implementation branches on a property of an integer input
(parity, power-of-two, `N < M`, `size == 0`), ask which values of that property the
**magnitude** tests use, not the structural ones. If one class of the property is
only ever seen by symmetry/metadata assertions, the other class's numbers are
unchecked.

The cheap independent measurement is to evaluate the result by a route that
shares no code with the design: here a direct DFT of the returned taps
(`sum tap[n] e^{-jwn}`), compared with the number the *plan* derives (`10^(6/20)`
at fc) and with 1 at DC. It took a dozen lines and found a 6 dB error the
existing suite had certified for months.

## What was done here

Not fixed in this lane (a half-sample design is a core change with goldens; a
follow-up owns it). `EqFirDesign` refuses an even tap count and the EQ pane
offers 1023 / 4095 / 6143 / 8191; FIR record Sec.4 carries the amendment. Delete
the refusal, restore the even list, and delete this file's "what was done" **and
the "until fixed" clause of its `memory/MEMORY.md` index line** once
`designLinearPhaseCore` has an even-`N` magnitude test that fails without the fix.
