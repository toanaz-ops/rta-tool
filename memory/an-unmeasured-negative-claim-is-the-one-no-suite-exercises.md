---
name: an-unmeasured-negative-claim-is-the-one-no-suite-exercises
description: "no test in this lane can distinguish X" is a claim about the whole suite, so no test in the suite ever runs it; it shipped false twice in three days, and both times the counter-evidence was already in the same commit
metadata:
  type: reference
---

Twice in three days, in the same lane, a correction written to retire a claim that
measurement did not support shipped a **new** claim that measurement did not support.
Both were negative, and that is not a coincidence.

**2026-09-16, L6a station 3.** An earlier revision of SPL-R7 said the typed literal
`9.9657843` "fails D2b's own bound at the top of the range". Refuted; the revision that
replaced it said instead:

> "**No numeric acceptance in this lane can distinguish the two constants.**"

**2026-09-18, L6a station 4 Wave 1.** That sentence was copied into
`core/include/rta/meter/Dose.h` and into a test SECTION named *"and the literal is NOT
rejected on accuracy — that reason was refuted"*. PR #20's verifier refuted it in one
mutation: `test_dose_tables.cpp`'s **D2a** bounds each regulator's own exact duration
formula at `1e-9 %`, and the typed literal misses it by **1600x** (`1.600e-06 %` at
130 dBA), red at **50 of the 51** rows. The other literal fails by 2485x. D2a was in the
same commit, one file away, written by the same person in the same afternoon.

## Why this kind of sentence survives review

A positive claim names a thing, and the thing can be run. "`10^(Q/q)` is exactly 2.0" is
a line of code. But a claim of the form

> no acceptance **in this lane** can distinguish X from Y

is a statement about **the whole suite**, quantified over every test — and a suite has no
test that ranges over its own tests. So the claim is the one kind that:

- nothing in CI exercises, at any level of effort;
- reads as *more* rigorous than the thing it replaced, because it is more sweeping;
- attracts no reviewer's attention, because there is no assertion to look at;
- and gets copied outward — into a header, a test name, a plan note, a PR body — each
  copy borrowing the authority of the last.

The 2026-09-15 attribution variant in
[[a-prescribed-mutation-is-not-proof-the-check-catches-it]] is the same shape seen from
the other side: a guard credited with a property it did not have. Here the code was
right and the *prose about the code* was wrong. Both times the prose outranked the
measurement in the reader's mind, which is exactly backwards.

## The rule

**A negative claim about what no test can detect is a mutation. Run it, or do not write
it.** It is the cheapest class of claim to check, because the check is already the
project's standard tool:

```
# the claim: "no acceptance here can tell the typed constant from the computed one"
cp <file> <file>.mutbak
<type the constant>
rm -f <exe>; touch <asserting TU>; cmake --build ...
<run the WHOLE tag, not the one case you were thinking of>
```

Ten minutes. It returned `50 failed` and the sentence was three artefacts deep by then.

And when the claim turns out to be false, do not merely delete it — **replace it with the
arithmetic**, in the suite, so the next person cannot reintroduce it.
`core/tests/test_dose_constants.cpp` now computes the worst deviation for the computed
and the typed constant over D2a's own 80–130 dB grid, for both regulators, and asserts
that the first clears the bound by four orders and the second fails it on 50 of 51 rows.
The claim is a test now. It cannot rot back into prose.

## Smell test, for reviewing your own writing

Distrust on sight, in a comment, a commit message, a record or a PR body:

- "no test / no acceptance / nothing in this lane can ..."
- "this is undetectable" / "no fixture could catch this"
- "X was refuted" where X is itself a negative
- any sentence whose subject is *the absence of evidence* rather than a measurement

Each one is either a mutation you have run and can paste, or a sentence to delete. There
is no third option — and "it is obviously true" is how both of these shipped.
