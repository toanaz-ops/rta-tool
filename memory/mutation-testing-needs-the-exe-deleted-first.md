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
