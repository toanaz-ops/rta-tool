# A fixture shrunk to fit a limit hides the limit's defect

**2026-09-25, lane L6a (SPL-pro), Wave 2 (`SplMeter`/`BlockAccumulator`).**
Found by an independent verifier who probed the shipped path instead of
trusting the fixture that had been narrowed to avoid it.

`BlockAccumulator::kReadyCapacity` bounds how many blocks a single `push()`
call can ready before the caller drains them — a real, load-bearing limit on a
fixed-size buffer. A builder's test needed a hop that closes several blocks at
once, hit the fixture against that capacity, and reduced the test from five
blocks per hop to four so it would fit inside `kReadyCapacity`. The change was
recorded as a fixture adjustment, out of scope for the task at hand — a
reasonable-sounding call, made under time pressure, about a number that looked
like a test-authoring detail rather than a production question.

It was not a detail. The verifier read `SplMeter`'s real per-hop close path —
not the shrunk fixture — and found that on the shipped code, a hop that closes
*more* blocks than the ready buffer's capacity **silently loses the excess
samples**: they are counted toward nothing, not even `droppedSamples`, because
the accounting path only runs for blocks that made it into the ready buffer.
The fixture that had been shrunk to avoid tripping the limit was shrunk
*because* it would have tripped a real defect, and shrinking it removed the
only test that could have shown that.

## Why this is easy to miss

A fixture that hits a capacity limit gives two signals that look identical
from inside the test: "this input is unrealistic, narrow it" and "this input
is realistic and the code under test has a hole at exactly this boundary."
Only reading the production code's behavior at and past the limit
distinguishes them. A builder mid-task, focused on making the current commit
green, has every incentive to read the first signal — it is faster, and the
test still proves *something*.

## The check this costs

**A limit that forces a fixture change is a finding to report, never a
fixture to shrink silently.** When a test's inputs have to be narrowed to fit
inside a constant the production code also uses, stop and ask the question the
other direction: what does the shipped code do on the input the fixture no
longer covers? If the answer is not written down and tested, the fixture
change is hiding a gap, not avoiding an irrelevant one. The fix here was not to
widen the fixture back — it was to give `SplMeter` an overflow-eviction path
that carries the evicted block's own sample count and `droppedSamples` forward
onto the block that inherits its lost time, and to add a case that exercises a
hop wider than `kReadyCapacity` and asserts the sample count is still exact.

## Related

- `memory/a-config-field-with-one-value-in-every-fixture.md` — same family
  from the other direction: a fixture too narrow in *cardinality* rather than
  *amplitude* to reach a branch.
- `memory/a-cap-checked-on-the-drain-path-is-unchecked-on-the-publish-path.md`
  — a sibling shape: a bound enforced on one path and silently absent on the
  path that actually ships.
