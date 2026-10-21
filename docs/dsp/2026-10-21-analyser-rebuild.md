# Analyser rebuild and thread exit: one slot per tick, and no force-kill

*Decision record. Written 2026-10-21 on `app/analyser-rebuild-and-exit`, after the
code it describes (CLAUDE.md "research, argue, build", rule 6). Source: audit
`prj010-fable-audit-20261021`, finding "APPLY / device change / SYNTHETIC toggle
rebuilds all 8 Analysers synchronously on the analysis thread" (written at
`0a8f236`, re-read on `63bd500`, where PR #78 had already moved the thread loop
into `AnalysisThreadLoop.cpp` and added the `CaptureBus` resync gate).*

## 1. The finding, re-read on the current tree

An epoch change (device change, SYNTHETIC toggle) or an Apply of a reference delay
ran `analyser = std::make_unique<Analyser>(cfg)` for all `kMaxTransferFunctions`
(8) slots inside one analysis-thread tick. Nothing drained, fed the SPL meters,
published, or checked `threadShouldExit()` until the last slot was built. The
audit's claim: at 96/192 kHz that stall approaches the ring's capacity, so an
Apply produces counted drops.

## 2. Measurement (this machine, MSVC Release, idle, `Analyser::Config` defaults)

`app/tests/test_analyser_rebuild_timing.cpp`, hidden (`[.][bench]`), 5 repetitions
per rate, each repetition replacing a built set (the thread's case: build, then
the old one is freed):

| rate | full set (8), median [min..max] | one slot, median | ring headroom `65536 / fs` |
|---|---|---|---|
| 48 kHz  | 106.9 ms [97.7 .. 108.3] | 14.0 ms | 1365 ms |
| 96 kHz  | 107.8 ms [105.1 .. 110.6] | 14.4 ms | 683 ms |
| 192 kHz | 112.2 ms [108.2 .. 120.7] | 14.9 ms | 341 ms |

**The audit overstated the margin.** On an idle machine the full rebuild is ~110 ms
against 341 ms of ring at 192 kHz: 3.0x headroom, not "approaches capacity". An
Apply on a quiet machine does not drop. Two things keep it a real defect anyway:

* the margin is 3x, and the 424 MB zero-fill is exactly the work a loaded machine
  (a CI runner, another session at 100% CPU, a swapping laptop) multiplies;
* the same stall is also a 110 ms response delay to `threadShouldExit()`,
  the drain, the SPL feed and the publish -- section 6 needs that bound.

The cost is not the allocation count (one `make_unique` is ~14 ms, 8 of them are
8x that) but that all of it sits in one uninterruptible tick.

## 3. Candidate approaches

**A. Build the replacement set off the analysis thread, hand over by pointer swap**
(the audit's first suggestion, and the obvious one).
Pro: the analysis thread never builds. Con, and the reason it is not chosen:
1. The allocation and zero-fill do not disappear, they move -- onto a second thread
   that competes with the first for memory bandwidth and the allocator lock.
2. A **new thread** is a new exit path (section 6), a new place for `bad_alloc` to
   be caught and forwarded across threads, and a new object whose lifetime must
   outlive nothing and be joined before `bus_`.
3. It still needs the one thing the incremental design needs: while the
   replacement is building, the old Analysers are stale and MUST NOT receive
   post-epoch samples, so the drain needs a per-slot "may I feed this?" gate and
   the publish needs a "may I read this?" gate. The off-thread build adds a
   thread on top of the gate it cannot avoid.

**B. Rebuild only the analysers with a live route.** Cuts a typical one-route
session from 8 slots to 1. Rejected as the fix: `analysers_[i]` for an idle `i`
must still be a correct engine the moment a role assignment makes route `i` live
without an epoch change, so "skip it" needs a lazy-construct-on-first-use path
and null handling in `buildPublishedSnapshot`, `publishAverageGroup`,
`drainPaired`... It changes the memory policy (424 MB resident today) and the
publish contract, for a saving the chosen design also gets for the live slots
(they rebuild first, section 4) without those changes.

**C. `Analyser::reconfigure`, reusing allocations when sizes are unchanged**
(e.g. a delay-only Apply). Rejected, on reading the engines: the delay is
**construction-only** in `DualFftEngine` and `MtwEngine` ("Implementation note 2",
`DualFftEngine.cpp`), and `bufferCapacity(fftSize, skipMagnitude)` folds the delay
into the ring size (up to 2^20 samples), so reusing the allocation for a *different*
delay is not reuse -- the ring must be resized or sized for the worst case
(8x larger for every slot, always). A device change at a different rate cannot
reuse either: `alpha_`, the PSD scale and the band weights are rate-dependent
constructor products. C is a core change in two engines with a memory cost, for
the one case (delay-only) that is already the cheap one.

**D (chosen). Rebuild one slot per tick on the analysis thread, ascending, with
each slot stale until its replacement exists.**
`AnalyserRebuild` (`app/src/measure/AnalyserRebuild.h`): `begin(cfg)` makes every
slot stale; `step()` replaces the next one. Argued against:
* *"It leaves a window of mixed configurations."* True, and that is the cost
  (section 5 states the rule that makes the window safe).
* *"It makes an Apply slower end to end."* True: 8 ticks x (10 ms poll + 14 ms
  build) ~ 200 ms instead of ~110 ms. Nothing waits on that number except
  `appliedReferenceDelaySamples()`, which now stores when the LAST slot lands --
  the same moment it used to mean (the coherence gate re-fill that follows,
  ~16 frames, is hundreds of ms anyway, record `2026-08-28-dual-fft.md`).
* *"A single failed slot used to kill the thread."* That is changed on purpose,
  section 5.

## 4. What shipped

* `AnalyserRebuild.h` -- the state machine, JUCE-free, unit-tested with no thread
  (`app/tests/test_analyser_rebuild.cpp`).
* `AnalysisThreadLoop.cpp` -- `startRebuild()` (called by the epoch observer and by
  Apply, in the same tick, before any drain) and `stepRebuild()` (one slot per tick,
  after the resync service and before the drain).
* `AnalysisThread.cpp` -- the drain refuses to feed a stale slot; the publish refuses
  to read one.
* Slots rebuild in ascending index. A route's slot is its position in the routing
  plan (dense from 0), so **live routes rebuild first**; idle slots follow and are
  never fed until they land.

## 5. The invariants, and what each one buys

1. **A stale slot is never fed.** `begin()` runs in the tick that observes the epoch
   (or the Apply), before that tick's `drain()`. From then on `drainPaired` /
   `drainRole` still `peek`/`discard` the hop (the ring must keep draining, and the
   SPL tap already ran) but push it only when `rebuild_.isFresh(slot)`. PR #78's
   ordering is untouched: `serviceResyncAsConsumer()` still runs first and `drain()`
   still sits behind `!bus_.resyncPending()`. The consequence of the new window: the
   first hops after a device change are consumed without being analysed, instead of
   being fed to Analysers built later in the same tick -- the same coherence-gate
   re-fill, a few ticks earlier in the sweep.
2. **A stale slot is never published.** `publishIfDue` returns until
   `freshCount() >= max(1, routes)` -- a snapshot reads slot 0 and one slot per
   route, and the position average checks the bin grid, so a snapshot combining
   two configurations is wrong, not merely old. `latest()` keeps the last good
   snapshot meanwhile, which is what the stalled thread showed before.
3. **A failed build never leaves a null slot and never marks a slot fresh.**
   The replacement is built into a temporary first. `std::bad_alloc` (the only
   realistic failure of a ~53 MB build) is caught in `stepRebuild`, recorded ONCE
   per sweep through `recordFault` (the slot the device panel already reads;
   `selectFault` prefixes it "ANALYSIS STOPPED:"), and retried on the next tick. It
   used to fall through to `run()`'s catch, which also records a fault but ends the
   thread and measurement with it, even for a transient shortage.
4. **Per-iteration work is bounded** (section 6): one slot, or one hop.

## 6. Exit path

`SyntheticInput::~SyntheticInput` and `AnalysisThread::~AnalysisThread` called
`stopThread(2000)`. `juce::Thread::stopThread` (`juce_Thread.cpp:248-276`), once
its timeout passes, logs "killing thread by force" and `TerminateThread()`s. A
thread killed there can hold the CRT heap lock or the loader lock; the very next
thing a destructor chain does (free, `exit()`) takes them. That is the leading
suspect for the intermittent test-process exit hang under load. Not proven here --
the hang did not reproduce on an idle machine in the runs below -- but the kill is
removed rather than tuned, because a 2 s deadline is exactly what a loaded runner
exceeds.

**Both loops are bounded per iteration and check the stop flag between iterations:**

| loop | one iteration | stop check | wake |
|---|---|---|---|
| `AnalysisThread::runBody` | `stepRebuild` (<= 1 slot, ~14 ms) or `drain` (<= 1 hop per ring group) + `publishIfDue` | `while (!threadShouldExit())`, again after `wait`, and now **per hop** in `drainPaired` and `drainRole` | `wait(10)` is woken by `stopThread`'s `notify()` |
| `SyntheticInput::runBody` | one `bufferSize` block render + `pushFromCallback` | `while (!threadShouldExit())` | `wait(waitMs_)` woken by `notify()` |

Two bounds were missing and are fixed: `drainRole` looped `while (peek == hop)`,
which a producer that keeps pace with the consumer never ends, and `drainPaired`'s
hop loop had no stop check (up to `ring capacity / hop` = 64 hops x N routes per
tick). `drainRole` is now bounded by `availableToRead() / hop` sampled once;
both check `threadShouldExit()` at the top of a whole hop, never between a peek
and its discard.

**Decision: signal, bounded wait with a logged overrun, then an unbounded join --
never a kill.** `stopThreadNoKill` (`ThreadStop.h`): `signalThreadShouldExit()`,
`notify()`, `waitForThreadToExit(2000)`; on overrun it logs
"has not exited in 2000 ms; joining without a force-kill" and waits without a
limit. The destructors cannot do anything safe with a thread still running (the
object is about to be destroyed under it), so the only choices are to wait or to
kill, and the kill is the one with the lock hazard. The unbounded wait is
justified by the bound above (a join is as long as one iteration), and its failure
mode is visible (a log line, a debugger on the joiner) where a killed thread's
lock hides it.

**Other threads in `app/` and `platform/`** (checked; all join, none force-kill, no
`stopThread` anywhere else): `Rt60Worker` (stop flag + cancel flag at band
boundaries, then `join`), `SweepIrWorker` (`join`; the deconvolution has **no
cancel flag**, so exit waits for the sweep to finish -- bounded by the analysis,
not by a signal, and not a kill), `AlignRunner` (no thread of its own; it uses the
two workers), `ApiServer` (`svr.stop()` then `join`), `SplLogPipeline` (stop flag,
drain, `join`). The only `TerminateThread` path was the two `stopThread(2000)`.

## 7. Not verified here

* The exit hang itself was not reproduced; the causal claim is the audit's and
  JUCE's own comment's ("very bad karma"), not measured. What is measured is that
  the destructors no longer have a kill path and finish well inside the bound.
* The per-hop stop check in `drainPaired`/`drainRole` has no mutant that a test
  catches: one hop at MTW defaults is ~1 ms, indistinguishable from scheduling
  noise at the test's resolution. It is argued from the code, not proven.
* `SweepIrWorker` exit latency is the length of one sweep deconvolution; not
  bounded by a signal.

## 8. Measured effect (APPLY at 192 kHz, SYNTHETIC running, MTW defaults, idle machine)

`test_analyser_rebuild_live.cpp`, second case: ring high-water (the larger of the
two channel rings' `availableToRead()`, busy-polled from the test thread from the
APPLY until `appliedReferenceDelaySamples()` moves), drops, against a bound of
`192000 * (3 * slot build + 30 ms)` samples with the slot build measured in the
same run (~11 ms, bound ~12 000 samples):

| build | ring high-water | drops |
|---|---|---|
| before (mutant M1: all slots in one tick, the old behaviour) | 19 968 samples (104 ms of audio, 30% of the 65 536 capacity) | 0 |
| after (one slot per tick) | 4 608 - 5 120 samples (24 - 27 ms) over 3 runs | 0 |

Both are zero-drop on an idle machine, as section 2 predicted: the change does
not fix a drop that was occurring, it removes the dependence of the drop on the
machine being idle (3.0x headroom before, ~13x after) and bounds the stall that
exit latency and the publish also wait on. The test fails on M1 at
`test_analyser_rebuild_live.cpp:163`.

## 9. Start-up: the first epoch check no longer rebuilds an identical set

`AnalysisThread`'s constructor builds the set from `config`; the first tick used
to rebuild it again because `lastEpoch_` started at 0 while the bus was already
at epoch >= 1. When the bus rate equals `config.sampleRate` the constructor now
adopts the bus epoch, so that second (identical) build, and the window in which
the first hops would be consumed unanalysed, no longer happen. Found because two
existing `test_routing_live.cpp` cases, which push hops right after construction,
went red under the sweep and green again with this.
