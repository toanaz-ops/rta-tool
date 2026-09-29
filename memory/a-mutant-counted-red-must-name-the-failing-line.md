---
name: a-mutant-counted-red-must-name-the-failing-line
description: A mutant script that counts any non-zero test exit as RED credits unrelated flakes to the mutant; RED means the expected test failed at a named file:line, printed in the table
metadata:
  type: feedback
---

PR #58 (routing matrix rows follow the device channel count), 2026-09-29. The
builder's mutation script looped every mutant and counted **any non-zero exit
of the test executable** as RED. One mutant was credited RED because an
unrelated wall-clock test failed under the load the mutation run itself
created. The check the mutant was meant to be caught by,
`latest() != nullptr`, was **vacuous**: with the mutation in place it still
held. The verifier reran the same mutant and got GREEN. The table said 100%
RED; the truth was one mutant no test could kill.

**Why:** a non-zero exit only says *something* failed. On a loaded machine
that something is often a timing test that has nothing to do with the mutant
(the same week: `routing_live` Apply, STORE `mixed`, SPL-log disable — three
flakes fixed in #59 and #65). Counting the exit code makes the flake rate
part of the mutation score, and it can only ever inflate it.

**How to apply:**
- A mutant is **RED only when the test the row names fails at a named
  `file:line`** — the script parses the failing location out of the runner's
  output and prints it in the table beside the mutant. A row with no location,
  or a location in a different file, is **not RED**; it is "failed elsewhere"
  and needs a rerun before it counts.
- Rerun a RED mutant once more, alone and unloaded, before believing it. A
  flake will not reproduce; a real kill will.
- A guard whose assertion holds under the mutation (as `latest() != nullptr`
  did) is a vacuous guard. Strengthen the assertion until the named test goes
  RED at its own line, then restore and confirm GREEN. This is the same trap
  as [[a-prescribed-mutation-is-not-proof-the-check-catches-it]]: a plan or a
  table that names both halves has still not run them against each other.
- PR #64's table shows the shape that worked: "RED = exit non-zero AND a
  FAILED location lies in the expected test file", and each row carries the
  location.
- Delete the exe and rebuild before every mutant, or a stale binary decides
  the result ([[mutation-testing-needs-the-exe-deleted-first]]).
