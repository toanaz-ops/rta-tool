# Implementation plan — platform/AudioIo + the first pixel of real measurement

*2026-08-27. Station 3 of the pipeline in `docs/reports/README.md`. Derived from
the approved decision record `docs/specs/2026-08-27-platform-audioio-design.md`
(architecture A: JUCE-native callback + per-channel SPSC rings + one-struct
snapshot). Nothing here re-opens that decision; this document turns it into
exact files, exact tests, exact acceptance numbers.*

---

## Amendment A1 (Fable review, before build)

T1's deliverable is renamed to avoid a head-on collision with the generator
implementation plan being drafted in parallel, which owns
`core/.../gen/{Prng,Oscillator,Noise,Sweep,Mls}` and its own
`test_generator.cpp`:

- `core/include/rta/gen/SignalGenerator.h`  → `core/include/rta/gen/Synthetic.h`
- `core/src/gen/SignalGenerator.cpp`        → `core/src/gen/Synthetic.cpp`
- `core/tests/test_generator.cpp`           → `core/tests/test_synthetic.cpp`
- class names: `rta::gen::SyntheticPink`, `rta::gen::SyntheticSine`

The two pinks are different on purpose and must both exist: `SyntheticPink` is
frequency-domain, exact by construction, block-looped — a deterministic test
feed for snapshots and CI. The product generator's pink (Kellet, real-time,
never repeats) arrives with the generator plan. Neither replaces the other;
the Synthetic header must say so.

Every later reference to T1's files in this plan reads through this rename.

## 0. Definition of done

One deliverable, in one sentence: **a `rtatool` window whose log-frequency band
plot is driven by real audio from a real interface, and whose entire analysis
chain can also be driven by deterministic synthetic input with no hardware at
all, on CI, and offscreen.**

Concretely, all six must hold at once:

1. `ctest --test-dir build -C Release` is green, including the new cases in §5.
2. `rtatool_snapshot` writes **both** `shots/specimen.png` (unchanged design
   specimen) and `shots/rta-view.png` (the real RTA view fed by synthetic pink
   noise), and the second is identical run to run on the same machine.
3. `rtatool.exe` launched with a real interface present shows moving bars.
4. `rtatool.exe` launched with **no** interface present still opens, reports the
   fault in the device panel, and can be switched to synthetic input.
5. Zero new warnings at `/W4`.
6. No file added or modified by this work exceeds **400 lines**.

Explicitly **out of scope** (do not let it creep in): transfer function,
coherence, delay finder, SPL calibration, weighting curves, detectors, Leq,
generator output *to a device*, presets, recall, ASIO SDK integration beyond the
existing `RTA_ENABLE_ASIO` switch.

---

## 1. Layering decisions this plan makes (and why)

The decision record fixes the concurrency architecture. Three placement
questions it left open are settled here, because getting them wrong is what
makes the deliverable untestable.

### 1.1 `platform/` is split into two targets, not one

```
platform/types/   rta_platform_types   NO JUCE. ChannelConfig, CaptureBus,
                                       DeviceState, Fault. Built ALWAYS.
platform/         rta_platform         JUCE. AudioIo only. Built with
                                       RTA_BUILD_APP=ON.
```

Why: CI configures with `-DRTA_BUILD_APP=OFF` (`.github/workflows/ci.yml`), so
anything behind that switch is never compiled by CI, let alone tested. The
parts of the I/O layer the decision record identifies as dangerous — role
bounds-checking against the channel count *the callback actually received*,
counting rather than swallowing short writes, draining every ring on a device
change — are pure integer and float-array logic. Put them in a JUCE-free target
and they are provable on three platforms with no sound card. Leave them inside
`AudioIo.cpp` and the only way to check them is to plug something in.

`rta_platform_types` gets the same architectural guard `rta_core` has (§4.3).

### 1.2 The audio callback body is a method on `CaptureBus`, not on `AudioIo`

`AudioIo::audioDeviceIOCallbackWithContext` is reduced to: `ScopedNoDenormals`,
then one call to `bus_.pushFromCallback(inputChannelData, numInputChannels,
numSamples)`, then clear the output buffers. Everything the record warns about
lives inside `pushFromCallback`, which takes `const float* const*` — plain C
arrays a test can build by hand.

`CaptureBus` owns: the rings, the role atomics, the per-channel drop counters,
the validity atomic, and the configuration epoch. `AudioIo` owns: the
`AudioDeviceManager`, the device enumeration surface, and the fault record.
`SyntheticInput` (§3.4) writes into the same `CaptureBus`. That single seam is
what makes "the analysis chain is drivable without hardware" true rather than a
second code path that drifts.

### 1.3 The pure half of `app/` is compiled into a test target of its own

`Snapshot`, `Levels`, `Analyser`, `SyntheticSnapshot` and `PlotGeometry` must
not include JUCE. They are then compiled directly (not linked out of `rtatool`,
which is a `juce_add_gui_app` and carries JUCE's module defines) into
`rtatool_analysis_tests`, built whenever `RTA_BUILD_TESTS` is on — so CI runs
the measurement logic on Linux, macOS and Windows even though it never builds
the GUI.

This is the same reasoning `app/CMakeLists.txt` already records for listing
`MainComponent.cpp` in two targets: two short source lists is the cheaper
problem.

### 1.4 Conventions this plan fixes, so nobody invents them at 2 a.m.

| Thing | Decision | Reason |
|---|---|---|
| Level reference | **dBFS with a full-scale SINE = 0.0 dB**: `10*log10(power) + 3.0103` | Makes the ledger's acceptance line ("1 kHz @ -20 dBFS reads -20 dBFS") assert exactly -20.0, and matches what every analyser on a rack shows. Defined once, in `Levels.h`. |
| Level floor | `-120.0` dB, clamped | `log10(0)` on a silent channel otherwise poisons the whole plot with `-inf` / NaN. |
| Frequency labels | whole hertz, no decimals, no `k` abbreviation: `20 50 100 200 500 1000 2000 5000 10000 20000` | CLAUDE.md "Reading out numbers". A `k` abbreviation would be a new convention; this plan does not invent one. |
| dB labels | one decimal, e.g. `-20.0` | CLAUDE.md, literally. Applies to gridline labels and to the readout line. |
| Pink noise | generated in the **frequency domain** (magnitude proportional to `k^-1/2`, seeded random phase, one `RealFft::inverse`, block looped) | Makes the -3.01 dB/octave slope exact **by construction**, so the test is a closed-form assertion instead of a tolerance on a filter approximation. A one-pole ladder (Kellet / Voss-McCartney) is ±0.05 dB at best and its error is frequency-dependent, which is indistinguishable from a banding bug. **Do not silently swap this for a filter.** |
| PRNG | `std::mt19937` seeded explicitly, mapped to `[0,1)` by hand — **no `std::uniform_real_distribution`** | The engine is specified bit-exactly by the standard; the distributions are not, so a distribution makes the snapshot differ between libstdc++ and MSVC. |
| Analysis publish rate | at most 20 Hz (50 ms) | A view repainting faster than the eye resolves costs CPU during a show and buys nothing. |
| Ring capacity | `bit_ceil(max(8 * bufferSize, 4 * fftSize))` samples per channel | ~341 ms at 48 kHz with a 4096-point FFT: enough slack to survive a GUI stall without dropping. Worst case 64 ch x 16384 x 4 B = 4 MB. |
| `kMaxChannels` | 64 | Covers a MADI or Dante interface. Fixed-size role array, so no allocation anywhere near the callback. |
| Ice blue | **not used by this deliverable** | Reserved for one meaning only (CLAUDE.md / SODIUM RACK); this view has no such meaning yet. |

---

## 2. Every rule in the decision record, mapped to where it is enforced

The record's table is the acceptance criteria for the I/O layer. Each row lands
as a task **and** as either a test or a named manual step.

| Rule (decision record) | Task | Enforced by |
|---|---|---|
| `ScopedNoDenormals` is the FIRST statement in the callback | T7 | ctest `audioio_scoped_no_denormals_is_first` — greps `platform/src/AudioIo.cpp` and fails if any statement precedes it in the callback body (§4.3) |
| `audioDeviceAboutToStart` retargets rate-dependent state AND drains every ring | T2, T7 | `test_capture_bus.cpp` → "prepare drains every ring and bumps the epoch"; manual step M3 |
| Read the device type BACK from the manager; never echo the requested string | T7 | Manual step M1 (hardware-verified only) |
| `setAudioDeviceSetup` returns EMPTY string on success | T7 | Manual step M1 |
| The callback defensively checks a validity atomic | T2, T7 | `test_capture_bus.cpp` → "an inactive bus writes nothing" |
| Buffer-size / rate changes need a reload path outside the callback | T7, T9 | Manual step M4 |
| No auto-reconnect in the I/O layer; report the fault, let the app own retry | T7, T9 | `AudioIo` has no retry timer (review); manual step M5 |
| Roles bounds-checked against the channel count the callback RECEIVED | T2 | `test_channel_config.cpp` → "a role beyond the callback's channel count is ignored" |
| Short ring writes are counted, not swallowed | T2 | `test_capture_bus.cpp` → "a full ring counts the whole block as dropped" |
| Snapshot is ONE struct published by atomic pointer swap | T3, T8 | `test_analyser.cpp` → "consecutive publishes hand out different immutable pointers" |

---

## 3. Exact file list, with line budgets

Hard cap 400, aim 300 (CLAUDE.md). Budgets include the SPDX header and the
comment blocks the project's conventions require. A file that overshoots its
budget by more than ~20% means the split in this plan was wrong — say so in the
report rather than letting it grow.

### 3.1 New in `core/` — the deterministic signal source

| File | Budget | Contents |
|---|---|---|
| `core/include/rta/gen/SignalGenerator.h` | 130 | `rta::gen::PinkNoise` (spectral-domain, seeded, looping block) and `rta::gen::Sine` (exact frequency and amplitude). Both expose `void render(std::span<float> out)`, which continues where the last call stopped, and `void reset()`. |
| `core/src/gen/SignalGenerator.cpp` | 170 | Pink noise: fill `numBins` with magnitude `k^-1/2` (DC = 0), phase from the hand-mapped mt19937, one `RealFft::inverse`, normalise the block to unit RMS, then loop it. Sine: phase accumulator in `double`, wrapped, never restarted per block. |

`core/CMakeLists.txt`: add `src/gen/SignalGenerator.cpp`. The header lives under
`core/include/rta/gen/` per the "Headers under `core/include/rta/<area>/`"
convention — a new area, matching the ledger's `gen/SignalGenerator` entry.

### 3.2 New — `platform/types/` (no JUCE, always built)

| File | Budget | Contents |
|---|---|---|
| `platform/types/CMakeLists.txt` | 30 | `add_library(rta_platform_types STATIC ...)`, alias `rta::platform_types`, PUBLIC include dir, `cxx_std_20`. Links `rta::core` (it uses `rta::dsp::RingBuffer`). |
| `platform/types/include/rta/platform/ChannelRole.h` | 60 | `enum class ChannelRole { Unused, Measurement, Reference }`, `kMaxChannels = 64`, `std::string_view toString(ChannelRole)`. |
| `platform/types/include/rta/platform/ChannelConfig.h` | 120 | Roles as `std::array<std::atomic<int>, kMaxChannels>`; set/get on the message thread; `snapshot(int numChannelsReceived, std::array<ChannelRole, kMaxChannels>& out)` reading **relaxed** and forcing `Unused` at and beyond `numChannelsReceived`. Non-copyable. Plus `firstChannelWithRole(ChannelRole)`. |
| `platform/types/include/rta/platform/CaptureBus.h` | 170 | Owns `std::vector<rta::dsp::RingBuffer<float>>`, a `ChannelConfig`, `std::array<std::atomic<std::uint64_t>, kMaxChannels> drops_`, `std::atomic<bool> active_`, `std::atomic<std::uint64_t> epoch_`, `std::atomic<double> sampleRate_`. API: `prepare(double sampleRate, int numChannels, std::size_t ringCapacity)`, `setActive(bool)`, `pushFromCallback(const float* const* in, int numChannels, int numSamples) noexcept`, `ring(int ch)`, `dropCount(int ch)`, `totalDrops()`, `epoch()`, `sampleRate()`, `config()`. |
| `platform/types/src/CaptureBus.cpp` | 150 | `prepare` allocates, `reset()`s every ring and bumps `epoch_`; `pushFromCallback` does the role snapshot, then per-channel `RingBuffer::write`, adding `numSamples` to `drops_[ch]` on a `false` return. |
| `platform/types/include/rta/platform/DeviceState.h` | 110 | `struct DeviceState { std::string typeName, deviceName; double sampleRate; int bufferSize; int numInputChannels, numOutputChannels; bool open; double cpuLoad; std::vector<std::string> inputChannelNames; }` and `struct Fault { enum class Kind { None, OpenFailed, DeviceError, DeviceStopped }; Kind kind = Kind::None; std::string message; std::uint32_t sequence = 0; }`. `std::string`, not `juce::String`: this is the app-facing side of the layer. |

### 3.3 New — `platform/` (JUCE, `RTA_BUILD_APP=ON`)

| File | Budget | Contents |
|---|---|---|
| `platform/CMakeLists.txt` | 40 | `add_library(rta_platform STATIC ...)`, alias `rta::platform`, links `rta::platform_types`, `rta::core`, `juce::juce_audio_devices`, `juce::juce_recommended_config_flags`, `juce::juce_recommended_warning_flags`. ASIO handling mirrored from `app/CMakeLists.txt` (`JUCE_ASIO=1` plus the include dir when `WIN32 AND RTA_ENABLE_ASIO`). |
| `platform/include/rta/platform/AudioIo.h` | 190 | The class: a `juce::AudioIODeviceCallback` owning a `juce::AudioDeviceManager` and a `CaptureBus`. Surface fixed in §3.3.1. |
| `platform/src/AudioIo.cpp` | 240 | Lifecycle, the three callback overrides, fault recording. |
| `platform/src/AudioIo_Devices.cpp` | 220 | Enumeration and configuration: type / device / rate / buffer getters and setters, `currentState()`. Split out purely for the 400-line cap; same class. |

#### 3.3.1 `AudioIo` public surface (fix it here, do not improvise)

```cpp
bool  start();                       // opens with the desired type/device
void  stop();
bool  isRunning() const;

void  setDesiredDeviceType(std::string);   // applied on the next start()
void  setDesiredDevice(std::string);
bool  setSampleRate(double);         // false = refused; restarts the device
bool  setBufferSize(int);            // false = refused; restarts the device

std::vector<std::string> availableDeviceTypeNames();
std::vector<std::string> availableDeviceNames();
std::vector<double>      availableSampleRates();
std::vector<int>         availableBufferSizes();

DeviceState currentState() const;    // type read BACK from the manager
Fault       lastFault() const;       // mutex-guarded; sequence increments
CaptureBus& bus();                   // the analysis side reads from here
```

Non-`const` where JUCE forces it (`getAvailableDeviceTypes()`,
`AudioIODevice::getAvailableSampleRates()`), exactly as handsfree's
`AudioEngine` documents. Conversion at the boundary is
`juce::String::toStdString()`, which is UTF-8 — a device name carrying
Vietnamese characters must survive the round trip (global rule 6).

### 3.4 New — `app/src/measure/`

| File | Budget | JUCE? | Contents |
|---|---|---|---|
| `app/src/measure/Levels.h` | 60 | no | `levelDbFs(double power)`, `kLevelFloorDb = -120.0`, `kFullScaleSineOffsetDb = 3.0102999566398120`. Header-only. One definition of "dB" for the whole app. |
| `app/src/measure/Snapshot.h` | 110 | no | `struct BandReading { float centreHz, lowerHz, upperHz, levelDb; bool underResolved; }` and `struct Snapshot { std::uint64_t sequence; double sampleRate; std::size_t fftSize; int fraction; std::vector<BandReading> bands; std::vector<float> spectrumDb; bool hasReference; std::vector<BandReading> referenceBands; std::uint64_t framesAnalysed, droppedSamples; float peakBandLevelDb, peakBandCentreHz; }`. A value type, published as `shared_ptr<const Snapshot>`. |
| `app/src/measure/Analyser.h` | 110 | no | `Analyser::Config` (fftSize 4096, hopSize 1024, sampleRate, fraction 3, 20..20000 Hz, Hann, Exponential, 0.5 s) and the class: `pushMeasurement(span)`, `pushReference(span)`, `reset()`, `publish(std::uint64_t droppedSamples)`, `framesAnalysed()`. |
| `app/src/measure/Analyser.cpp` | 200 | no | One `SpectrumEngine` + one `BandWeights` per role, plus preallocated scratch. `publish()` runs `BandWeights::apply` on `density()` (**never** on `spectrum()` — trap T-2), converts through `Levels.h`, copies `BandWeights::band(i).underResolved`, fills `spectrumDb`, increments the sequence. |
| `app/src/measure/SyntheticSnapshot.h` | 70 | no | `struct SyntheticSpec { enum class Source { PinkNoise, Sine }; Source source = PinkNoise; double sineHz = 1000.0; double amplitude = 0.1; std::uint32_t seed = 0x5EED; double seconds = 4.0; Analyser::Config analysis; };` and `std::shared_ptr<const Snapshot> makeSyntheticSnapshot(const SyntheticSpec&);` |
| `app/src/measure/SyntheticSnapshot.cpp` | 90 | no | Offline: generate → `pushMeasurement` in 1024-sample chunks → `publish(0)`. No threads, no timing, deterministic. |
| `app/src/measure/SnapshotSource.h` | 60 | no | `struct SnapshotSource { virtual ~SnapshotSource() = default; virtual std::shared_ptr<const Snapshot> latest() const = 0; };` plus `StaticSnapshotSource`, which holds one — used by the snapshot tool. |
| `app/src/measure/AnalysisThread.h` | 110 | **yes** | `class AnalysisThread : public juce::Thread, public SnapshotSource`. Ctor takes `rta::platform::CaptureBus&` and an `Analyser::Config`. |
| `app/src/measure/AnalysisThread.cpp` | 190 | **yes** | `run()`: `wait(10)`; if `bus.epoch()` changed, rebuild the `Analyser` at the new rate and drain; drain each role's ring into scratch allocated once in the ctor; publish at most every 50 ms through `std::atomic<std::shared_ptr<const Snapshot>>`. Whole body inside `try / catch` (trap T-3). |
| `app/src/measure/SyntheticInput.h` | 80 | **yes** | `class SyntheticInput : public juce::Thread`. Writes generator output into a `CaptureBus` in `bufferSize`-sample blocks paced by `wait()`, so the live pipeline runs with no device. |
| `app/src/measure/SyntheticInput.cpp` | 130 | **yes** | Calls `bus.prepare(rate, 2, capacity)` and `bus.setActive(true)` on start, then writes through the same `pushFromCallback` entry point the driver uses — one write path, not two. |

### 3.5 New / changed — `app/src/view/`

| File | Budget | JUCE? | Contents |
|---|---|---|---|
| `app/src/view/PlotGeometry.h` | 130 | no | `struct PlotGeometry { float left, right, top, bottom; double fLowHz = 20.0, fHighHz = 20000.0, dbTop = 0.0, dbBottom = -90.0; float xForHz(double) const; double hzForX(float) const; float yForDb(double) const; };` plus `std::vector<double> decadeTicks(double lo, double hi)` returning the 1-2-5 sequence. Pure `double` math, no JUCE. |
| `app/src/view/MeasureColours.h` | 50 | yes | App-side semantic names built **from** `az::ui` tokens: `trace = az::ui::accent`, `underResolved = az::ui::faded`, `grid = az::ui::border`, `axisText = az::ui::dim`. This file exists so the words "trace" and "coherence" never appear inside `ui/az_ui/`. |
| `app/src/view/PlotAxes.h` | 50 | yes | `drawGrid`, `drawFrequencyLabels`, `drawLevelLabels`, all taking `const PlotGeometry&`. |
| `app/src/view/PlotAxes.cpp` | 170 | yes | Hairline grid consistent with `az::ui::drawEngravedDivider`; frequency labels as whole hertz in `az::ui::monoFont`; level labels one decimal every 10 dB. |
| `app/src/view/RtaView.h` | 100 | yes | `class RtaView : public juce::Component, private juce::Timer`. Ctor takes `const SnapshotSource&`. `setSource`, `paint`, `resized`, `timerCallback` (30 Hz, repaint **only** when `sequence` changed), and `renderTo(juce::Graphics&, juce::Rectangle<int>)` so the snapshot tool can draw it with no timer running. |
| `app/src/view/RtaView.cpp` | 280 | yes | Bars from `bands`, geometry from `PlotGeometry`, readout line along the top, under-resolved marking per §3.5.1. |
| `app/src/view/DevicePanel.h` | 90 | yes | `class DevicePanel : public juce::Component, private juce::Timer`. Four `juce::ComboBox` (type, device, rate, buffer), a synthetic-input toggle, a fault line. Polls `AudioIo::lastFault().sequence` at 4 Hz. |
| `app/src/view/DevicePanel.cpp` | 250 | yes | Population and change handlers. Every combo is repopulated from `AudioIo::currentState()` **after** a change, never from the requested value (trap: the record's "read the type BACK" rule is a UI rule too). |
| `app/src/view/ChannelRoleTable.h` | 80 | yes | `class ChannelRoleTable : public juce::Component, private juce::ListBoxModel`. Rows are device input channels; a click cycles `Unused → Measurement → Reference → Unused`. |
| `app/src/view/ChannelRoleTable.cpp` | 200 | yes | Row painting, with `az::ui::elideMiddle` for channel names — that helper exists precisely for "Analogue 1" against "Analogue 11". |

#### 3.5.1 Under-resolved bands: a boundary, not a tint

`BandWeights` already flags any band spanning fewer than
`kMinBinsPerBand = 3.0` transform bins. Tint alone is not enough — on a dark
panel, at six feet, a 20% desaturation is invisible. Draw **all three**:

1. A **vertical engraved boundary line** at the upper edge frequency of the
   highest-frequency under-resolved band: one dark hairline with the faint
   sheen line beneath it, the same bevel `az::ui::drawEngravedDivider` uses.
2. Every under-resolved bar drawn as a **1 px outline only**, no fill, in
   `MeasureColours::underResolved`, so it reads as "measured but not resolved"
   rather than as a solid value.
3. A tracked-uppercase legend `RESOLUTION LIMIT` in `az::ui::legendFont`,
   left-aligned immediately right of the boundary line, with the crossover
   frequency as whole hertz.

If no band is under-resolved, draw none of the three. (At the default 4096 @
48 kHz down to 20 Hz, several bottom bands **will** be flagged — that is the
expected picture, not a bug.)

### 3.6 Changed — existing files

| File | Change | Lines after |
|---|---|---|
| `app/src/MainComponent.*` → `app/src/dev/SpecimenComponent.*` | `git mv`, rename the class to `SpecimenComponent`, update the doc comment ("Phase 0 window contents" → "the design-system specimen sheet, reached from the developer menu"). **No other change** — a rename plus a behaviour change in one commit is a rename nobody can review. | 37 / 283 (unchanged) |
| `app/src/MainComponent.h` (new file, same path) | The real window contents: owns `AudioIo`, `AnalysisThread`, `SyntheticInput`, `DevicePanel`, `ChannelRoleTable`, `RtaView`. **Member declaration order is load-bearing** (trap T-1). | 110 |
| `app/src/MainComponent.cpp` (new file, same path) | Wiring and layout only. No DSP, no drawing beyond the background — "composition root: wiring + view models". | 210 |
| `tools/snapshot.cpp` | Render two components: `SpecimenComponent` → `specimen.png` (path unchanged) and `RtaView` fed by `makeSyntheticSnapshot` → `rta-view.png`. Factor the per-component work into a local `renderComponent(juce::Component&, const juce::File&, int, int)` so both go through one code path, keeping both existing traps handled (manual `resized()`; `createComponentSnapshot(..., true)`). | 130 |
| `app/CMakeLists.txt` | New sources for both targets; link `rta::platform` into `rtatool` only. `rtatool_snapshot` must **not** link `juce_audio_devices` — it renders from a synthetic snapshot, so it needs `rta::core` + `az_ui` and nothing more. | 150 |
| `CMakeLists.txt` (root) | `add_subdirectory(platform/types)` unconditionally after `core`; `add_subdirectory(platform)` inside the `RTA_BUILD_APP` block before `ui`; `add_subdirectory(platform/tests)` and `add_subdirectory(app/tests)` inside the `RTA_BUILD_TESTS` block. | 80 |
| `core/CMakeLists.txt` | add `src/gen/SignalGenerator.cpp`. | 32 |
| `core/tests/CMakeLists.txt` | add `test_generator.cpp`. | 41 |
| `core/tests/check_no_framework_deps.cmake` | Add an optional `LABEL` variable (default `rta_core`) used in the two messages, so the same script can guard `platform/types`. Three lines, no logic change. | 40 |
| `docs/reports/`, `docs/features/FEAT-phase1-rta-spl-generator.md`, `memory/MEMORY.md`, `docs/HANDOFF.md` | Updated by the closing task T13, not before. | — |

### 3.7 New — test files

| File | Target | Budget |
|---|---|---|
| `core/tests/test_generator.cpp` | `rta_core_tests` | 160 |
| `platform/tests/CMakeLists.txt` | — | 40 |
| `platform/tests/check_callback_shape.cmake` | — | 50 |
| `platform/tests/test_channel_config.cpp` | `rta_platform_tests` | 130 |
| `platform/tests/test_capture_bus.cpp` | `rta_platform_tests` | 220 |
| `app/tests/CMakeLists.txt` | — | 40 |
| `app/tests/test_levels.cpp` | `rtatool_analysis_tests` | 80 |
| `app/tests/test_analyser.cpp` | `rtatool_analysis_tests` | 250 |
| `app/tests/test_synthetic_snapshot.cpp` | `rtatool_analysis_tests` | 90 |
| `app/tests/test_plot_geometry.cpp` | `rtatool_analysis_tests` | 130 |

---

## 4. Exact CMake changes

### 4.1 Root `CMakeLists.txt`

```cmake
add_subdirectory(core)

# platform/types is JUCE-free on purpose, so it builds and is tested on CI
# even when RTA_BUILD_APP is OFF. See docs/plans/2026-08-27-... §1.1.
add_subdirectory(platform/types)

if(RTA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)      # fetches Catch2; must stay first
    add_subdirectory(platform/tests)
    add_subdirectory(app/tests)
endif()

if(RTA_BUILD_APP)
    ... (JUCE acquisition, unchanged) ...
    add_subdirectory(platform)
    add_subdirectory(ui)
    add_subdirectory(app)
endif()
```

`app/tests` sits **outside** the `RTA_BUILD_APP` guard deliberately: it compiles
only the JUCE-free sources of §3.4, so it needs no JUCE and CI runs it.

### 4.2 `app/tests/CMakeLists.txt`

```cmake
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The JUCE-free half of app/ -- measurement view models and plot geometry --
# compiled straight into a test target. It is NOT linked out of `rtatool`:
# that is a juce_add_gui_app and carries JUCE's module defines, and this target
# must stay buildable with RTA_BUILD_APP=OFF so CI exercises it.
add_executable(rtatool_analysis_tests
    test_levels.cpp
    test_analyser.cpp
    test_synthetic_snapshot.cpp
    test_plot_geometry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Analyser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/SyntheticSnapshot.cpp
)
target_include_directories(rtatool_analysis_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(rtatool_analysis_tests PRIVATE
    rta::core Catch2::Catch2WithMain)
catch_discover_tests(rtatool_analysis_tests)
```

`platform/tests/CMakeLists.txt` is the same shape, linking
`rta::platform_types` and `Catch2::Catch2WithMain`.

### 4.3 New guard tests

```cmake
# platform/tests/CMakeLists.txt
add_test(NAME platform_types_has_no_framework_deps
    COMMAND ${CMAKE_COMMAND}
            -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../types
            -DLABEL=rta_platform_types
            -P ${CMAKE_SOURCE_DIR}/core/tests/check_no_framework_deps.cmake)

if(RTA_BUILD_APP)
    add_test(NAME audioio_scoped_no_denormals_is_first
        COMMAND ${CMAKE_COMMAND}
                -DSOURCE=${CMAKE_SOURCE_DIR}/platform/src/AudioIo.cpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_callback_shape.cmake)
endif()
```

`check_callback_shape.cmake` finds the body of
`audioDeviceIOCallbackWithContext` and fails if the first non-comment,
non-blank statement is not a `juce::ScopedNoDenormals` declaration. It exists
because the cost of getting that wrong was measured at ~80x CPU in production
and is invisible until the room is full. The test is registered only when
`RTA_BUILD_APP` is ON, because the file it greps is not present in a
CI-configured tree.

---

## 5. TDD stations

**Every station below is written and seen to FAIL before the implementation
exists.** The failing-first assertion is named so there is no ambiguity about
what "red" looked like. Expectation sources are labelled per CLAUDE.md's
verification standard: `closed-form` / `standard` / `golden` / `rule-derived` /
`regression-lock`. Nothing here is a regression lock; if an implementer needs
one, it must be labelled as one in the test name.

### 5.1 `core/tests/test_generator.cpp` → `rta_core_tests`

| `TEST_CASE` name | Failing-first assertion | Expectation source |
|---|---|---|
| `Pink noise falls at 3.01 dB per octave` | Render 2^20 samples, Welch them with `SpectrumEngine` (4096 / 1024, Hann, Linear), least-squares fit `10*log10(density)` against `log2(f)` over 100 Hz..10 kHz; slope is `-3.0103 ± 0.2` dB/octave. | **closed-form** — the generator sets `|X(k)| ∝ k^-1/2`, so the PSD is exactly `1/f` by construction; the tolerance covers only Welch's estimator variance. ±0.2 dB is the ledger's own acceptance line. |
| `Pink noise is bit-identical for a given seed` | Two `PinkNoise{seed = 0x5EED}` render 8192 samples each; the two buffers compare bitwise equal. Seed `0x5EEE` differs in at least one sample. | **closed-form** (property). This is what makes `rta-view.png` reproducible. |
| `A sine reads its exact amplitude and frequency` | 1 kHz at amplitude 0.5, 48 kHz, 65536 samples through `SpectrumEngine`: `binFrequency(argmax)` within one bin of 1000 Hz, and `sqrt(2 * spectrum[argmax])` within 0.1% of 0.5. | **closed-form** — a bin-centred sine has mean square `A^2/2`. |
| `A generator resumes phase across render calls` | 1000 samples in one call versus two calls of 500: bitwise equal. | **closed-form** (property). Catches the classic "restart the phase each block" bug, which puts a click at every buffer boundary. |

### 5.2 `platform/tests/test_channel_config.cpp` → `rta_platform_tests`

| `TEST_CASE` name | Failing-first assertion | Expectation source |
|---|---|---|
| `Channels default to Unused` | Fresh `ChannelConfig`: `role(ch) == Unused` for every `ch < kMaxChannels`. | rule-derived — never analyse a channel nobody assigned |
| `A role outside kMaxChannels is refused` | `setRole(kMaxChannels, Measurement)` returns `false`; `setRole(-1, ...)` returns `false`; no crash in a debug build. | rule-derived |
| `A role beyond the callback's channel count is ignored` | `setRole(5, Measurement)`, then `snapshot(2, out)`: `out[5] == Unused`, and every entry in `out[0..1]` is `Unused` too. **This is the record's bounds rule** — clip to what the callback received, never to what the device advertised. | rule-derived (decision record) |
| `firstChannelWithRole finds the lowest match` | Channels 7 and 3 both `Reference`; result is 3. | closed-form |

### 5.3 `platform/tests/test_capture_bus.cpp` → `rta_platform_tests`

The fixture builds `const float* const*` by hand out of `std::vector<float>` —
no JUCE, no device.

| `TEST_CASE` name | Failing-first assertion | Expectation source |
|---|---|---|
| `An inactive bus writes nothing` | `prepare(48000, 2, 4096)`, roles set, `setActive(false)`, push 256 samples: `ring(0)->availableToRead() == 0` **and** `totalDrops() == 0` — a block discarded on a stopped device is not a drop. | rule-derived (record: "the callback defensively checks a validity atomic") |
| `A measurement channel reaches its ring intact` | Push a 256-sample ramp on ch 0 as `Measurement`: `availableToRead() == 256` and the read-back equals the ramp under exact float comparison — this path is a copy, not arithmetic. | closed-form |
| `An unassigned channel is not written` | Same push, ch 1 `Unused`: `ring(1)->availableToRead() == 0`. | rule-derived |
| `A full ring counts the whole block as dropped` | Fill ch 0's ring to capacity, push 256 more: `dropCount(0)` grows by **exactly 256**, `availableToRead()` is unchanged, and the existing contents are unchanged. | closed-form — `RingBuffer::write` is documented all-or-nothing, so a refused block is 256 dropped samples, never a partial splice |
| `prepare drains every ring and bumps the epoch` | Write, then `prepare(96000, 2, 4096)`: every ring reads `availableToRead() == 0`, `sampleRate() == 96000`, `epoch()` strictly greater. | rule-derived (record: "drains every ring") — stale samples from the previous device splice onto the new session and mislabel every bin-to-Hz conversion |
| `pushFromCallback allocates nothing` | Push 10,000 blocks with a counting `operator new` armed; the count is 0. | rule-derived (CLAUDE.md real-time safety). If the counting-allocator trick proves fragile on MSVC, downgrade it to a review checklist item and **say so in the report** — do not delete the intent |
| `More channels than the bus was prepared for are clipped` | `pushFromCallback(in, 128, 64)` on a bus prepared for 2: no out-of-range access, no crash, only the prepared rings written. | rule-derived |

### 5.4 `app/tests/test_levels.cpp` → `rtatool_analysis_tests`

| `TEST_CASE` name | Failing-first assertion | Expectation source |
|---|---|---|
| `A full-scale sine reads 0.0 dBFS` | `levelDbFs(0.5)` within 1e-9 of `0.0` — a unit-amplitude sine has mean square 0.5. | closed-form |
| `Silence reads the floor, not negative infinity` | `levelDbFs(0.0) == -120.0` exactly, and `std::isfinite` holds. | rule-derived (§1.4) |
| `Ten times the power is ten dB` | `levelDbFs(0.05) - levelDbFs(0.5)` within 1e-9 of `-10.0`. | closed-form |

### 5.5 `app/tests/test_analyser.cpp` → `rtatool_analysis_tests`

| `TEST_CASE` name | Failing-first assertion | Expectation source |
|---|---|---|
| `A 1 kHz sine at -20 dBFS reads -20 dB in its own band` | 48 kHz, 4096 / 1024 Hann, Linear, 1/3 octave; sine amplitude 0.1 (that is -20 dBFS by §1.4), 4 s. The band whose `centreHz` is closest to 1000 Hz reads `-20.0 ± 0.1` dB. | **closed-form** — Parseval, plus `BandWeights::responseAt` being exactly 1 at the mid-band frequency (IEC 61260-1 design goal, already asserted in `test_bandweights.cpp`). If this fails, suspect the density/ENBW path or the dB offset. **Do not loosen the tolerance**; the feature ledger fixes it at ±0.1 dB |
| `Neighbouring bands do not see the tone` | Same run: the bands either side read at least 20 dB lower. | standard — IEC 61260-1 class-0 stopbands are far deeper than 20 dB; 20 is a deliberately slack bound on leakage |
| `Pink noise gives a flat third-octave spectrum` | Pink noise, fixed seed, 8 s, Linear averaging (≥ 350 frames at a 1024 hop): every band with `centreHz` in [50, 10000] is within **±0.5 dB** of the mean of those bands. | **closed-form** — pink noise carries equal energy per fractional octave by definition, and §1.4's generator makes that exact. Tolerance arithmetic: with ~350 frames the standard error is about `4.34 / sqrt(350) ≈ 0.23` dB, so ±0.5 dB is roughly 2σ per band; ±0.5 dB is also the ledger's acceptance line |
| `Under-resolved bands are flagged below the transform's limit` | Config with `fftSize = 1024` at 48 kHz (46.875 Hz bins): every band with `upperHz - lowerHz < 3 * 46.875` has `underResolved == true`, and no band above that does. | rule-derived — `BandWeights::kMinBinsPerBand = 3.0`, documented in the header |
| `Consecutive publishes hand out different immutable pointers` | Two `publish()` calls: the pointers differ, `sequence` increments by exactly 1, and the first snapshot's contents are unchanged after the second publish. | rule-derived — the record's one-struct atomic-swap rule; this is what proves a reader can hold a snapshot across a frame |
| `A snapshot's band count matches the band definition` | `bands.size() == OctaveBands(3, 20, 20000).size()` and `spectrumDb.size() == fftSize / 2 + 1`. | closed-form |
| `Dropped samples are carried into the snapshot` | `publish(1234)` → `droppedSamples == 1234`. | rule-derived — a drop count that never reaches the UI is a drop count nobody sees |

### 5.6 `app/tests/test_synthetic_snapshot.cpp` → `rtatool_analysis_tests`

| `TEST_CASE` name | Failing-first assertion | Expectation source |
|---|---|---|
| `The same spec produces the identical snapshot` | Two `makeSyntheticSnapshot(spec)` calls: every `bands[i].levelDb` compares bitwise equal. | closed-form (property) — the precondition for `rta-view.png` being reviewable as a diff |
| `A different seed produces a different snapshot` | `seed ^ 1` changes at least one band. | closed-form (property) — guards against a seed that is accepted and ignored |
| `The default synthetic spec is a usable picture` | Default spec: at least 20 bands, peak band level between -60 and 0 dBFS, no NaN, and no band in [50, 10000] Hz sitting on the -120 floor. | rule-derived — a fixture that renders an empty or clipped plot is broken, and the snapshot tool would write it happily |

### 5.7 `app/tests/test_plot_geometry.cpp` → `rtatool_analysis_tests`

| `TEST_CASE` name | Failing-first assertion | Expectation source |
|---|---|---|
| `The axis ends land exactly on the plot edges` | `xForHz(fLowHz) == left` and `xForHz(fHighHz) == right`, both within 1e-4. | closed-form |
| `A decade's geometric mean sits at its midpoint` | `xForHz(sqrt(100 * 1000))` is the midpoint of `xForHz(100)` and `xForHz(1000)` within 1e-4. | closed-form — that is what "logarithmic" means, and it is the one property a linear axis would silently pass every other test with |
| `Hz and x round-trip` | 40 frequencies spread over the range: `hzForX(xForHz(f))` within 1e-6 relative. | closed-form |
| `dB maps top-down and clamps` | `yForDb(dbTop) == top`, `yForDb(dbBottom) == bottom`, `yForDb(dbBottom - 40)` clamps to `bottom`. | rule-derived — an unclamped floor draws bars off the bottom of the component and into the panel below |
| `decadeTicks yields the 1-2-5 sequence in range` | `decadeTicks(20, 20000)` equals `{20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000}`. | rule-derived (§1.4) |

### 5.8 Hardware-verified only — cannot run on CI

These are the JUCE-device behaviours the decision record catalogues. Each is
listed **because it cannot be automated**, not because it is optional. Run them
on this machine with the interface attached and paste the observed result into
the report — no "done" without output.

| # | Step | Pass looks like |
|---|---|---|
| **M1** | Set the device-type combo to `ASIO` on a machine with **no** ASIO driver registered. | The combo re-reads `AudioIo::currentState().typeName` after the attempt and shows the type actually in use (e.g. `Windows Audio`), **not** the requested string, with a fault line explaining it. JUCE silently keeps the current type; `setAudioDeviceSetup` returning an EMPTY string is the success case. |
| **M2** | With a real interface: channel 1 = Measurement, channel 2 = Reference, speak into the mic. | Bars move. The readout shows a plausible peak band and `0` drops. |
| **M3** | While running, change the sample rate 48000 → 96000 in the panel. | The frequency axis stays honest (a 1 kHz tone still reads 1000 Hz), `framesAnalysed` restarts, and there is no burst of impossible high-frequency energy in the first snapshot after the change. That burst is exactly what stale rings splicing across a rate change produce. |
| **M4** | While running, change the buffer size. | The device reloads, audio resumes, the app does not freeze. (The historical ASIO freeze came from reset logic living inside a callback that had already stopped firing.) |
| **M5** | Unplug the interface mid-run. | A fault appears in the device panel, the app stays alive and responsive, and **nothing reconnects on its own**. Re-plug, press start, it recovers. |
| **M6** | Stop the device from the panel while audio is flowing. | No crash, no assertion. Some platform/JUCE combinations keep firing callbacks after `closeAudioDevice()`; the validity atomic is what absorbs that. |
| **M7** | Loopback: interface output → interface input, playing pink noise from any player. | The third-octave plot is flat within a few dB across the interface's own response. (A full self-check driven by our own generator is a later deliverable; this is the visual smoke test.) |

---

## 6. Task sequence

The rule: **every commit builds and every commit's tests pass.** A task is a
commit boundary. Tasks in the same wave touch disjoint file sets and can run in
parallel as separate sub-agents; waves are strictly ordered.

**Build-file ownership rule.** Within a wave, exactly one task edits the root
`CMakeLists.txt` and exactly one edits `app/CMakeLists.txt` — the task named as
owner. Because this plan fixes every path in advance, the owner can add a
sibling's files to the build in the same commit. Two agents editing one
CMakeLists inside one wave is the merge conflict this rule exists to prevent.

### Wave A — three agents in parallel

| Task | Files | Build-file owner |
|---|---|---|
| **T1 — pink noise and sine, in core** | `core/include/rta/gen/SignalGenerator.h`, `core/src/gen/SignalGenerator.cpp`, `core/tests/test_generator.cpp`, `core/CMakeLists.txt`, `core/tests/CMakeLists.txt` | itself (core only) |
| **T2 — CaptureBus and ChannelConfig** | all of `platform/types/**`, all of `platform/tests/**`, root `CMakeLists.txt`, `core/tests/check_no_framework_deps.cmake` (add `LABEL`) | **root** |
| **T5 — move the specimen aside** | `git mv app/src/MainComponent.* app/src/dev/SpecimenComponent.*`, class rename, `app/CMakeLists.txt`, `tools/snapshot.cpp` (include and type name only) | **app** |

After wave A: `ctest` green, and `rtatool_snapshot` still writes `specimen.png`.
T5 changes no pixels — diff the PNG against the pre-change one and say so in the
commit message.

### Wave B — one agent (everything downstream needs its types)

| Task | Files | Build-file owner |
|---|---|---|
| **T3 — the measurement view model** | `app/src/measure/{Levels.h,Snapshot.h,Analyser.h,Analyser.cpp,SyntheticSnapshot.h,SyntheticSnapshot.cpp,SnapshotSource.h}`, `app/src/view/PlotGeometry.h`, all of `app/tests/**`, root `CMakeLists.txt` (one line) | **root**, **app/tests** |

### Wave C — two agents in parallel

| Task | Files | Build-file owner |
|---|---|---|
| **T6 — the plot** | `app/src/view/{MeasureColours.h,PlotAxes.h,PlotAxes.cpp,RtaView.h,RtaView.cpp}`, `app/CMakeLists.txt` | **app** |
| **T7 — AudioIo** | `platform/CMakeLists.txt`, `platform/include/rta/platform/AudioIo.h`, `platform/src/AudioIo.cpp`, `platform/src/AudioIo_Devices.cpp`, `platform/tests/check_callback_shape.cmake` and its `add_test`, root `CMakeLists.txt` (one line) | **root** |

T6 and T7 share no file. T6 compiles against T3's `Snapshot` and reads from a
`StaticSnapshotSource`, so it needs nothing from T7.

### Wave D — two agents in parallel

| Task | Files | Build-file owner |
|---|---|---|
| **T8 — analysis thread and synthetic input** | `app/src/measure/{AnalysisThread.h,AnalysisThread.cpp,SyntheticInput.h,SyntheticInput.cpp}` | — |
| **T9 — the device panel** | `app/src/view/{DevicePanel.h,DevicePanel.cpp,ChannelRoleTable.h,ChannelRoleTable.cpp}`, `app/CMakeLists.txt` (adds T8's files too) | **app** |

### Wave E — sequential, one agent

| Task | Files |
|---|---|
| **T10 — rewire MainComponent** | new `app/src/MainComponent.h/.cpp`, `app/src/Main.cpp` (window size, developer menu entry for the specimen), `app/CMakeLists.txt` |
| **T11 — the RTA snapshot** | `tools/snapshot.cpp` (render `RtaView` from `makeSyntheticSnapshot` → `rta-view.png`), `app/CMakeLists.txt` (snapshot target source list) |
| **T12 — /W4 sweep and the hardware run** | whatever the warnings say; then execute M1..M7 and record the output |
| **T13 — close the phase** | `docs/reports/002-*.md`, `docs/features/FEAT-phase1-rta-spl-generator.md` (status + next), `memory/MEMORY.md`, `docs/HANDOFF.md`. Global rule 12: status updated where the roadmap lives, what the human can run, and a handoff for the next phase written now, not later |

---

## 7. Build and test commands, and the output that counts as pass

Record the **baseline test count first**, so "N tests passed" means something:

```bash
ctest --test-dir build -C Release -N | tail -1
```

Configure (this machine: MSVC 14.51 / VS Build Tools 2026, JUCE 9.0.1 reused
from the handsfree checkout):

```bash
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=ON -DRTA_JUCE_PATH="D:/DEV CAVE EP3/PROJECT005-AZ-handsfree/external/JUCE"
```

```bash
cmake --build build --config Release --parallel
```

```bash
ctest --test-dir build -C Release --output-on-failure
```

Pass: `100% tests passed, 0 tests failed out of <baseline + new>`, where the new
cases are every `TEST_CASE` in §5.1–§5.7 plus
`platform_types_has_no_framework_deps` and
`audioio_scoped_no_denormals_is_first`. The pre-existing
`core_has_no_framework_deps` must still pass — if a change to `core/` made it
fail, the change is wrong, not the test.

CI parity — what the GitHub runners do, and the check that proves §1.1 actually
worked:

```bash
cmake -S . -B build-ci -DCMAKE_BUILD_TYPE=Release -DRTA_BUILD_TESTS=ON -DRTA_BUILD_APP=OFF
```

```bash
ctest --test-dir build-ci -C Release --output-on-failure
```

Pass: green, and the listing contains `rta_platform_tests` and
`rtatool_analysis_tests` cases. If those are absent, the JUCE-free split did not
happen and the platform layer is untested on CI.

Snapshots:

```bash
cmake --build build --config Release --target rtatool_snapshot --parallel
```

```bash
build/app/rtatool_snapshot_artefacts/Release/rtatool_snapshot.exe shots 1100 760
```

Pass: two lines on stdout — `wrote ...\shots\specimen.png  (1100 x 760)` and
`wrote ...\shots\rta-view.png  (1100 x 760)` — and exit code 0. Run it twice and
compare the two `rta-view.png` files byte for byte; identical, or the
determinism requirement (§1.4, §5.6) is not met. From Git Bash, invoke the exe
through `cmd //c`; direct invocation returns 127.

Warnings:

```bash
cmake --build build --config Release --parallel 2>&1 | grep -c "warning C"
```

Expected `0`. The three that will bite: `C4244` / `C4267` narrowing between
`std::size_t`, `int` and `float` (use an explicit `static_cast` and mean it),
`C4100` unreferenced formal parameter in JUCE overrides (omit the name), and
`C4324` alignment padding — already suppressed where it is intentional in
`RingBuffer.h`; do not add a second blanket suppression.

---

## 8. Acceptance checklist

- [ ] `ctest` green in the `RTA_BUILD_APP=ON` tree, every new case from §5 present by name.
- [ ] `ctest` green in the `RTA_BUILD_APP=OFF` tree, **including** `rta_platform_tests` and `rtatool_analysis_tests`.
- [ ] `core_has_no_framework_deps` and `platform_types_has_no_framework_deps` both pass.
- [ ] `audioio_scoped_no_denormals_is_first` passes.
- [ ] `rtatool_snapshot` writes `specimen.png` (visually unchanged) **and** `rta-view.png`.
- [ ] `rta-view.png` is byte-identical across two consecutive runs.
- [ ] `rtatool.exe` with a real interface: M2 observed and pasted into the report.
- [ ] `rtatool.exe` with no interface: opens, shows the fault, synthetic input drives the plot.
- [ ] M1, M3, M4, M5, M6 executed and their observed behaviour recorded.
- [ ] Zero `warning C` at `/W4`.
- [ ] `wc -l` over every added or changed file: all ≤ 400.
- [ ] Nothing under `ui/az_ui/` uses measurement vocabulary (grep `trace`, `coherence`, `band`, `spectrum`, `rta`).
- [ ] Nothing under `core/` or `platform/types/` includes JUCE — the guards prove it.
- [ ] Every new source file starts with `// SPDX-License-Identifier: AGPL-3.0-or-later`.

---

## 9. Handoff to implementers

### 9.1 The five traps from the decision record, verbatim

Copied word for word from the table in
`docs/specs/2026-08-27-platform-audioio-design.md`. Each one already cost
somebody a debugging day. Read them before writing `AudioIo.cpp`, not after.

> | `ScopedNoDenormals` is the FIRST statement in the callback | handsfree: subnormal limit cycle measured at ~80x CPU (77 ms/s vs 0.9 ms/s) |

> | `audioDeviceAboutToStart` retargets rate-dependent state AND drains every ring | stale samples from the previous device splice onto the new session and mislabel every bin-to-Hz conversion |

> | Read the device type BACK from the manager; never echo the requested string | JUCE silently keeps the current type if "ASIO" isn't registered |

> | `setAudioDeviceSetup` returns EMPTY string on success | inverse of the usual convention |

> | The callback defensively checks a validity atomic | on some platform/JUCE combos callbacks keep firing after `closeAudioDevice()` |

### 9.2 Traps this plan adds, from reading the code that already exists

- **T-1. Member declaration order in `MainComponent` is load-bearing.** Declare
  `AudioIo` **before** `AnalysisThread` and `SyntheticInput`. Members die in
  reverse order, so the threads must stop before the bus and the device they
  read from. Both thread classes must also call `stopThread(2000)` in their own
  destructors — belt and braces, because a thread still draining a destroyed
  ring is a crash that happens only on shutdown, on a customer's machine, once.

- **T-2. `BandWeights::apply` takes `density()`, never `spectrum()`.** The
  header says why: summing the power spectrum over a band over-counts broadband
  energy by the window's ENBW — 1.76 dB with Hann, on every noise and music
  source, on a plot that looks entirely reasonable. Both getters exist on
  `SpectrumEngine` and both compile.

- **T-3. `SpectrumEngine` throws.** `process()` can throw `std::logic_error`
  on internal inconsistency and the constructor throws `std::invalid_argument`
  on a bad hop or rate. `AnalysisThread::run()` must wrap its body in
  `try / catch (const std::exception&)`, record the message as a fault and stop
  cleanly. An exception escaping `juce::Thread::run()` terminates the process —
  during a show.

- **T-4. The snapshot's allocation belongs on the analysis thread.**
  `publish()` allocates a fresh `Snapshot` (about 8 KB with `spectrumDb`) at
  most 20 times a second, on the analysis thread. That is deliberate and
  allowed. The audio callback allocates nothing, ever — which is what the
  allocation test in `test_capture_bus.cpp` is for.

- **T-5. `std::atomic<std::shared_ptr<T>>` on MSVC uses an internal spinlock.**
  The decision record accepts this precisely because neither side of that
  boundary is the audio callback. If anyone moves a snapshot read into the
  callback, that acceptance evaporates. Writer = analysis thread, reader =
  message thread, and nothing else.

- **T-6. `setSize()` does not call `resized()` without a desktop peer, and
  `createComponentSnapshot` needs `true` for children.** Both are already
  handled in `tools/snapshot.cpp`; the new `RtaView` render path must keep both,
  or `rta-view.png` comes out blank and looks like a drawing bug.

- **T-7. `std::uniform_real_distribution` is not portable.** The mt19937 engine
  is specified bit-exactly; the distributions are not. Map to `[0,1)` by hand,
  or `rta-view.png` differs between MSVC and libstdc++ and the determinism test
  passes on one machine and fails on another.

- **T-8. `juce::String` cannot cross threads.** It is reference-counted.
  `AudioIo`'s fault record is a `std::string` behind a mutex, written on the
  device thread, read on the message thread, and never touched by the callback —
  the same split handsfree's `AudioEngine` documents at length.

- **T-9. Frequency labels are whole hertz; dB carries one decimal.** Different
  quantities, different rules, and CLAUDE.md says explicitly not to unify them
  for tidiness. `1000 Hz`, `-20.4 dB`.

- **T-10. `ui/az_ui/` stays free of measurement words.** If the plot needs a
  colour, it names it in `app/src/view/MeasureColours.h` from an `az::ui` token.
  The moment a colour is called `coherence` inside the module, `az_ui` stops
  being a design system and becomes this app's GUI in a different folder.
