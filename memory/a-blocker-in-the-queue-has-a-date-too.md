# A blocker in the queue has a date too

2026-09-18, lane L-API closeout.

`docs/HUMAN-QA-QUEUE.md`'s first `[!]` said GitHub Actions was billing-blocked
at the account level. It was, from 2026-09-16. Four PR bodies, five handoff
entries and the master plan's status snapshot then repeated it: "Actions is
billing-blocked, so these are local runs and the three-OS acceptance is NOT
collected." Every one of those sentences was written by reading the queue rather
than by reading GitHub.

Measured at the closeout with one command, `gh run list`: a push to `main` ran
the full three-OS matrix and **passed** at 2026-09-17T17:31Z, and every run
since had executed. So the block had lifted a day earlier, and **PRs #16, #17,
#18 and #19 merged with a live, visibly failing matrix rather than with none** —
ubuntu and windows green at 774/774, **macos red at 770/774**. One of the four
failures was the merging lane's own golden byte-compare.

The position was strictly worse than the one everybody believed they were in,
and it was invisible for the same reason a green build is not proof of correct
logic: nobody ran the check that would have shown it. "There is no CI signal"
and "there is a CI signal and it is red" look identical from inside a document
that asserts the first.

This is `a-public-issue-has-a-date-too.md` turned inward. That one was about
trusting a stranger's description of code; this one is about trusting **our own
queue's description of our own infrastructure**. The queue is the more dangerous
of the two, because it reads as authoritative and because a `[!]` is written
precisely to stop people re-checking.

## The rule

A recorded blocker is a measurement with a date on it, not a standing fact.
Before citing one in a PR body, a report or a status snapshot, **re-run the
cheapest command that would refute it** and paste the output. For CI that is
`gh run list`; for a purchase it is still a purchase; for a tool version it is
`--version`.

And when the blocker is what excuses missing evidence, re-checking is not
optional: "the gate cannot be satisfied" and "the gate is not being met" carry
the same consequence for the merge and opposite consequences for what the
session is obliged to do about it.
