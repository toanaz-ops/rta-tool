# Two builds disagreeing is not evidence that one is wrong

Lane L4b, 2026-08-30. The building and reviewing sessions measured the same
quantity on the same fixture and got **+38.3 %** and **+3.0 %**. Four exchanges
went into finding the cause. Ruled out by measurement, one at a time: the truth
convention (both used the unfiltered envelope and agreed to four digits),
truncation method, noise placement, lead-in length, filter order.

None of them. The cause was the **ensemble size**. Same cell, same code, same
construction, changing only the seed family:

    +38.3   +25.6   -8.7   +2.7   +35.7   +13.9   +3.9   +3.0   +22.9   -1.3 %

A median of 24 realisations wanders across **47 percentage points** here. Neither
session had measured a bias. Both had drawn once from that distribution. At
n = 400 the two agreed within one standard error.

**Before hunting a mechanism for a disagreement, ask what the standard error of
the summary statistic is.** Two independent builds are supposed to disagree by
their own noise; only a gap larger than that noise is a finding.

The part worth being uncomfortable about: **both sessions printed the IQR beside
every median for four rounds and neither read it.** The data declared its own
noise the whole time. Worse, this lane's own probe carries the line "one seed is
not a measurement" in its docstring — written by the session that then treated a
median of 24 as one. The same error, one level up.

A statistic whose property is an **extremum** — "the worst case stays below X" —
is the least protected of all by a small sample, because a maximum can only grow
as more of the tail is sampled. This lane's shipped `B*T < 6` gate is exactly
that shape: re-checked at n = 100 its worst case rose from 5.58 to 5.71, still
under 6.0 but with the margin moving the wrong way. Any re-derivation of such a
constant starts at n >= 100. See [[a-threshold-read-off-a-grid-is-that-grids-floor]].
