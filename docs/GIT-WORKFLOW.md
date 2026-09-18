# Git workflow — GitHub is the record, not the local `main`

*Adopted 2026-09-15 at the owner's instruction. Until then, lanes merged into a
local `main` with `--no-ff` and `push` was a separate owner's word; the local
`main` drifted up to 36 commits ahead of `origin/main` and CI on GitHub ran only
on the occasional push. This document replaces that habit.*

Remote: `https://github.com/toanaz-ops/rta-tool` (private). Default branch `main`.

## The four rules

1. **`origin/main` is the truth.** A commit that is not on `origin/main` is not
   merged, no matter what the local `main` says. Every session starts with
   `git fetch origin` and `git rev-list --count origin/main..main`; if that is
   not zero, something was merged locally and not pushed — push it or say why.
2. **No commit lands on `main` except through a pull request.** No local
   `git merge` into `main`, no `--no-ff` in the primary checkout. Lane branches
   are pushed, a PR is opened, CI runs on three OSes, an independent verifier
   reads the real files, and then the PR is merged on GitHub.
3. **CI green is the merge gate.** The workflow `.github/workflows/ci.yml`
   builds `rta_core` with `RTA_BUILD_APP=OFF` on ubuntu / macos / windows. A red
   matrix job blocks the merge, full stop. It is the only proof that `core/` is
   still framework-free and portable — the property the whole project sells.
   The `RTA_BUILD_APP=ON` configuration is not on CI (it needs JUCE); its tally
   goes in the PR body, measured on this machine, and the verifier re-measures.
   **Warnings are part of green.** The `Warnings` step greps each OS's build
   log for `warning:` / `warning C…:` and fails on a non-zero count, so a gcc
   or AppleClang warning blocks the merge exactly as an MSVC one does. Until
   2026-09-18 only `warning C` was counted, and gcc sat at 16 unseen. The
   step's comment in `ci.yml` records why a log count was chosen over
   `-Werror`; a warning that must be tolerated is excluded there, by pattern,
   with its reason — never by `-Wno-…` in `CMakeLists.txt`.
4. **Merge is still the owner's word, in the current conversation.** A PR that
   is green and verified waits. The owner says "merge" (or "merge and push" —
   they are now the same act), the orchestrator runs `gh pr merge`. "Do all of
   it" is not that word.

## Branch naming

```
<lane>/<what>          l7/eq-app-session-verify, l7/align-order4-probe
ci/<what>              ci/portability-fixes
docs/<what>            docs/github-workflow
```

One lane sub-task per branch. A branch that is fully merged is deleted on GitHub
(**enable "Automatically delete head branches" in repo settings — the agent could
not; the API call was denied by the permission gate**) and locally with
`git branch -d`. Do not commit further to a branch after its PR merged: the
merge commit makes it diverge (this bit the L7 orchestrator on 2026-09-07).

Every builder works in its own `git worktree` on its own branch, with its own
build directory name. The primary checkout stays on `main` and stays clean; it
is used only to `git pull --ff-only origin main`.

## PR contents

The template `.github/pull_request_template.md` is the checklist. The parts that
matter most, in the order a verifier reads them:

- **Tallies pasted from the command, at a named commit.** `N/N` for OFF and ON.
  A number without its command is a claim, not evidence (CLAUDE.md
  "Verification standard").
- **Guards red-then-green.** Every guard test the change could reach was made to
  fail by a mutation and passed again after revert. Otherwise the guard may have
  quietly stopped guarding.
- **Deviations from the record and plan**, named. Silent re-decisions are the
  thing the decision records exist to prevent.

## Merge method

`gh pr merge <n> --merge` (a merge commit, not squash). The lane's small commits
are the audit trail the handoffs cite by hash (`7756da9..6940f92`); squashing
would orphan every hash in `docs/HANDOFF.md`. Rebase-merge is also acceptable
when the branch is linear and the hashes were never cited.

After merge, in the primary checkout:

```
git -C "D:\DEV CAVE EP3\PRJ010-RTA-TOOL" pull --ff-only origin main
```

If `--ff-only` refuses, someone merged locally again. Stop and reconcile; do not
force.

## What GitHub cannot enforce on this plan

This repository is private on the free plan. Branch protection and rulesets
return HTTP 403 ("Upgrade to GitHub Pro or make this repository public"). So the
gate is procedural: the orchestrator does not run `gh pr merge` on a red or
unverified PR, and this document is what the next session reads. Two repo
settings the owner can flip by hand that make the procedure harder to skip:

- *Settings → General → Pull Requests → Allow auto-merge* — then
  `gh pr merge --auto --merge` queues the merge behind the CI checks.
- *Settings → General → Pull Requests → Automatically delete head branches.*

Making the repository public would enable branch protection outright; the
licence (AGPL-3.0-or-later) already permits it. That is the owner's decision.

## Verification before merge, unchanged

The PR replaces the local merge, not the verifier. The sequence is:

1. Builder pushes branch, opens PR, iterates until the CI matrix is green.
2. Orchestrator dispatches an independent verifier (no `Write`/`Edit`; commit
   before dispatch — `memory/a-verifier-with-bash-can-git-checkout-your-uncommitted-fix.md`)
   that checks out the PR head in its own worktree, rebuilds both configurations
   with `--clean-first`, re-measures the tallies, and tries to refute the
   load-bearing claims by mutation.
3. Verifier verdict + CI status + tallies go in a PR comment (`gh pr comment`).
4. Owner says "merge". Orchestrator merges, pulls `main`, updates
   `docs/HANDOFF.md` to name the merge commit.

## Handoffs still name commits, now on GitHub

`docs/HANDOFF.md` and the shared handoffs keep citing commit hashes and tallies.
Add the PR number next to the merge commit (`merged PR #12 at <sha>`), so the
next session can open the PR and read the verifier's comment instead of
re-deriving it.
