# SWEEP lane (Sweep→IR producer) — station-1/2 research

*2026-09-29. Read at `origin/main` `d06ab21` (worktree HEAD `ea036d5`, same
content). Scope: the missing producer the 2026-09-27 plan review named "(b)
Sweep→IR" (`docs/plans/MASTER-EXECUTION-PLAN.md:30`). The lane plays a sweep,
captures it and deconvolves it, so that an IR and its spectrum exist in the
running app. Research and plan only.*

## Corrections to the brief

1. **"L4c" is already taken, with a different scope.** The L4a record defines
   L4c as the draggable IR gate (G25), the min/excess-phase display (G24) and
   offline WAV (G22) (`docs/dsp/2026-08-30-sweep-ir-l4a.md:22`; master plan
   `:352`). None of those is a capture button. This lane is P4's "Farina
   quick-measure mode" (master plan `:352`). Call it **SWEEP** and leave L4c's
   scope alone, so that shipping the capture does not tick off the gate too.
2. **LOCATE hangs in SYNTHETIC today.**
   - `pollLocatePipeline` waits for `output().renderedSamples()` ≥ 480
     (`app/src/MainComponentDelay.cpp:49-54`).
   - The only caller of `OutputEngine::render` is the device callback
     (`platform/src/AudioIo.cpp:147`), and SYNTHETIC stops the device
     (`MainComponent.cpp:159`).
   - So the readout sits at "locating..." forever, and `locateWaitingForSettle_`
     then refuses every later LOCATE and CAL click (`MainComponentDelay.cpp:19`,
     `MainComponentCalibration.cpp:38,50`).
3. **XOVER sums two traces by bin index with no axis check.**
   - `sumResponses` walks `min(a.size(), b.size())` bins
     (`core/src/dsp/VirtualProcessor.cpp:58-65`).
   - `CrossoverPaneView` admits any visible trace that has phase
     (`app/src/view/CrossoverPaneView.cpp:143-156`).
   - ALIGN refuses mismatched FFT sizes (`AlignmentWizard.cpp:215`); XOVER
     does not.
   - The hazard is latent today: session Open can place a 44.1 kHz trace beside
     a 48 kHz STORE. One SWEEP press makes it reachable.
4. **XOVER ignores `appliedDelaySamples`.** Nothing under `app/src/view` reads
   it; ALIGN reconciles it (`AlignmentWizard.cpp:249-250`). This lane does not
   introduce it, because SWEEP traces carry 0. It is flagged, not scheduled.

## Part A — What exists

- **Excitation.**
  - `gen::Sweep` renders one ESS and then zeros (`core/src/gen/Sweep.cpp:180`).
  - Defaults: 20 Hz–20 kHz, 10 s, −6 dBFS peak, 2-octave fade-in
    (`Sweep.h:64-105`).
  - `buildInverseFilter()` allocates and runs in O(N) (`Sweep.h:171-177`).
  - It is already an `OutputEngine` source (`OutputEngine.h:31-32`), played
    through a 10 ms master ramp (`OutputEngine.cpp:185`, `Oscillator.h:97`).
  - At the defaults, that ramp sits wholly inside the 2 s fade-in
    (L = 10/ln 1000 = 1.45 s; two octaves = L·ln 4 = 2.0 s).
- **Deconvolution.**
  - `deconvolve` is linear, with its origin at `Ninv−1`
    (`Deconvolver.h:119-121`; `Deconvolver.cpp:128`).
  - A 10 s sweep needs a 2^21 transform (`Deconvolver.h:112-113`), about 8 MB
    of tables (record `:990-994`).
  - `inBandNormalisation` and `bandFlatness` are at `Deconvolver.h:142-153`.
- **Spectrum.** `analyseSpectrum` transforms from `origin − round(2·fs/f_lo)`
  to the end of the buffer, padded to the next power of two
  (`IrSpectrum.cpp:40-58`). Phase is in radians, referenced to t = 0. At full
  capture length that is about 10^6 bins.
- **Capture.**
  - `armLocateCapture(route, length)` and `locateCapture()`
    (`AnalysisThread.h:145,154`) hand over paired reference and measurement
    vectors (`AnalysisThread.h:41-44`).
  - The length has no cap (`RawCaptureBuffer.h:28-33`).
  - The arm is picked up at the next drain (`AnalysisThread.cpp:183-186`), and
    the thread drains every 10 ms (`AnalysisThread.cpp:17`).
  - Calibration shares the same capture (`MainComponentCalibration.cpp:41`).
  - The header is at 398 lines.
- **Consumers.**
  - ALIGN takes raw IRs: `AlignmentWizard::supplyImpulseResponses(Deconvolution, Deconvolution)`
    (`AlignmentWizard.h:204`).
  - XOVER and EQ take a `Trace`. `Trace::make` requires `fftSize/2+1` bins
    (`Trace.h:52,63`).
- **Clock.** `DeviceState` names one device
  (`platform/types/include/rta/platform/DeviceState.h:21`;
  `AudioIo::setDesiredDevice(std::string)`, `AudioIo.h:80`). Input and output
  therefore share a clock. The record's drift hazard (`:1143-1153`) belongs to
  G22 (WAV import), not to this lane.

## Part B — How others do it

**REW** (help page "Making Measurements", fetched 2026-09-29; docs only):
- Sweep length is set in samples; the default is 256k, about 5.5 s at 48 kHz.
- The default level is −12 dBFS rms.
- The sweep spans half the start frequency to twice the end frequency.
- There is a configurable "start delay".
- "The overall duration includes silent periods before and after the sweep."
- Timing reference is either a loopback ("the reference channel signal must be
  looped back from output to input") or an acoustic 5–20 kHz sweep.
- There is an "Adjust clock with loopback" option.
- Repetitions exist, but "best results are generally obtained by using single,
  longer sweeps".
- The IR-graph page gives a 125 ms window width for full-range measurements.
  Which window edge that is does not appear in the fetched text
  (**unverified**). The length of the post-sweep silence is not stated.

**Open Sound Meter** (code, `src/generator/sinsweep.cpp`, fetched 2026-09-29):
- The exponential sweep **loops**: its phase resets at the end of each period.
- There is no inverse filter.
- The sweep is just one more excitation for OSM's continuous dual-FFT; its IR
  is the IFFT of the transfer function.

**pyfar** (API docs, fetched):
- `exponential_sweep_time` defaults to a 90-sample fade-out and no fade-in.
- `dsp.deconvolve` is a regularised spectral division.
- It pads only the shorter signal to the longer one, so it is **cyclic** unless
  `fft_length` is given.
- The record already weighs the Kirkeby regularisation (`:83-110`).

**ARTA** (**UNVERIFIED — from memory, manual not read**): dual-channel mode
divides by the reference channel so the sound card cancels out; single-channel
mode deconvolves against the known stimulus.

| | excitation | anchors t = 0 | level reference | post-roll | IR→FR |
|---|---|---|---|---|---|
| REW | one-shot log sweep | loopback or acoustic, optional | cal / loopback cal | "silent periods", length unstated | windowed IR → FFT |
| OSM | looping sweep into dual-FFT | reference channel, always | reference channel | n/a | none — TF is primary |
| pyfar | library | caller | caller | caller pads | regularised division |
| ARTA (unverified) | sweep/MLS/noise | dual-channel reference | reference channel | — | windowed IR |

They disagree on two points:
- **What anchors t = 0.** REW makes a reference optional; OSM makes it
  mandatory.
- **Whether to deconvolve at all.** OSM does not.

## Decision → evidence

| decision | evidence |
|---|---|
| reuse `armLocateCapture`; no new `AnalysisThread` API | the handoff is already paired and uncapped (`RawCaptureBuffer.h:28-33`); the header is at 398/400 |
| require route 0's REF channel (a loopback) | LOCATE and CAL already do (`MainComponentCalibration.cpp:117-125`); matches REW's loopback timing mode; cancels interface latency |
| t = 0 := the reference-channel IR peak | a pure-delay round trip is exact to the sample (record `:907-910`) |
| deconvolve off the message thread | 2^21 transform, ~8 MB of tables (record `:990-994`) |
| capture = pre-roll + sweep + 1.5 × RT60 | record §9 table (`:1025-1032`) |
| normalise to the loop's in-band mean | `inBandNormalisation` (`Deconvolver.h:142`); matches TRANSFER's H = Y/X |
| spectrum window ≥ 0.5 s, starting 2 cycles of `f_lo` before t = 0 | lead-in rule (record `:888-926`) |
| no taper and no user gate here | the gate belongs to G25 (record `:934-938`) |
| SYNTHETIC loops through the real `OutputEngine` | keeps one write path (`SyntheticInput.h:18-23`); fixes correction 2 |

## Part C — Candidates

### C1. What anchors t = 0

**The obvious choice: single-channel Farina, with t = 0 at `originIndex`.**
Argued against:
- The peak then sits at pre-roll + output buffer + converter + acoustic delay.
- The pre-roll is set by a 2 Hz UI timer (`MainComponent.cpp:119`), so it is
  not known to the sample.
- Two sweeps would disagree by hundreds of samples for reasons unrelated to the
  loudspeaker, and XOVER would sum a sub and a main at a random relative delay.

**B: dual-channel spectral division Y/X** (ARTA, Müller's measured reference).
It cancels the loop exactly, but it brings back the regularisation constant the
record declined outside [f1, f2] (`:98-110`).

**Chosen: the loopback as a timing reference only.**
- Deconvolve both channels with the same Farina inverse.
- Take the reference peak as t = 0 and relabel `originIndex` to it (a metadata
  change; the samples are untouched).
- Normalise by the reference's in-band mean.

What the rejected options would cost:
- Rejecting B: the loop's own ripple is not divided out. It is bounded by the
  configuration's flatness (−0.05…+0.22 dB at the defaults, record `:237`) and
  reported per capture via `bandFlatness`.
- Rejecting the obvious choice: a rig with no loopback cannot sweep, which is
  already true of LOCATE.

### C2. Deconvolve at all?

**OSM's model:** loop the sweep into the live dual-FFT and STORE the result.
This needs no new DSP path. Rejected because:
- harmonics do not separate in time, which is the ESS's defining advantage
  (record §3 `:162-176`);
- there is no IR for `supplyImpulseResponses` or for RT60;
- a 4096-point frame holds only 85 ms of IR.

Cost of rejecting it: a second measurement path to maintain.

### C3. How long is the spectrum

- **The full tail** (`analyseSpectrum` untruncated). Rejected:
  - about a million bins, which is 4 MB per trace in a session;
  - it includes the noise tail that reads as decay (record `:1110-1142`).
- **The live fftSize (4096)**, so that SWEEP traces pair with STORE traces.
  This is the obvious choice for XOVER interop. Argued against:
  - 85 ms minus a 25 ms lead-in leaves 60 ms, so a microphone beyond about
    12 m falls outside the window;
  - moving the window would need a non-zero `appliedDelaySamples`, which XOVER
    ignores (correction 4);
  - ALIGN, the other consumer that pairs captures, takes IRs as `Deconvolution`, not as traces.
- **Chosen: N = nextPow2(⌈0.5·fs⌉ + leadIn).**
  - That is 32768 at 44.1 or 48 kHz, and 65536 at 96 kHz.
  - It keeps at least 500 ms after t = 0, with phase relative to the reference
    arrival and `appliedDelaySamples = 0`.
  - Cost: a SWEEP trace cannot be summed with a STORE trace, so XOVER must
    refuse that pair rather than mis-sum it (correction 3).
  - Interop by resampling has no closed-form test. It is refused for the same
    reason STORE refused to resample MTW
    (`docs/research/2026-09-27-store-trace.md:70-77`).

### C4. The SYNTHETIC path

- **A: a test-only harness** that pushes arrays into `CaptureBus`. Rejected: it
  proves the pipeline but not the button, and leaves correction 2 in place.
- **B: a fake `AudioIo` device.** Rejected: that is a second write path, which
  `SyntheticInput.h:18-23` exists to prevent.
- **Chosen: `SyntheticInput` calls `OutputEngine::render` itself.**
  - It uses the rendered block as the reference whenever the engine is not
    quiescent.
  - It routes that block through the existing delay and noise
    (`SyntheticImpairment.h:28-81`), plus a new optional `BiquadCascade`
    (`rta/dsp/Biquad.h:37`) on the measurement copy.
  - This is safe because SYNTHETIC has already stopped the device, which
    leaves the synthetic thread as the only caller of `render`
    (`OutputEngine.h:71-78`).
  - What it cannot show: acoustic latency, clock drift and real noise. Those
    are the human-try row.

## Open for the owner

See the plan's owner-default table. None of those decisions blocks station 3.
