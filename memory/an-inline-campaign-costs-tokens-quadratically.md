---
name: an-inline-campaign-costs-tokens-quadratically
description: Running a build+mutation+verify campaign inline in the main session grows the window quadratically; dispatch the loop to a subagent instead
metadata:
  type: feedback
---

The RTA+ session (Fable, effort high) ran an entire C++ build + mutation-testing
+ verify campaign *inline* in the main session: 13,385 messages, 76% of a 1M
context window, 680k tokens of it conversation body. The pattern in every
transcript window was identical — one Bash call per step, a line of prose
between each ("Now mutation B"), thousands of times.

**Why:** every turn re-sends the whole growing transcript through the model, so
an inline grind costs tokens roughly quadratically in its own length. Overhead
(MCP tools, skills, memory) was only ~7% — the cost was the message body itself,
made of micro-turns and dumped green build logs (`849/849` printed every run).

**How to apply:**
- Dispatch the campaign to a subagent (`Agent`); its transcript does not count
  against the parent window, only the final report returns. See [[mutation-testing-needs-the-exe-deleted-first]] for how to run the loop correctly inside it.
- `ctest --output-on-failure`; pipe full runs to a file and report the tally.
- One script loops all mutants → one compact PASS/FAIL table; batch independent
  Bash into one turn; no prose per mutant.
- Match effort to the work — mechanical verify does not need high.
- When the PR merges, hand off and archive; start the next task in a fresh
  window, not a 700k-token one.

Measures taken 2026-09-18: RTA+ effort lowered to medium; the "Token & context
discipline" section added to CLAUDE.md.
