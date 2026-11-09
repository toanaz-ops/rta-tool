# H8 review fixes

*2026-11-09. Findings: `docs/reviews/2026-11-09-h8-review-fable.md` plus the UI test (`shared/reports/2026-11-09-ui-tests/rta-h8-ui-test.md`). Everything new is **unmeasured** on hardware.*

| id | task | called from (production caller) | operator path | proof |
|---|---|---|---|---|
| M-1 | prominence rms over the searched window, not the whole deconvolution | `SweepIrPipeline.cpp::findPeak`, used by every SWEEP / ALIGN / RT60 | press SWEEP (rail) | test: 30 dB louder echo past the window is refused as "ambiguous", not silently |
| M-4 | **PROVISIONAL, owner card HW-184:** accept the windowed peak only within 12 dB of the global peak (`kMaxWindowedBelowGlobalDb`), else `ArrivalOutOfWindow` whose message says the arrival is ambiguous | same | press SWEEP (rail) | tests at +11 dB (accepted) and +13 dB (refused) |
| M-2 | notice when the API is enabled but not running (title bar + log) | `MainComponentApi.cpp::parentHierarchyChanged` | launch with `RTA_API_ENABLED=1` and the port already taken | `apiNotRunningText` test; smoke: window title |
| M-3, L-4 | docs: no token = unauthenticated local read; silent env fallbacks | `HANDOFF.md` H8 block, `MASTER-EXECUTION-PLAN.md` row 12 | n/a (docs) | read |
| L-1 | low-SNR hint only on the two "no peak" refusals; reuses `shippedSweepConfig` | `MainComponentSweep.cpp::finish` -> `lowSnrHintApplies` | lower GEN below -12, press SWEEP | positive and negative tests |
| L-2, L-3 | constant-time bearer compare; ASCII-only env parsing | `ApiPolicy.cpp`, `ApiSettingsFromEnv.h` (MainComponent constructor) | `RTA_API_TOKEN` set, `curl -H "Authorization: Bearer ..."` | tests |
| L-5, L-6 | doc corrections (master plan line 139, H8 plan c1, sweep plan refs) | docs | n/a | read |
| L-8 | picker id 0 clamps to slot 0 | `MainComponentRail.cpp` picker `onChange` -> `TfSlotPicker.h::tfSlotForPickerId` | clear/select the TF picker | `[tf_slot_picker]` pure test; `[main_component_rail]` clear-to-id-0 case (mutation: raw `id - 1` goes RED) |
| 6a | minimum window size and a default that fits the work area | `Main.cpp::MainWindow` -> `WindowSizing.h` | launch on a 1366x768 screen; drag the window narrower | `windowSizingFor` tests; smoke: GetWindowRect vs work area; `test_main_component_small_screen.cpp`: masthead GEN/OUT on screen and DEVICE/START reachable by scroll at 1300x698 and 1100x610 content. FINDING: START is NOT visible without scrolling at 1366x768 (rail viewport 168 px tall, START shows ~12 of 62 px; hidden at the minimum) -- owner decision pending, locked by a REGRESSION LOCK case |
| L-7 | `RoutingMatrix::onCellClicked` sets role then tf as two writes | `RoutingMatrix.cpp` | n/a | comment; one-tick torn pair: that tick plans from the old tf and pushes its hops by route position -- the same shift any role change causes (no Analyser rebuild on role change; `configEpoch` has no consumer) |
| 6b | SPL empty-state explanation | `view/SplView.cpp::paint` -> `SplEmptyState.h` | open the SPL pane with SYNTHETIC on | text test |
