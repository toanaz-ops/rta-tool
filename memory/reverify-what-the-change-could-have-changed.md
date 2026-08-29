# Re-verify what the change could have changed — prove it with a diff, not a ritual

After merging lane L5c into `main` (2026-08-29) I re-ran the full suite in two
fresh build directories, ~12 minutes of JUCE compilation, to re-derive a number
I had already proven could not have moved.

The proof was one command, and I ran it *before* the rebuild:

```
git diff --name-only <last-tested-commit> <merged-commit> | grep -v '^docs/'
```

It returned nothing. Every difference between the tree that measured 357/317
and the merged tree was under `docs/` — four Markdown files. CMake globs no
Markdown. The compiled inputs were byte-identical, so the measurement carried
over by construction, and the rebuild could only ever confirm it.

## The rule

**Re-verification is owed to a change in what gets compiled, not to the act of
merging.** Before re-running anything after a merge, rebase, or cherry-pick:

1. Diff the tree you are about to trust against the tree you last measured.
2. If every changed path is outside the build's inputs — `docs/`, `memory/`,
   `*.md`, `.gitignore` — **the prior measurement stands.** Cite the diff
   command and its empty output as the evidence. That is stronger evidence
   than a fresh run, because it proves the two trees compile the same bytes
   rather than observing that they happened to agree once.
3. If any compiled input differs, re-run — and re-run **the configuration that
   covers those inputs**, not every configuration by reflex. A change confined
   to `app/src/view/` does not need the `RTA_BUILD_APP=OFF` suite, which does
   not build that directory at all.

## Why this is not a licence to skip testing

The distinction is between *evidence* and *ceremony*. This project's standing
rule is that a claim needs a command and its output — and a name-only diff
returning nothing IS a command and its output. What it refuses is the weaker
habit of re-running a suite because a git operation happened, which produces a
number nobody doubted and teaches the next reader that green runs are cheap.

The failure this guards against is the opposite one, and it is worth naming so
the rule is not over-applied: **when `main` has moved in code, the merged tree
is a tree nobody has ever tested**, even though both parents were green. That
case is exactly when to rebuild, and the diff in step 1 is what tells the two
cases apart.

## Related

- The two-configuration rule is separate and still holds: `RTA_BUILD_APP=ON`
  and `OFF` cover different target sets, so their counts are reported
  separately and **never summed**. See `docs/HANDOFF.md`'s baseline block,
  which is the one place those numbers live.
- Throwaway build directories in the main checkout are gitignored, so they are
  invisible to `git status` and easy to leave behind. Delete them in the same
  breath as creating them.
