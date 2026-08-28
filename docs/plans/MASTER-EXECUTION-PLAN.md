# Master execution plan — phases, dependencies, parallel lanes

*2026-08-28. The owner creates one session per lane below. Every lane follows
the five-station pipeline in docs/reports/README.md (research → analysis →
Opus plan → Sonnet build with TDD → adversarial verify → report). A lane's
session prompt is: "Read docs/plans/MASTER-EXECUTION-PLAN.md lane <X>, then
docs/reports/README.md, then the decision records it names. Continue the
pipeline from the current git state."*

## Ground truth at time of writing

*Ground truth re-measured 2026-08-28. The 103/103 figure this section carried
was two waves out of date; treat any count in a plan as a claim to re-verify,
not as a fact.*

Done (**159/159 tests**, zero /W4 warnings on a clean rebuild): core
FFT/RealFft, OctaveBands, SpectrumEngine, BandWeights, IEC 61260 FilterBank
(class 1 verified on CI), Window, RingBuffer, Synthetic signals; platform
CaptureBus/ChannelConfig/AudioIo; app measurement view model + RtaView plot +
PlotAxes + Readouts; az_ui design system; snapshot tooling. Also landed since
this section was written: Wave D (AnalysisThread, SyntheticInput, DevicePanel,
ChannelRoleTable), the Meters track (Weighting, Detector, Leq), the Generator
track (66c770c), and Wave E.

Nothing is in flight on this repo. Phase 1's only open item is **T12's by-hand
hardware pass**, which needs the owner and an interface — see
`docs/reports/T12-hardware-run.md`. It does not block L2 or L5: it exercises
`platform/AudioIo` against real hardware, and neither lane touches that path.

## Dependency spine

```
P1 (RTA/SPL/gen, finishing now)
 ├─→ P2 dual-FFT  ──→ P3 MTW ─────────────┐
 │        │                               │
 │        └─→ P4 sweep/IR ──→ P4b THD/STI │
 │                  │                     │
 ├─→ P5 traces/targets/tuning-visuals ←───┘
 │                  │
 ├─→ P6 SPL-pro/multichannel workflows    │
 │                  │                     │
 └─→ P7 solvers (auto-EQ, auto-delay, virtual processor, wizard) ← needs P2+P4+P5
                    │
     P8 research lanes (SyncSource-TF, AES-75, network audio, DSP SDK)
     P9 productization (i18n, installers, licensing page, docs)
```

## Lanes the owner can open as separate sessions

| Lane | Scope (decision records to read first) | Depends on | PARALLEL-SAFE with |
|---|---|---|---|
| **L2 — Dual-FFT engine** | P2: cross-spectrum, H=Sxy/Sxx, coherence, delay finder (+GCC-PHAT), phase unwrap, group delay, FIFO averaging (G1), environment input (G16). Records: spec roadmap, banding doc | P1 core (done) | L5, L6a, L-web. NOT with L3/L4 (same core/dsp files likely shared) |
| **L3 — MTW** | P3: decimation cascade, per-band FFT sizes, stitching, CONCURRENT with fixed engine (G2). Needs its own station-1 research pass first | L2 interface | L5, L6a |
| **L4 — Sweep/IR** | P4: Farina quick-measure mode (FR+IR one shot), ETC, Schroeder+Lundeby, EDT/T20/T30, C50/C80/D50, STI/STIPA (G4, needs IEC 60268-16), polarity checker (G21), offline dual-FFT vs WAV (G22), min/excess phase (G24), drag IR gating (G25) | P1 + generator's Sweep class | L2 partially (coordinate on core/CMakeLists — serialize integration commits), L5, L6a |
| **L5 — Traces, targets, tuning visuals V1/V2 (display half)** | P5 + docs/specs/2026-08-28-interactive-tuning-visuals.md: trace library, target curves + X-curve (G5), corridor + judgement colours + match score, Bode layout (G9), multi-plot workspaces (G6), cepstrum/wavelet (G23), spectrograph, session persistence | P1 app (done); solvers NOT needed (they are L7) | L2, L3, L4, L6a — app/ui side, disjoint from core DSP lanes |
| **L6a — SPL-pro** | P6 subset: SPL logging/history/alarms/PDF/web viewer (G7), dose IEC 61252 (G8) | Meters track (in flight — wait for it to land) | everything except L6b |
| **L6b — Multichannel workflows** | P6 subset: spatial averaging (G14), coherence weighting (G15), sequencing + auto-discard (G20), full routing matrix, presets, remote API | L2 (multi-TF) | L4, L5 |
| **L7 — Solvers** | P7: auto-EQ + suggestions (both modes), auto-delay + suggestions, virtual processor (G11), alignment wizard (G17), crossover surface (G18), FIR export (G10) | L2 + L4 + L5 | L6a, L8 |
| **L8 — Research lanes** (each is a station-1/2 pass producing a decision record, no code until approved) | G12 SyncSource-class TF; G13 AES-75; G19 Dante/AVB/Milan; G26 DSP plug-in SDK | none (research only) | ALL — safe anytime, read-only |
| **L9 — Productization** | i18n VI/EN, installers (3 OS), website content refresh cadence, manual | everything shippable | L8 |
| **L-web — Website content package** | docs/marketing/ upkeep: re-render mockups after each landed phase, keep VI/EN copy current | none | ALL |

## Orchestration topology (owner question answered 2026-08-28)

**One orchestrator session is sufficient and preferred** — proven by the Phase-1
session itself, including three-level spawning (orchestrator → lane coordinator
→ workers; the meters lane ran its own W/D/L children and rolled results up).
The plan above plus docs/reports/README.md is the orchestrator's rulebook.

Conditions that hold regardless of topology:
1. **2-3 concurrent BUILD lanes maximum** — build directories and the two core
   CMake files are physical contention, not organizational.
2. **The orchestrator stays thin**: delegates everything, reads reports not
   files, keeps its context for judgement. When context runs low it writes the
   rule-8 handoff and the NEXT orchestrator session continues — sessions chain
   serially, they do not run in parallel on this repo.
3. **Separate sessions only for**: L-web (different repo), L8 research lanes
   (read-only, contention-free), and the successor orchestrator.

Opening prompt for the orchestrator session:
"Đọc docs/plans/MASTER-EXECUTION-PLAN.md + docs/reports/README.md +
docs/HANDOFF.md. Vận hành như orchestrator: mỗi lane một agent cấp trung theo
pipeline 5 trạm, tôn trọng cột PARALLEL-SAFE và các luật dưới đây. Giữ mình
mỏng — không tự code, không đọc file lớn, mọi claim phải qua verifier."

## Session-collision rules (from hard-won incidents this week)

1. **core/CMakeLists.txt + core/tests/CMakeLists.txt are the contention
   points.** A lane touching core serializes its ONE integration commit;
   coordinate via git pull before that commit, never via long-lived branches.
2. Each concurrent session uses its OWN build directory (build-<lane>).
   Configure core-only (default RTA_BUILD_APP=OFF) unless the lane needs JUCE.
3. Read docs/HANDOFF.md and `git log --oneline -15` at session start; the
   Seph harness flags parallel-session hazards (az-harness rules apply).
4. A lane's verifier agent gets no Write tools. Never skip station 5.

## Suggested opening order with ~2 concurrent sessions

1. **L2** (the heart — Smaart-class dual-FFT) + **L5** (visuals) in parallel.
2. Then **L4** + continue L5; L8 research lanes fire-and-forget anytime.
3. Then **L3** + **L6b**; then **L7** once L2/L4/L5 are in; **L6a** after
   meters; **L9** last.
