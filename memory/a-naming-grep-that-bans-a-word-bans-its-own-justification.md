---
name: a-naming-grep-that-bans-a-word-bans-its-own-justification
description: four structural checks in one wave went red against this project's own headers, because a header must say "hysteresis" / "OSHA" / "peak" to record why the thing is absent; scan code with comments stripped, require the word in the prose, and make a homonym allow-list expire
metadata:
  type: reference
---

L6a Wave 1 shipped four claims of the form "this code deliberately does NOT contain
X". Record §6: the alarm latch has no hysteresis, no debounce, no margin. Record §7a:
peak never enters the dose integral. Record §11: no regulator's name is in `core/`.
Each was given a grep, exactly as the plan asked. **Every one of the four went red on
its first run, and in every case the code was correct and the check was wrong.**

The reason is the same each time, and it is structural rather than careless:

```
// NO HYSTERESIS AND NO DEBOUNCE, and that is a decision with a reason rather
// than an omission. Flicker is a property of the QUANTITY, not of the
// comparator ...
```

A file cannot record why it omits something without naming the thing it omits. So a
grep over the raw file text finds the word in the very paragraph that justifies the
word's absence — and the cheapest way to make such a check pass is to **delete the
paragraph**. A check that trains the next author to remove the reasoning is worse than
no check: it converts a documented decision into an undocumented one and calls that
progress.

## The shape that works

Two halves, and the second is what makes the first safe to keep:

1. **Scan the CODE, with comments stripped.** `core/tests/support/SourceScan.h`'s
   `stripLineComments` is fifteen lines. The naming claim is about identifiers,
   parameters and members — never about prose.
2. **REQUIRE the word in the prose.** `CHECK(header.find("no hysteresis and no
   debounce") != npos)`. Now the word is banned from the code *and mandatory* in the
   comment, so neither half can be satisfied by deleting the other.

And a third, because a stripper that silently reads nothing passes everything:
**assert a sentinel the file must contain.** That one caught itself too — `D3b`'s first
sentinel was `"dosesettings"`, which is in `Dose.h` and not in `Dose.cpp`, so the
vacuity guard went red on the `.cpp`. That red is the only evidence a vacuity guard
works, which is an argument for writing one even when it looks redundant.

## The homonym half: the word may not be your word

The fourth check was different in mechanism and identical in outcome. W1-E's E2 row
says to grep `app/src/export` and `app/src/view` for a readout label saying `peak`
without a qualifier. Run as written it reports **five** offenders, and not one is a
sound pressure level:

| literal | what it actually is |
|---|---|
| `"peaking"` (x2) | an EQ filter TYPE, `rta::eq::FilterType::Peaking` |
| `"peak_0dbfs"` | an FIR normalisation mode |
| `"peak_gain_db"` | an FIR design quantity: the filter's own gain peak |
| `"coefficient_peak"` | the largest FIR coefficient |

"Peak" is an ordinary English word that four subsystems use for four different
quantities. A word-based check across a whole codebase will collide with a homonym, and
the collision is not a bug in the other subsystem.

The fix is an allow-list that **expires**: each exemption carries its reason, and every
exemption must actually be FOUND or the case goes red. A renamed literal then leaves a
hole the check reports instead of a hole it widens. Same discipline as reading a guard's
`OK (N files scanned)` line rather than predicting it
([[core-must-not-include-frameworks]]).

## What to do when a plan hands you "grep for X"

Assume it will fire on your own justification and on somebody else's vocabulary, and
budget for both. Write it as: strip comments, require the sentinel, require the prose,
name every exemption, make every exemption prove it is still needed. Then the negative
claim is a test. Written the short way it is a countdown to someone deleting a comment.

The behavioural half still carries the real weight. The grep for `hyster` proves
nothing at all about behaviour -- a latch holding a `previousDb_` and an epsilon matches
none of those words. What proves it is a fixture whose amplitude is one ULP of the limit
(see [[a-fixture-can-be-too-well-behaved-to-fail]]): a 0.005 dB hysteresis planted as a
mutation left the 0.01 dB case GREEN and turned the one-ULP case red, and that asymmetry
is why both amplitudes ship.
