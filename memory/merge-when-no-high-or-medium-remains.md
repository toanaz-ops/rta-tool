---
name: merge-when-no-high-or-medium-remains
description: Owner-approved review policy (2026-09-25) — verify until a round finds no HIGH/MEDIUM (no cap), merge on no HIGH/MEDIUM + CI 3/3, batch LOWs to a lane-end follow-up; builders self-check before the first PR
metadata:
  type: feedback
---

**Policy (owner, 2026-09-25).**
- Merge a wave PR once CI is 3/3 green and NO HIGH or MEDIUM finding remains.
- LOW findings (vacuous test, wording mismatch, stale allow-list, a slack bound)
  go on a follow-up list and are fixed together in one PR at the end of the lane.
- **No cap on verifier rounds.** A round was first capped at 2; the owner
  reversed that the same day: "if the rounds still catch bugs, keep running".
  After every fix round that changes behaviour, run a narrow verifier round.
  Stop when a round finds no HIGH or MEDIUM.
- Every builder prompt carries a **mandatory self-check** to run before opening
  the PR:
  - Count warnings with CI's pattern `warning( [A-Z]+[0-9]+)?:`, never MSVC's
    `warning C` alone ([[a-tally-counts-the-prefix-it-greps]]).
  - Every acceptance row has a mutant that is shown RED, and no fixture is
    uniform where the property being tested depends on a difference
    ([[a-fixture-can-be-too-well-behaved-to-fail]],
    [[a-config-field-with-one-value-in-every-fixture]]).
  - Tolerances are derived in a comment, never typed
    ([[a-tolerance-inherited-from-a-plan-is-that-plans-fixture]]).
  - No fixture may be shrunk to fit a limit. If a limit bites, report it as a
    defect.

**Why:** L6a Waves 2–4a took 2–5 fix rounds per PR. Every round found something
real, but about half the rounds existed only to close LOW findings. Each LOW
fix then needed a three-config rebuild plus a verify pass, and some of those
fixes introduced new defects of their own. The findings that justified review
were all caught in rounds 1–2:
- C-weighted dose under an LAeq label (340× wrong),
- a dangling reference that turned CI red,
- allocation in the analysis-thread feed,
- `Clear` published for a comparison that never ran.

**How to apply:**
- Grade every verifier finding HIGH, MEDIUM or LOW before dispatching a fix
  round, and send the builder only the HIGH and MEDIUM ones.
- Write any record amendment against the shipped code, never ahead of it. A6
  had to be corrected three times because it was written before the code.
