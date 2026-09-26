# Git workflow — GitHub is the record, not the local `main`

*Adopted 2026-09-15 at the owner's instruction. Until then, lanes merged into a
local `main` with `--no-ff` and `push` was a separate owner's word; the local
`main` drifted up to 36 commits ahead of `origin/main` and CI on GitHub ran only
on the occasional push. This document replaces that habit.*

Remote: `https://github.com/toanaz-ops/rta-tool` (public since before 2026-09-26; see
"What GitHub cannot enforce — and what it now can" below). Default branch `main`.

## The four rules

1. **`origin/main` is the truth.** A commit that is not on `origin/main` is not
   merged, no matter what the local `main` says. Every session starts with
   `git fetch origin` and `git rev-list --count origin/main..main`; if that is
   not zero, something was merged locally and not pushed — push it or say why.
2. **No commit lands on `main` except through a pull request.** No local
   `git merge` into `main`, no `--no-ff` in the primary checkout. Lane branches
   are pushed, a PR is opened, CI runs on three OSes plus the Windows ON job,
   an independent verifier reads the real files, and then the PR is merged on
   GitHub.
3. **CI green is the merge gate.** The workflow `.github/workflows/ci.yml`
   builds `rta_core` with `RTA_BUILD_APP=OFF` on ubuntu / macos / windows. A red
   matrix job blocks the merge, full stop. It is the only proof that `core/` is
   still framework-free and portable — the property the whole project sells.
   **Warnings are part of green.** The `Warnings` step greps each OS's build
   log for `warning:` / `warning C…:` and fails on a non-zero count, so a gcc
   or AppleClang warning blocks the merge exactly as an MSVC one does. Until
   2026-09-18 only `warning C` was counted, and gcc sat at 16 unseen. The
   step's comment in `ci.yml` records why a log count was chosen over
   `-Werror`; a warning that must be tolerated is excluded there, by pattern,
   with its reason — never by `-Wno-…` in `CMakeLists.txt`.

   **The `RTA_BUILD_APP=ON` configuration is on CI too, since 2026-09-26** —
   a second, separate workflow, `.github/workflows/ci-app-on.yml`, builds and
   tests the JUCE app (`MainComponent*.cpp`, the view layer, `app/tests_juce`,
   `platform/tests_juce`, `ui/tests`, `rtatool_snapshot`) on `windows-latest`
   only, at the owner's own instruction (2026-09-26): it proves the ON
   configuration builds and its tests pass, not that it is portable across
   compilers — that claim stays rule 3's alone, on `rta_core` with
   `RTA_BUILD_APP=OFF`. Whether `RTA_BUILD_APP=ON` also builds clean on
   ubuntu-latest/macos-latest has not been attempted. Not a fourth matrix leg
   of `ci.yml`: the two jobs' steps (JUCE fetch/cache, the offscreen GUI
   snapshot, its artifact upload) don't overlap with the OFF job's at all, so
   a separate file reads as two short files instead of one long one with
   `if(RTA_BUILD_APP)`-shaped conditions on every step, and a cold run here
   already takes ~25 minutes (measured, PR #36's first run) — three of those
   with no evidence yet that ON is even portable is not obviously worth the
   wait. It runs the same warning gate as `ci.yml` (copied, not
   reimplemented) and the whole ON ctest tree with no test excluded — see
   `ci-app-on.yml`'s own header comment for why every ON test already
   tolerates a CI box with no audio hardware and no desktop peer. A PR now
   shows both jobs as checks; both must be green before "merge" (rule 4)
   applies. A human still looks at the uploaded `rtatool-snapshot-<run_number>`
   artifact per PR — that upload is not a substitute for the human GUI pass,
   it is what makes that pass possible without a local ON rebuild.
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

## What GitHub cannot enforce — and what it now can

*Corrected 2026-09-26:* the repository is now **public**
(`gh api repos/toanaz-ops/rta-tool --jq .visibility` → `public`). Two
consequences that earlier text in this repo may still contradict:

- **Actions minutes on standard GitHub-hosted runners are free** for a public
  repository. A "2000 min/month quota" or "billing-blocked" claim is stale;
  re-run the command above before citing one
  (`memory/a-blocker-in-the-queue-has-a-date-too.md`).
- **Branch protection is now available**. While the repo was private on the
  free plan, it returned HTTP 403. Nobody has enabled it yet, so the gate is
  still procedural: the orchestrator does not run `gh pr merge` on a red or
  unverified PR. Requiring the CI checks in a branch rule is a settings change
  for the owner to make.

Two repo settings the owner can flip by hand that make the procedure harder
to skip:

- *Settings → General → Pull Requests → Allow auto-merge* — then
  `gh pr merge --auto --merge` queues the merge behind the CI checks.
- *Settings → General → Pull Requests → Automatically delete head branches.*

## Verification before merge — the review loop (revised 2026-09-26)

The PR replaces the local merge, not the verifier. The owner set this loop on
2026-09-25/26 after L6a; the reasoning is in
`memory/merge-when-no-high-or-medium-remains.md`.

1. **The builder self-checks before opening the PR**:
   - Warnings are counted with CI's pattern `warning( [A-Z]+[0-9]+)?:`, never
     MSVC's `warning C` alone.
   - `tools/diffmut.py --base origin/main` shows no SURVIVED mutant on the
     lines the PR adds, or each survivor is explained in the PR body.
   - Every tolerance is derived in a comment.
   - No fixture is shrunk to fit a limit. A limit that bites is a finding.
   - Every file is under 400 lines. The `source_files_are_under_400_lines`
     ctest enforces this.
2. The builder pushes, opens the PR, and iterates until CI is green (the OFF
   matrix on three OSes, plus the Windows ON job).
3. The orchestrator commits, then dispatches an independent verifier. The
   verifier has no `Write`/`Edit`, checks out the PR head in its own worktree
   under `.claude/worktrees/verify-*` (a Temp path is too long for MSVC's
   FileTracker), re-measures, and tries to refute the load-bearing claims by
   mutation. **Every finding is graded HIGH / MEDIUM / LOW.**
4. The fix round goes back to the same builder with the HIGH and MEDIUM
   findings. LOW findings go on a lane-level list, fixed together in one PR
   at the end of the lane.
5. **After every fix round that changes behaviour, run another narrow
   verifier round.** There is no cap on the number of rounds: stop when a
   round finds no HIGH or MEDIUM. A fix round that touches only tests or
   comments can be closed by the orchestrator reading the diff and CI.
6. Merge once CI is 3/3 green and no HIGH or MEDIUM remains. Merge on the
   owner's word, or under a lane-level delegation the owner gave in the
   conversation. Always run `gh pr checks N` first and merge with
   `gh pr merge N --merge --match-head-commit <sha>`.

**Rebuild what the change could have changed, not everything.**
- **OFF** always.
- **ON** only when the diff touches JUCE-side code: `app/src/*Main*`,
  `app/src/view/`, `app/src/dev/`, `app/tests_juce/`, `platform/`, `ui/`, or
  any file listed in the `rtatool` source lists. The Windows ON CI job covers
  the rest.
- **The forced atomic fallback** (`-DRTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK=ON`)
  only when the diff touches `AtomicSharedPtr`, `Snapshot` publication, or
  any file matching `no_std_atomic_over_shared_ptr`'s scan.
- **A verifier reuses the builder's build directories** for anything it does
  not mutate, and builds fresh only where it mutates. A name-only diff decides
  which of these applies (`memory/reverify-what-the-change-could-have-changed.md`).
- The full three-config rebuild runs once, before the lane closes.

## Handoffs still name commits, now on GitHub

`docs/HANDOFF.md` and the shared handoffs keep citing commit hashes and tallies.
Add the PR number next to the merge commit (`merged PR #12 at <sha>`), so the
next session can open the PR and read the verifier's comment instead of
re-deriving it.
