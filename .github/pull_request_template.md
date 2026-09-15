<!-- Title: <lane>: <what> — e.g. "L7-EQ: Tasks E/F/G — EqSession, trust mask, EqVerify" -->

## What / why
One paragraph. Link the decision record (`docs/dsp/...`) and plan (`docs/plans/...`) this implements.

## Measured, not copied
Paste the exact commands and tallies. Numbers are a snapshot at the commit named.

```
commit: <sha>
ctest --test-dir build-<x> -C Release          (RTA_BUILD_APP=OFF) -> N/N, 0 failed
ctest --test-dir build-<x>-on -C Release       (RTA_BUILD_APP=ON)  -> N/N, 0 failed
warning C in both build logs                    -> 0
```

## Guards still guard
For each guard test the change could touch: the mutation that made it RED, the red line, then GREEN after revert.

## Deviations from the plan / record
List each one and why. "None" is an acceptable answer only if true.

## Owner decisions this PR does NOT make
Reference `docs/HUMAN-QA-QUEUE.md` items; defaults chosen are named, not silent.

## Docs
- [ ] `docs/HANDOFF.md` top section updated with commit hashes + tallies
- [ ] roadmap row in `docs/plans/MASTER-EXECUTION-PLAN.md` updated if a phase/wave changed state
- [ ] `memory/` lesson written if one was paid for
