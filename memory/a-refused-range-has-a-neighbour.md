---
name: a-refused-range-has-a-neighbour
description: When a review finds a value range that must be refused, check the adjacent range the fix leaves open -- H9's fix refused a gate start inside the fade-in and still accepted every start after the arrival
metadata:
  type: feedback
---

PR #107 (H9 IR gate): Fable found that a gate start between -0.5 ms and 0.0 ms
put the arrival inside the fade-in and silently attenuated or deleted the direct
sound. The builder's fix refused exactly that band (`S <= 0 && -S < Fi`) and left
every start AFTER the arrival accepted -- a gate with no direct sound at all,
readout still "valid above N Hz", one marker drag past the refused band. The
orchestrator caught it only by reading the fix's condition, not its tests; the
header comment even said "allowed on purpose".

**Why:** a fix scoped to the reviewer's failure scenario guards that scenario;
the defect is the property ("the direct sound keeps full weight"), and its
violation usually spans more than the example range.

**How to apply:** for any refusal/guard fix, write the property as one inequality
(here `-S >= Fi`), then check the condition in the code IS that inequality with no
extra conjunct, and add a test on the far side of the reviewer's range (here
+0.1 ms, +2.0 ms, and `Fi = 0`). Related: [[a-fixed-defect-returns-through-the-silent-fallback]].
