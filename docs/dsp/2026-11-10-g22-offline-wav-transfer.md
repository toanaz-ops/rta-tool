# G22 -- offline dual-FFT transfer function from a WAV file

*2026-11-10. Station 1 (research) and station 2 (decision) for master-plan row 5's
last gap. Code read at `origin/main` `925fb3a` + `d087302`. §6 (shipped) is
written after the code exists (CLAUDE.md rule 6). Unmeasured on hardware.*

## 1. Correction to the brief

The parity table (`docs/dsp/2026-08-28-competitive-parity.md`, row G22) names
"WaveCapture, REW" as the tools that do this. Station 1 does not support either
citation as written (`memory/the-parity-table-is-a-hypothesis.md`):

- **REW** "Offline measurement" deconvolves a recorded response against REW's
  OWN sweep. "Using a timing reference signal is required for offline
  measurement" (REW help, `offlinemeasurements.html`). The stimulus must be a
  sweep saved from REW's generator (`file.html`, Import sweep recordings).
  Clock adjustment is anchored on timing markers at the start and end of that
  sweep. REW has no generic "two channels of a recording -> transfer function"
  feature that station 1 could find.
- **WaveCapture / Live-Capture**: station 1 found only real-time descriptions
  (8-channel transfer function, delay finder; prosoundweb). No document of a
  file-based transfer function was read. Unverified. Do not cite it.
- **Open Sound Meter**: WAV is a generator only (`src/generator/wav.cpp`).
  There is no file analysis source. Its live delay finder is an integer-sample
  cross-correlation, applied by zero-padding a ring (`src/source/measurement.cpp`
  ~424-445, 541-547).
- **Friture**: PortAudio input only.
- **Smaart v9**: no documented file-input transfer function was found.
  Unverified.

The gap is real (the workflow is "record console feed + mic on the multitrack,
analyse later"), but the feature is closer to a SpectraFoo-style "load audio,
auto-delay the source" than to anything REW ships. The decisions below
therefore come from this codebase's own engine and from first principles, not
from a competitor's behaviour.

## 2. What the codebase already has (reuse map)

| need | existing | where |
|---|---|---|
| H1 + coherence over a pair of spans of any length | `DualFftEngine::process` + `makeSnapshot` (the ONLY place coherence is gated) | `core/include/rta/dsp/DualFftEngine.h`, `TransferEstimator.h:79` |
| integer bulk delay applied before the FFT | `Config::referenceDelaySamples` (discards the lagging stream's first \|D\| samples; cap 1<<20) | `core/src/dsp/DualFftEngine.cpp:37-42,116-127` |
| delay + trust verdict | `suggestDelay` (GCC-PHAT, `DelayPolicy`, `Accepted/BelowFloor/WindowEmpty`) | `core/include/rta/dsp/DelayPolicy.h:86` |
| a stored transfer trace | `Trace::make` + `setPhase` + `setCoherence`; library groups are free strings | `app/src/trace/Trace.h`, `TraceLibrary.h` |
| WAV decoding | JUCE `WavAudioFormat`: PCM, IEEE float, EXTENSIBLE (`0xfffe`) and RF64 (`ds64`) | JUCE 9.0.1 `juce_WavAudioFormat.cpp:1224,1243,1300,1319` |
| a "pair of buffers -> outcome" worker | `analyseSweepCapture` on `SweepIrWorker` | `app/src/measure/SweepIrPipeline.h:79` |

No production code reads an audio file today. `juce_audio_formats` is already
linked into `rtatool` and the JUCE test target.

**The one missing engine piece.** `TransferAveraging` is `Fifo` (last
`fifoDepth` frames, default 16, which is about 0.4 s at 48 kHz) or
`Exponential`. Neither averages a recording. A `Fifo` whose depth is the frame
count would, but it allocates `depth x numBins x 32 B`: 2 000 frames at 8 193
bins is 524 MB. G22 needs a third mode, `Cumulative`. It keeps running sums of
Sxx, Syy and Sxy with no drop-off and uses O(numBins) memory.

## 3. The physics that constrains the design

**Delay before the transform, not after.** A reference misaligned by tau inside
an N-sample window biases both the cross-spectrum and the coherence downward
(Seybert & Hamilton 1978, J. Sound Vib.; extended to windowed data by Trethewey
& Evensen 1984). The remedy those papers give is to align the data first, and
the engine already does this: the integer delay is applied before the FFT. The
fractional remainder stays in the phase as a slope of at most half a sample,
which is the same as the live path. Station 1 could not read Bendat & Piersol's
equation numbers, so this record does not cite one.

**Clocks.** A reference and a measurement recorded by different devices have
two sample clocks. Consumer and pro-audio crystals are rated at +/-10 to
+/-50 ppm, and one measured USB interface sat about 160 ppm off (station 1,
vendor and forum grade).

- Arithmetic: 50 ppm x 3 600 s = 0.18 s, which is 8 640 samples at 48 kHz.
- Any single bulk delay is therefore wrong across a recording.
- Even a residual drift of Delta samples spread evenly across the averages
  costs `20 log10 |sinc(f Delta / fs)|` (normalised sinc) of magnitude.
  Delta = 1 sample at 20 kHz / 48 kHz gives sinc(0.417) = 0.738, or -2.6 dB.

**Two channels of ONE file share a clock by construction.** This is the same
argument L4a used to ignore drift for a single duplex interface
(`docs/dsp/2026-08-30-sweep-ir-l4a.md` ~1143-1153). L4a told L4c to "detect it,
correct it, or refuse the file; it must not discover it". §5 D1 answers that
question.

**The acoustic path also moves.** Speed of sound changes about 0.17 %/K, so a
10 m path moves about 50 us (2.4 samples at 48 kHz) per kelvin. Over a whole
show the delay drifts for physical reasons, and averaging everything smears the
high end exactly like clock drift does. That is why G22 analyses a chosen
range, not a whole show.

## 4. Candidates

**A. One file, two channel indices (chosen).**

- The operator picks a multichannel WAV and which channel is the reference and
  which the measurement.
- The shared clock is guaranteed.
- Against: recorders that write one mono file per track are excluded in v1.
  Those files also share a clock, but the app cannot prove it from the files.

**B. Two files, one channel each, with drift detection or resampling.**

- Covers per-track recorders and two-device recordings.
- Against: detection needs at least two well-correlated windows far apart.
- Correction needs a resampler with a fitted ppm. That is a second DSP
  component with its own accuracy claims and its own tests.
- A file pair from two devices that is merely "detected" gets refused anyway,
  because the sinc loss above makes it useless without correction.
- Cost: about one more lane. It is deferred, not rejected (follow-up card).

**C. Play the WAV through the live engine (virtual soundcheck loopback).**

- Reuses everything, including MTW.
- Against: it needs a device and real-time playback (a one-hour file takes one
  hour). The result is a FIFO-16 snapshot, not a whole-range average, and it
  is not deterministic for a test.

**The obvious-looking option, argued against: average the whole file.**

- It is the simplest UI (no range controls).
- But a show-length file averages over temperature drift and over
  programme-dependent coherence. The operator's actual question is "what did
  the system do during song 3".
- A start + length range costs two numeric fields and bounds both the
  smearing and the work.

**FFT size, argued.**

- *The live fixed FFT (4096, 11.7 Hz bins)* matches STORE traces bin for bin,
  but it throws away what offline buys. There is data to spare, so the 4096
  window's 40 Hz-wide low-frequency bins are a choice, not a constraint.
- *MTW* has no stored form: `Trace` is uniform-bin only, and storing MTW would
  need a schema change. Rejected for v1.
- *Chosen: 16 384 samples, hop 4 096 (Hann, 75 % overlap).* That is 2.93 Hz
  bins at 48 kHz. `EqTraceGrid` and the TRANSFER overlay read each trace's own
  `binHz()` (`app/src/measure/EqTraceGrid.cpp:13`), and GATED traces already
  flow at 16 384.

## 5. Decisions

**D1. One file, two distinct channels.**

- The file needs at least two channels, and ref != meas.
- Formats: JUCE `WavAudioFormat` and `AiffAudioFormat` only. Lossy codecs are
  not registered, because they alter phase and timing.
- Any sample rate. fs comes from the header.
- RF64 is decoded by JUCE but not tested here: a test file would be larger than
  4 GB. The record says so rather than claiming coverage.
- Two-file input is refused by construction (the chooser takes one file). It is
  proposed as its own lane.

**D2. Range.**

- Start and length, in seconds, chosen by the operator. The default is the
  whole file.
- Read by streaming `AudioFormatReader::read` in blocks. The whole file is
  never loaded (one hour x 2 ch x float32 = 1.4 GB).

**D3. Engine.**

- `DualFftEngine` with fftSize 16 384, hop 4 096, Hann, H1, and the new
  `TransferAveraging::Cumulative`.
- Its `effectiveAverages()` is `fifoEffectiveAverages(window, hop, frameCount)`.
  A FIFO deep enough never to drop a frame IS the cumulative average. That
  identity is the core test: Cumulative after F frames equals Fifo with
  depth >= F, within a tolerance derived from summation order, not bitwise
  (`memory/a-bitwise-check-must-copy-the-expression-not-the-arithmetic.md`).
- Coherence goes through `makeSnapshot` only (`check_coherence_gate.cmake`).

**D4. Delay.**

- Up to three GCC-PHAT windows of `W = min(2^18, rangeLength)` samples, at the
  range's start, middle and end. Overlapping windows collapse to fewer when the
  range is short.
- Each window uses `suggestDelay`, with `DelayPolicy` lags limited to
  +/- round(1.0 s x fs).
- The bulk delay D is the delay of the highest-trust `Accepted` window. It is
  applied as `referenceDelaySamples` (both signs; |D| <= 1<<20).
- If no window is accepted, the operator gets a refusal that carries the best
  measured trust and the floor it needed.
- If two accepted windows disagree by more than one sample, the operator gets a
  refusal naming both delays: "the system changed or the channels do not share
  a clock -- pick a shorter range". The tolerance is one sample because the
  sinc loss of a one-sample spread is already -2.6 dB at 20 kHz (§3). Anything
  larger is worse, not a judgement call.

**D5. Minimum range.**

- The range must yield `effectiveAverages >= minimumEffectiveAverages` (8.0)
  AFTER the delay discard.
- The test computes the smallest qualifying frame count by calling
  `fifoEffectiveAverages`. It does not hand-derive it
  (`memory/a-default-must-be-run-through-the-gate-it-feeds.md`).
- Below that, the operator gets a refusal with the reachable count and the
  needed count.

**D6. Output.**

- A `Trace` with magnitude, phase and coherence, in library group `WAV`.
- Because it carries coherence, it is an EQ measurement candidate. That is
  intended.
- `CaptureMeta`:
  - `deviceName` = "WAV <basename> ch<M> vs ch<R>"
  - `averagingType` = "Cumulative"
  - `averagingDepth` = frame count
  - `effectiveAverages`
  - `appliedDelaySamples` = D
  - `sampleRate` from the file
- No schema bump: the file name rides in `deviceName`. A dedicated source-file
  field is a schema-v4 follow-up.
- Group `WAV` is not `SWEEP`, so the RT60, ALIGN and IR pickers (which need an
  IR) never list it.

**D7. Thread.**

- A worker (`std::async`, newest request wins, a cancel flag checked between
  blocks). Never the live `AnalysisThread`.
- The readout shows "analysing" until the result lands.

**D8. Operator path.**

- Rail: a `LOAD WAV` button beside `STORE`, which opens a `juce::FileChooser`
  (one file).
- Then a WAV row: `REF ch`, `MEAS ch`, `START s`, `LENGTH s`, `ANALYSE`, and a
  readout. The readout shows the delay in samples and ms (one decimal), the
  trust word as in Locate, Neff, and the trace name, or the refusal sentence.
- Tests drive a `performLoadWav(juce::File)` / `analyseWavForTest` seam. The
  chooser itself stays untested, like `launchSave`.

**D9. Readout numbers.** Delay in whole samples plus ms with one decimal; dB
with one decimal; Hz whole (CLAUDE.md "Reading out numbers").

## 6. Shipped

*Written against the shipped code, commits `26103c9`..`ead3ad9` plus the lane-end LOW
commit `bb7cef2`. Fable phase review over `925fb3a..ead3ad9`: 0 HIGH, 0 MEDIUM, 9 LOW.*

**Engine (D3).** `TransferAveraging::Cumulative` (`core/src/dsp/DualFftEngine.cpp`)
keeps double running sums of Sxx, Syy and Sxy. It divides by `frameCount` every
frame and allocates no FIFO slots; `fifoDepth` is ignored for it. Neff is
`fifoEffectiveAverages(window, hop, frameCount)`. `reset()` zeroes the sums and
re-arms the delay skip. The test (`core/tests/test_dualfft_cumulative.cpp`)
holds it to Fifo(depth F) and Fifo(depth F+7) within a derived 2Fu summation
bound, and against Fifo(depth F-1) as a must-differ.

**Delay (D4), as built** (`app/src/measure/WavTransfer.cpp`):

- W = min(2^18, range) samples. The windows are start, middle and end, and they
  collapse when the range is short:
  - one window below 2W (10.92 s at 48 kHz). DelayMoved CANNOT fire there.
  - two windows from 2W to 3W.
  - three windows only at 3W (16.38 s) or more.
- So a short song-length range gets no delay-movement check. This is a
  property of the design, not a bug: a shorter range also accumulates less
  drift.
- Lags are limited to +/- round(1 s x fs), also capped at 1<<20 (that cap binds
  only above 1.048 MHz).
- The D5 range check runs once at D = 0 before the delay stage, and again
  after |D| is known.
- Sign chain: `meas[n] = y[n - D]` -> `suggestDelay` gives +D -> the engine
  drops the first D measurement samples. The closed-form tests at D = +37 and
  D = -53 assert phase with no residual slope.

**Minimum range (D5).** F_min is the smallest frame count with
`fifoEffectiveAverages(Hann 16384, 4096, F) >= 8.0`. The test computes it. It is
F = 15 (Neff 8.068; F = 14 gives 7.549), i.e. 16384 + 14 x 4096 = 73 728
samples = 1.536 s at 48 kHz after the |D| discard. A positive length that rounds
to zero samples is its own refusal (`RangeRoundsToZero`); an empty WAV reads
"has no samples".

**UI (D8), as built:**

- The `LOAD WAV` row sits UNDER `STORE` on the rail, not beside it (rail width).
- The chooser filter is `*.wav;*.aif;*.aiff`.
- `START` and `LENGTH` accept digits and '.' only. `LENGTH` is prefilled with
  the file length floored to 10 ms, so "whole file" never rounds past the end.
  Empty means to the end.
- A mono file loads, shows the refusal sentence, and `ANALYSE` stays disabled.
- Readout: `delay N samples (x.x ms), <good|fair|poor> trust, N.N averages ->
  stored <name>`. The trust thresholds are Locate's.
- The worker opens its own reader per job. The message thread only probes the
  header at LOAD.
- Newest wins: start() cancels, waits and drops the old future, so a stale
  result cannot land.
- Polling runs on the existing 2 Hz timer.
- Cancel granularity: cancel is checked between 65 536-sample blocks and between
  delay windows. The uncancellable unit is one 2^18-sample window pair plus one
  PHAT, so a second `ANALYSE`, a new `LOAD` or closing the app mid-analysis
  blocks the message thread for that long.
- Test seams: `performLoadWavForTest`, `setControlsForTest`, `analyseClicked`,
  `waitForJobForTest` (not the plan's `analyseWavForTest`).

**Trace (D6).**

- Group `WAV`.
- `deviceName` = "WAV <file> ch<M> vs ch<R>", 1-based; `channelRoles` also
  1-based.
- `averagingType` "Cumulative". `SessionDecode` treats it as an opaque string,
  so it reloads.
- Shade: `TraceLibrary::assignSharedShade` with no group filter, i.e. the lowest
  index unused across the whole library, so it never sits on shade 0 beside the
  first SWEEP trace.

**Formats.** WAV 24-bit and float32 and AIFF 24-bit are tested end to end
(`app/tests_juce/test_wav_transfer_file.cpp`). RF64 is decoded by JUCE but
untested. Every worker exception is reported as ReadFailed.

**LOWs fixed in the same lane:**

- L1: the EQ target picker excludes `GATED`.
- L2: a GATED trace takes the lowest shade unused across SWEEP + GATED.
- L3: ALIGN NoSignal paths that never measured a confidence print no number.
  The `confidenceDb == 0.0` test also catches `Polarity.cpp`'s digitally-silent
  floor branch, which cannot happen on real audio. A measured-flag in core is
  the clean fix (follow-up).
