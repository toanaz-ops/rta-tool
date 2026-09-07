# The lock-free generator output path (lane L7, sub-lane L7-OUT)

*Decision record, station 2 of the pipeline in `docs/reports/README.md`.
Written 2026-09-06 on `1ad48da` from the station-1 report
`docs/research/2026-09-06-l7-output-path-station1-research.md`. Every claim
about the code below was re-read in the file it names before it was built on;
where the research was wrong or silent, §1 says so. **This record defines the
interface that L7-DELAY, L7-EQ, L7-ALIGN and G20 auto solo/mute consume** —
§6 is the contract those records cite; §11 is what they may assume.*

## 0. What L7-OUT is for, and the owner's ruling

Every solver in L7 plays a signal before it can do its own job: auto-EQ
verifies a correction by playing through it, auto-delay plays to find an
arrival, the alignment wizard plays one loudspeaker box then the next, and
G20 auto solo/mute is nothing *but* choosing which output plays. Today the
audio callback clears every output (`platform/src/AudioIo.cpp:134-144`) and
`core/src/gen/*` has no caller in `app/` outside the deterministic test feeds.
L6b §8 refused to build solo/mute inside the sequencer lane because it changes
the callback contract, and asked for this pass.

The owner ruled (`docs/HUMAN-QA-QUEUE.md`, "Từ lane L6b", 2026-09-06):
*"Đường output generator (auto solo/mute, G20) → GỘP VÀO L7. Không lane
riêng. Vì mọi solver L7 cũng cần phát tín hiệu, đường output lock-free được
thiết kế MỘT LẦN trong trạm 1 của L7 (prerequisite), phục vụ cả solver
excitation lẫn G20 auto solo/mute."* Designed once, first, for both consumers.

## 1. Provenance: what the research established, and what this record corrects

The research's central recommendation — render synchronously inside the
callback, gate each output through `rta::gen::RampedGain`, route through a
relaxed-atomic table shaped like `ChannelConfig`, keep `ScopedNoDenormals`
first — **is confirmed** (§2). Five things it got wrong or left open are
corrected here so nobody builds on them:

1. **"Every generator class is span-in/span-out with no FFT" is false for
   `Synthetic.h`.** `core/src/gen/Synthetic.cpp:43` constructs a `RealFft` and
   allocates a block inside `SyntheticPink`'s constructor, in the same
   `rta::gen` namespace as the real-time sources. The callback-eligible set is
   therefore a **closed type** (§4), and `Synthetic.h` is outside it by name.
2. **The research does not say how the active source is swapped.** Assigning a
   `std::variant` on the message thread while the callback reads it is a data
   race on the discriminant; and `RampedGain::target_` is a *relaxed* atomic
   (`Oscillator.cpp:73`), which cannot publish the contents of a freshly
   written object to the audio thread. §4 defines a quiescent-swap protocol
   with one release/acquire pair.
3. **`RampedGain` is neither copyable nor movable** (it holds
   `std::atomic<bool>`), so a `prepare()` on device reconfiguration cannot
   rebuild the per-output gates by assignment; rebuilding them by `emplace`
   from the device thread would race the message thread's `requestOn()` —
   the exact shape of the use-after-free `CaptureBus.h:52-75` closed. One
   small core addition is the honest fix (§5, §9); "no core change" does not
   survive contact with the constructor.
4. **The `RampedGain` header disagrees with a per-output-only design.**
   `Oscillator.h:69-77` says the ramp is "deliberately ONE ramp for the whole
   generator" so that *switching source* is click-free, and warns that ramps
   "wired independently of this one would silently reintroduce the click".
   Per-output gates alone cannot make a source switch click-free. §3 keeps
   both: a master source gate and per-output gates.
5. **Confirmed as stated:** the callback body and its clear loop
   (`AudioIo.cpp:124-144`); the guard's mechanism — the first non-blank,
   non-`//` line after the opening brace must match `ScopedNoDenormals`
   (`check_callback_shape.cmake:101-133`), registered only when
   `RTA_BUILD_APP` is ON (`platform/tests/CMakeLists.txt:38-43`);
   `kRequestedOutputChannels = 2` (`AudioIo_Devices.cpp:23`); `RampedGain` has
   no caller outside its own tests; `Sweep::nextSample` returns `0.0f` forever
   once `n_ >= lengthSamples_` (`Sweep.cpp:180`); `CaptureSequencer::onStep`
   is a bare `std::function<void(int)>` (`CaptureSequencer.h:69`).

## 2. Decision: the callback renders the source itself, synchronously; no feeder thread

**Decision.** The audio callback renders one block from the active source into
a fixed scratch buffer and fans it out through the gates. No thread other than
the audio thread ever produces output samples.

**Against the obvious alternative — a feeder thread rendering into an SPSC
ring the callback drains, the mirror image of `CaptureBus`.** It looks like
symmetry and it is not, on the one property that matters: **slack**. On the
capture side the ring absorbs a *consumer* that is late; the producer is the
audio thread, which is never late because it *is* the deadline, and a late
consumer costs a dropped block of analysis, never a broken device stream. On
the playback side the ring's producer would be an ordinary thread — JUCE gives
it no priority and no deadline — competing with repaints, the analysis thread
and the driver's own IRQ work, and the *consumer* is the deadline. A producer
that is late by one scheduling quantum leaves the callback with nothing
correct to hand back: either an underrun (the dropout this whole architecture
exists to prevent) or a fallback (zeros, stale samples) — and a fallback that
emits zeros mid-signal is a step discontinuity, i.e. exactly the click the
ramp exists to remove, returning through the branch nobody tests
(`memory/a-fixed-defect-returns-through-the-silent-fallback.md`). Two further
costs the research did not name: **mute latency equals ring depth** — a panic
mute at a show would take effect `depth × blockSize` samples later (eight
blocks of 512 at 48 kHz is 85 ms), unless the gate stays in the callback, in
which case the callback is doing per-sample work anyway and the feeder bought
nothing; and the feeder duplicates every piece of state (armed, source, epoch)
across three threads instead of two. Feeder threads earn their place for
disk-bound sources that already accept a large safety buffer; nothing here
reads a disk.

**Against the minimal patch — a hardcoded generator member and a bool.** It
cannot express "channel 3 on, channel 7 off, channel 5 fading in while 3
fades out", which is what a wizard stepping through boxes and G20 both need,
and every later caller would grow its own gating. The input side did not stay
"channel 0 to ring 0, hardcoded" for the same reason.

**What the render costs, bounded not measured.** Per sample: pink is seven
multiply-adds and one PCG32 draw; sine is one `std::sin`; sweep is one
`std::exp` and one `std::sin`; MLS is a shift and an XOR; each gate is one
multiply, plus one `std::cos` per sample *during a ramp only* (480 samples per
event at 48 kHz). A 512-sample block at 48 kHz has a 10.7 ms deadline; the
worst case above is on the order of tens of microseconds. The plan measures
it with `DeviceState::cpuLoad` (already surfaced) idle versus pink on two
outputs, and reports the figure — a report headline, not an assertion.

## 3. Decision: two gate stages — one master source gate, one gate per output

**Decision.** The scratch block passes first through a **master**
`RampedGain` (the "one ramp for the whole generator" of `Oscillator.h:69-77`),
then is copied to each routed output through that output's **own**
`RampedGain`. Both stages are the shipped class, unchanged in shape: closed
form `g(p) = ½(1 − cos πp)`, 10 ms, C¹ at both ends, continuous through a
reversal (`test_generator_osc.cpp:187-263`).

The master gate is what makes *starting, stopping and swapping the source*
click-free, and it is the gate whose Idle state defines quiescence (§4). The
per-output gates are what make *solo and mute* click-free and independent.
When both stages ramp in the same samples the output is the product of two
raised cosines — still C¹, still click-free; the order in which a consumer
arms and routes does not matter for clicks.

**Independent per-output gates are the primitive; strict solo is app
policy.** Neither Smaart's nor REW's documentation describes an interlock;
Smaart's "independent output trim" reads as N addressable outputs; SMPTE
ST 202 §5.6 and M1's *Autosolo* imply mutual exclusion *during a sequence*.
These are two call patterns, not two primitives: the platform exposes
`routeOutput(channel, active)`; `app/` supplies `soloOutput(ch)` = every other
output off, `ch` on, and G20's sequence step calls it. A handover between two
outputs has a closed-form property worth asserting: the falling and rising
gates are complementary, `½(1 + cos πp) + ½(1 − cos πp) ≡ 1`, so the summed
drive of the two outputs is constant to float rounding through the whole
handover. The cost of the alternative — an interlock baked into the platform —
is that it cannot express Smaart's multi-mono routing, and it puts a policy
where a mechanism belongs. The cost of the chosen design is that an `app/`
caller who forgets to mute the others leaves two boxes playing: audible,
visible in the routing matrix, never a dropout.

**The ramp is a design constant, never zero.** REW lets an operator set sweep
fade to zero because the worst outcome in a listening room is an unpleasant
noise. Here the worst outcome is a live PA, and the generator record already
made click-free start/stop a real-time rule, not a preference
(`docs/dsp/2026-08-27-generator.md`). The interface exposes no ramp length;
10 ms (`RampedGain`'s default, inside its 5–20 ms design range) is fixed. A
signal that genuinely needs a hard edge is a *source* with its own envelope,
designed in `core/` with its own test — not a gate option.

## 4. Decision: one active source in a closed variant, swapped only while quiescent

**Decision.** The engine holds exactly one active source in

```
SourceVariant = std::variant<std::monostate, gen::Oscillator, gen::DualSine,
                             gen::WhiteNoise, gen::PinkNoise, gen::Sweep, gen::Mls>
```

Every alternative is a `noexcept`, allocation-free, span-out renderer by its
own header's contract (`Sweep.h:160-162`, `Mls.h:53-58`, `Prng.h:28-29`,
`Oscillator.h`, `Noise.h`); every one is a value type with no heap and a
`noexcept` move, so the variant can never become valueless and `std::visit`
can never throw on the audio thread. `Synthetic.h` is not in the set (§1.1).
Constructors — which may throw (`Sweep`, `Mls`) and compute — run on the
message thread, before the value is handed over.

**The quiescent-swap protocol.** The slot is written by the message thread
only, and only when the source is *quiescent*: disarmed, **and** the callback
has published that the master gate reached `Idle`. The chain that makes this
a proof rather than a hope:

1. The callback stores `publishedIdle_ = true` with **release** at the end of
   the block in which the master gate reached `Idle`; while `Idle` and
   disarmed it never touches the slot.
2. `setSource()` loads `publishedIdle_` with **acquire** and refuses (returns
   `false`, writes nothing) unless it is true and `armed_` is false. On
   success it move-assigns the slot and records the current output epoch in
   it.
3. `armSource()` stores `armed_ = true` with **release**. The callback loads
   `armed_` with **acquire** once per block, first among its output-side
   reads; only then does it drive the master gate on and visit the slot.

Release/acquire on (1) orders the message thread's writes after the audio
thread's last reads; release/acquire on (3) orders the audio thread's first
reads after the message thread's writes. Nothing else on the source path
needs more than relaxed. Because only the message thread arms, "quiescent
now" stays true until that same thread arms again — no window for the audio
thread to re-enter. **Parameter changes (frequency, level) are swaps** in this
version: disarm, wait for quiescence (one 10 ms fall), set, arm (one 10 ms
rise). No L7 consumer turns a knob live — solvers set a signal and play it —
so live parameter atomics are deferred (§12), not designed badly now.

**One signal, correlated across routed outputs.** The scratch block is the
same for every routed output. Decorrelated per-output noise
(`ChannelSeeds::forChannel`, `Prng.h:118`) is N source instances and N
scratch buffers — trivial memory, N× render cost — and is a later amendment;
nothing in solo, sequenced or single-box solver use needs it.

**A device reconfiguration invalidates the source.** `prepare(sampleRate,
numOutputs)` runs on the device thread before the callback is inserted (the
same slot as `CaptureBus::prepare`). It **never writes the slot** — the
message thread may be inside `setSource()` — it bumps `outputEpoch_`, disarms,
and retargets the gates (§5). The callback renders silence for a slot whose
recorded epoch differs from the current one; the app, which already watches
`CaptureBus::epoch()` to rebuild analysis state, re-sets the source. Sources
carry their sample rate at construction (`Oscillator`, `Sweep`), so a source
built for 48 kHz playing at 96 kHz would be the wrong pitch — the epoch rule
makes that impossible rather than unlikely.

## 5. Decision: the routing table is `ChannelConfig`'s shape; gates are retargeted, never rebuilt

**Decision.** `OutputRole {None, Routed}` per output channel in a fixed
`std::array<std::atomic<int>, kMaxChannels>`, relaxed store from the message
thread, relaxed load from the callback, bounds-checked against
`[0, kMaxChannels)` on write and against **`numOutputChannels` actually
received this block** on read — the same three rules `ChannelConfig.h:37-64`
already states and the same reasoning ("a role flip a few samples late is a
UI-visible glitch, never a memory-safety issue"). Relaxed is correct here
because a routing flip publishes no object; the per-output `RampedGain`'s own
relaxed `target_` is set in the same call.

**The requested output-channel count rises from 2 to `kMaxChannels`**,
mirroring the input side's ceiling (`AudioIo_Devices.cpp:14-24`: "JUCE opens
as many as the device actually has, never more"). It is one constant in one
file, the table and the callback are sized for 64 regardless, and the
bounds-against-received rule makes a smaller device safe. Whether a real
multi-output interface refuses a 64-output request is a hardware question
(§13) with a one-line fallback, not a design fork.

**Gates are retargeted in place.** `prepare()` must give every gate the new
ramp length in samples (10 ms is 480 samples at 48 kHz, 960 at 96 kHz; a gate
left at 480 samples would be 2.5 ms at 192 kHz, outside the design range).
`RampedGain` cannot be reassigned (§1.3), and re-`emplace`-ing 65 objects from
the device thread while the message thread may be calling `requestOn()` on
one of them is a use-after-destruction. So `core` gains one method,
`RampedGain::prepare(double sampleRate)`, which rewrites only the
audio-thread-owned fields (`rampLenSamples_`, `pos_ = 0`, `state_ = Idle`) and
never `target_`. It is safe from the device thread for the same reason
`CaptureBus::prepare` is — the callback is quiesced — and it cannot race the
message thread because the two touch disjoint fields. This is the whole core
change: about five lines and one closed-form test (§10, T8).

## 6. The interface — what every L7 sub-lane and G20 consume

Lives in `platform/types/` (JUCE-free, scanned by
`platform_types_has_no_framework_deps`), takes plain C arrays on the audio
side like `CaptureBus::pushFromCallback`, and is therefore provable with no
device.

```cpp
// platform/types/include/rta/platform/OutputEngine.h   (rta_platform_types)
namespace rta::platform {

enum class OutputRole { None, Routed };

using SourceVariant = std::variant<std::monostate, gen::Oscillator, gen::DualSine,
                                   gen::WhiteNoise, gen::PinkNoise, gen::Sweep, gen::Mls>;

class OutputEngine {
public:
    OutputEngine();                       // the ONLY allocation: scratch, sized once

    // Device thread, before the callback is inserted (CaptureBus::prepare's slot).
    // Bumps outputEpoch(), disarms, retargets every gate. Never writes the slot.
    void prepare(double sampleRate, int numOutputChannels);

    // ---- message thread: the source (quiescent-swap protocol, §4) ----------
    [[nodiscard]] bool sourceIsQuiescent() const noexcept;   // acquire
    bool setSource(SourceVariant&& source);                  // false unless quiescent
    void armSource() noexcept;                               // release
    void disarmSource() noexcept;

    // ---- message thread: per-output gates. THE call every consumer uses. ----
    // Relaxed; false and no effect outside [0, kMaxChannels). Sets the role and
    // the gate's target in one call. Strict solo is the caller's loop (§3).
    bool routeOutput(int outputChannel, bool active) noexcept;
    [[nodiscard]] OutputRole role(int outputChannel) const noexcept;

    // ---- telemetry: written by the callback, read anywhere ------------------
    [[nodiscard]] std::uint64_t renderedSamples() const noexcept;  // since last arm
    [[nodiscard]] std::uint64_t outputEpoch() const noexcept;      // bumped by prepare()
    [[nodiscard]] double sampleRate() const noexcept;

    // ---- audio callback: the ONLY audio-thread entry point. Wait-free. ------
    // Renders (if armed, current-epoch, gate not Idle) into scratch in chunks,
    // master-gates it, copies through each routed output's gate, clears every
    // other channel, publishes counters. `output == nullptr` is a no-op.
    void render(float* const* output, int numOutputChannels, int numSamples) noexcept;
};

}  // namespace rta::platform
```

`AudioIo` owns one `OutputEngine output_`, exposes `output()` beside `bus()`,
calls `output_.prepare()` from `audioDeviceAboutToStart` beside
`bus_.prepare()`, and its callback's clear loop becomes the one line
`output_.render(outputChannelData, numOutputChannels, numSamples);`.

The consumer sequence, identical for a human toggle, `CaptureSequencer::onStep`,
a solver and G20: `setSource(...)` → `routeOutput(ch, true)` (and `false` for
the rest, if solo) → `armSource()` → poll `renderedSamples()` → `disarmSource()`
→ wait `sourceIsQuiescent()` before the next `setSource`. The audio thread
never learns which caller it was.

## 7. What changes in the audio-callback contract, and why the two guards still hold

Today (`AudioIo.cpp:111-145`): `ScopedNoDenormals`; `bus_.pushFromCallback`;
clear every output. After this record:

1. `const juce::ScopedNoDenormals noDenormals;` — **unchanged, still the first
   statement.** `check_callback_shape.cmake` finds the last non-`//` line
   naming the function, walks braces to the body, and requires the first
   non-blank, non-`//` line strictly after the opening brace to match
   `ScopedNoDenormals`. Every new line is appended *after*
   `pushFromCallback`; the render lives in `OutputEngine::render` in its own
   file, so `AudioIo.cpp` gains one call and no second definition that could
   confuse the "last line naming the function" search. Two things the plan
   must not do: put a `/* */` comment mentioning the function name after the
   definition, or move the definition. The guard is ON-only; CI with
   `RTA_BUILD_APP=OFF` does not run it, which is why the render logic is
   tested in `rta_platform_tests` (§10) rather than through the callback.
2. `bus_.pushFromCallback(...)` — unchanged.
3. `output_.render(outputChannelData, numOutputChannels, numSamples);` —
   replaces the clear loop. Idle path: one acquire load of `armed_`, one
   relaxed load of the master gate's published state, then clear — the same
   cost class as today's clear plus what `isActive()` already costs.

**Real-time safety, operation by operation, on the armed path:** one acquire
load; `kMaxChannels + 1` relaxed loads; `std::visit` (a jump table) into
`process(std::span<float>)` of one of six `noexcept`, heap-free sources whose
per-sample work is `sin`/`exp`/PCG32/seven IIR taps/LFSR; `RampedGain::
nextGain` (`cos` during ramps, `noexcept`); `std::copy_n`/`std::fill_n` into
caller-owned buffers; relaxed stores of two counters and one release store of
a bool. Chunked rendering through a fixed scratch means any `numSamples` is
handled with no growth path — there is no "buffer too small" branch to fall
through. No `new`, no `mutex`, no file, no FFT (the only FFT-bearing generator
is excluded by type, §1.1), no `juce::` symbol in the engine at all
(`FloatVectorOperations::clear` becomes `std::fill_n`). Denormals: FTZ/DAZ is
already set by statement 1 before any of this runs; the pink IIR decaying
toward silence after a mute is exactly the limit cycle it exists for.

**A second tripwire.** `check_callback_shape.cmake` is parameterised by
function name and grown with a forbidden-token pass over the body
(`new |malloc|mutex|lock_guard|vector<|Fft|fopen|printf`), run over
`AudioIo::audioDeviceIOCallbackWithContext` and `OutputEngine::render`. A
grep, not a proof — "a tripwire for the ordinary and the careless", as the
coherence guard calls itself — made red once deliberately before it ships.

## 8. Rejected options and what each would have cost

| Rejected | Cost that decided it |
|---|---|
| Feeder thread + SPSC ring (mirror of capture) | A late producer is a dropout, not a late plot; mute latency = ring depth (85 ms at 8×512); the zero-fill fallback is the click reintroduced (§2) |
| Hardcoded generator member + one bool | Cannot express per-output fade-in/fade-out; every caller regrows gating (§2) |
| Per-output gates only, no master | Source switch clicks; contradicts `Oscillator.h:69-77`'s stated design (§3) |
| Strict solo interlock in the platform | Cannot express multi-mono; policy in a mechanism layer (§3) |
| Operator-settable fade down to 0 (REW) | A click into a live PA; the generator record already ruled it a real-time rule (§3) |
| Live parameter atomics in v1 | Six sources × N parameters of racy setters to redesign; no L7 consumer needs it (§4, §12) |
| Rebuild gates by `emplace` in `prepare()` | Device-thread destruction under a message-thread `requestOn()` — the `CaptureBus` UAF shape (§5) |
| Keep `kRequestedOutputChannels = 2` | A wizard cannot address box 3; the table would be a lie about its own width (§5) |
| Render only when something is routed | A sweep's clock would stall on an unrouted step; "armed" must mean the source advances (§4) |
| A completion callback from the audio thread | A `std::function` call in the callback; counters plus a 20 Hz poll the app already runs suffice (§11) |

## 9. Boundary: what each layer owns

- **`core/`** — the six sources and `RampedGain`, unchanged except
  `RampedGain::prepare(double)` (§5). Still `std::span` in, numbers out; the
  `core_has_no_framework_deps` guard is untouched.
- **`platform/types/`** — `OutputEngine` (`OutputEngine.h`, `OutputEngine.cpp`,
  each under 300 lines): the routing table, 65 gates, the slot, the scratch,
  the protocol, `render()` over plain arrays. JUCE-free, scanned by
  `platform_types_has_no_framework_deps`.
- **`platform/`** — `AudioIo` gains one member, one accessor, one `prepare`
  call, one `render` call, and `kRequestedOutputChannels = kMaxChannels`.
- **`app/`** — policy and wiring only: `soloOutput(ch)` and `muteAll()` as a
  loop over `routeOutput`; `CaptureSequencer::onStep` bound to
  `soloOutput(outputOfMember(step))`; solvers calling the §6 sequence; the
  member→output map (a preset field, §12). No `app/` code touches a gate or a
  source directly.

## 10. How CI proves this with no sound card

Platform tests in `platform/tests/test_output_engine.cpp` (JUCE-free, run
with `RTA_BUILD_APP=OFF`), one core test beside the existing `RampedGain`
tests, one contract test in `platform/tests_juce/`. Everything here is
identity arithmetic over already-golden'd sources; **no new golden vector is
needed**, and none is added for tidiness.

1. **Idle contract preserved.** Nothing armed: every channel of a 2×512 block
   is exactly `0.0f`; `renderedSamples() == 0`; `output == nullptr` is a
   no-op.
2. **Closed-form ramped sine.** `Oscillator(48000, 1000, −6 dBFS)`, route
   channel 1 only, arm; render 480 then 512 samples. Channel 1 sample `n`
   equals `A·sin(2π·1000·n/48000)·g(n)` with `g(n) = ½(1 − cos(πn/480))` for
   `n < 480` and `1` after (both gates rise together: `g²` for the first
   480 samples, since routing and arming land in the same block); channels 0
   and 2..N exactly `0.0f`. Tolerance `1e-6` absolute — float output, the
   figure the core ramp test uses.
3. **Complementary handover.** Channel 2 routed and steady as the reference
   copy; then in one block `routeOutput(1, true)`, `routeOutput(0, false)`
   from a steady channel 0: for every sample `out0 + out1 == out2` within
   `1e-6` — the identity `½(1 + cos) + ½(1 − cos) ≡ 1` observed on real
   output, and `RampedGain`'s reversal continuity carried through the fan-out.
4. **Bounds against received.** Route channel 5; render with
   `numOutputChannels = 2`: no write outside the two buffers (run under the
   sanitiser configuration), channels 0–1 zero. `routeOutput(−1, …)` and
   `routeOutput(64, …)` return `false` and change no role.
5. **Quiescent protocol.** `setSource` while armed returns `false` and the
   sine's phase stays continuous across the refusal (assert `phaseTurns()`
   progression through the visible output). After `disarmSource()` and one
   full ramp of rendering, `sourceIsQuiescent()` is true and `setSource`
   succeeds; the first armed block of the new source starts from the ramp's
   `g(0) = 0`.
6. **Chunking is invisible.** `numSamples = 3 × scratchCapacity + 17` equals
   a single-pass reference built from a standalone `Oscillator` and two
   `RampedGain`s, sample-exact (same float operations in the same order).
7. **A sweep ends at zero and the counter tells when.** `Sweep` of 0.1 s;
   render past `lengthSamples()`: every later sample is exactly `0.0f`;
   `renderedSamples()` equals the samples rendered since arm exactly.
8. **`RampedGain::prepare` (core).** After `prepare(96000)` a 10 ms ramp
   takes exactly 960 samples to reach unity, `g(480) = 0.5` to `1e-6`; state is
   `Idle` and `pos_` is 0 regardless of prior state; `target_` is untouched
   (a pending `requestOn()` survives and rises after the prepare).
9. **Epoch invalidation.** `prepare()` at a new rate → `outputEpoch()`
   increments, the engine is disarmed, and an `armSource()` on the stale slot
   renders silence until `setSource` is called again.
10. **Callback contract** (`tests_juce`, ON only): `AudioIo` driven with
    `audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 256, ctx)` as
    `test_audio_io_contract.cpp:182` already does — zeros before arming
    (today's contract), a non-zero routed channel and a zero unrouted one
    after.
11. **Guards.** `audioio_scoped_no_denormals_is_first` green with the new
    line present; the forbidden-token pass over both functions green, and made
    red once by planting `std::mutex m; std::lock_guard g(m);` in `render()`
    and watching it fail. Counts after the lane are read from each guard's
    own output, not predicted here.
12. **Cost, measured not asserted.** `cpuLoad` idle versus pink on two
    outputs at 48 kHz/512, recorded in the report.

## 11. What the consuming records may assume

For L7-DELAY, L7-EQ, L7-ALIGN and G20, without re-deriving it:

- **One signal at a time**, identical on every routed output; N independent
  gates; strict solo is `app/`'s `soloOutput`, not the platform's.
- **The sequence** is §6's: set → route → arm → poll → disarm → quiescent.
  `setSource` while armed is refused, never queued.
- **The first 10 ms after arming, and the 10 ms after a route change, are not
  stationary.** A solver that averages should start its window after
  `renderedSamples() ≥ 480 · fs/48000`, or simply wait for the coherence gate
  it already waits for.
- **A sweep ends itself at zero** (`Sweep`'s own fade-out) and the output is
  silent thereafter; completion is `renderedSamples() ≥ lengthSamples() +
  tail`, read by polling. There is no completion callback.
- **Latency between what is rendered and what is captured is unknown to this
  layer.** No loopback reference is provided (§12); the delay finder
  (`findDelayPhat`) is how a solver learns it, exactly as the L7-DELAY research
  already concluded ("nothing in the delay math needs playback").
- **Never touch a gate or the slot from anywhere but the message thread.**
- **The SysTune and Smaart gap does not block anything.** Whether Smaart's
  solo is strictly interlocked decides only the default of an `app/` policy
  that the primitive expresses either way.

## 12. What this record does not decide

- Live parameter changes without a swap (per-source atomic setters).
- Decorrelated per-output noise (N sources, N scratch buffers).
- Two *different* signals live on two outputs at once — no surveyed tool
  needs it; revisit before any solver that does.
- Feeding the rendered block back as an internal *reference* channel (REW's
  model) — it touches `CaptureBus`'s write path and L2's reference semantics,
  and deserves its own paragraph in whichever record first needs it.
- Where output routing and the member→output map persist (schema 4 of
  `SessionCodec`, under L6b §9's "loads unbound rather than rebinding" rule).
- Headroom and clipping policy for the summed drive — `Noise.h:92-98` places
  it in `app/`, and nothing here changes that.
- The UI for the generator panel and the routing matrix's output half.

## 13. Open questions for a human

1. **Does a real multi-output interface open with 64 requested output
   channels?** The input side asks for 64 and has never been refused; the
   output side has never asked. If a device refuses, the fallback is to lower
   `kRequestedOutputChannels` to that device's count — one constant, nothing
   else moves. Needs a session with real hardware, not a code change.
2. **G20's default: strict solo or additive?** The primitive is indifferent;
   `soloOutput` is the proposed default for a *sequence* (SMPTE ST 202 §5.6,
   M1 *Autosolo*), additive for a *manual* toggle (Smaart's model). One
   sentence from the owner fixes both.
3. **Should a device reconfiguration silently drop the armed source (this
   record), or refuse the reconfiguration while armed?** Dropping matches
   `CaptureBus::prepare` draining rings; refusing would be the first place a
   device-panel action is blocked by generator state.

## Sources

This repo: `platform/src/AudioIo.cpp:111-145`, `AudioIo_Devices.cpp:14-24`,
`platform/tests/check_callback_shape.cmake`, `platform/tests/CMakeLists.txt`,
`platform/types/include/rta/platform/{ChannelConfig,CaptureBus}.h`,
`core/include/rta/gen/{Oscillator,Noise,Sweep,Mls,Prng,Synthetic}.h`,
`core/src/gen/{Oscillator,Sweep,Synthetic}.cpp`,
`core/tests/test_generator_osc.cpp:187-263`, `core/tests/check_no_framework_deps.cmake`,
`app/src/measure/{CaptureSequencer,RoutingPlan}.h`,
`docs/dsp/2026-08-27-generator.md`, `docs/dsp/2026-09-06-multichannel-l6b.md` §8,
`docs/research/2026-09-06-l7-output-path-station1-research.md`,
`docs/research/2026-09-06-l7-auto-delay-station1-research.md`,
`docs/HUMAN-QA-QUEUE.md` ("Từ lane L6b"), `docs/plans/MASTER-EXECUTION-PLAN.md` (L7 row),
`memory/a-fixed-defect-returns-through-the-silent-fallback.md`.
External, as reached by station 1: REW Signal Generator and Soundcard help
(configurable fade, output selection); Rational Acoustics Smaart Suite feature
list and Signal Generator fly-up guide (multi-mono/stereo, independent trim);
SMPTE ST 202 §5.6; M1 *Autosolo*; JUCE `AudioIODeviceCallback` documentation
and forum consensus on locks in the callback. The Smaart v8 routing chapter and
the SysTune manual's generator chapter were **not** read past summary depth
(§11, last point).
