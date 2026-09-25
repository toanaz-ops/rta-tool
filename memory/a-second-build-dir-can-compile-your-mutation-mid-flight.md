---
name: a-second-build-dir-can-compile-your-mutation-mid-flight
description: a background `cmake --build` in a DIFFERENT build tree (build-on) reads the SAME shared source files a foreground mutation-testing loop is editing (build-off) -- it can compile the mutated bytes into its own binary with no error, no warning, and a clean `git status` afterward
metadata:
  type: reference
---

Found 2026-09-25, PR #29 round-3 fix pass, while running the three-mutant
verification pass this repo's own final-verification procedure asks for.

The setup: `build-off`, `build-fb` and `build-on` are three SEPARATE CMake
build directories, but they all compile from the ONE shared source tree
(`app/src`, `app/tests`) this worktree holds. A full `cmake --build build-on`
was kicked off in the background (`run_in_background: true`) to satisfy the
task's "rebuild ALL THREE configs" step, and -- while it was still compiling
-- the FOREGROUND mutation-testing loop against `build-off` edited
`SplChannelState.cpp` (mutant 1: revert the Ln fix), built+tested `build-off`,
then `git checkout --` restored the file to HEAD.

`build-off`'s own cycle was fine: it always rebuilds+tests+restores+rebuilds
inside one continuous sequence, so its own final GREEN was against clean
source. `build-on`, running concurrently in a SEPARATE process, had no such
guarantee -- MSBuild's own parallel compile queue got to `SplChannelState.cpp`
SOMETIME during that window, and there was no way to know from `build-on`'s
side whether that "sometime" fell before, during, or after the mutation was
sitting in the file.

The result: `build-on`'s link succeeded (exit 0, zero warnings -- a mutated
source file compiles just as cleanly as the fixed one, this is not a syntax
mutation), and only `ctest` caught it -- two failing tests, both Ln-related,
matching mutant 1's own RED signature exactly. `git status --porcelain` was
EMPTY at the moment those two tests failed: the source was already correctly
restored; only the ALREADY-LINKED BINARY still held the mutated compile.

This is the same family as
[[mutation-testing-needs-the-exe-deleted-first]]'s "RESTORE half is stale
too" section, but the trigger is different: that file is about ONE build
directory's own incremental staleness after a same-process mutate/restore
cycle. This is about a SECOND, INDEPENDENT build process reading the same
files a first process is actively mutating -- there is no mtime bug to fix
here, no missing `touch`, no incremental-build trap; the only real fix is
**don't run a mutation-testing loop and a from-source rebuild of an unrelated
build directory at the same time when both compile the same source tree**.

What actually resolved it: `git status --porcelain` (proved the SOURCE was
clean) was not enough to trust the BINARY, exactly as the referenced file's
own closing paragraph says. The fix was `rm -rf build-on` and a full
from-scratch reconfigure + rebuild, run with NO other process touching
`app/src`/`app/tests` -- then `ctest` went 1005/1005, confirming the earlier
2 failures were the race and not a real regression.

Practical rule for a session doing final multi-config verification AND
mutation testing in the same pass: either (a) finish ALL mutation cycles
FIRST, confirm the source tree is at a clean, final `git status`, and only
THEN kick off the from-scratch rebuilds of the other configs, or (b) if a
background rebuild of another config is already running, do not start
mutating shared source files until it completes. Launching them "in
parallel to save wall-clock time" is exactly the false economy that produced
this: the mutation pass itself took a few minutes either way, and the
corrupted `build-on` had to be deleted and rebuilt from scratch regardless,
costing more time than the parallelism saved.
