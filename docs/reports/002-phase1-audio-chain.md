# Report 002 — Phase 1: the audio chain, end to end

*2026-08-28. Covers everything landed since report 001 (Phase 0).*

*Numbers below were re-measured on 2026-08-28 by the successor orchestrator, not
carried forward from the previous session — the counts it inherited were already
stale. 50 commits on `main`, plus 2 on `claude_desk/orchestrator-ke-nhiem-04b17b`
awaiting merge. Shared build **159/159** green, **zero** warnings at /W4 on a
full clean rebuild.*

## What the human can run, right now

```
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure     # 159/159
build/app/rtatool_artefacts/Release/"RTA Tool.exe"        # the app: flip SYNTHETIC, watch a live RTA
cmd //c "build\app\rtatool_snapshot_artefacts\Release\rtatool_snapshot.exe shots 1100 760"
                                                          # 6 renders, exit 0
```

The app opens on the real measurement window: device panel (ASIO/WASAPI),
channel-role table, live RTA from AnalysisThread. The SYNTHETIC switch runs the
whole chain with no hardware. F1 shows the design-system specimen. The plot's
readout line now carries a third segment — `100 Hz    -28.7 dB    36 FRAMES` —
so the analysis frame count is visible rather than buried in the snapshot struct.

Those four lines are **Git Bash**. This machine's own terminal is PowerShell,
where the exe's space needs the call operator: `& "…\RTA Tool.exe"`. Launching
the app for the hardware pass uses the separate ASIO-enabled build instead —
see `docs/reports/T12-hardware-run.md`, which carries that command verbatim.

## What was built (by pipeline station)

**Research → decision records** (docs/dsp/, docs/specs/): AudioIo architecture,
weighting/detectors/Ln conventions, single-rate SOS filter bank (overturned the
planned decimation cascade with measured numbers), generator choices, the
competitive parity ledger G1-G26 with five recorded rejections, interactive
tuning visuals with dual-mode auto-EQ/auto-delay.

**Core DSP** (all closed-form/golden verified): Window, RingBuffer, Fft/RealFft
(NumPy goldens; the DC/Nyquist inverse convention caught by adversarial
review), OctaveBands (base-10 G=10^0.3 — the base-2 trap), SpectrumEngine
(scipy Welch; PS-vs-PSD 1.76 dB trap pinned), BandWeights (IEC design-goal
weighting, closed forms 0.5 at edges, 1.047198 noise bandwidth), FilterBank
(Butterworth 6-SOS, class-1 mask 1408 breakpoints on CI, ba-form structurally
impossible plus a guard), Weighting A/C/Z (bilinear SOS, Table 3 at exact
frequencies), Detector (one-pole mean square, formulas only), Leq/SEL/Lpeak/Ln.

**Platform**: CaptureBus/ChannelConfig (JUCE-free, CI on 3 OS), AudioIo (JUCE
device layer, 7 production rules each enforced at a named line, a
ScopedNoDenormals guard ctest), rings allocated once forever (the prepare()
realloc use-after-free closed structurally, 16 MB fixed).

**App**: Levels/Snapshot/Analyser (JUCE-free test target, GCC CI-parity),
RtaView + PlotAxes (under-resolved bands marked by geometry), DevicePanel +
ChannelRoleTable, AnalysisThread + SyntheticInput, MainComponent (T10), three
lane-L5 preview views for marketing.

**Fixed at the root**: az_ui's exit-time heap corruption (function-local static
Typeface cache destroyed after JUCE teardown → deliberate leak; proven by the
snapshot tool exiting 0 with a plain return).

## What adversarial verification refuted (and what that bought)

- RealFft::inverse trusted DC/Nyquist imaginary parts (Fable) → fixed, plus
  golden cases built from spectra rfft could never produce.
- FilterBank "complete" refuted on three counts (reset() contract, untested
  Nyquist drop branch) → all closed with proof the new tests bite.
- FOUR wrong literals in planning docs refuted by re-derivation (−1.9895,
  −1.99080, −2.7003, Table 3 C column shifted an index). None reached an
  assertion: tests assert formulas.
- The orchestrator itself was refuted twice by the generator coordinator when
  it characterized code it had not read → standing rule: relays carry routing
  facts only.

## Orchestration lessons (for the successor session)

1. Child-agent completion notifications route to the SESSION ROOT, not to the
   coordinator that spawned them. Coordinators must poll the filesystem, never
   wait for messages.
2. Relays between agents must carry routing facts only; every technical claim
   is read from disk by the recipient.
3. Two or three build lanes max; own build dir per lane; the two core CMake
   files are the contention points.
4. The guard scripts bite across tracks by design — resolve by conforming
   (ZPK path, hand-rolled loops), never by punching holes.

## Outstanding

- ~~Generator track~~ — LANDED: commit 66c770c, goldens byte-identical, scratch
  build dirs removed.
- **T12 hardware pass M1-M7** (AudioIo plan §5.8) — the one item left, and it
  needs the owner's hands, not another agent. Everything a machine could do for
  it is now done and committed:
  - Its automated half is measured: 159/159 and zero /W4 warnings on a full
    clean rebuild.
  - Two of the seven steps were **not observable** and would have been recorded
    dishonestly. M1 wanted an explanation that was never drawn (JUCE keeps the
    current device type silently, so no fault is ever recorded); M3 wanted
    `framesAnalysed`, which no widget displayed. Both are fixed in 9f6dce1,
    adversarially reviewed, with the logic in a JUCE-free `Readouts.h` under the
    same guard as `PlotGeometry.h`.
  - An ASIO-enabled build is prepared at `build-asio/` (SDK already on this
    machine; `ASIOAudioIODeviceType@juce` confirmed present in the binary, so
    the TYPE combo really will list ASIO).
  - The sheet to fill in: `docs/reports/T12-hardware-run.md`. It records that M1
    has **two** legitimate outcomes, which the plan does not — and that the
    explanation appears only after START, which is the easiest way to write down
    a false failure.
- **Known defect, not introduced here:** the framework-dependency guard's
  explicit-list variant silently stops guarding a file whose path is misspelled
  (`file(GLOB_RECURSE)` on a missing path yields nothing, and only an all-empty
  result fails). Measured: one real path plus one bogus path still reports
  `OK (1 files scanned)`, exit 0. Coverage is currently proven by the scanned
  count, not by the guard. Recorded in
  `memory/core-must-not-include-frameworks.md`.
- STI needs IEC 60268-16; any Class-1 SLM claim needs the full Table 2 —
  both paywalled, tracked in the weighting decision record.
