---
name: mutation-testing-needs-the-exe-deleted-first
description: cmake --build --target X can log "-> X.exe" without relinking, so a mutation test reads a stale binary and false-PASSes; delete the .exe first
metadata:
  type: reference
---

On this machine (MSBuild via the Visual Studio generator), `cmake --build <dir>
--config Release --target rta_core_tests` sometimes prints `rta_core_tests.vcxproj ->
...\rta_core_tests.exe` in its log while **not actually rewriting the exe** — verified
by mtime/size, the exe stays byte-identical even though the changed `.cpp` recompiled
and the static lib relinked.

Consequence for **mutation testing**: after mutating a source file and rebuilding, the
test harness can run the **stale** binary and report the mutation as caught/not-caught
incorrectly — a false PASS that makes a broken test look like it still guards. Two
independent verifiers hit this on 2026-09-07 (an EQ classifyDip mutation and a
MinimumPhase fold mutation both "passed" first, then failed correctly once the exe was
forced fresh).

Fix: **delete the test exe before every rebuild during mutation testing**, e.g.
`rm -f build/core/tests/Release/rta_core_tests.exe` (or the platform/app equivalent),
then `cmake --build ...`. Or check the exe mtime/size actually changed before trusting
the run. This is distinct from, but rhymes with, the header-only-change staleness noted
elsewhere — see [[a-misconfigured-build-goes-99-percent-of-the-way]].

## Deleting the exe is NOT enough when the mutation is in a header

Found 2026-09-15 by the independent verifier of PR #4 (L7-EQ Tasks E/F). Mutating a
**header-only** file — `EqTrustMask.h`'s floor comparison — and rebuilding with the exe
already deleted still ran unmutated code: the build emitted

```
warning MSB8029: The Intermediate directory or Output directory cannot reside under
the Temporary directory ...
```

and then only **relinked** the existing objects. No `.cpp` had changed its own
timestamp, so MSBuild considered every translation unit up to date and never re-read
the header. The mutation looked *not caught*, which is the same false verdict as a
stale exe, arriving by a different road — and it is the more dangerous of the two,
because the exe genuinely is fresh, so an mtime check on it passes.

The verifier's own probe only went red once a `.cpp` in the same translation unit was
also touched.

So the procedure for a **header** mutation is: delete the exe, **and** `touch` (or edit)
one `.cpp` that includes that header, **then** build. Better still, put the mutation in
a `.cpp` when one exists on the same path — a mutation you can place in either file
belongs in the file the build system actually tracks. And when a scratch worktree lives
under `%TEMP%`, treat `MSB8029` in the log as a standing warning that incremental
decisions in that tree are not trustworthy.
