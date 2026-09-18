# A tally counts the prefix it greps

**Date:** 2026-09-18. **Where:** `.github/workflows/ci.yml`, `.github/pull_request_template.md`.

Every PR body and the template measured `warning C` — MSVC's prefix. gcc on
`ubuntu-latest` had emitted 16 warnings and AppleClang 2 on every run since at
least d071269, and the tally read 0 on each of them, because gcc writes
`warning:` and never `warning C`. The three-OS matrix was the merge gate, and
two of the three compilers' diagnostics were not in it.

Nine of the sixteen were `-Wdangling-reference`, a class that names undefined
behaviour. Reading the code showed all nine were the heuristic's false
positive (the temporary was the `std::string` built from a name literal, which
`findCase` compares and never returns), but a green tally had been asserting
that with no one having looked.

**Why:** a grep pattern is a claim about the *format* of what it counts. It
was written against one compiler's output and its zero was then read as a
statement about all three. The same shape as the attribution lesson in
`a-prescribed-mutation-is-not-proof-the-check-catches-it.md`: a check credited
with a property it never touches stops anyone looking for the real one.

**How to apply:** when a number in a PR body comes from a grep, ask what
prefix each producer actually writes and run the pattern over a log from each
one. For diagnostics the producers are the compilers *and the linker*
(`ld: warning:` on macOS was one of the two). The CI `Warnings` step now
counts `warning( [A-Z]+[0-9]+)?:` per OS and fails on non-zero; before it,
the pattern was run over the baseline run's three logs and gave 16 / 2 / 0,
which is the number that had been reported as 0.
