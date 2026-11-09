# H8 -- reactivation lane: TF slot picker, API enable switch, H7 follow-ups

*2026-11-09. Owner reactivated development after the H7 pause (2026-10-22). Code read at `origin/main` `6f1417f`. Everything new here is **unmeasured** until the hardware session (no mic, no standards purchase needed for this lane). Every task names its production caller and its operator path (CLAUDE.md rules 5 and 6).*

## Scope

In: master-plan row 11 (TF slots unreachable), row 12 (API has no switch), H7 follow-ups M1, N1, N3.
Out (owner): multi-pane selector, XOVER delay, L4c, MTW v4, anything needing a mic or a standards purchase. Not touched: the 14 leftover worktrees (owner card pending).

## Tasks

| id | task | called from (production caller) | operator path (what a human does to produce the runtime input) | proof |
|---|---|---|---|---|
| a | TF slot picker: a ComboBox "TF 1".."TF 8" in the left rail above the routing matrix; `onChange` calls `RoutingMatrix::setActiveTransferFunction(id - 1)`. A ROLE cell of a channel on slot > 0 reads `MEAS TF3` / `REF TF3`, so the assignment can be read back. | `MainComponentRail.cpp` constructor (`tfSlotPicker_.onChange`) -> `RoutingMatrix.h:84`; `RoutingMatrix.cpp::refreshFromConfig` | Pick a slot in the TF picker, then click a channel's ROLE cell (UNUSED -> MEAS -> REF). The click tags the channel with the picked slot. | `test_main_component_rail_layout.cpp` (picker items, retarget, layout), `test_routing_matrix.cpp` (label) |
| b | API enable switch: `apiSettingsFromEnvironment()` (header-only, `app/src/api/ApiSettingsFromEnv.h`) reads `RTA_API_ENABLED` (1/true/on/yes), optional `RTA_API_PORT`, `RTA_API_TOKEN`; bind address is NOT configurable (stays 127.0.0.1, `startRefusal` unchanged). Default OFF: no variable set = behaviour identical to today. | `MainComponent.cpp` constructor, where `ApiSettings apiSettings;` was hard-coded | Set `RTA_API_ENABLED=1` in the shell, launch `RTA Tool.exe`, `curl http://127.0.0.1:4736/...` (routes in `docs/dsp/2026-09-16-remote-api.md`). | `app/tests/test_api_settings_env.cpp` |
| c1 | M1: `SweepIrPipeline` `findPeak` searches a window, not the whole deconvolved capture. | `SweepIrPipeline.cpp` (SWEEP / ALIGN / RT60 all go through it) | Press SWEEP (rail) with SYNTHETIC on. | new pipeline test: a clean, louder echo past the window (venue noise in the pre-roll/tail is NOT covered by it -- H8 review L-6) |
| c2 | N1: SWEEP refusal text names low SNR / the level when the level is below the working floor. | SWEEP readout in `MainComponentSweep.cpp` | Lower GEN level, press SWEEP, read the readout. | refusal-text test |
| c3 | N3: remove the 100 ms sleep from the destructor-order test. | test only | n/a (test hygiene; no runtime input) | the test itself, repeated runs |

## Rules for the lane

- One PR per task a, b, c. Plan doc rides with a.
- Row 11 flips BUILT(partial) -> stays partial only for `CaptureSequencer` (still deferred); row 12 flips CORE ONLY -> BUILT only with the env path as its operator path (a human can enable it without rebuilding).
- No network exposure change by default: default OFF, loopback only, no new bind option.
- Not verified on hardware: slot assignment across multiple real inputs; label `unmeasured`.
