# A redirected build's own exit code is not the wrapper's

**Date:** 2026-09-25. **Where:** L6a task W2-E1, `app/CMakeLists.txt`,
`app/tests_juce/CMakeLists.txt`.

`cmake --build build-on --config Release --parallel > log 2>&1; echo "EXIT:$?"`
run as a backgrounded shell task reported "completed (exit code 0)" in the
task notification. That code is the *wrapper's* exit status — the trivially
successful `echo` — not the build's. The build had actually failed (`EXIT:1`
inside the redirected log), and grepping the wrong file (the background
task's own truncated stdout, which held only `EXIT:1` and a shell-exit line,
because everything real had gone into `log`) for `warning C` returned zero
matches and was read as "clean". `RTA Tool.exe`, `rtatool_snapshot.exe` and
`rtatool_routing_live_tests.exe` had 3–6 unresolved externals each
(`SplChannelState`/`SplHistory`/`SplAlarms` were never added to those three
targets' explicit, no-glob source lists — `SplHistory.cpp`/`SplAlarms.cpp`
had no caller anywhere in `app/src` before this task, so had never needed to
be there). `ctest` on the same tree still reported "954/955 passed" with one
"Not Run", because the JUCE-free `rtatool_analysis_tests` target that most
tests live in does not depend on those three broken `.vcxproj`s and built
fine regardless — a mostly-green ctest tally hid a dead app binary.

**Why:** redirecting a command's entire output into a file and then checking
the *driving shell's* exit code or its own separate stdout capture answers "did
the shell script run", not "did the command succeed". The two only coincide
when nothing between the command and the `echo`/notification can itself
succeed independently of it — here, `; echo "EXIT:$?"` always runs and always
exits 0, so the wrapper always reports success regardless of the build.

**How to apply:** when a command's real output is redirected to a file, read
the *exit code from inside that file* (`echo "EXIT:$?" >> log`, then `tail`
the log) or check the real log's own content (`grep -i "error\|LNK"`) —
never the backgrounding tool's own "completed" status or a stdout capture
that was itself redirected away. A "tests still mostly pass" tally does not
substitute for this when the test binaries and the shipped app are different
targets built from an overlapping but not identical source list: a `ctest`
green does not prove the GUI app you are about to ship still links.
