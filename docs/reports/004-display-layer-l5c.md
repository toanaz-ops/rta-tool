# L5c — the display layer, and the bridge nobody owned

*2026-08-29. Station 5 report for lane L5c. Ten tasks, 37 commits,
`78ef14f..b7801db`, on `claude_desk/continue-6bacde`.*

## The numbers

Measured on the committed tree. **Two configurations, measured separately, and
they must not be added together** — they cover different target sets.

```
ctest --test-dir build-l5c     -C Release   -> 357/357   (RTA_BUILD_APP=ON)
ctest --test-dir build-l5c-off -C Release   -> 317/317   (RTA_BUILD_APP=OFF)
measure_has_no_framework_deps               -> OK, 29 files scanned (21 before)
core_has_no_framework_deps                  -> OK, 70 files (unchanged)
platform_types_has_no_framework_deps        -> OK,  5 files (unchanged)
shots/rta-view.png (md5)   -> d3e698964c8ffd7bcf2148b154f96e3d  (UNCHANGED)
git diff --stat 78ef14f..HEAD -- core/      -> empty
```

Every one of these was reproduced independently by the whole-branch reviewer,
which built both configurations, ran both suites, re-rendered the snapshots and
re-hashed them. `rta-view.png` being byte-identical across the whole lane is the
evidence that `RtaView`'s rendering was not disturbed.

`shots/transfer.png` and `shots/workspace.png` are new and deterministic.
`main-live.png` changed as expected and is **not** reproducible run to run — it
renders from a real-time thread. Do not pin a hash to it.

## What landed

| # | Task | Where |
|---|---|---|
| 1 | Generic weighted vertical split, plus `az_ui`'s first test target | `ui/az_ui/theme/Layout.*`, `ui/tests/` |
| 2 | Pane split 5:3 and the ONE shared frequency axis | `app/src/view/{AxisMetrics,BodeLayout}.h` |
| 3 | Phase decimation: unwrap along bins, wrap at draw | `app/src/view/PhaseDecimator.h` |
| 4 | Coherence → alpha, floored, bridged | `app/src/view/CoherenceAlpha.h` |
| 5 | The dual-FFT reaches `measure::Snapshot` | `app/src/measure/{Snapshot.h,Analyser.*}` |
| 6 | Two rings drained in lock-step; synthetic impairments | `app/src/measure/{PairedDrain,SyntheticImpairment}.h`, `AnalysisThread.*` |
| 7 | Stroking a field, faded per column | `app/src/view/TraceStroke.*`, `StoredTraceLayer.*` |
| 8 | The Bode composite | `app/src/view/{TransferView,TransferRibbon}.*` |
| 9 | Workspace `[pane]` sections, schema v2 | `app/src/trace/Workspace.h`, `SessionCodec.*`, `view/PaneRegistry.h` |
| 10 | The library reaches the plot | `app/src/view/WorkspaceView.*`, `MainComponent.*` |

**Two debts the project carried for two sessions are closed.** The stored-trace
path is wired — `MainComponent` owns a `TraceLibrary` and hands it to every pane.
And `app/` calls the dual-FFT engine, which had been built and tested in `core/`
and never invoked from anywhere: `grep -rn "DualFftEngine" app/` used to return
nothing.

`core/` was not modified. Not one line.

## What the reviewers refuted

Every task was reviewed by an agent with no write tools, and every review found
something. **More than twenty defects were found in the plan itself** — the plan
was wrong far more often than the implementations were. Three were behavioural,
not test gaps, and all three have the same shape:

1. **A NaN guard placed after the reduction that hides NaN.** `columnAlpha`
   sanitised nothing before `decimateToColumns`, whose min accumulation asks
   `v < minValue` — false for NaN. So `{1.0, NaN}` dropped the NaN and painted a
   column containing an unmeasurable bin at **full confidence**, while
   `{NaN, 1.0}` floored correctly. Order-dependent trust.
2. **A precondition checked after the mutation it guards.** `pushPair` validated
   its two spans' lengths inside `DualFftEngine::process`, which runs after both
   spectrum engines are fed and `hasReference` latches — so a "refused" call left
   a half-accepted pair behind.
3. **An assignment with no matching unassignment.** Task 10 assigned channel
   roles on entering SYNTHETIC and cleared none on exit. After a round trip, on a
   **mono** device `drainPaired` returned immediately on a null ring and
   **nothing drained at all**, including the measurement channel. The plot
   freezes, with no channel-1 row in the table to clear the role from.

The lane's own conclusion: **what gets added is examined; what has to be undone
is not.**

A fourth, found only by the whole-branch review because it lives *between*
tasks: `bridgeGaps` and `columnAlpha` were each correct and each tested, but
`columnAlpha`'s comment encoded an assumption about `hasData` that `bridgeGaps`
invalidated the moment they met — producing a visible barcode on the live trace
at the LF end. Its fix is recorded in the decision record as §5a, because it was
an interaction the record had not decided.

## Nine claims that a test caught something, which it did not

The project's standing lesson is that the right question about a test is not
"is it green" but "what wrong implementation would make it red". Applying it
found nine tests in this lane that asserted less than they claimed:

- a layout test whose rectangle sat at the origin, so an implementation ignoring
  `area.getY()` stayed green;
- a 5:3 tolerance so tight it **failed on correct code** at untested heights
  (`cross = −(5a mod 8)` ranges to 7; the bound was 5);
- a clamp deletable with every assertion still passing;
- twice, a flag reset placed at the one position where it is a no-op;
- a fixture that was **invariant under the very transformation** it claimed to
  detect (a flat phase trace, unchanged by bridging);
- an "independent reimplementation" that was textually identical to the thing it
  checked;
- a dimness assertion that checked presence;
- a tripwire that compiles out under NDEBUG, in a project that builds Release.

Every one was found by running against broken code. None was found by reasoning.

## Traps for the next lane

1. **`ctest -R` cannot select a Catch2 tag.** `catch_discover_tests` names each
   case by its `TEST_CASE` *sentence*. A tag matches zero tests and **exits 0**.
   Use the binary directly for tags (`rtatool_analysis_tests.exe "[tag]"`), keep
   `ctest -R` for guards and the `TEST_PREFIX` targets (`^ui/`, `^view/`), and
   read the case **count**, because "0 tests passed" looks exactly like success.
   This plan documented that trap and then walked into it in six of its own
   verification steps.
2. **The framework guard passes while watching nothing.** It globs a
   caller-supplied path list and only fails hard when the *whole* list is empty,
   so one mistyped path silently stops it watching a file. Register a file, then
   read the scanned count back.
3. **Do not background a long build in a subagent.** A subagent is not woken by
   the completion notification the way the coordinator is; it will wait forever.
4. **New sources belong in every target that needs them** — `rtatool`,
   `rtatool_snapshot`, `rtatool_view_tests`. Building only the test target hides
   the other two. This was missed twice.

## Debt, triaged and none of it blocking

Full triage in `docs/HANDOFF.md`. The largest by far: **`MainComponent.cpp`
links into no test binary**, so the synthetic-role reset has no automated
coverage. The reviewer checked whether extracting a free function would close
that and concluded it would not — the untested risk is the *call site*, not the
reset logic. Closing it needs a test target that can construct `MainComponent`
without an audio device.

## Open for the owner

Three questions, all in `docs/dsp/2026-08-29-display-layer-l5c.md`: the pane cap
and named workspaces (§ question 1), the spectrograph colour ramp (question 2),
and — new from this lane — **whether unwrap should show stored phase traces at
all** (question 3, §5a). With several stored captures at different delays the
axis could stretch over thousands of degrees and flatten the live trace to a
line, so it is really a question about whether one pane can hold two delays.

Nothing here waits on a purchase.
