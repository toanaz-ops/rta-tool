# The lock-free generator output path — station 1 research

*2026-09-06. Station 1 research, lane L7-OUT.*

Owner instruction (`docs/HUMAN-QA-QUEUE.md`, "Từ lane L6b", 2026-09-06): the
output path is folded into L7 and researched **first**, because auto-EQ
verification, auto-delay playback, the alignment wizard, and G20 auto
solo/mute all need to play a signal before any of them can do their own job.
L6b's own decision record (`docs/dsp/2026-09-06-multichannel-l6b.md` §8)
explicitly refused to build generator solo/mute inside the sequencer lane
because it changes the audio-callback contract — "the one real-time rule in
CLAUDE.md" — and required its own research pass first. This is that pass.

## Decision → evidence table

| Candidate decision | Evidence | Source |
|---|---|---|
| The callback must render the generator **synchronously, in-line**, not via a background thread feeding a ring | Capture already proves the fixed-allocation, no-lock, relaxed-atomic idiom works for one direction (device → app); playback is timing-inverted (app → device) and has no slack to absorb producer jitter | `platform/types/include/rta/platform/CaptureBus.h` (fixed 16 MB allocation, `epoch()`/`active_` relaxed atomics), reasoning below in "Candidate architectures" |
| `core` already ships the exact click-free ramp primitive this path needs — reuse it, do not reinvent | `rta::gen::RampedGain`: atomic `target_` (control thread) + audio-thread-only `pos_`/`state_`, closed-form raised-cosine `g(p)=0.5(1-cos(πp))`, tested for the reversal case | `core/include/rta/gen/Oscillator.h:69-113`, `core/src/gen/Oscillator.cpp:69-128`, `core/tests/test_generator_osc.cpp:187-262` |
| `RampedGain` is currently **unused** outside its own tests — this lane is its first real caller | `grep RampedGain` hits only the header, the .cpp, its test file, `Sweep.cpp` (independently re-derives the same closed form for an *offline* fade, not this class), and the impl plan | Repo-wide grep, 2026-09-06 |
| Output routing should be DATA read by the callback via relaxed atomics, mirroring `ChannelConfig`'s already-proven input-side pattern | `ChannelConfig`: per-channel `std::atomic<int>` role + tfIndex, message-thread `setRole`/`setTransferFunction`, callback-thread `snapshot()`/`role()`, explicit "why atomic not mutex" class comment | `platform/types/include/rta/platform/ChannelConfig.h:34-64` |
| The callback currently does nothing on the output side but clear it — this is the exact contract line that must change | `AudioIo.cpp:134-144`: "This deliverable produces no audio output ... clearing is the entire output-side contract" | `platform/src/AudioIo.cpp:134-144`, `AudioIo_Devices.cpp:18-24` ("driving a generator OUT to a device is explicitly out of scope") |
| `ScopedNoDenormals` must stay the literal first statement; the guard is a brace-counting grep, not a semantic check | `check_callback_shape.cmake` walks the function body and asserts the first non-comment, non-blank line matches `ScopedNoDenormals` | `platform/tests/check_callback_shape.cmake:49-133`, `platform/tests/CMakeLists.txt:39` |
| Output channel count is currently hard-capped at 2, unlike input's `kMaxChannels` (64) | `kRequestedOutputChannels = 2`, commented "this deliverable never writes anything but silence to it" | `platform/src/AudioIo_Devices.cpp:14-24` |
| Per-channel decorrelated noise (needed for multi-output solo/routing) is already designed, just unwired | "Decorrelated multichannel: per-channel PRNG state, seeds derived from one master seed via SplitMix64" | `docs/dsp/2026-08-27-generator.md` "Real-time and multichannel rules" |
| Click-free start/stop and a per-sample ramp state machine in the callback were already anticipated at generator design time, before this lane existed | "Click-free start/stop: 5-20 ms raised-cosine ramp run by a per-sample state machine in the callback; UI sets a target atomically, audio thread ramps" | `docs/dsp/2026-08-27-generator.md` "Real-time and multichannel rules" |
| Overload detection (needed so auto-EQ/alignment solvers know a captured block is trustworthy) is already a pure `core/` function usable per-hop | Overload = "three or more consecutive samples with `\|x\| ≥ 1 − 2⁻¹⁵`" (Smaart LE v9.1 p.78), implemented as `rta::dsp::hasOverload`, run on the analysis thread, never the callback | `docs/dsp/2026-09-06-multichannel-l6b.md` §8, `app/src/measure/CaptureSequencer.h:54-58` |
| REW ships a *user-configurable* fade on sweeps, settable to zero — this project should NOT copy that flexibility | "The sweeps have a configurable raised cosine fade in and fade out (which can be set to zero for no fade)" | REW Signal Generator help, `roomeqwizard.com/betahelp/help_en-GB/html/siggen.html` |
| Smaart's generator has per-output routing, multi-mono/stereo modes, independent output trim, and 10 output presets — evidence the "N outputs, each independently addressable, each with its own trim" shape is the professional norm, not this project inventing complexity | "The signal generator supports multi-output capabilities with mono, multi-mono, and stereo modes, independent output trim, and 10 programmable output presets" | Rational Acoustics, Signal Generator Fly-Up Menu guide + Smaart Suite feature list |
| REW's output selection is per-measurement / per-generator-panel, drawn from whatever outputs the device exposes, not restricted to a stereo pair | "REW can place its test signals on either or both of the output channels, and if there are more than 2 available output channels, the output channel selection can be made directly on the measurement panel or signal generator" | REW Soundcard Preferences help |
| SMPTE ST 202 and M1 Autosolo assume the analyser cycles through outputs one at a time, automatically | Already recorded: "SMPTE ST 202 §5.6 ('each channel or bank shall be measured separately in turn') and M1's Autosolo both assume the analyser drives the outputs" | `docs/dsp/2026-09-06-multichannel-l6b.md` §8 (citing SMPTE ST 202, M1 docs) |
| JUCE's audio-thread contract forbids taking a lock, allocating, or blocking in the callback — the same rule this project already states in `CLAUDE.md` | JUCE forum consensus: "you should never lock anything in the process callback, not even a mutex"; ramping via a smoothed coefficient (~20 ms) is the standard click-free pattern | JUCE forum, "Understanding Lock in Audio Thread"; JUCE docs, `AudioIODeviceCallback` |

## Where sources disagree

1. **Whether a fade is mandatory or an operator-configurable convenience.**
   REW lets the operator set sweep fade to *zero* — a deliberate escape hatch
   for a room-measurement tool where a click costs nothing worse than an
   unpleasant noise. This project's own decision record treats click-free
   start/stop as a **hard real-time rule** (`docs/dsp/2026-08-27-generator.md`),
   not a user preference, because the failure mode here is a live PA system,
   not a listening room. **Recommendation: do not expose "fade = 0" as an
   option on the output path.** `RampedGain`'s constructor already restricts
   the range to 5-20 ms (default 10 ms) — keep that a design constant, not a
   user-facing slider down to zero.

2. **Static per-output configuration vs. automated single-channel cycling.**
   Smaart's model (this research's clearest documentation) is a **static
   routing assignment** the operator sets once — multi-mono/stereo modes,
   independent trim, presets — and the generator simply plays through
   whatever is configured, indefinitely. SMPTE ST 202 and M1's Autosolo
   assume the opposite: the **analyser drives the cycling itself**, one
   output at a time, automatically, as part of the measurement sequence.
   These are not reconcilable into one "the" design — they are two different
   consumers of the same primitive. See "smallest interface" below: this
   research recommends a primitive general enough that Smaart's usage is "set
   routing once, never call the sequencer hook" and SMPTE/M1's usage is
   "`CaptureSequencer::onStep` calls the same routing setter every step" —
   the same one function, two call patterns, no branch in the platform layer
   for which philosophy is active.

3. **Independent per-output gates vs. strict interlocked solo.**
   Neither Smaart's docs nor REW's describe a strict "turning one output on
   forces all others off" interlock — Smaart's wording ("independent output
   trim") reads as N independently addressable outputs. SMPTE ST 202's "each
   channel... measured separately in turn" and the word "Autosolo" itself
   imply strict mutual exclusion during an automated sequence. **This
   research could not find primary-source confirmation that Smaart's own
   solo button is strictly interlocked** (the fetched fly-up-menu article
   does not cover it — see the open question below). Recommendation: build
   the general primitive (N independent gated outputs) and let strict solo
   be a *policy* the caller applies (turn target on, turn every other output
   off) rather than a constraint baked into the platform layer — the general
   version can express both usages; the strict version cannot express
   Smaart's multi-output routing.

4. **Documentation depth reachable for SysTune and Smaart's actual routing
   mechanics is shallow.** The SysTune manual PDF could not be parsed as text
   by the fetch tool available in this pass (binary/compressed stream); the
   Smaart fly-up-menu article explicitly does not cover routing mechanics,
   solo, or ramping. Everything above attributed to Smaart comes from the
   Smaart Suite feature list page and search-engine-summarized fragments of
   the v8 User Guide PDF, not a direct read of the routing chapter. Flagged
   as a genuine gap, not papered over — see open questions.

## Candidate architectures

### A — In-callback synchronous render, atomic routing table (recommended)

The callback renders one block from the active `core` generator into a
preallocated (never re-allocated after construction — same idiom as
`CaptureBus`'s fixed 16 MB) scratch buffer, then for each output channel
whose role reads `Routed` in a new `OutputRoutingConfig` (relaxed atomics,
same shape as `ChannelConfig`), copies the scratch buffer through that
channel's own `RampedGain::apply()` (per-output solo/mute, click-free); every
other output channel is cleared exactly as today.

```
core/      Oscillator, WhiteNoise, PinkNoise, Sweep, Mls, RampedGain
           -- ALL ALREADY EXIST, span-in/span-out, no device knowledge.
           No core change needed for the render step itself.

platform/  NEW: OutputRoutingConfig  (mirrors ChannelConfig: per-output
             atomic<int> role {None, Routed}, relaxed store/load)
           NEW: one RampedGain per output channel (audio-thread-owned,
             control-thread requestOn()/requestOff())
           NEW: AudioIo owns ONE active generator variant (std::variant or
             a small tagged union over Oscillator/WhiteNoise/PinkNoise/
             Sweep/Mls) + a preallocated scratch std::vector<float>,
             sized once like CaptureBus's rings.
           CHANGED: audioDeviceIOCallbackWithContext's output-clearing loop
             becomes: render into scratch (if any output is Routed --
             short-circuit on one atomic check when nothing is routed, same
             cost as today's clear-only path), then per-channel: Routed ->
             scratch through this channel's RampedGain; else -> clear.

app/       Sets which generator variant + parameters (message thread,
           mirrors setDesiredDevice's "applied on next start/block" shape).
           Owns the POLICY: CaptureSequencer::onStep, G20's auto solo/mute,
           and a future auto-EQ/alignment-wizard solver all call the SAME
           platform setter (routeOutput(channel, active) roughly) --
           this is the "share one mechanism" answer.
```

**Trade-offs.** Cheapest possible callback-thread cost (bounded trig/PRNG
calls, no different in kind from what the analysis side already tolerates
for the input rings); reuses `RampedGain` byte-for-byte as designed and
tested; extends a pattern (`ChannelConfig`) already reviewed and shipped for
the input side, so a reviewer already knows the shape. Costs: `core` grows
one small new orchestration type if AudioIo should not own a `std::variant`
of five generator classes directly (a thin `GeneratorEngine` facade in
`core/src/gen/` — still framework-free, still span-in/span-out — is cleaner
than five raw pointers in `AudioIo`, but is new surface, not a reuse of
something that exists today).

### B — Background thread renders into a lock-free SPSC ring the callback drains (the "obvious" symmetric mirror of capture — argue against)

Symmetric-looking to `CaptureBus`: a `PlaybackThread` (mirroring
`SyntheticInput`) renders generator blocks and pushes them into a ring; the
callback pops from the ring instead of computing anything itself.

**Why this looks obvious.** It is the literal mirror image of the proven
input path, and it would let a heavier generator (say, MLS with more state)
run with zero per-sample cost inside the real callback.

**Why it is wrong here, argued specifically:** capture and playback are not
symmetric in the property that actually matters — *slack*. On the capture
side, the ring exists to absorb the analysis thread being scheduled late;
the audio thread always pushes on time (it IS the callback), and a
sluggish consumer just sees data pile up until it catches up or the ring
drops the oldest block — never a functional failure of the device stream.
On the **playback** side, the device pulls from the callback with a hard
deadline (this callback's `numSamples`, right now); if the producer thread
that is supposed to have refilled the ring is scheduled even a few
milliseconds late — a GC-free C++ program still competes for OS scheduling
with UI repaints, the analysis thread, and the device driver's own IRQ
work — the callback has *nothing correct* to hand back and must either
underrun (a genuine dropout, the exact failure this whole architecture
exists to prevent) or emit stale/garbage samples. `SyntheticInput`'s
`wait()`-paced push loop is provably safe for the direction it fills
(*into* a bus a slower consumer drains); reversing the direction across a
hard real-time deadline reverses which side must never be late, and that
side becomes a plain thread with no priority or deadline guarantee JUCE
gives it. This is not a hypothetical: it is the standard reason production
audio engines render synth/generator voices from *inside* the callback
rather than from a feeder thread, reserving feeder threads for genuinely
disk-bound sources (sample streaming) that already accept a large
safety-margin buffer.

### C — Extend the callback's existing `if (outputChannelData != nullptr)` loop in place, generator as a raw `AudioIo` member, no routing abstraction

Minimal diff: keep one hardcoded generator member on `AudioIo`, write it to
channels 0/1 when a bool flag is set, skip building `OutputRoutingConfig` or
per-channel `RampedGain` array at all.

**Trade-offs.** Smallest patch, but it does not answer "which solver drives
this" — G20's auto solo/mute, an alignment wizard driving one loudspeaker
box at a time, and a future N-output routing matrix all need *different*
subsets of outputs active at different times, and a single bool cannot
express "channel 3 on, channel 7 off, channel 3 fading to off while channel
5 fades on" (the exact reversal case `RampedGain` is built and tested for).
It also re-litigates, per caller, logic `ChannelConfig` already solved once
on the input side — every future caller would grow its own ad hoc gating
instead of sharing one. Rejected for the same reason the input side did not
stay "the callback writes channel 0 to ring 0, hardcoded."

## What changes in the audio-callback contract

Today (`AudioIo.cpp:111-145`):
1. `ScopedNoDenormals` (must stay literally first — the guard).
2. `bus_.pushFromCallback(...)`.
3. `if (outputChannelData != nullptr) { clear every channel }`.

Proposed:
1. `ScopedNoDenormals` — **unchanged, still first**. The change adds lines
   strictly *after* it; `check_callback_shape.cmake` only inspects the first
   non-comment statement, so nothing about the guard's mechanism needs to
   change, and it keeps enforcing the same invariant against the new code
   for free.
2. `bus_.pushFromCallback(...)` — **unchanged**, still second.
3. **NEW**: a single relaxed atomic check ("is anything routed right now?")
   — on the common idle case (no generator active, e.g. between
   measurements) this is the *only* added cost versus today, matching the
   cost `ChannelConfig`/`CaptureBus` already pay for their own idle-state
   checks (`isActive()`, `role()`).
4. **NEW, only reached when something is routed**: render one block from the
   active `core` generator into the preallocated scratch buffer (bounded,
   deterministic-cost calls — `std::sin`/PRNG draws/IIR taps, the same class
   of operation `pushFromCallback` and the analysis-thread hop processing
   already perform elsewhere in this codebase; no allocation, because the
   scratch buffer is sized once outside the callback exactly like
   `CaptureBus`'s rings).
5. **CHANGED**: the output loop becomes per-channel: `Routed` → scratch
   through that channel's `RampedGain::apply()`; else → `clear()` (today's
   only behaviour, preserved verbatim for every non-routed channel).

RT-safety is preserved because every new piece is a member of the same
family of primitive already proven safe in this exact function: relaxed
atomic loads (`ChannelConfig`'s pattern), a fixed-size buffer allocated once
outside the callback (`CaptureBus`'s pattern), and a per-sample closed-form
math state machine with no branch that can block (`RampedGain`, already
written and tested). No new lock, no new heap call, no new file I/O, and
critically **no FFT** — every one of the five generator classes is a
span-in/span-out sample-domain function by the generator decision record's
own design (`docs/dsp/2026-08-27-generator.md` explicitly rejected the one
generator idea — FFT-shaped pink noise — that would have violated this).

## The smallest interface

```cpp
// platform/types — new, mirrors ChannelConfig.h's shape exactly.
enum class OutputRole { None, Routed };
class OutputRoutingConfig {
    bool setRole(int outputChannel, OutputRole) noexcept;     // message thread
    OutputRole role(int outputChannel) const noexcept;         // any thread
};

// platform — one call, used identically by a human toggle,
// CaptureSequencer::onStep, and G20 auto solo/mute.
void AudioIo::routeOutput(int outputChannel, bool active) noexcept {
    routing_.setRole(outputChannel, active ? OutputRole::Routed : OutputRole::None);
    ramps_[outputChannel].requestOn/*or requestOff*/();
}
```

`core` computes a signal; `platform` decides which physical outputs receive
it and makes the transition click-free; `app` decides *when* and *which*
channel, whether that decision comes from a human clicking solo, a
sequencer step, or a future solver — all through this one function. Nothing
above needs to know which caller invoked it, which is the actual answer to
"how do auto solo/mute (G20) and solver playback share one mechanism": they
share it by being two different call sites for the identical
`routeOutput`/`RampedGain` pair, exactly as `CaptureSequencer::onStep`
already documents itself as "whatever reacts to it" without knowing what
that will be (`CaptureSequencer.h:29-31`).

## Open questions for a human or a follow-up measurement

1. **Strict solo interlock vs. independent gates** (disagreement 3 above):
   does the owner want G20 "auto solo" to force every other output off, or
   can several be active at once (Smaart's apparent multi-output model)?
   This is a policy decision for `app/`, not a platform primitive question,
   but it decides whether `app/` needs a "the other N are now off" loop.
2. **Output channel ceiling.** `kRequestedOutputChannels = 2` today
   (`AudioIo_Devices.cpp:23`); raising it to something like `kMaxChannels`
   (mirroring the input side) is a real device-negotiation question — some
   interfaces may refuse to open at a high output-channel request the way
   none observed here refuse a high input request. Needs a check against a
   real multi-output interface, not just a code change.
3. **One active generator vs. N simultaneous independent sources.** This
   research assumes one logical generator (optionally decorrelated per
   output channel via the already-designed `ChannelSeeds::forChannel` split)
   feeding any number of routed outputs. If a solver ever needs two
   *different* signals live on two different outputs at once (unclear from
   any surveyed tool), the "one active generator" assumption in Architecture
   A needs revisiting before that solver is built.
4. **SysTune's and Smaart's actual solo/routing mechanics** could not be
   read past search-summary depth in this pass (PDF text extraction failed
   for SysTune; the Smaart article fetched does not cover it). If routing
   semantics become contentious, a direct read of the Smaart v8 User Guide's
   routing chapter and the SysTune manual's generator chapter (both PDFs
   already located, listed in the search results above) by a human or a
   PDF-capable follow-up pass would close disagreement 3 with more
   confidence than search-engine summaries allow.
5. **Preset persistence.** L6b's schema-3 `[tf]`/`[average]` sections do not
   cover output routing; whether output routing belongs in the same session
   schema, and under what "loads unbound rather than silently rebinding"
   rule (L6b §9's own device-name-plus-channel-count precedent), is a
   question for whichever lane implements this, not blocking here.
