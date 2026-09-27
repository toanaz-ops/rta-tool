# STORE lane — station-3 plan

*2026-09-27. Follows `docs/research/2026-09-27-store-trace.md`. Owner
decisions 2026-09-27 (`docs/plans/MASTER-EXECUTION-PLAN.md`): STORE is the
next lane, and every plan task below names its production caller (rule 5)
and its operator path (rule 5's operator-path sub-bullet).*

## Recommended design, in five lines

One global **STORE** button (RTA/TRANSFER rail, beside EXPORT REPORT),
enabled on RTA and TRANSFER, disabled on SPL/XOVER. On click it reads
`analysisThread_.latest()` on the message thread, converts the fixed-FFT
data the current pane shows (`spectrumDb` on RTA; `transfer` on TRANSFER,
refusing with a readout if no reference is fed) into a `Trace` — degrees to
radians at the one crossing, a fresh monotonic id, `CaptureMeta` assembled
from the `Snapshot` plus a new `AnalysisThread::config()` getter — and calls
`library_.add(trace, "<PANE> @ HH:MM:SS", "<PANE>")`. No new adjustable
knobs on `Trace`/`TraceLibrary`: post-hoc delay/gain/polarity trims already
exist one layer up, in `VirtualTrace`.

## Owner decisions — 2026-09-27

Answered in the orchestrating session after reading this plan and the
research. Each was the recommended option:

1. **Freeze the fixed-FFT data, and say so.** TRANSFER's three panes default
   to MTW (`app/src/view/TransferView.h:132`), and `MtwBlock` is live-only
   (`app/src/measure/Snapshot.h:96-101`). So the readout after STORE must
   name the engine it froze ("STORED (FIXED FFT)"). When the pane being
   stored is showing MTW, it must add that the screen shows MTW. MTW storage
   is a later lane that needs the L5 frequency-vector amendment.
2. **One global STORE button, branching on the current pane.** It is enabled
   on RTA/TRANSFER and disabled on SPL/XOVER.
3. **No adjustable knobs on `Trace`.** Post-hoc trims use `VirtualTrace`.
4. **Defaults accepted as written:**
   - the name is `<PANE> @ HH:MM:SS`, and the group is the pane label;
   - no cap and no keyboard shortcut;
   - `calibrationOffsetDb` is 0 / dBFS outside SPL calibration.

## Station-3 task table

| task | what it builds | called from | operator path | acceptance | mutant that must go RED |
|---|---|---|---|---|---|
| **T1 — `AnalysisThread::captureConfig()` getter** | Shipped as `[[nodiscard]] CaptureConfig captureConfig() const noexcept` (`AnalysisThread.h`, beside `latest()`), returning a small `rta::measure::CaptureConfig` BY VALUE -- corrected in fix round 1 (MEDIUM F2) from an earlier `const Analyser::Config& config()` that returned a reference into `baseConfig_`, which `applyPendingReferenceDelay()` (the analysis thread) mutates live on APPLY | `storeClicked()` (`app/src/MainComponentStore.cpp`) is the one caller — `analysisThread_.captureConfig()` is called INLINE as an argument to `CaptureConverter::traceFromSnapshot(...)` (`MainComponentStore.cpp:120`); `traceFromSnapshot` itself never calls `captureConfig()`, it only consumes the already-fetched `CaptureConfig` value as a parameter (R5, PR #51 round-2 review) | none directly — an operator never calls this; it exists so `traceFromSnapshot` has one | a unit test constructs `AnalysisThread` with a non-default `Config` (distinct `window`/`averaging`/`transferAveraging`/`transferFifoDepth`) and asserts `captureConfig()` returns those exact values, not `CaptureConfig{}`'s defaults; a `static_assert` in `AnalysisThread.h` additionally fails to COMPILE if the return type is ever changed back to a reference | change the getter to `return CaptureConfig{};` — the test must fail |
| **T2 — degrees→radians conversion, one function** | `std::vector<float> degToRadPhase(std::span<const float> phaseDeg)` (`app/src/trace/CaptureConverter.h/.cpp`), reusing `180.0/std::numbers::pi` the same literal `AnalyserPublish.cpp:21` already uses (inverted) | T5 | none — a pure conversion function | closed-form identity: `degToRadPhase({0, 90, -90, 180})` == `{0, pi/2, -pi/2, pi}` within float epsilon; round-trip identity `radToDeg(degToRad(x)) == x` for `x` in `{-179.9, -90, 0, 90, 179.9}` (the one existing rad→deg helper, `AnalyserPublish.cpp`'s inline loop, stays the round-trip partner — no second constant is introduced) | flip the constant to `pi/180.0` (radians-as-degrees) — the identity test must fail |
| **T3 — monotonic capture id** | `std::string nextCaptureId()` (`app/src/trace/CaptureConverter.cpp`), a function-local `static std::atomic<std::uint64_t>` counter, same shape as `TraceLibrary`'s own `nextGeneration()` (`TraceLibrary.cpp:13-16`) | T5 | none — internal | 1000 sequential calls return 1000 distinct strings, including when called twice with no time advancing (fake-clock-proof by construction — nothing here reads a clock) | replace the counter with `std::chrono` millisecond timestamps — a tight loop calling it twice inside one millisecond must produce a collision the test catches |
| **T4 — Snapshot/pane → `Trace` conversion** | `std::optional<rta::trace::Trace> traceFromSnapshot(const Snapshot&, PaneView pane, const CaptureConfig&, std::string deviceName, std::string channelRoles)` (`app/src/trace/CaptureConverter.h/.cpp` -- `CaptureConfig` per fix round 1 MEDIUM F2, not the full `Analyser::Config` first sketched here; framework-free — no JUCE include, so it can sit beside `TraceLibrary` under the `measure_has_no_framework_deps`-equivalent gate for `trace/`) | T5's `storeClicked()` | none directly — a pure function over the caller's `Snapshot`/pane; the operator path is T5's button | RTA pane: given a synthetic `Snapshot` with a known `spectrumDb`, the returned `Trace` has that exact magnitude vector and `has(Field::Phase)==false`. TRANSFER pane: given a synthetic `Snapshot` with `hasReference=true` and a known `transfer` block, the returned `Trace`'s magnitude equals `transfer->magnitudeDb`, phase equals T2's conversion of `transfer->phaseDeg`, coherence equals `transfer->coherence` when present and is empty when absent (never a zero-filled stand-in — `Trace.h:17-19`'s own rule). TRANSFER pane with `hasReference=false`: returns `std::nullopt`. The returned `CaptureMeta.appliedDelaySamples` equals `transfer->appliedDelaySamples`, so XOVER and `VirtualTrace` know the phase reference the trace was frozen under. Assert this with a non-zero delay in the fixture. | drop the `hasReference` check — a `nullopt`-should-refuse case must now wrongly build a `Trace` from empty data, and the test must catch it |
| **T5 — the STORE button** | `storeButton_` (`juce::TextButton`, `MainComponent.h`, beside `exportReportButton_`), `storeClicked()` -- shipped in its own file, **`MainComponentStore.cpp`** (this file's own 400-line-cap split, same shape as `MainComponentSpl.cpp`/`MainComponentPanes.cpp`, not `MainComponentSession.cpp` as first sketched), beside a readout label (`storeReadout_`) following the `exportReportReadout_` convention | `MainComponent::MainComponent()` (`MainComponent.cpp`) wires `storeButton_.onClick = [this] { storeClicked(); }`; `storeClicked()` is the first-ever call to `library_.add(...)` that carries a **live** capture rather than a session replay | **the STORE button itself, in RTA or TRANSFER pane** — this task's own operator path | `test_main_component_store.cpp`: (a) RTA pane, running (or SYNTHETIC) input, click STORE, assert `library_.entries().size()` grew by 1 and the new entry's trace has no phase; (b) TRANSFER pane with a reference fed, click STORE, assert the new trace `has(Field::Phase)`; (c) TRANSFER pane with no reference, click STORE, assert the library is unchanged and the readout states the refusal; (d) two STORE clicks in the same pane produce two distinct trace ids (T3's guarantee, exercised end-to-end); (e) the readout names the frozen engine ("FIXED FFT"), and on TRANSFER names EXACTLY the panes currently showing MTW (fix round 1 HIGH F1 correction: per-pane, via `TransferView::effectiveSource`, never a constant -- an operator can toggle any pane to FIXED, or MTW can be unavailable outright) | delete the `hasReference` guard in `storeClicked()` (not T4's copy — the call site's own check, if one is added defensively) — case (c) must go RED; separately, reverting the MTW clause to an unconditional append — the "all panes FIXED: no clause" case must go RED |
| **T6 — button enable/disable by pane** | `storeButton_.setEnabled(...)` call inside `selectPaneView()` (`MainComponentPanes.cpp:67`, beside the existing toggle-state sync at `:86-90`) | `MainComponent::selectPaneView` | switching to SPL or XOVER via the pane selector buttons | a pane-switch test asserts `storeButton_.isEnabled()` is true on Rta/Transfer and false on Spl/Xover, for every pane reached both by a button click and by session Open's `restorePaneView_` path (`MainComponentPanes.cpp:101-136`, which has its own history of missing a toggle-state line — see the `MainComponentPanes.cpp:133` comment on a prior omission) | remove the `restorePaneView_` half of the wiring, leaving only the button-click half — a test that opens a session directly into SPL/XOVER must catch the still-enabled button |
| **T7 — naming/grouping defaults** | `"<PANE> @ HH:MM:SS"` name, `"<PANE>"` group, built in `storeClicked()` from `currentPaneView()` and `juce::Time::getCurrentTime()` | `storeClicked()` (same call site as T5) | the STORE button, same press | two STORE clicks at least one second apart produce two distinct, non-empty names; the group string exactly matches the pane label used elsewhere in the UI (`paneRtaButton_`/`paneTransferButton_`'s own text, so a rename of those buttons cannot silently diverge from the group name) | hardcode the same name string on every call — the distinctness assertion must go RED |
| **T8 (human-try row)** — nothing new to build; this is the end-to-end proof the whole lane exists for | — | — | **In `rtatool.exe`: start SYNTHETIC or a live device with a reference channel routed, switch to TRANSFER, press STORE, wait a moment, press STORE again. Switch to XOVER. Pick the two just-stored traces as HP and LP in the pane's own combo boxes. See the target/summation line the pane already draws.** | this is the acceptance — a human sees it, screenshotted via `tools/snapshot.cpp` per CLAUDE.md's "Seeing the GUI" section for the report, not asserted by a mutant | N/A — this row proves the *chain*, not one function; T4's and T5's mutants already cover the links |

## What this plan deliberately does not build

- **MTW storage.** Needs the L5 frequency-vector amendment
  (research C1); named, not scheduled.
- **A calibration-offset source for RTA/TRANSFER STORE.** Research C3:
  `CaptureMeta.calibrationOffsetDb` stays `0.0f`/`DbFs` outside SPL
  calibration, on purpose, rather than inventing one.
- **Any adjustable delay/gain/polarity/coherence-ignore knob on `Trace`
  or `TraceLibrary`.** That capability already exists in `VirtualTrace`
  (research C3); duplicating it here was the design this plan argued
  against.
- **A cap or eviction policy on `TraceLibrary`.** Research C4: none exists
  today and none is added by this lane.
- **A keyboard shortcut for STORE.** Research C4: no button in the app has
  one; adding one only for STORE was not justified by anything found.
