---
name: positions-are-not-replicates
description: Inverse-variance weighting is optimal only when every input estimates the same quantity; across microphone positions the difference between inputs is the signal, so the "statistically optimal" weight lets one clean position own the average
metadata:
  type: project
---

**What happened (lane L6b, station 2, 2026-09-06).** The obvious weight for a
coherence-weighted spatial average is the inverse-variance weight from Bendat &
Piersol, `w ∝ n_d·γ²/(1−γ²)`. It is the textbook optimum, and the record nearly
adopted it. Pushed through the numbers, a position at `γ² = 0.99` outvotes one
at `0.9` eleven to one and one at `0.5` ninety-nine to one; the "spatial"
average becomes the cleanest microphone with company. It also needs a floor,
because `1/(1−γ²)` explodes as `γ² → 1`, and any floor would have been a
judgement dressed as statistics.

**Why.** Inverse-variance weighting minimises the variance of a weighted mean
**of replicate estimates of one quantity**. Positions in a room are not
replicates: the difference between them is the thing the average exists to
summarise. Under the additive-noise model `γ² = |H|²Sxx/(|H|²Sxx+Snn)` the
weight is also highest exactly on the constructive peaks of a comb, so the
optimum is biased toward peaks. The record shipped the bounded `W = u·γ²`
instead, which discounts a noisy position 3:1 rather than 99:1 and needs no
floor. Record `docs/dsp/2026-09-06-multichannel-l6b.md` §3.

**How to apply.** Before adopting an "optimal" estimator across N inputs, ask
whether the N inputs are replicates of one truth or samples of a population
whose spread is the answer. If the latter, the estimator's objective is wrong
before its formula is. Solvers (L7) that average across captures or positions
inherit this. Related: [[a-threshold-read-off-a-grid-is-that-grids-floor]]
(the floor the rejected weight would have needed),
[[a-default-must-be-run-through-the-gate-it-feeds]] (the table that decided it).
