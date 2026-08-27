# Report 002 — Phase 1: the audio chain, end to end

*2026-08-28. Covers everything landed since report 001 (Phase 0). 46 commits on
main; shared build 149/149 green (updated after the generator landed), zero warnings at /W4; 47+ commits after
the generator landed (66c770c).*

## What the human can run, right now

```
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure     # 149/149
build/app/rtatool_artefacts/Release/"RTA Tool.exe"        # the app: flip SYNTHETIC, watch a live RTA
cmd //c "build\app\rtatool_snapshot_artefacts\Release\rtatool_snapshot.exe shots 1100 760"
                                                          # 6 renders, exit 0
```

The app opens on the real measurement window: device panel (ASIO/WASAPI),
channel-role table, live RTA from AnalysisThread. The SYNTHETIC switch runs the
whole chain with no hardware. F1 shows the design-system specimen.

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

- ~~Generator track~~ — LANDED after this report's first writing: commit
  66c770c, shared build 149/149, goldens byte-identical, scratch build dirs
  removed. Phase 1's remaining gap is exactly one item:
- **T12 hardware pass M1-M7** (AudioIo plan §5.8): needs a real ASIO interface
  plugged in — the owner's hands. Everything else in Phase 1 is done.
- STI needs IEC 60268-16; any Class-1 SLM claim needs the full Table 2 —
  both paywalled, tracked in the weighting decision record.
