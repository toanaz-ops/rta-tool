---
name: a-default-must-be-run-through-the-gate-it-feeds
description: The L3 record chose "averaging in seconds, uniform across bands" for a good-sounding reason; the first time anyone pushed the default through the real effectiveAverages function, three bands could never open the coherence gate. Compute the downstream consequence of a default with the shipped function before the record ships it.
metadata:
  type: feedback
---

**What happened (2026-09-05, lane L3 MTW).** Record §5 specified the averaging
time constant in seconds, the same for every band, arguing "an operator turning
a knob means seconds". The station-3 planner ran the default (0.5 s, inherited
from the fixed engine) through the real `exponentialEffectiveAverages` -- which
carries a 75 %-overlap penalty D = 1.925 that the header's `(2-a)/a` ceiling
does not -- and found the 65536-, 32768- and 16384-point bands asymptote at
2.1, 3.6 and 6.6 effective averages against a coherence gate of 8. The feature
the record made the display default (MTW coherence) would never have appeared
below 750 Hz. The smallest uniform tau that clears the gate is 3.5 s, at which
the top band averages 682 looks. The record was reversed to frames-uniform.

**Why it matters.** A default is a claim about behaviour. The reasoning that
picked it was about the knob, not about the number the knob produces at the
other end of the chain (Neff -> gate -> what the operator sees). Checking the
number needed the *shipped* function, not the formula in the header comment:
the two differ by the overlap factor, and the header one says "fine" at 2.0 s.

**How to apply.** Before a record fixes a default that feeds a threshold, gate,
refusal or floor elsewhere in the code, push it through the real function and
put the resulting table in the record. If the function's header carries a
simplified formula, do not use it -- run the code path or reproduce it from
the `.cpp`. Related: [[a-threshold-read-off-a-grid-is-that-grids-floor]],
[[a-fixed-defect-returns-through-the-silent-fallback]].
