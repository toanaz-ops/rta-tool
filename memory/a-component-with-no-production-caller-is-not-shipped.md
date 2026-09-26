# A component with no production caller is not shipped

**2026-09-25/26, lane L6a (SPL-pro), Waves 2-4.** Found by PR #27's verifier,
paid for across three more PRs (#29, #31, #32) and eleven verifier rounds
total.

The station-3 implementation plan built and unit-tested every SPL component —
the block accumulator, the Ln histogram, the alarm latch, dose, the
`#key=value` log — and assigned **no task** to wire any of it into the running
app. `enableSplLogging` had no caller anywhere in `app/src`. The publish fold
that was supposed to feed alarms, dose and Ln from the analysis thread left
them empty through Wave 0, and nothing in the suite could see it: every test
called the component directly, and a component called directly by its own
test does not need a production caller to pass.

## Why the suite stayed green

Unit tests for `SplMeter`, `SplAlarms`, `meter::Dose`, and the log pipeline all
construct the type under test and call it themselves. That is correct test
design for a pure component — but it means the acceptance criterion
"`enableSplLogging` compiles and its tests pass" is satisfiable whether or not
`app/src/MainComponent*.cpp` ever calls it. A plan that lists tasks by
component, with no task or acceptance line naming the production call site,
can close every task and still ship a program in which nothing happens.

The wiring gap was not one bug; it was a missing task class, and closing it
took three separate PRs because each layer's wiring turned out to need its own
attention: W2-E1 fixed the analysis thread's per-channel state and publish
fold, W2-E2a fixed the log pipeline's actual start/stop and thread wiring, and
W2-E2b fixed applying calibration to a live session and exporting from one.

## The check this costs

**Every plan row names its production caller.** Not "this function will be
called from the app" in prose — the file and, where possible, the call site:
"`AnalysisThread::feedSpl`, called from `MainComponent::pollSplLogging` on
every audio poll." A row with no caller named is a row whose task is "make
this compile and pass its own test," which is a real but insufficient bar for
anything meant to run.

**A wave does not close until `grep` finds a caller.** For every new public
type or function a wave adds, `grep -rn "<Symbol>" app/src` (excluding
`app/tests`) must return at least one hit outside the file that declares it.
Zero hits outside the declaring file and its own tests is the exact signature
this defect had — findable in seconds, and nobody ran the grep because no task
asked for it.

## Related

- `memory/a-config-field-with-one-value-in-every-fixture.md` — a sibling
  failure mode: a field the code reads and throws away, discoverable the same
  way (ask what actually consumes a value, not what merely declares it).
- `memory/a-fixed-defect-returns-through-the-silent-fallback.md` — once wired,
  a caller can still take the wrong branch silently; wiring existing is
  necessary but not sufficient.
