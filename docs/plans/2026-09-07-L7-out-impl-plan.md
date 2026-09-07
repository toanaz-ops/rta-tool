# L7-OUT — the lock-free generator output path (lane L7, sub-lane L7-OUT)

> **For agentic workers:** REQUIRED SUB-SKILL `superpowers:test-driven-development` and `superpowers:executing-plans`. Steps are checkboxes; the failing test is written and *seen to fail* before the code. Station 5 (adversarial verify, a reviewer with **no Write tools**) is not optional.

*2026-09-07, lane L7, sub-lane L7-OUT, station 3. Written from the decision record `docs/dsp/2026-09-06-l7-output-path.md` after reading the real files it cites — `platform/src/AudioIo.cpp`, `platform/types/include/rta/platform/{ChannelConfig,CaptureBus}.h`, `core/{include,src}/gen/{Oscillator,Noise,Sweep,Mls}.{h,cpp}`, `platform/tests/check_callback_shape.cmake`, `platform/{tests,tests_juce,types}/CMakeLists.txt`, `app/src/measure/{CaptureSequencer,RoutingPlan}.h` — not the record's description of them. Worktree `.claude\worktrees\continue-pending-work-f8aa84`, HEAD `1ad48da`. Every acceptance number below is a closed-form identity; L7-OUT needs **no golden vector** (record §10).*

## 0. What L7-OUT is, and why it is built once

The audio callback clears every output today (`AudioIo.cpp:138-144`) and `core/src/gen/*` has no `app/` caller. Every L7 solver plays a signal before it can do its own job, and G20 auto solo/mute is nothing but choosing which output plays (record §0). L7-OUT designs the lock-free output path **once, first**, as the prerequisite both solver excitation and G20 consume (owner ruling, record §0). The four consuming records — L7-DELAY, L7-EQ, L7-ALIGN, G20 — cite record §6 as the contract and §11 as what they may assume; this plan builds exactly that contract and nothing past it.

## Global constraints

- SPDX header `// SPDX-License-Identifier: AGPL-3.0-or-later` on every new file.
- **`core/` never includes JUCE/Qt/a device API** (guard `core_has_no_framework_deps`); `RampedGain::prepare` stays framework-free. **`platform/types/` is equally framework-free** (guard `platform_types_has_no_framework_deps`); `OutputEngine` includes only the six framework-free `gen` headers plus `<variant>`.
- **Real-time safety in the callback** (record §7): `render()` does no `new`/`malloc`/lock/file/FFT; every source in the variant is `noexcept`, heap-free, span-out. FTZ/DAZ (`ScopedNoDenormals`) stays the literal first statement of the callback.
- **Hard cap 400 lines, aim 300**, headers too. Budgets are given per file below; the record requires `OutputEngine.{h,cpp}` each under 300 (§9).
- **Never assert a value the implementation produced.** Closed form only here (record §10: "identity arithmetic over already-golden'd sources; no new golden vector").
- Build dirs **`build-l7out`** (OFF) / **`build-l7out-on`** (ON), Visual Studio generator, **never Ninja** (`memory/a-misconfigured-build-goes-99-percent-of-the-way.md`). MSVC `/W4` is the truth: **0 warnings**.

```
cmake -S . -B build-l7out -G "Visual Studio 18 2026" -A x64
cmake --build build-l7out --config Release --parallel
ctest --test-dir build-l7out -C Release --output-on-failure
```

For the ON config append `-DRTA_BUILD_APP=ON` and use `build-l7out-on`.

## Reconciliations made while planning (take to the orchestrator; do not silently re-decide in code)

- **OUT-R1 — the routing table is factored into a header-only `OutputRoutingConfig`.** Record §6 embeds the `std::array<std::atomic<int>, kMaxChannels>` role table inside `OutputEngine`; §9 also requires `OutputEngine.cpp` under 300 lines. *Reconciled:* the table becomes a header-only `OutputRoutingConfig.h` mirroring `ChannelConfig.h` exactly (the same relaxed-store/relaxed-load/bounds-against-received pattern, `ChannelConfig.h:37-64`), and `OutputEngine` composes it — the identical `CaptureBus`-owns-`ChannelConfig` split the repo already uses. The public `OutputEngine` API of §6 is unchanged. Flagged because it adds one file the record's file list (§9) did not name.
- **OUT-R2 — `RampedGain` must remember its ramp seconds for `prepare(sampleRate)` to recompute the length.** The shipped ctor computes `rampLenSamples_` from `sampleRate*rampSeconds` and discards `rampSeconds` (`Oscillator.cpp:70`). `prepare(double sampleRate)` (record §5) receives only the rate, so it cannot recompute the length without a stored `rampSeconds_`. *Decision:* add one audio-thread-owned, set-once `double rampSeconds_;` member. This is inside "rewrites only the audio-thread-owned fields" (record §5) and never touches `target_`.
- **OUT-R3 — the no-alloc proof is the forbidden-token cmake guard plus a documented review.** The repo has **no** allocation-counting test harness (searched: no such fixture in `core/tests` or `platform/tests`). *Decision:* the enforced no-alloc check for `render()` is the forbidden-token grep guard (record §7's "second tripwire"), made red once deliberately; the accompanying review check is that `render()` writes only caller-owned buffers and the constructor-sized scratch. Named so nobody looks for a harness that is not there.
- **OUT-R4 — the three OUT open questions (record §13) are folded where answered.** G20 default = the owner's setting, default option 1 (strict solo for a sequence, additive for a manual toggle) — planned in Task D. Device-reconfig-while-armed = drop the source, mirror `CaptureBus::prepare` (record §4 chose this) — planned in Task B (`prepare` disarms + bumps epoch). The 64-output hardware question stays open (§13.1); its fallback is one constant, planned as a comment, not a code fork.

---

## The API this lane builds (record §6, verbatim contract)

```cpp
// platform/types/include/rta/platform/OutputEngine.h   (rta_platform_types)
namespace rta::platform {

enum class OutputRole { None, Routed };

using SourceVariant = std::variant<std::monostate, gen::Oscillator, gen::DualSine,
                                   gen::WhiteNoise, gen::PinkNoise, gen::Sweep, gen::Mls>;

class OutputEngine {
public:
    OutputEngine();                                    // the ONLY allocation: scratch, sized once
    void prepare(double sampleRate, int numOutputChannels);   // device thread; bumps epoch, disarms, retargets gates

    [[nodiscard]] bool sourceIsQuiescent() const noexcept;     // acquire
    bool setSource(SourceVariant&& source);                    // false unless quiescent
    void armSource() noexcept;                                 // release
    void disarmSource() noexcept;

    bool routeOutput(int outputChannel, bool active) noexcept; // relaxed; false outside [0,kMaxChannels)
    [[nodiscard]] OutputRole role(int outputChannel) const noexcept;

    [[nodiscard]] std::uint64_t renderedSamples() const noexcept;   // since last arm
    [[nodiscard]] std::uint64_t outputEpoch() const noexcept;       // bumped by prepare()
    [[nodiscard]] double sampleRate() const noexcept;

    void render(float* const* output, int numOutputChannels, int numSamples) noexcept;  // audio thread; wait-free
};
}  // namespace rta::platform
```

**Atomics and memory order (the quiescent-swap, record §4).** Only the message thread writes the slot; only while quiescent.
- `publishedIdle_` (`std::atomic<bool>`): `render` stores `master_.state()==Idle` with **release** at block end; `setSource`/`sourceIsQuiescent` load with **acquire**.
- `armed_` (`std::atomic<bool>`): `armSource` stores `true` **release**; `disarmSource` stores `false` release; `render` loads **acquire** once per block, first among the output-side reads, then drives the master gate on/off.
- `renderedSamples_` (`std::atomic<uint64_t>`): reset to 0 in `armSource` **before** the `armed_` release; incremented relaxed by `render` on advancing blocks.
- `outputEpoch_` (`std::atomic<uint64_t>`): bumped by `prepare`; `slotEpoch_` recorded by `setSource`, compared relaxed by `render` (mismatch → silence).
- Routing roles and per-output gate targets: **relaxed** both sides (a routing flip publishes no object — the same reasoning as `ChannelConfig`).

Two gate stages (record §3): one **master** `RampedGain` (its `Idle` defines quiescence), then per-output `RampedGain` gates. Both are the shipped `g(p)=½(1−cos πp)`, 10 ms, never zero. Independent per-output gates are the primitive; strict solo is app policy (Task D).

---

## Task A — `RampedGain::prepare(double)` — the ONE core change (record §5, §10 T8)

Smallest, most independent, and every later task's device-reconfig path depends on it, so it goes first.

**Files.** Modify `core/include/rta/gen/Oscillator.h` (declaration after line 102; add `double rampSeconds_;` per OUT-R2), `core/src/gen/Oscillator.cpp` (impl beside the existing `RampedGain` methods), `core/tests/test_generator_osc.cpp` (add `TEST_CASE`s beside the existing `RampedGain` tests at 187-263). No CMakeLists edit (`test_generator_osc.cpp` is already in `rta_core_tests`, `core/tests/CMakeLists.txt:44`).

```cpp
// Oscillator.h, inside RampedGain public: — device thread, callback quiesced.
// Rewrites only audio-thread-owned fields; NEVER touches target_ (record §5).
void prepare(double sampleRate) noexcept;   // rampLenSamples_ = round(sampleRate*rampSeconds_); pos_=0; state_=Idle
```

**RED first.** Add a `TEST_CASE` that calls `gain.prepare(96000.0)`; the build fails (no such member). Paste that.

| # | case | closed-form acceptance |
|---|---|---|
| **A1 (first)** | ramp length re-scales | after `RampedGain(48000).prepare(96000.0)`, a full rise takes **exactly 960** samples to reach `1.0` (`960 = lround(96000·0.010)`), and the mid-ramp gain at `pos_=480` is `½(1−cos(π·480/960)) = ½(1−cos(π/2)) = 0.5` within `1e-6` |
| A2 | state is reset regardless of prior | drive to `Running`, then `prepare(96000)`: `state()==Idle`; the first `nextGain()` after a fresh `requestOn()` returns `0.0` (starts from `g(0)`), i.e. `pos_` is 0 |
| A3 | `target_` survives | `requestOn()` (sets `target_`), then `prepare(96000)`, then pump `nextGain()`: the gate rises to `1.0` over 960 samples with no extra `requestOn()` — the pending request published before `prepare` is still honoured |

- [ ] **Accept:** OFF ctest `base_core+1` (A1–A3 add one `TEST_CASE` file section group; count read from ctest, not predicted). Zero `/W4`.
- [ ] **Mutation:** make `prepare` also do `target_.store(false)` → A3 fails (gate never rises); revert. **Mutation 2:** drop `pos_=0` → A2 fails; revert.
- [ ] **Commit:** `git add core/include/rta/gen/Oscillator.h core/src/gen/Oscillator.cpp core/tests/test_generator_osc.cpp` → `feat(core): RampedGain::prepare retargets the ramp length on a rate change, target_ untouched`

## Task B — `OutputRoutingConfig` + `OutputEngine` (platform/types; record §4-§6, §10 T1-T9)

The whole JUCE-free engine, provable with no device. Depends on Task A (`render`/`prepare` retarget gates through `RampedGain::prepare`).

**Files.** Create `platform/types/include/rta/platform/OutputRoutingConfig.h` (≤ 90, header-only, OUT-R1), `platform/types/include/rta/platform/OutputEngine.h` (≤ 130), `platform/types/src/OutputEngine.cpp` (≤ 280), `platform/tests/test_output_engine.cpp` (≤ 320); modify `platform/types/CMakeLists.txt` (add `src/OutputEngine.cpp`) and `platform/tests/CMakeLists.txt` (add `test_output_engine.cpp` to `rta_platform_tests`).

**Construction notes the builder will hit.** `RampedGain` is non-movable/non-copyable (holds `std::atomic<bool>`, `Oscillator.h:107`) and has no default ctor, so the 65 gates (`std::array<gen::RampedGain, kMaxChannels>` + one master) are built in place at a placeholder rate via an `index_sequence` helper and retargeted by the first `prepare()` — `OutputEngine()` takes no sample rate (record §6). Scratch is one `std::vector<float>(kScratchCapacity)` allocated in the ctor (the ONLY allocation); `kScratchCapacity = 2048`, and `render` chunks any `numSamples` through it (no growth path — record §7). `render` clears **every** output channel not currently receiving signal, so JUCE's "write or clear every output" contract is satisfied entirely inside `render` (it replaces the old clear loop).

**RED first.** `test_output_engine.cpp` opens with `#include "rta/platform/OutputEngine.h"`; the build fails at the include. Paste it. Then, once the header exists, the idle contract — the cleanest closed form:

| # | case (record §10) | closed-form acceptance |
|---|---|---|
| **B1 (first)** | idle contract preserved | nothing armed: every channel of a `2×512` block is exactly `0.0f`; `renderedSamples()==0`; `render(nullptr, 2, 512)` is a no-op |
| B2 | closed-form ramped sine | `Oscillator(48000,1000,−6dBFS)`, `routeOutput(1,true)`, `armSource()`; render 480 then 512. Channel 1 sample `n` `== A·sin(2π·1000·n/48000)·g(n)` with `g(n)=½(1−cos(πn/480))` for `n<480` (routing+arming land in the same block, so **both gates rise together → the product is `g` once per stage, i.e. `g(n)²` over the first 480**) and `1` after; `A=10^(−6/20)`; channels 0 and 2..N exactly `0.0f`. Tol `1e-6` |
| B3 | complementary handover | channel 2 routed and steady as a reference copy; in one block `routeOutput(1,true)`, `routeOutput(0,false)` from a steady channel 0: every sample `out0+out1 == out2` within `1e-6` — the identity `½(1+cos πp)+½(1−cos πp) ≡ 1` on real output |
| B4 | bounds against received | `routeOutput(5,true)`; render with `numOutputChannels=2`: no write outside the two buffers (run under the sanitiser config), channels 0–1 zero. `routeOutput(−1,…)` and `routeOutput(64,…)` return `false` and change no role |
| B5 | quiescent protocol | `setSource` while armed returns `false`, and the sine's output stays phase-continuous across the refusal; after `disarmSource()` and one full 10 ms ramp of rendering, `sourceIsQuiescent()` is `true`, `setSource` succeeds, and the new source's first armed block starts from `g(0)=0` |
| B6 | chunking is invisible | `numSamples = 3·kScratchCapacity + 17` equals a single-pass reference built from a standalone `Oscillator` + two `RampedGain`s, sample-exact (same float ops, same order) |
| B7 | sweep ends at zero, counter tells when | `Sweep` of 0.1 s; render past `lengthSamples()`: every later sample is exactly `0.0f`; `renderedSamples()` equals the samples rendered since arm exactly |
| B8 | epoch invalidation (record §13.3) | `prepare()` at a new rate → `outputEpoch()` increments and the engine is disarmed; an `armSource()` on the stale slot renders silence until `setSource` is called again |

- [ ] **Accept:** OFF ctest `base_platform+8` (B1–B8; count read from ctest). Zero `/W4`.
- [ ] **Mutation 1:** make `setSource` skip the `publishedIdle_ && !armed_` guard → B5 lets a swap through mid-signal and the phase jumps; revert. **Mutation 2:** drop the epoch compare in `render` → B8's stale slot plays; revert.
- [ ] **Commit:** `git add platform/types/include/rta/platform/OutputRoutingConfig.h platform/types/include/rta/platform/OutputEngine.h platform/types/src/OutputEngine.cpp platform/tests/test_output_engine.cpp platform/types/CMakeLists.txt platform/tests/CMakeLists.txt` → `feat(platform): the lock-free OutputEngine — quiescent-swap source, two gate stages, N independent outputs`

## Task C — `AudioIo` integration + the two guards (platform ON; record §7, §10 T10-T11)

Wires `render` into the one callback line and proves both tripwires. ON config only (`AudioIo.cpp` and its guard exist only when `RTA_BUILD_APP=ON`).

**Files.** Modify `platform/include/rta/platform/AudioIo.h` (include `OutputEngine.h`; add `OutputEngine output_;` member and `output()`/`const output()` accessors beside `bus()`), `platform/src/AudioIo.cpp` (replace the clear loop 138-144 with one `output_.render(...)` line; add `output_.prepare(sampleRate, numOutputs)` in `audioDeviceAboutToStart` beside `bus_.prepare`; change `kRequestedOutputChannels` 2→`kMaxChannels` and rewrite the now-false comment at lines 18-21), `platform/CMakeLists.txt` (no change — `rta_platform` already links `rta::platform_types`). Create `platform/tests/check_no_rt_hazards.cmake` (≤ 90, parameterised `SOURCE`+`FUNCTION`, forbidden-token pass `new |malloc|mutex|lock_guard|vector<|Fft|fopen|printf` over the named function's body, reusing `check_callback_shape.cmake`'s brace-walk). Modify `platform/tests/CMakeLists.txt` (register the two hazard tests) and `platform/tests_juce/test_audio_io_contract.cpp` (add the output-contract `TEST_CASE`).

`audioDeviceAboutToStart` output-channel count: `device->getActiveOutputChannels().countNumberOfSetBits()` clamped to `kMaxChannels`, mirroring the input side (`AudioIo.cpp:161-162`). `prepare` runs before the callback is inserted — the same safety `CaptureBus::prepare` relies on.

**RED first.** Add the output-contract `TEST_CASE` to `test_audio_io_contract.cpp` (call shape from `test_audio_io_contract.cpp:182`): before the integration the callback still clears, so an assertion that a routed channel is non-zero after arming fails. Paste it.

| # | case | closed-form acceptance |
|---|---|---|
| **C1 (first)** | callback output contract (record §10 T10) | `AudioIo` driven with `audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 256, ctx)`: outputs are exactly `0.0f` before arming (today's contract, preserved); after `io.output().setSource(Oscillator{...})`, `routeOutput(0,true)`, `armSource()`, a routed channel is non-zero and the unrouted channel is exactly `0.0f` |
| C2 | `ScopedNoDenormals` stays first | `audioio_scoped_no_denormals_is_first` prints `OK (const juce::ScopedNoDenormals noDenormals;)` with the new `output_.render(...)` line present — paste the STATUS line |
| C3 | render has no RT hazards | `output_render_has_no_rt_hazards` (unconditional; `OutputEngine.cpp` is in the OFF tree too) green; made **RED once** by planting `std::mutex m; std::lock_guard<std::mutex> g(m);` in `render()` → paste the failure naming the token → revert |
| C4 | callback has no RT hazards | `audioio_callback_has_no_rt_hazards` (ON-only; `SOURCE=AudioIo.cpp`) green |

- [ ] **Accept:** ON ctest `base_on+1` for C1 plus the two new guard tests registered (`ctest -R rt_hazards` lists both). Zero `/W4`. `audioio_scoped_no_denormals_is_first` still green.
- [ ] **Guard proof (record §7's "made red once"):** C3's deliberate RED is not optional — a guard nobody has seen fail is a guard nobody knows is watching.
- [ ] **Commit:** `git add platform/include/rta/platform/AudioIo.h platform/src/AudioIo.cpp platform/tests/check_no_rt_hazards.cmake platform/tests/CMakeLists.txt platform/tests_juce/test_audio_io_contract.cpp` → `feat(platform): the callback renders the OutputEngine; two tripwires, one made red`

## Task D — app output policy + G20 wiring (app ON; record §3, §9; owner ruling)

Policy over the platform primitive: strict solo and additive toggle as `app/` loops over `routeOutput`, and the G20 sequencer binding. No `app/` code touches a gate or the slot directly (record §9).

**Files.** Create `app/src/measure/OutputPolicy.h` (≤ 90, header-only over `rta::platform::OutputEngine&`, like `RoutingPlan.h`) and `app/tests/test_output_policy.cpp` (≤ 160); modify `app/CMakeLists.txt`/`app/tests/CMakeLists.txt` (add the test) and `app/src/MainComponent.{h,cpp}` (own an `OutputEngine*` handle via `audioIo_.output()`; bind the sequencer's `onStep`).

```cpp
// app/src/measure/OutputPolicy.h  --  namespace rta::measure
enum class SoloMode { StrictSolo, Additive };   // owner ruling: sequence default StrictSolo (option 1), manual toggle Additive
void muteAll(rta::platform::OutputEngine& e) noexcept;                 // routeOutput(ch,false) for all
void soloOutput(rta::platform::OutputEngine& e, int outputChannel) noexcept;  // that ch on, every other off
void applyToggle(rta::platform::OutputEngine& e, int outputChannel, bool on, SoloMode mode) noexcept;
//   StrictSolo + on  => soloOutput; Additive => routeOutput(ch,on) leaving the rest
```

**RED first.** `test_output_policy.cpp` includes `OutputPolicy.h`; the build fails at the include. Paste it.

| # | case | closed-form acceptance |
|---|---|---|
| **D1 (first)** | strict solo | after `soloOutput(e, 3)`: `role(3)==Routed`, `role(k)==None` for every other `k` in `[0,kMaxChannels)`; with a source armed, render → only channel 3 is non-zero |
| D2 | mute all | after `muteAll(e)`: `role(k)==None` for all `k`; render → every channel exactly `0.0f` |
| D3 | additive vs strict | `applyToggle(e,2,true,Additive)` then `applyToggle(e,5,true,Additive)` → both `Routed`; then `applyToggle(e,5,true,StrictSolo)` → only channel 5 `Routed`, all others `None` |
| D4 | G20 sequencer binding | `CaptureSequencer::onStep = [&](int step){ soloOutput(e, outputOfMember(step)); }` (`CaptureSequencer.h:69`): stepping the sequencer solos exactly the stepped member's output — asserted through `role()` after `endCapture`, no device |

The member→output map (`outputOfMember`) is a preset field whose persistence is deferred to schema 4 (record §12); Task D injects it as a plain `std::vector<int>` / identity map, and does not persist it.

- [ ] **Accept:** ON ctest `base_on+2` (D1–D4 across one test file; count read from ctest). Zero `/W4`.
- [ ] **Mutation:** make `applyToggle(...,Additive)` call `soloOutput` → D3 fails (channel 2 gets muted); revert.
- [ ] **Commit:** `git add app/src/measure/OutputPolicy.h app/tests/test_output_policy.cpp app/src/MainComponent.h app/src/MainComponent.cpp app/CMakeLists.txt app/tests/CMakeLists.txt` → `feat(app): strict-solo and additive output policy over routeOutput; G20 sequencer binding`

## Task E — prove the guards still GUARD, both configs (record §7, §10 T11)

No new files; a procedure whose output goes in the report. Every count is **read from the guard's own `(N files scanned)` / STATUS line**, never predicted here.

- [ ] **GREEN, framework guards, new files scanned.** `platform_types_has_no_framework_deps` and `core_has_no_framework_deps` green with the new `OutputEngine`/`OutputRoutingConfig`/`RampedGain::prepare` files in scope; paste each `(N files scanned)` line (both counts rise). **RED once:** add `#include <juce_core/juce_core.h>` atop `OutputEngine.h`, reconfigure, `ctest -R platform_types_has_no_framework_deps` → paste the failure naming `OutputEngine.h` → remove the line.
- [ ] **GREEN, callback shape.** `audioio_scoped_no_denormals_is_first` green with the `output_.render(...)` line present (Task C C2).
- [ ] **GREEN + RED, RT-hazard guards.** Both `output_render_has_no_rt_hazards` and `audioio_callback_has_no_rt_hazards` green; the render one shown red once (Task C C3).
- [ ] **Lengths + no stray edits.** `wc -l` over every new file (all < 400, `OutputEngine.{h,cpp}` < 300); `git diff --stat main -- platform/types/include/rta/platform/ChannelConfig.h platform/types/include/rta/platform/CaptureBus.h` is empty (the mirror is a new file, the originals are untouched).

---

## Numbers the builder must measure, not copy

Every figure is a prediction to falsify. Measure the baselines on HEAD `1ad48da` **before** Task A; if a measurement disagrees, the plan is wrong and the orchestrator hears about it — do not bend the code to hit it.

| quantity | how |
|---|---|
| ctest OFF baseline `base_core` / `base_platform` | `--clean-first` build of `1ad48da`, `build-l7out` |
| ctest ON baseline `base_on` | `--clean-first` ON build, `build-l7out-on` |
| OFF after A / B | `base_core+1` / `base_platform+8` (re-derive if a `TEST_CASE` list changes) |
| ON after C / D | `base_on+1` (C1) + 2 guard tests / `base_on+2` (D1–D4) |
| `platform_types_has_no_framework_deps` / `core_has_no_framework_deps` scanned | the guard prints `(N files scanned)`; both rise |
| MSVC `/W4` warnings | grep the build log; a warning is a defect |

## Build sequence and acceptance gate

1. **Task A first** (`RampedGain::prepare`, OFF) — every later task's `prepare`/reconfig path depends on it.
2. **Task B** (`OutputEngine` core logic, OFF) — the whole engine, provable with no device.
3. **Task C** (`AudioIo` integration + guards, ON).
4. **Task D** (app policy + G20 wiring, ON).
5. **Task E** (guards shown to guard, both configs).

**Acceptance gate:** OFF and ON ctest both green at the measured counts; **0 `/W4` warnings** in both configs; `audioio_scoped_no_denormals_is_first` green with the render line present; both RT-hazard guards green **and each shown red once**; both framework guards green with risen scanned counts and one shown red once; every new file < 400 lines (`OutputEngine.{h,cpp}` < 300).

## What L7-OUT does NOT include (deferred; record §11, §12, §13)

- Live parameter changes without a swap (per-source atomic setters) — a swap is the v1 mechanism (record §4, §12).
- Decorrelated per-output noise (N sources, N scratch buffers) — one correlated signal on every routed output (record §4).
- Two *different* signals live on two outputs at once (record §12).
- Feeding the rendered block back as an internal reference channel — touches `CaptureBus`'s write path (record §12).
- Persistence of the routing table and the member→output map (schema 4 of `SessionCodec`, record §12).
- Headroom/clipping policy for the summed drive — stays in `app/` (record §12).
- The generator-panel UI and the routing matrix's output half (record §12).
- **Open, needs a human (record §13.1):** does a real multi-output interface open with 64 requested output channels? The input side has never been refused; the output side has never asked. Fallback if refused: lower `kRequestedOutputChannels` to the device's count — one constant, planned as a comment at `AudioIo.cpp:23`, no design fork.

## For the station-4 builder, first read

1. The record `docs/dsp/2026-09-06-l7-output-path.md` is binding; this plan implements only its §6 contract and flags OUT-R1..R4, which the orchestrator amends first.
2. Order is fixed by dependency: **A (RampedGain::prepare) → B (OutputEngine) → C (AudioIo) → D (app policy) → E (guards)**. One commit per task.
3. The failing test first, seen to fail (the missing member / missing `#include`), then the header, then the body. Paste the command and its output for every "done" — a green build proves it compiles, not that the numbers are right (`CLAUDE.md`).
4. `ChannelConfig.h`/`CaptureBus.h` are the mirror templates, not edit targets — prove them untouched with the diff (Task E).
5. Every count here is a prediction you are expected to falsify if it is wrong.
</content>
</invoke>
