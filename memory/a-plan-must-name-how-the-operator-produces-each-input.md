---
name: a-plan-must-name-how-the-operator-produces-each-input
description: orphan_check.py proves "the app's entry point can reach this function" — it never asks "what button does an operator press to produce the value this function receives". Five lanes (Save/Open, XOVER, L7-EQ-UI, L7-ALIGN-UI, any RT60 UI) planned UI that consumes a stored trace or a captured IR while no lane built the STORE or Sweep→IR path that produces one.
metadata:
  type: feedback
---

**2026-09-27, plan review.** `docs/plans/MASTER-EXECUTION-PLAN.md`'s
2026-09-26 wiring-debt pass (`memory/a-component-with-no-production-caller-is-not-shipped.md`)
fixed "the linker cannot reach this function" by adding `tools/orphan_check.py`
and a plan rule that every task names its production caller. One day later,
a plan review at `origin/main` `e2c67a1` found the next layer of the same
mistake, one level up: the caller check answers "does code call this code?",
never "does an operator have a way to produce the DATA this code needs?".

## What was actually missing

Two whole producer lanes are absent from the plan, and five lanes downstream
of them were scheduled as if the inputs already existed:

- **STORE** — nothing freezes the live measurement into `TraceLibrary`.
  `TraceLibrary::add` (`app/src/trace/TraceLibrary.cpp:60`) has zero
  production callers; only tests call it. The full button list is
  `SYNTHETIC, LOCATE, APPLY, CAL START/END, EXPORT REPORT, RTA/TRANSFER/SPL`
  (`app/src/MainComponent.h:244-370`) — no STORE, no freeze.
- **Sweep→IR** — nothing plays a sweep, captures it, and deconvolves it in
  `app/src`. `git grep -liE "rt60|schroeder|lundeby" origin/main -- app/src`
  returns empty: the whole sweep/IR/RT60 stack (P4, lanes L4a+L4b) lives only
  in `core/`. `EqSession.h:9-10` says outright it is a dev-preview specimen,
  not a `MainComponent` binding.

Five consumers were planned or shipped against these non-existent producers:
Save/Open (PR #43), the XOVER pane (PR #45), L7-EQ-UI, L7-ALIGN-UI, and any
RT60 UI. Every one of them can compile, link, pass its tests, and even show
`orphan_check.py` green — because the check only asks whether `app/src`'s
entry point reaches the code. It says nothing about whether a human, sitting
at the running `rtatool.exe`, can put a value into that code's input.

## Why the existing check could not catch this

`orphan_check.py` (`memory/reachability-is-the-linkers-question.md`) reads the
MSVC `/OPT:REF` linker map and asks one question: is this symbol reachable
from `main()`? A function fully wired into `MainComponent` — called on a
button press, storing into a real field — answers that question exactly the
same as a function that reads an empty `TraceLibrary` nobody ever populated,
because the check only ranges over the call graph, never over the data that
graph is supposed to move. "Every task names its production caller" (plan
rule 5) inherited the same blind spot: naming the function that calls
`TraceLibrary::get()` says nothing about who calls `TraceLibrary::add()`.

## The check to add

For every plan task, name the **operator path**: the button or action a human
uses, in the running app, to produce the runtime input the task consumes —
not the function that will call the task's code, but the human action that
puts real data where that function can find it. A task whose input has no
operator path must either build one or explicitly name the later lane that
will. `docs/plans/MASTER-EXECUTION-PLAN.md`'s task tables now carry this as an
"operator path" column, and CLAUDE.md's "Before each phase" rule 5 records it
as a sub-bullet. "BUILT" now means an operator can reach the feature in the
app, not merely that other code calls it.

## Related

- `memory/a-component-with-no-production-caller-is-not-shipped.md` — the
  code-reaches-code layer of this same mistake, found one day earlier by
  `orphan_check.py`; this file is the operator-reaches-data layer the tool
  cannot see.
- `memory/reachability-is-the-linkers-question.md` — what `orphan_check.py`
  actually measures, and why that measurement stops at the call graph.
