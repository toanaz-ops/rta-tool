---
name: a-header-ceiling-is-not-the-reachable-average-count
description: exponentialEffectiveAverages never reaches the header-quoted (2-a)/a ceiling; the overlap-correlation penalty D divides it down, so quoting the ceiling overstates reachable averages by nearly 2x for Hann at 75% overlap.
metadata:
  type: reference
---

**What happened (lane L3 MTW, station 3, 2026-09-05).** The plan needed the
asymptotic effective-average count `exponentialEffectiveAverages` reaches at a
given `alpha`, to compare against the coherence gate of 8. Reading it off
`core/src/dsp/AverageCount.cpp`'s header-adjacent comment gives the textbook
ceiling `Neff_raw -> (2-a)/a` as frames go to infinity. That is not the value
the function returns: the real return is

```
Neff = 1 + (Neff_raw - 1) / D,   D = 1 + 2 * sum_{m>=1} c(m*hop)^2
```

where `c` is `overlapCorrelation()`, the same window-autocorrelation penalty
L2 already uses. For Hann at 75% overlap (`hop = N/4`) only three lags
survive and `D = 1.9246389` -- **identical in every FFT size**, because
`hop/N` is what decides it, not `N` itself.

**Why.** `(2-a)/a` is the ceiling of an idealised exponential average built
from *independent* frames. Real frames at 75% overlap are correlated with
their neighbours, and `D` is exactly the correction for that correlation.
The penalty applies to `Fifo` as much as to `Exponential` -- overlap
correlation does not care which averaging mode consumes it -- which is why
it shows up identically in `fifoEffectiveAverages` too.

**How to apply.** Never quote `(2-a)/a`, `1/a`, `2/a`, or any other
independent-frames formula as "the effective averages this engine reaches".
Compute or quote the divided value instead. Two reference points, Hann at
75% overlap, verified against the real literals in
`core/tests/test_mtw_layout.cpp:213,241,271`:

- `fifoEffectiveAverages(hann(N), N/4, 16) = 8.5866271` -- the FIFO ceiling
  at the shipping default depth 16, 7.3% over the coherence gate of 8.
- `exponentialEffectiveAverages` at `alpha = 1 - exp(-1/16) = 0.060586937`
  reaches **17.1123296** asymptotically -- not the header ceiling
  `(2-a)/a ~= 32.01`. Quoting the ceiling instead of the divided value
  overstates reachable averages by nearly 2x.

Related: [[a-default-must-be-run-through-the-gate-it-feeds]] -- the sibling
finding from the same session: a default computed straight from the
undivided header formula looked like it cleared the gate and did not.
