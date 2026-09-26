---
name: reachability-is-the-linkers-question
description: "Does production call this?" cannot be answered by grep; orphan_check v1 was wrong in both directions after two rounds, v2 asks the MSVC linker (/Ob0 /Gy /OPT:REF /OPT:NOICF /MAP) and cycles, qualified calls and test-only callers fall out for free
metadata:
  type: project
---

`tools/orphan_check.py` exists because L6a built components nothing in the
app called ([[a-component-with-no-production-caller-is-not-shipped]]).

v1 decided "is it called" by searching comment-stripped source for the
name. Two review rounds found false verdicts both ways: 30 flags on main
including `pollSplLogging` (really called), a qualified `X::make()` call
read as a self-definition, a same-file helper flagged, and an A<->B cycle of
two unwired components never flagged. Each patch traded one hole for another.

v2 (PR #39, merged `b562209`) links `rtatool` with `RTA_ORPHAN_LINKMAP=ON`:
`/Ob0 /Gy` compile, `/OPT:REF /OPT:NOICF /MAP` link. The linker discards
everything the entry point cannot reach, so a candidate absent from the map
is an orphan. Tests are not linked into `rtatool`, so a test caller never
counts. The remaining heuristic is only "which lines DECLARE a function",
and it still needed four rounds (attributes, operators, templates, digit
separators `1'000` read as char literals).

**Why:** a regex has no notion of reachability, only of a spelling
appearing somewhere.

**How to apply:** when a question is "is X reachable / used / linked",
ask the toolchain (linker map, symbol table) before writing a text scanner.
Known limits of the linker answer: a never-called virtual override of an
instantiated class reads live through the vtable; a call to a side-effect-free
callee whose result is discarded can be deleted by the compiler (false orphan).
