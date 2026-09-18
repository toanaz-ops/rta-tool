# A config field that has one value in every fixture is an untested field

**2026-09-18, lane L6a Wave 0 (W0-B / W0-D).** Found while writing the
handoff, not by any test. Nothing was red.

`SplConfig::metrics` is a vector of `SplMetricSpec`, and each spec carries its
own `weighting`. `SplMeter` runs **one** weighting per instance — by design,
stated in its own class comment. `SplSession` built **one A-weighted meter per
channel** and read every metric's window from it. So a configuration naming
`LCeq` would have published A-weighted numbers under a C-weighted label, and
`weighting` was a field the code read and threw away.

Every fixture in the lane had exactly one weighting in `metrics`. Seven
`TEST_CASE`s over `SplMeter`, eight over the publish path, five over the
session, five over the live drain — and not one of them could have failed,
because with one value in the list the wrong lookup and the right lookup
return the same object.

## The check this costs

For any field a config repeats — a vector of specs, a map, an array of
presets — ask: **does a fixture exist in which this field takes two different
values at once?** If not, the field is untested no matter how many cases read
it, because the bug is precisely "the implementation instantiates one of them".
A second distinct value is the whole test; it does not need to be a realistic
configuration, only a different one.

The same question in a form that is quicker to ask: **count the instances the
implementation constructs, and compare it with the cardinality the config can
express.** Here it was one meter against three weightings. A cardinality
mismatch is the defect; a fixture with one value hides it by construction.

## What it is NOT

This is not "test more combinations". A fixture with two *window lengths* would
have proved nothing — two metrics differing only in `windowBlocks` legitimately
share one chain, because `combineBlocks` is a recompute over whatever tail it is
handed. The field that matters is the one that decides **which object** serves
the request, not the one that decides what is asked of it. Find that field
first; it is usually the one an object is *constructed* with rather than the one
a method takes.

## The fix, for shape

`SplSession` now builds one chain per **distinct** weighting its metrics name,
in first-seen order (at most three — A, C and Z is the whole enum), and metrics
sharing a weighting share a chain. `window(channel, weighting)` returns an
**empty span** for a weighting the session never built, which `combineBlocks`
turns into an absent Leq — never a substitution, so a caller learns it asked
for something absent instead of quietly reading a neighbour's numbers.

Two counters then needed care in the *other* direction, and both are the kind of
thing fanning one input out to N objects creates: `droppedSamplesTotal` reads
**one** chain rather than summing over them (the same loss rides all of them, so
summing reports it N times and makes a reconstructed timestamp *late* — the same
defect `droppedSamples` exists to prevent, mirrored), and `blockCount` reports
the **minimum** across chains so one that fell behind shows up rather than being
hidden by a neighbour that did not.

## Related

- `memory/a-default-must-be-run-through-the-gate-it-feeds.md` — the same family:
  a value shipped without being pushed through the thing that consumes it.
- `memory/a-fixture-can-be-too-well-behaved-to-fail.md` — that one is about a
  fixture's *amplitude* being too small to trip an assertion; this one is about
  its *cardinality* being too small to reach a branch.
