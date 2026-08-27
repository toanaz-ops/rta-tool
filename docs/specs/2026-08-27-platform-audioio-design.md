# platform/AudioIo — design decision

*2026-08-27. Station-2 analysis of the AudioIo research pass. Decision record;
the implementation plan derived from it lives in docs/plans/.*

## Decision: JUCE-native callback + per-channel SPSC rings + one-struct snapshot

`AudioIo` (in `platform/`, NOT `app/` — the design doc's `app/` placement is
superseded by CLAUDE.md's module-boundaries section) implements
`juce::AudioIODeviceCallback` directly and owns one `AudioDeviceManager`,
the shape proven in production by handsfree's `AudioEngine`.

- **Channel roles**: `ChannelRole { Measurement, Reference, Unused }` per device
  input channel, snapshotted by relaxed atomics at the top of each callback and
  bounds-checked against the channel count the callback actually received —
  never against what the device advertised.
- **Handoff**: one `rta::dsp::RingBuffer<float>` per enabled channel. The
  callback does nothing but `ScopedNoDenormals`, role snapshot, and ring writes,
  counting — not swallowing — short writes. A dropped block spliced silently
  becomes broadband energy indistinguishable from a real transient.
- **Snapshot publishing**: the analysis thread publishes ONE struct (spectrum +
  band powers + sample rate + sequence + drop counts) via
  `std::atomic<std::shared_ptr<const Snapshot>>`. This reconciles CLAUDE.md's
  "atomic pointer swap" with handsfree's mutex-and-copy rationale (two related
  fields must be consistent at one instant): swap the whole struct and both
  goals hold. MSVC's atomic<shared_ptr> uses an internal spinlock — acceptable
  here because neither side of THIS boundary is the audio callback; the
  hard-real-time boundary is callback→ring, which stays wait-free.

## Rules imported from production evidence (each cost someone a debugging day)

| Rule | Source |
|---|---|
| `ScopedNoDenormals` is the FIRST statement in the callback | handsfree: subnormal limit cycle measured at ~80x CPU (77 ms/s vs 0.9 ms/s) |
| `audioDeviceAboutToStart` retargets rate-dependent state AND drains every ring | stale samples from the previous device splice onto the new session and mislabel every bin-to-Hz conversion |
| Read the device type BACK from the manager; never echo the requested string | JUCE silently keeps the current type if "ASIO" isn't registered |
| `setAudioDeviceSetup` returns EMPTY string on success | inverse of the usual convention |
| The callback defensively checks a validity atomic | on some platform/JUCE combos callbacks keep firing after `closeAudioDevice()` |
| Buffer-size/rate changes need a reload path outside the callback | historical ASIO freeze: reset logic lived inside a callback that had stopped firing |
| No auto-reconnect in the I/O layer; report the fault, let the app own retry | handsfree ships this split |

## Corrections to earlier documents

- **Open Sound Meter DOES support ASIO** (`src/audio/plugins/asioplugin.*`,
  behind `USE_ASIO`, optional because it needed the proprietary SDK). Our README
  and spec claimed it has none; corrected to "not in default builds". The
  honest remaining gap is multichannel role routing + provable DSP, not ASIO's
  existence.
- OSM's mutex-based audio→math handoff is NOT evidence that locks are fine
  here: its WASAPI/CoreAudio capture runs on ordinary QThreads, only its ASIO
  path is time-critical. Our reference driver IS the hard-real-time case.

## Rejected

- **Backend abstraction beneath JUCE** (OSM's Plugin shape): portability
  insurance this project hasn't asked for; JUCE 9.0.1 is the committed device
  layer, and the abstraction wouldn't buy the safety property that matters.
- **Actor/message-queue device owner**: solves a concurrency hazard that a
  single message thread already serializes away, at the price of the synchronous
  "did it take?" feedback a device-config UI needs most, and it doubles the
  state machines that can race during device loss mid-show.
