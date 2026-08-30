# A public issue has a date too

2026-08-30, EP06 review round, lane L4b literature pass.

A GitHub issue on pyrato reported that `intersection_time_lundeby`'s convergence
check never works because `preliminary_crossing_point` is never reassigned
between iterations. The builder session quoted it — honestly labelled as
unverified — and was one step from writing the bug into a decision record as a
live defect of the reference implementation.

Reading the **current** `main` of pyrato showed the loop now does
`old_crossing_point = crossing_point`, recomputes, and breaks on
`|old − new| < 0.01`: the convergence check works. What survives of the issue is
a vestigial `preliminary_crossing_point` assignment — dead code, not a broken
mechanism. The issue described a snapshot that no longer exists.

This is the AES-2id failure in a new costume. That one cited a standard for a
scope it never had; this one nearly cited a bug for a version that no longer has
it. Both come from treating a *description of a source* as the *source*.

## The rule

An issue, a blog post, a changelog entry — each describes the code as it stood
on its date. Before any of them enters a decision record, read the current code
at a pinned commit and cite that commit. The lesson the issue teaches ("blind
transcription copies the reference's bugs") can be kept; the bug itself must be
re-verified against what is actually there today.
