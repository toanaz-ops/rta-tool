---
name: a-verifier-with-bash-can-git-checkout-your-uncommitted-fix
description: A verifier agent has no Write tool but still has Bash; to revert its own mutation probe it ran `git checkout -- file` on the shared worktree, which reset the session's UNCOMMITTED fix to HEAD — commit before dispatching one, or its revert reverts your work
metadata:
  type: feedback
---

**What happened (lane L6b closeout, 2026-09-06).** An adversarial verifier was
dispatched with no Write tool — the intended safety boundary — to mutation-probe
an uncommitted fix. To undo a `sed` mutation it made during the probe, it ran
`git checkout -- app/src/measure/AnalysisPublish.cpp`. But the fix under test was
**working-tree change, never committed**, so `git checkout` reset the file to
`HEAD` — the *pre-fix* state — wiping both the mutation AND the real fix, and it
briefly reported a GREEN that was running against the reverted file. It caught
this itself (`git diff HEAD` came back empty, which was suspicious for a file it
had just "restored"), reconstructed the patch from a diff captured earlier, and
reapplied it; the main session then independently re-verified the tree matched
the intended diff and rebuilt from it (158/158). No harm survived, but only
because the diff was still recoverable.

**Why.** "No Write tool" is not "no mutation". `git checkout`, `git apply`,
`git reset`, `git stash` all reach the tree through Bash, and none of them are
Write/Edit calls. Against COMMITTED work `git checkout -- file` is a safe revert;
against UNCOMMITTED work it is a destroy, because HEAD does not contain the work.
A verifier told to "revert your own mutations" cannot distinguish its mutation
from the fix it is standing on when both live only in the working tree.

**How to apply.** Commit the fix (or at least a WIP commit) BEFORE dispatching a
verifier that will run git commands to set up and tear down mutation probes —
then `git checkout -- file` reverts only to the committed fix, not past it. Or
tell the verifier to build in a throwaway git worktree/clone so its reverts
never touch the live tree. Either way, after any agent reports done, re-run the
build from the CURRENT tree yourself before trusting a "GREEN" — a pass reported
against a file the agent just git-touched proves nothing. Related to rule 7
(one session per shared resource) and [[reverify-what-the-change-could-have-changed]].

---

**It is not only a verifier (2026-09-18, lane L6a Wave 0).** The same loss
happened with no second agent anywhere: one builder mutating its OWN code to
show a test red, and reverting with
`git checkout -- app/src/measure/AnalysisPublish.cpp`. HEAD was the previous
task's commit, so that checkout reverted the mutation **and the uncommitted fix
underneath it** in one step. The next ctest read 689 with a single failure — the
fix's own new test, failing against code that no longer contained the fix. Cost:
one rebuild, plus a minute spent reading a correct failure as a flaky test.

So the rule is not "watch the verifier". It is: **a mutation probe assumes HEAD
holds the thing you want back.** Commit the fix first and the assumption is
true. Mutation testing is a git operation whether or not anyone thinks of it as
one, and the single-agent case is easier to get wrong precisely because there is
nobody else to suspect. Pairs with
[[mutation-testing-needs-the-exe-deleted-first]], which is the other half of
doing this correctly: commit, then delete the exe, then mutate.
