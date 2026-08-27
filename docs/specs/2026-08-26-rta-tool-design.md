# RTA Tool — design

*2026-08-26. Status: approved. Covers Phase 0 and Phase 1 in detail; later
phases are a roadmap and get their own spec before they start.*

## Problem

Live-sound engineers tuning a PA need a dual-FFT analyser. The commercial
standard (Smaart) is expensive; the open-source options each fall short in a way
that matters:

| | gap |
|---|---|
| [Open Sound Meter][osm] | no ASIO; DSP entangled with Qt/QML so accuracy cannot be verified independently |
| [OpenOptimize][oo] | dormant since 2019 |
| [Friture][friture] | a visualiser, not a measurement instrument — no transfer function |
| [REW][rew] | excellent room analysis, but Java and not built for live low-latency dual-FFT |

None of them can prove their numbers are correct without a sound card in the loop.

## The goal, as the owner set it

**Build the best RTA analyser there is: every feature superior to what is on the
market.** Stated 2026-08-27.

An ambition is not a specification. To guide a decision it has to say what
"better" means, against whom, and how anyone would know. So each area below
names the incumbent, the claim, and the measurement that settles it.

| Area | Who currently leads | What "better" means here | How it gets checked |
|---|---|---|---|
| Band accuracy | Class-1 sound level meters; Smaart for display | Ship **both** an FFT banding path and an IEC 61260 **class 0** filter bank, side by side. No analyser on the market offers both from one input | per-band verdict against the IEC 61260-1 mask, on CI |
| Provability | nobody | Every DSP claim asserted against an independent reference, on CI, with no sound card | the test count, and what each test is pinned to |
| Low-frequency resolution | Smaart MTW: better than 1/48 octave from 60 Hz up, ~800 points | match it, then beat it at the bottom octave | compare MTW output against a long fixed FFT in the overlap region |
| Real-time safety | Smaart | audio callback does nothing but copy | dropout counters under load |
| Multichannel ASIO | closed-source tools only; Open Sound Meter has none | open source, with ASIO, since the SDK went GPLv3 in Oct 2025 | runs at a show |
| Honesty of the display | most tools draw under-resolved bands with no qualification | mark them, visibly, not by tint alone | the specimen render |
| Licence | commercial, per-seat | AGPL-3.0 | — |

**The rule that keeps this from becoming a slogan:** superiority is a claim, and
this project does not make claims it has not measured. A feature that cannot be
shown to beat the incumbent ships as parity and the feature ledger says so. A
claim without a number beside it is a marketing line, and this document is not
for marketing.

## Goals

1. RTA, SPL metering, signal generation, dual-FFT transfer function
   (magnitude / phase / coherence), and impulse-response analysis.
2. Multichannel ASIO on Windows; CoreAudio, ALSA and JACK elsewhere.
3. Microphone calibration files and dBSPL calibration.
4. **Every DSP claim provable on CI without hardware.**

## Non-goals

Not a DAW, not an EQ processor, not a room-correction filter generator. It
measures and displays; changing the system is the engineer's job.

## Architecture

```
PRJ010-RTA-TOOL/
├── core/     rta_core -- pure C++20 measurement engine, no framework deps
├── app/      JUCE application shell: audio I/O, threading, GUI
├── tools/    gen_golden.py -- NumPy/SciPy reference generator (dev only)
└── docs/     specs, DSP derivations, handoffs
```

### The load-bearing constraint

`core/` must never include JUCE, Qt, or any audio-device API. It takes
`std::span<const float>` and returns numbers.

This buys three things:

1. **Provability.** Algorithms are asserted against closed-form identities,
   published standards, and golden vectors — on CI, with no sound card. This is
   the project's differentiator; everything else follows from it.
2. **Reversibility.** The GUI framework choice stops being fatal. Replacing the
   shell does not touch the engine.
3. **Reuse.** The same engine serves a future VST/AU plugin build and a CLI
   batch processor without modification.

Enforced by the `core_has_no_framework_deps` ctest, which greps `core/` and exits
non-zero on a violation. Verified by deliberately injecting one.

### Threading

```
[audio callback]  ->  lock-free SPSC ring buffer  ->  [analysis thread]  ->  snapshot
   memcpy only                                          runs rta_core       atomic swap
   no malloc, no lock, no file I/O                                                |
                                                                                  v
                                                                            [UI, 60 fps]
```

Running an FFT inside the audio callback is the standard failure of home-grown
analysers: it drops samples and corrupts the very measurement being taken, at a
show, under time pressure.

## Phase 1 scope — `core/`

| Module | Contents | Reference standard |
|---|---|---|
| `dsp/RingBuffer` | lock-free SPSC, wait-free writer | — |
| `dsp/Window` | Hann, Hamming, Blackman-Harris, Flat-top, Tukey, with amplitude (ACF) and energy (ECF) correction factors and ENBW | Harris (1978) |
| `dsp/Fft` | real-input FFT, precomputed twiddles, behind an interface so a faster backend can be swapped in after profiling | — |
| `dsp/SpectrumEngine` | overlap-add, Welch averaging (power and exponential) | — |
| `dsp/OctaveBands` | 1/1 to 1/48 octave; FFT-bin summation and true filterbank modes | IEC 61260 |
| `dsp/Biquad`, `dsp/Filter` | RBJ cookbook, cascades | — |
| `dsp/Weighting` | A / C / Z, analog pole-zero into bilinear transform | IEC 61672-1 |
| `meter/LevelMeter` | Fast / Slow / Impulse detectors, peak, true-peak, RMS | IEC 61672-1 |
| `meter/Leq` | Leq, LAeq, LCeq, LZeq, Lmax/Lmin, L10/L50/L90, Lpeak | IEC 61672-1 |
| `gen/SignalGenerator` | pink, white, sine, dual sine, Farina log sweep, MLS | — |
| `cal/CalibrationCurve` | `.frd` / `.txt` / `.cal` parsing, log-frequency interpolation | — |
| `cal/SplCalibration` | 94 / 114 dB reference tone to dBSPL offset | IEC 60942 |

## Phase 1 scope — `app/`

`AudioEngine` (wraps `juce::AudioDeviceManager`), `ChannelRouter` (per-input role:
Measurement / Reference / Unused, multichannel from the start), `AnalysisThread`,
and the UI: `LogFreqPlot` (OpenGL-backed), `MeterStrip`, `GeneratorPanel`,
`DevicePanel`, `CalibrationPanel`.

## Acceptance criteria — Phase 1

Each row is an automated test that runs on CI.

| Check | Passes when |
|---|---|
| 1 kHz sine at −20 dBFS through a Hann window | peak at 1000 Hz ±0.5 Hz, magnitude −20 dBFS ±0.1 dB (this is what validates ACF) |
| pink noise through 1/3-octave bands | all bands equal within ±0.5 dB |
| A-weighting at 31.5 Hz … 8 kHz | inside IEC 61672-1 Class 1 tolerance |
| Fast / Slow detector, 1 kHz burst | rise and decay times per IEC 61672-1 |
| Leq of a reference WAV | matches the Python reference within ±0.1 dB |
| generator pink-noise slope | −3.01 dB/octave ±0.2 dB |
| Farina sweep then deconvolution | recovers a unit impulse, SNR > 60 dB |

Plus a manual loopback check — output patched back to input on the same
interface — that exercises the whole chain on real hardware.

## Roadmap

| Phase | Contents |
|---|---|
| 0 | repo, CMake, CI on three OSes, `rta_core` skeleton, test harness, licence, docs |
| 1 | RTA + SPL + generator (this spec) |
| 2 | dual-FFT: cross-spectrum, `H = Sxy/Sxx`, coherence, delay finder, phase unwrap, group delay |
| 3 | multi-time-window engine (decimation cascade, per-band FFT sizes, band stitching) |
| 4 | impulse response: Farina deconvolution, ETC, Schroeder integration with Lundeby truncation, EDT/T20/T30, C50/C80/D50 |
| 5 | spectrograph, trace library, target curves, trace maths, session persistence |
| 6 | full multichannel routing, presets, remote API, i18n (VI/EN), installers |

MTW is deliberately *after* a working fixed-window dual-FFT. Fixed-window
measurement is usable at a real show on its own, and separating the two means a
band-stitching bug cannot be confused with a cross-spectrum bug.

## Licence

AGPL-3.0-or-later. See README.

[osm]: https://github.com/psmokotnin/osm
[oo]: https://github.com/John-Benton/OpenOptimize
[friture]: https://github.com/tlecomte/friture
[rew]: https://www.roomeqwizard.com/
