---
name: a-shared-kernels-numeric-factor-is-not-transferable
description: a tolerance/oversampling factor proven adequate for one caller is invalid for another whose input conditioning differs — the min-phase 8x vs 64x case
metadata:
  type: project
---

`rta::dsp::minimumPhaseFromMagnitude` (Wave 0) does **no internal oversampling** —
it runs at the length it is handed, so the cepstral oversampling factor is the
**caller's** responsibility, not the kernel's.

FIR export (G10) ships **8×** and it is adequate — but *only because* FIR windows
its target first (`designLinearPhaseCore`), which caps notch sharpness well below
what would demand a large factor. L7-EQ's G24 excess-phase null test shares the same
kernel but feeds **raw measured magnitude** (sharp, un-windowed), so 8× time-aliases
the min-phase impulse. EQ measured the real requirement: **64× minimum, ships 128×**
(`kExcessPhaseOversamplingFactor`) — 16× FIR's number. A session that blindly reused
8× would have mis-classified dips on real measurements.

Two lessons:
1. **A numeric factor/tolerance is a property of (kernel + that caller's input
   conditioning), never of the kernel alone.** When a second caller adopts a shared
   kernel, it must re-derive the factor for its own inputs. Do not copy the first
   caller's number or its justification.
2. **The magnitude-identity residual `|H_min| == |H_lin|` is NOT evidence for the
   factor** — it is the algebraic tautology `Re(FFT(fold(c))) == FFT(c)`, flat at
   every factor. The real evidence is reconstructed min-phase **impulse-tail energy
   past nFft/2** (cepstral-aliasing) and/or reconstructed-impulse convergence. In the
   EQ case the tail-energy metric, not the excess-phase swing, is what drives 64×
   (swing alone converges at 8×–16×) — so even the swing convergence is not the
   proof. See [[the-parity-table-is-a-hypothesis]].

Verified 2026-09-07 (Wave 1 FIR verifier flagged the caveat; Wave 2 EQ build measured
and confirmed it). The FIR oversampling comment (`core/src/dsp/FirDesign.cpp`) was
corrected to say this; the EQ `gen_autoeq_algo.py` docstring precision fix (tail-energy
drives 64×, not swing) is still owed at EQ closeout.
