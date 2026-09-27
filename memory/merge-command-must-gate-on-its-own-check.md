---
name: merge-command-must-gate-on-its-own-check
description: 2026-09-27 PR #44 merged with the app-ON CI job RED because the orchestrator chained `cat <checks>` then `gh pr merge` in one command; with no branch protection GitHub merged it. Gate the merge on the parsed result, never on having printed it.
metadata:
  type: feedback
---

On 2026-09-27 the orchestrator ran one Bash command: `cat <checks output> && gh pr merge 44 ...`.
The printed checks showed `rtatool app (RTA_BUILD_APP=ON) fail`. The merge still
ran, because `cat` succeeded. The repo has no branch protection enabled, so GitHub
accepted the merge. The failure turned out to be a pre-existing flaky test
(`test_spl_epoch_freeze.cpp:137`, a sampling race). That was luck, not
process.

**Why:** printing a result is not checking it. A human skimming the output
afterwards is too late, because the merge has already landed on `origin/main`.

**How to apply:**
- Never put `gh pr merge` in the same command as the step that fetches CI.
  Parse first: `gh pr checks N --json name,state --jq '[.[]|select(.state!="SUCCESS" and .state!="SKIPPED")]|length'`
  must print `0`. Only then run the merge, as a separate tool call.
- A red check that is "just a flake" is still red. Fix the flake
  (make the test deterministic), or re-run it and record why. Do not merge over it.
- The durable fix is the owner's: enable branch protection with required
  checks. The repo is public, so it is available
  (`docs/GIT-WORKFLOW.md` "What GitHub cannot enforce").
