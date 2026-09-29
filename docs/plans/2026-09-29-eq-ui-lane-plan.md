# EQ-UI lane (L7-EQ UI): station-3 plan

*2026-09-29. Checked against `origin/main` `d06ab21`; the worktree tree is identical (`git diff HEAD origin/main` is empty). This lane wires `EqSession`, `EqVerify`, `FirTextWriter`, `FirWavWriter` and `EqTextExport.h` into `rtatool`. The governing records are `docs/dsp/2026-09-06-l7-auto-eq.md` (the "EQ record") and `docs/dsp/2026-09-06-l7-fir-export.md` (the "FIR record"). CLAUDE.md rules 5 and 6 apply to every row. The table format follows `docs/plans/2026-09-27-store-lane-plan.md`.*

*Owner defaults in this plan adopted 2026-09-29 under the owner's "execute
autonomously" instruction (plan review #2, docs/plans/MASTER-EXECUTION-PLAN.md).
Any row can still be overridden before its task is built. T0 (expose the
auto-offset) is adopted.*

## Goal

The operator path this lane builds:

1. STORE on the TRANSFER pane, with a reference fed.
2. Open the **EQ** pane.
3. Pick a measurement trace, and optionally a target trace.
4. Press AUTO EQ, or SUGGEST and then ACCEPT or DECLINE each chip.
5. See the measured, predicted ("ghost") and correction curves.
6. EXPORT FIR (text or WAV), or EXPORT LIST.
7. On a live rig: VERIFY, then ADOPT.

None of this is reachable today:
- `app/cmake/rtatool_sources.cmake` lists no Eq or Fir source. So `EqSession.cpp`, `EqVerify.cpp`, `FirTextWriter.cpp` and `FirWavWriter.cpp` are orphans.
- `EqSession.h:9-10` still sends the reader to a dev-preview specimen (EQ-R4).
- That specimen was never built: `git log --all -- 'app/src/dev/preview/EqPreview*'` is empty.

## What the shipped API allows, and the one core change

**No core change is needed for the operator path itself.**
- `setMeasurement` (`EqSession.h:100-102`) takes `hz`, measured, target, coherence, complex H and `fs`. A stored `Trace` supplies all of them:
  - `hz_k = k·binHz()` (`Trace.h:99-101`);
  - phase in radians, and coherence (`Trace.h:85-92`).
- Suggest (`:109`), Auto EQ (`:116`), accept/decline/markApplied (`:120-127`) and the ghost (`:148`) already exist.
- FIR design has a per-bin half-grid overload (`FirDesign.h:89-92`).
- VERIFY is a complete state machine (`EqVerify.h:110-150`).

**Coherence is mandatory.**
- The allocator's `validate` requires a coherence vector the same length as `hz` (`EqAllocator.cpp:18-19`).
- `EqSession` passes an empty span when the lengths differ (`EqSession.cpp:101-102`), then turns the resulting throw into an empty ranking (`:118-125`).
- So an RTA-pane STORE trace, which has no coherence, can be a **target** but never a **measurement**.

**App-side gap: the band cap cannot change.** `EqSessionConfig` is set only in the constructor (`EqSession.h:84`), and `config()` is a getter with no setter (`:140`). The record wants the N cap on a UI control (EQ record §2), so T1 adds a setter.

**Core gap: the auto-offset `c` is private.** It lives in the anonymous namespace of `EqAllocator.cpp` (`:14`, `:32`). Without it:
- the target line cannot be drawn where the allocator actually aimed;
- VERIFY's residual RMS is plain `measured − target` (`EqVerify.cpp:75-78`). Against a FLAT target that reports the absolute dBFS level, not the deviation.

T0 exposes `c`. That is a core change, so it needs its own EQ-record §3 amendment, written after the code (rule 6).

**If the owner declines T0:** T5 draws no target line and T7 reports only the flagged count, saying why. The operator path still works either way.

## Owner-default decisions

| # | decision | default | justification |
|---|---|---|---|
| D1 | Pane | A fifth selector button, `EQ`, backed by `PaneView::Eq` and built through the same factory seam as XOVER (`PaneFactory.cpp:19-24`). Layout: controls column on the left. On the right, a dB chart (measured, ghost, and target + c) above a correction strip showing ΣR. Untrusted bins are shaded; declined bands are hatched. | EQ record §2: the predicted trace is drawn as a ghost. §7: `ghost_k = m_k + ΣR_i` over filters not yet applied (`EqSession.h:57`). |
| D2 | Measurement picker | Offers visible traces that carry coherence. Starts "not asked", with no default. | No coherence means every bin is untrusted (`EqTrustMask.h:32-44`). The "not asked" start follows XOVER (`CrossoverPaneView.h:17-19`). |
| D3 | Target | **FLAT** by default; the alternative is any visible stored trace. Tilt, hand-drawn and file-import targets are deferred. | EQ record §5 ships FLAT with auto-offset and a reference trace now. |
| D4 | Max bands | 6, adjustable 1..16 | EQ record §2 (default 6, "capped by a UI control"); §3 (`N ≤ 16`); `EqSession.h:37` |
| D5 | Boost limit and Q caps | +6 dB, shown as a readout, not a control. Q caps 10 (boost) and 20 (cut). `roomT60Sec` stays unset. | EQ record §3 labels these as judgements; §12.2 leaves them open (`EqSession.h:38-41`). No producer of RT60 exists in the app (master plan, "Plan review — 2026-09-27", row "any RT60 UI"). |
| D6 | Number of chips | 3 | EQ record §2: "top 1–3" |
| D7 | Trust floor | 0.7, a constant | `EqTrustMask.h:30` |
| D8 | FIR phase | No default. EXPORT stays disabled until the operator answers. | FIR record §4: "no default the UI pre-selects silently" |
| D9 | FIR length | No default. The picker offers 1024, 4096, 6144 and 8192 taps and prints `fs/N` and `(N−1)/(2fs)` beside each. | FIR record §9 prints both numbers; §11 chooses no default. These four rows are the §2 table's entries in the "below ~8k taps" convolver regime. 65536 is excluded: its minimum-phase design is the 16.8M-point transform §11 flags, and it would run on the message thread. |
| D10 | FIR sample rate | The measurement trace's own `meta.sampleRate`, with no resampling | `responseDb` takes `fs`, so the filter specs exist at that rate. FIR record §5 writes the rate in three places. |
| D11 | What the FIR realises | ΣR over filters **not** marked applied, i.e. ghost − measured | This is `EqSession.h:48-64`'s membership rule. Exporting applied filters would put their correction in the rig twice. |
| D12 | Normalisation | `as_designed`, with `peak_0dbfs` as a toggle | FIR record §5 |
| D13 | File dialog | Async `FileChooser` in save mode with the overwrite warning. Default path `Documents/RTA Tool/fir/<stem>.txt|.wav`, where the stem comes from `firFilenameStem` (`FirExport.h:46-51`). Cancel does nothing. The write happens on the message thread, as UTF-8 with no BOM. | Follows `MainComponentSession.cpp:76-86`. No BOM because of EQ record §7 amendment 3. |
| D14 | VERIFY | LIVE device only. Plays pink noise at −12 dBFS on output 0. Corridor 3 dB, 3σ. | `EqVerify.h:46-47`, `:113`, `:120`. SYNTHETIC mode stops the device (`MainComponent.cpp:159`). |
| D15 | T0 (expose `c`) | Recommended | Without it the FLAT-target VERIFY RMS is the absolute level, not the deviation (see above). |

## Station-3 task table

| task | files | called from | operator path | acceptance (closed-form or record-derived; mutant) |
|---|---|---|---|---|
| **T0 — expose auto-offset** | `core/include/rta/eq/EqAllocator.h`, `core/src/eq/EqAllocator.cpp` (move `autoOffset` out of the anonymous namespace as `autoOffsetDb`; no arithmetic change); `EqSession.h/.cpp` gain `levelOffsetDb()`; EQ record §3 amendment after the code | `EqPaneModel::targetLineDb()` (T2); the VERIFY target in `MainComponentEq` (T7) | AUTO EQ, VERIFY | Two trusted bins at f = 100 and 200 Hz, γ² = 1, r = 0 and 3. The weights are 0.01 and 0.005, so c = **1.0** exactly. `gen_autoeq.py --check` stays unchanged. **Mutant:** an unweighted mean gives 1.5 and must go red. |
| **T1 — band cap setter** | `EqSession.h/.cpp`: `setMaxFilters(int)`, clamped to [1,16] | `EqPaneModel::setMaxFilters` ← the BANDS combo | BANDS combo | Three separated bumps (`EqSessionFixture` `bumpDb` at 250 Hz, 1 kHz and 4 kHz). With `setMaxFilters(2)`, `runAutoEq` commits exactly 2. Out-of-range inputs clamp: 0 → 1, 17 → 16. **Mutant:** a no-op setter commits 3 and must go red. |
| **T2 — pane model** | `app/src/measure/EqPaneModel.h/.cpp` (JUCE-free; add to the `measure_has_no_framework_deps` GLOBS) | the EQ pane's combos (T5). Owned by `MainComponentEq` (T6), so it survives a pane rebuild (`MainComponentPanes.cpp:101-109` rebuilds the pane on every switch). | measurement and target combos | A `Trace` at fftSize 64 and 48 kHz has 33 bins, and `hz_k == k·750` bitwise. Coherence 0.69 on bins 0–16 and 0.70 on bins 17–32 gives 16 trusted bins (`>=`, `EqTrustMask.h:48`). A bin with m = 20·log10(0.5) dB and φ = π/2 reconstructs to \|H\| = 0.5 and arg H = π/2, within 1e-6. A same-grid target is copied bitwise. A −3 dB/oct tilt on another grid resamples to −3·log2(f/f0) within 1e-4 dB; the log-linear rule (`FirDesign.h:63-69`) is exact for a tilt. A trace with no phase gives an empty H and a readout "G24 gate off" (the fallback in `EqGainSolve.h:32-43`). A trace with no coherence is absent from the measurement list and present in the target list. **Mutant:** reading stored radians as degrees must fail the arg check. |
| **T3 — FIR half-grid** | `app/src/measure/EqFirDesign.h/.cpp` (JUCE-free) | `MainComponentEq::exportFir*` (T6) | EXPORT FIR TXT or WAV | At 48 kHz and N = 4096: M = 32768, per FIR record §2 (`M ≥ 8N`) and `FirDesign.cpp:303`'s own rule, giving 16385 bins. Peaking filter, +6 dB, fc 1500 Hz, Q 2: fc lands exactly on bin 1024 (1024·48000/32768 = 1500). There \|H\| = 10^(6/20) within 1e-5. \|H\| at DC and at Nyquist = 1 within 1e-6 (R = 0 at both ends, EQ record §3). An empty filter set gives every bin exactly 1.0f. `designFftSize` == 32768, and `groupDelaySamples` follows `FirDesign.h:41`. **Mutant:** summing applied filters too makes an applied +6 dB fixture read 1.995 at bin 1024; it must go red. |
| **T4 — pane vocabulary** | `PaneRegistry.h` (`Eq` / `"eq"`), `PaneSelectorDecision.h`, `PaneFactory.h/.cpp` (Eq branch; the factory also takes an `EqPaneBinding`), `MainComponentPanes.cpp` (fifth cell), `MainComponent.h` (`paneEqButton_`) | `selectPaneView`, `restoreWorkspaceFromSession` | EQ button; OPEN SESSION of a saved `eq` pane | The round-trip loop over five enumerators passes (extends `test_pane_selector_decision.cpp:67`). The factory builds `EqPaneView` for `Eq`. STORE is disabled on the EQ pane through both paths (`MainComponentPanes.cpp:123-124` and `:175-176`). **Mutant:** deleting the `"eq"` arm in `paneViewName` must turn the round trip red (the same shape as the mutant at `:62`). |
| **T5 — pane view** | `app/src/view/EqPaneView.h/.cpp`, `app/src/view/EqChartRenderer.h/.cpp` | `makePaneFactory`'s Eq branch | STORE → EQ → pick → AUTO EQ or SUGGEST → ACCEPT, DECLINE, APPLIED | With an empty library, the refusal names "STORE on TRANSFER with a reference". With the 8 dB bump fixture (`makeLinearFixture`) injected, picked in the real combo and AUTO EQ clicked on the real button: at least 1 filter is committed, and \|ghost − target\| at the bump is less than \|measured − target\|. That is the `test_eq_session.cpp:95` invariant, now reached through the button. SUGGEST shows min(3, ranked) chips. After DECLINE on the top chip, the next SUGGEST omits its fc. Switching to TRANSFER and back leaves the committed list intact. **Mutant:** a model owned by the pane loses the list on switch-back and must go red. |
| **T6 — controller and exports** | `app/src/MainComponentEq.h/.cpp` (owns `EqPaneModel` and the dialogs); `MainComponent.h/.cpp` (member `eq_`, declared after `library_` and before `workspace_`, per the rule at `MainComponent.h:381-386`); both source lists | EXPORT buttons, through `EqPaneActions` | EXPORT FIR TXT / WAV / LIST | EXPORT stays disabled until both phase and length are answered. `performExportFirText(file)` writes a header with `sample_rate_hz=48000`, `taps=4096`, `phase=linear` and `normalization=as_designed`, followed by 4096 coefficient lines. The file stem is `eq_48000Hz_4096taps_lin`. The WAV reads back as 1 channel, float32, 48000 Hz, 4096 samples, bitwise equal to the taps. The filter list round-trips through `parseFilterList`, keeping the `applied` token (EQ record §7, amendment 2). **Mutant:** pre-selecting Linear must fail the disabled-until-asked assertion. |
| **T7 — VERIFY** | `app/src/MainComponentEqVerify.cpp`; `MainComponent.cpp` (`timerCallback` calls `eq_.poll()`); `MainComponentDelay.cpp` (LOCATE refuses while a verify is running) | VERIFY button; `timerCallback` at 2 Hz (`MainComponent.cpp:119`) | LIVE device, output 0 to a speaker, reference fed: VERIFY | Tested against a standalone `OutputEngine` rendered by hand (the `test_eq_verify.cpp:90` pattern) plus scripted snapshots. Each of these refuses with a named reason: device not running; no unapplied filter; fftSize or fs differ from the bound trace; Exponential transfer averaging; engine not quiescent (`EqVerify.h:106`). A snapshot taken less than depth·fftSize/fs after Measuring is not submitted: 16·4096/48000 = 1.365 s at the defaults (`Analyser.h:48-49`). Staying in Waiting longer than 5000 ms disarms and says so (`captureTimedOut`, following `MainComponentCalibration.cpp:33`). A fresh `EqVerify` is built per press, because `arm` refuses unless Idle (`EqVerify.cpp:132-135`). The Done readout equals `renderVerifySummary`. The after-measurement lands in the library in group "EQ". **Mutant:** submitting the first Measuring snapshot must fail with the pre-excitation fixture. |
| **T8 — ADOPT** | `MainComponentEq` | the ADOPT button, enabled only at Done | VERIFY → ADOPT | ADOPT marks every filter that was in the prediction as applied and binds the VERIFY trace. Afterwards `ghostDb == measured` bitwise (`EqSession.h:57`), and SUGGEST proposes nothing at those fcs (the `test_eq_session.cpp:172` invariant, reached through the button). **Mutant:** binding without `markApplied` leaves ghost ≠ measured and must go red. |
| **T9 — specimens** | `tools/snapshot_eq.h/.cpp`, `tools/snapshot.cpp` (+2 lines), `app/CMakeLists.txt` | `rtatool_snapshot` | — | See the specimen section below. |
| **T10 — human try** | — | — | **A, no hardware:** SYNTHETIC → TRANSFER → STORE → EQ → pick "TRANSFER @ …" → AUTO EQ → BANDS 2 → AUTO EQ → EXPORT FIR TXT (Linear, 4096) → open the file and read the `sample_rate_hz`, `taps` and `phase` header lines → EXPORT FIR WAV → load it in REW or Audacity. **B, hardware:** a loudspeaker on output 0, a mic and a loopback reference. LOCATE → APPLY → TRANSFER → STORE → EQ → AUTO EQ → dial the listed filters into the processor → VERIFY → read "VERIFY: x of y bins trusted, z flagged; residual a → b dB" → ADOPT → SUGGEST offers nothing at the adopted fcs. | A human sees each step. **B is VERIFY's only end-to-end proof:** SYNTHETIC stops the device, so CI can never play the excitation. N/A for mutants. |

**Waves.** Wave A is T0–T6 plus T9. Wave B is T7 and T8.

## Hot files

These are touched by other lanes. PRs that touch `MainComponent*` are reviewed one at a time (GIT-WORKFLOW decision 4). Hot here:
- `MainComponent.h`, `MainComponent.cpp`, `MainComponentPanes.cpp`, `MainComponentDelay.cpp`
- `PaneFactory.*`, `PaneRegistry.h`, `PaneSelectorDecision.h`
- both `app/cmake/*_sources.cmake`
- `app/tests/CMakeLists.txt` (the GLOBS list), `app/tests_juce/main_component_tests.cmake`
- `tools/snapshot.cpp`
- `core/src/eq/EqAllocator.cpp` (T0 only)

## File-length budget

| file | now | this lane's ceiling |
|---|---|---|
| `MainComponent.h` | 400 | **H1's result + 3** (include, button, member). This lane opens only after H1 merges. 400 is the hard cap. |
| `MainComponent.cpp` | 306 | 320 |
| `MainComponentPanes.cpp` | 152 | 170 |
| `MainComponentDelay.cpp` | 122 | 126 |
| `PaneFactory.cpp` | 28 | 40 |
| `EqSession.h` / `.cpp` | 176 / 165 | 190 / 185 |
| `EqAllocator.cpp` | — | net 0 |
| `tools/snapshot.cpp` | 334 | 340 |

New files:
- `EqPaneModel` 150 (.h) / 250 (.cpp)
- `EqFirDesign` 60 / 100
- `EqPaneView` 160 / 300
- `EqChartRenderer` 50 / 250
- `MainComponentEq` 120 / 250
- `MainComponentEqVerify.cpp` 220
- `snapshot_eq` 40 / 220
- each test file 350

## orphan_check acceptance

At the close of each wave, `python tools/orphan_check.py --base <wave start> --build-dir build-orphan …` (CLAUDE.md rule 5) exits 0. In addition, all of these must be reachable from `rtatool`'s entry point:
- every non-inline symbol in `EqSession.cpp`;
- in `EqVerify.cpp`: `arm`, `poll`, `submitSnapshot`, `compareToPrediction`, `h1SigmaDb`, `renderVerifySummary`;
- in `FirTextWriter.cpp`: `firFilenameStem`, `renderFirText`;
- in `FirWavWriter.cpp`: `writeFirWav`;
- every new `.cpp`.

`EqTextExport.h` is all inline, so the tool reports it as UNCHECKABLE; T6's test proves it is reached instead.

Test hooks are `*ForTest` members of `EqPaneView` and `MainComponentEq`, named literally in `app/tests_juce`. **Do not add a `MainComponentTestAccess` wrapper:** that wrapper form is a known blind spot of the orphan check (`docs/tools/orphan-check-known-limits.md`). Tests reach the pane through the existing `pane()` wrapper plus a `dynamic_cast`.

## UI snapshot specimens (`tools/snapshot_eq.cpp`)

- **`main-live-eq.png`** (1280×800)
  - Setup: `MainComponent` in SYNTHETIC mode; the library is seeded with the `makeLinearFixture`-shaped 8 dB bump trace (γ² = 0.95).
  - Driven through the real controls: EQ button → combo pick → AUTO EQ button.
  - Prints FAILED if nothing is committed.
- **`main-live-store-eq.png`**
  - The real chain: TRANSFER → `storeClickedForTest` → EQ → pick the stored trace → SUGGEST.
  - Prints FAILED if the trace is not eligible or the pane refuses.
  - Synthetic H is close to flat (`SyntheticImpairment.h`: a delay plus a noise floor), so the readout must say "nothing to correct" rather than stay blank.

## Risks

1. **Declining T0** leaves the FLAT-target RMS meaningless. The mitigation is D15's fallback text.
2. **G24 cost.** `excessPhase` runs 128× oversampled (`ExcessPhase.h:43`). At the default fftSize of 4096 that is a 524288-point FFT on the message thread. T2 measures it; above 100 ms, move the work to a worker thread.
3. **LOCATE ignores `setSource`'s return value** (`MainComponentDelay.cpp:28`) and disarms at the end of its capture (`:71`). During a VERIFY it would silently kill VERIFY's excitation. T7's guard closes this path; the defect itself stays in LOCATE.
4. **EQ state is not saved with the session.** EXPORT LIST is the stand-in until a session-format lane adds it.
5. **A stale bound trace.** If the picked trace is hidden or removed, the model keeps its own copy and labels it "(removed)".
6. **Unreliable phase.** The G24 gate is only meaningful when the stored phase is delay-compensated (`CaptureMeta.appliedDelaySamples`, `Trace.h:46`). Human-try B runs LOCATE → APPLY first for this reason.

## Not built here

- Tilt, hand-drawn and imported targets (EQ record §5).
- Shelf filters: the allocator places peaking filters only (`EqAllocator.h:49-52`).
- 65536-tap FIR, other export rates, and least-squares FIR design.
- A NotMinimumPhase chip opening V2 (EQ record §12.4).
- The G11 virtual processor (EQ-R3).
