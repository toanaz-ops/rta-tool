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

## Touching *a* dependent `.cpp` is not enough either: it must be the TU that holds the assertion

Found 2026-09-16 by the independent verifier of PR #9 (L7-ALIGN Wave 3b), and it
sharpens the section above. The mutation — an implicit `operator Trace()` — went into
**both** `app/src/trace/VirtualTrace.h` and `VirtualTrace.cpp`, so the previous rule
("touch one `.cpp` that includes that header") was satisfied. With the exe deleted:

```
  VirtualTrace.cpp
  rtatool_analysis_tests.vcxproj -> ...\Release\rtatool_analysis_tests.exe
--- does G3 PASS against the MUTATED source? ---
100% tests passed, 0 tests failed out of 1
```

`VirtualTrace.cpp` recompiled, the exe genuinely relinked, and the test went **green
against mutated source**. The three `static_assert`s live in `test_virtual_trace.cpp`
— a *different* translation unit — and that one was never recompiled.

The rule that survives: **a compile-time assertion is only run by the compile that
reads it.** `static_assert`, a detection-idiom trait, a `[[nodiscard]]` diagnostic, a
concept — none of these is executed by the test binary at all, so "the exe is fresh"
and "a `.cpp` was rebuilt" are both beside the point. What has to be forced is the
translation unit that CONTAINS the assertion.

Concretely, for a header mutation guarded by a compile-time assertion:

```
rm -f <build>/.../rtatool_analysis_tests.exe
rm -f <build>/.../rtatool_analysis_tests.dir/Release/test_virtual_trace.obj
touch app/tests/test_virtual_trace.cpp
cmake --build ... --target rtatool_analysis_tests
```

which gives the red it should have given the first time:

```
test_virtual_trace.cpp(220,19): error C2338: static assertion failed:
  'a VirtualTrace must not be convertible to a Trace'
test_virtual_trace.cpp(226,19): error C2338: static assertion failed:
  'library.add(virtualTrace, "", "") must be ill-formed'
```

`--clean-first` does the same job with no bookkeeping to get wrong, and for a
single-target mutation probe it costs a minute. **Prefer it.** Note also that whether
you see the false PASS at all depends on the build tree's incremental state: in the
builder's own directory MSBuild's `.tlog` header-dependency scan did force the test TU
and the red appeared, while in the verifier's fresh worktree it did not. A recipe whose
correctness depends on which machine's `.tlog` files you inherited is not a recipe —
which is the whole reason this one is written down in steps.

## The RESTORE half is stale too, and that is the dangerous half

Added 2026-09-15 by the final verifier of PR #4, and it completes the previous section.
Touching a dependent `.cpp` is how the *mutation* gets compiled in. Nothing makes it get
compiled back **out**: restoring the header returns the source to its original bytes but
leaves the object files built from the mutated version on disk, and MSBuild — seeing no
`.cpp` newer than its `.obj` — happily relinks them.

The failure this produces is worse than a false "not caught", because it arrives at the
end of the cycle, when the tree looks correct and everyone has stopped watching:

- the working tree is clean and `git diff` is empty, so the mutation *looks* reverted;
- the binary still contains it, so a green run afterwards is green for the wrong build,
  and a red one sends you hunting a defect that is not in the source.

Two habits close it, and they cost seconds:

1. **Finish every mutation cycle with a full rebuild**, not a targeted one — after the
   last revert, before the run you intend to believe.
2. **Compare the mutated files against their `HEAD` blobs by hash** once you are done:
   `git stash list` and `git status` cannot see a byte-identical revert that is wrong,
   but `md5sum <file>` versus `git show HEAD:<path> | md5sum` proves the source really is
   what you think, and a full rebuild then proves the binary matches the source.

Write the mutation down, revert it, rebuild everything, hash-check, *then* claim.
