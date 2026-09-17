# The remote API, read-only (lane L-API)

*2026-09-16. Station-2 decision record. Station-1 research:
`docs/research/2026-09-16-remote-api-station1-research.md`. Repo at `6d9a53d`.*

> **AMENDED 2026-09-17 — read [§15](#15-amendment-2026-09-17--the-reconciliations-station-3-filed) before acting on any section below.**
> Station 3 (`docs/plans/2026-09-17-remote-api-impl-plan.md`) and the adversarial
> verify of it on PR #14 — two rounds — filed **eighteen** corrections against
> this record (`R1`..`R17`, plus `R16a`).
> The wrong sentences are left in place on purpose — a record whose errors are
> erased teaches the next session nothing — and each one now carries an inline
> pointer. The five that most change what a builder does: **§2**'s `split.py`
> (R2), **§8**'s port `4737` (R14), **§10**'s two files and its ON-only server
> (R1, R15), **§11** item 11's "rise by two" (R6), and **§14 q.3**'s "six
> endpoints" (R12). A sixth is new work this record did not contemplate at all:
> nothing in the CI configuration asserted that the emitted document is
> well-formed JSON (R16).

## 0. What this lane is for, and the owner's standing ruling

`docs/dsp/2026-09-06-multichannel-l6b.md` §10 deferred the remote API out of
L6b with a named list of what it could not decide — transport, bind default,
and whether it may write. Two of those the owner then settled the same day
(`docs/HUMAN-QA-QUEUE.md`, "Từ lane L6b"):

- **Read-only first.** The API exposes `SnapshotSource` — measurements and
  traces — and cannot change routing or config. Write access to routing is
  `docs/UPGRADE-BACKLOG.md`, because a remote write to routing during a live
  show is near-irreversible and needs authentication first.
- **Localhost by default**, REW's model. LAN bind plus a password is itself an
  opt-in later feature.

This record decides the rest, and it does not reopen those two.

What L6b §10 already fixed and this record keeps unchanged: the API lives in a
new sibling of `platform/`, never in `core/` (the framework guard names `asio`)
and never in `ui/`; it consumes `SnapshotSource`, not `Analyser`; it starts
read-only; and it binds localhost by default.

**One thing L6b §10 said that this record overrides.** L6b proposed the API
"reuse the `key=value` line convention before any JSON dependency is added".
That was a reasonable instinct — it is what `SessionCodec` does, and it avoids
a dependency. It is wrong here, for a reason L6b could not have known without
this pass: **every surveyed remote API in this product category speaks JSON**
(REW, Smaart, OSM, and Q-SYS's JSON-RPC), and a `key=value` line format has no
representation for the thing this API is mostly made of — a nested array of
per-bin floats with a named band table beside it. `key=value` is the right
format for a session index a human diffs; it is the wrong format for a wire
protocol whose clients are a browser, a Lua script, and `curl`.

## 1. Decision: HTTP/1.1 + JSON over TCP; OSC is a later, scalar-only surface

**Decision.** v1 serves **HTTP/1.1 with JSON bodies over TCP**. OSC over UDP is
**not** built in v1, and is recorded as a candidate *secondary* surface for
**scalars only** if the demand appears.

**Why not OSC, given that this is pro audio and OSC is the pro-audio lingua
franca.** Arithmetic, not taste. One non-fragmented Ethernet UDP datagram
carries 1500 − 20 (IPv4) − 8 (UDP) = **1472 bytes** — 368 float32 values and
nothing else, no address string, no type tags. This project's default curve is
`fftSize/2+1` = **2049** points, i.e. **8196 bytes** for one field of one
trace. RFC 8085 §3.2 says an application "SHOULD NOT send UDP datagrams that
result in IP packets that exceed" the path MTU, and where PMTU is unknown to
fall back below EMTU_S — 576 bytes for IPv4. The failure mode is what decides
it: a fragmented datagram reassembles **all-or-nothing**, so losing one of six
fragments discards the entire 20 Hz frame, with no retransmit and no way for
the receiver to know. On a shared show network the effective loss rate is worse
than the link's.

**And the two obvious rescues do not work.** OSC's blob type `b` is the correct
*encoding* for a float array — far better than 2000 `f` arguments — but an
encoding is not a transport. Bundles do not segment: OSC 1.0 says "the contents
of an OSC packet must be either an *OSC Message* or an *OSC Bundle*", so a
bundle lives inside one packet, and five 400-float messages in a bundle is one
8000-byte datagram that fragments identically. OSC has no sequence number, no
fragment index and no reassembly layer; once the receiver handles out-of-order
and missing chunks it has rebuilt a worse TCP.

A third, independent reason: **OSC-over-TCP has two incompatible framings in
the wild** — OSC 1.0's int32 size preamble, and OSC 1.1's required SLIP
(RFC 1055) double-END encoding. Choosing OSC as the primary transport means
choosing which half of the installed base to be incompatible with.

**What OSC would still be good for, later**: broadband SPL, Leq, a coherence
figure, a delay result, a handful of band levels — scalars that fit one
datagram at 20 Hz with room to spare, for an audience (QLab, TouchOSC,
Behringer/Midas, GALAXY-literate integrators) that genuinely exists. `juce_osc`
is already available, AGPLv3/commercial like every JUCE 9 module, so adding it
later costs no new licence question. §13 records it as not decided here.

## 2. Decision: the library is cpp-httplib, MIT, with TLS left undefined

*Amended by §15 **R2** (the amalgamated header, not `split.py`), **R3** (it lives at `external/`), **R7** (`/W4` and a vendored header) and **R16** (a second vendored header, nlohmann/json, test-only).*

**Decision.** Vendor **cpp-httplib** (yhirose/cpp-httplib, **MIT**), run
through its own `split.py` so the header cost is paid in exactly one
translation unit, and **never define `CPPHTTPLIB_OPENSSL_SUPPORT`** in v1.

**The licence line, which is the part that must not be got wrong.** This
project is **AGPL-3.0-or-later**. MIT is, in the FSF's words, "a lax,
permissive non-copyleft free software license, compatible with the GNU GPL",
and it flows into an AGPLv3 work imposing only notice retention. cpp-httplib's
`LICENSE` is the MIT text. Leaving the OpenSSL macro undefined means **no
third-party link dependency at all** and no second licence to reason about.

**`Mongoose` is disqualified, and this is the trap worth naming.** Its
`LICENSE` offers "the GNU General Public License version 2" with **no "or any
later version" clause**, or a paid commercial licence. GPL-2.0-only is
incompatible with GPLv3 and therefore with AGPLv3, and the usual escape — the
"or later" option — is not granted. Buying the commercial licence does not fix
it either: the commercial arm conflicts with the AGPL source release this
project is committed to. Neither arm works. (civetweb exists precisely because
of this: it is the MIT fork taken from the last MIT Mongoose in August 2013.)

**The case against cpp-httplib, and why it loses.** The real charge is compile
cost, and it was measured on this project's own toolchain rather than quoted:
MSVC 19.51.36248, `/std:c++20 /EHsc /O2 /c`, wall clock — empty TU 0.13 s, a TU
including only `<regex>`/`<thread>`/`<iostream>` 0.96 s, a TU including
`httplib.h` and instantiating `httplib::Server` **9.55 s cold** (5.97-6.83 s
warm), preprocessed output 210,318 lines / 9.5 MB. Single runs, so treat the
seconds as ±20%; the durable finding is the **6-7× ratio**. `split.py` confines
it to one TU, and this project's own file-length culture already says one file,
one job. The thread-model charge half-lands and is configured away (§4). The
TLS charge does not land in v1. Binary size was **not measured** and is not
asserted.

**The alternative was hand-rolling on `juce::StreamingSocket`, and it was
costed.** JUCE 9.0.1 ships **no HTTP server** — a grep across all 24 modules of
the pinned checkout for a `class` or `struct` named `*Server*` returns three
hits and not one of them speaks HTTP: `SVGPaintServer`,
`InterprocessConnectionServer`, and `HubPipeServer`
(`juce_graphics/native/juce_Direct2DMetrics_windows.h:264`, a Direct2D
debug-metrics named pipe). The second speaks JUCE's own length-prefixed framing
that no browser, `curl` or Lua script can talk to; the third is not a network
server at all. Hand-rolling means an
accept thread, read buffering with a header cap, request-line and header
parsing, `Host` validation, response framing, keep-alive state and its timer,
read and write timeouts, the 400/404/405/413/429/500 paths, and a shutdown that
does not leave `waitForNextConnection()` blocked. Several hundred lines against
a 400-line hard cap, and every bug in them is a security bug. Global rule 10
("reuse existing code … prefer the standard library over a new dependency")
points **at** the MIT header here: the smallest change that satisfies the task
is the one that does not reimplement HTTP.

**civetweb is the named fallback** if the compile cost proves intolerable in
practice: also MIT for the default build, a compiled C library so no per-TU
tax, `listening_ports = "127.0.0.1:<port>"` for a loopback bind. It needs four
CMake defaults changed — `CIVETWEB_ENABLE_SSL` OFF (it is **ON** by default),
`BUILD_TESTING` OFF, `ENABLE_SERVER_EXECUTABLE` OFF, and `SERVE_NO_FILES` /
`DISABLE_CGI` **ON**, because **CGI is on by default** in an embedded server and
a JSON-only API has no use for that attack surface.

## 3. Decision: polling with a version token in v1; no push, and never REW's push

**Decision.** v1 is **poll-only**. Every 200 carries the snapshot's `sequence`
in the body **and sets `ETag: "<sequence>"` on the response**. The `ETag` is not
decoration: HTTP conditional requests are driven by a validator the *server*
issued, so without it a client has nothing legitimate to echo and only the
`?since=` form would actually work. A client that wants change detection then
sends `If-None-Match: "<sequence>"` (or `?since=<sequence>`, which needs no
validator) and receives **304 Not Modified** with no body when nothing has
advanced; the 304 repeats the same `ETag`. There is **no** WebSocket, no SSE
and no webhook in v1.

**Why polling is enough here.** The publish rate is already bounded at 20 Hz
(`AnalysisThread::publishIfDue`, `kMinPublishIntervalMs`), and the app's own
views poll `latest()` at exactly that rate (`RtaView.cpp:26`,
`TransferView.cpp:34`). A client polling the same slot at the same rate learns
everything the UI learns, one snapshot late at worst. Push would buy latency
this data does not have, at the cost of a connection lifecycle to get right.

**Why not REW's subscription model, specifically.** REW's push is an outbound
POST to a **caller-supplied URL**: the client posts `{"url": "http://…"}` and
REW then POSTs updates to that address. For an API whose entire premise is
"read-only", that is not a read-only primitive — it turns the analyser into an
HTTP client aimed at an address an untrusted caller chose, which is the textbook
SSRF pivot. **Anything push-shaped in this project keeps the connection
inbound.** If push is ever built, it is a WebSocket or SSE on the same port,
never a callback URL.

**Why a version token is not optional.** Smaart's own documentation records the
bug this prevents: its clients "cannot detect setting changes made by the host
or by other clients". This project already has the fix and already says why —
`Snapshot::sequence` "increments by exactly one on every `Analyser::publish`"
and is "the only field a reader needs to notice 'there is something new'
without comparing pointers" (`Snapshot.h`). Publishing it on the wire costs one
integer. REW ships a nanosecond timestamp plus a running sample count for the
same purpose, and Q-SYS's `ChangeGroup.Poll` returns only what changed since
the last poll — three independent sources converging on the same shape.

**The client declares its own budget.** `?rate=` is not a knob this server
enforces by throttling upward; the *client* decides how often to ask, and the
server enforces a ceiling (§8). Smaart is the prior art and publishes its
numbers: Spec/TF Stream FPS "is set to the maximum allowable value (23 FPS) by
default", Command Timeout 2000 ms. GALAXY's equivalent is a subscription rate
argument with a 30 ms default and a 0-100 ms range.

## 4. Decision: one API thread, reading the same published pointer the UI reads

*Amended by §15 **R15**: the thread is a `std::thread`, not a `juce::Thread`, so the server compiles and is tested in the `RTA_BUILD_APP=OFF` configuration CI runs. The ownership and ordering contract in this section is unchanged.*

**Decision.** The server owns **one** thread, created and joined by the
composition root. It calls `SnapshotSource::latest()`, takes a
`shared_ptr<const Snapshot>` copy, and serialises **from that copy**. It never
holds the pointer across a request. Serialisation happens on the API thread,
never on the analysis thread and never on the message thread.

**The one rule about the audio callback.** The callback is
`juce::ScopedNoDenormals` followed by exactly two calls —
`bus_.pushFromCallback(...)` and `output_.render(...)`
(`platform/src/AudioIo.cpp:116-148`) — and
`platform/tests/check_callback_shape.cmake` greps that function. **Nothing in
this lane adds a third call, a socket, an allocation, a lock, or a branch to
that function.** The API cannot reach it: the server lives in `app/`, and
`platform/` cannot see `app/`.

**Why reading `latest()` is safe, stated honestly.** `AtomicSharedPtr` is the
publish primitive, and it is **not lock-free on this project's own toolchain**:
its class comment records the measurement — on MSVC 14.51 `is_lock_free()`
returns false on the C++20 path (the specialisation spins on a bit in the
control block), and Apple libc++'s fallback takes a lock out of a small
address-keyed table. The design's safety has never come from lock-freedom; it
comes from **who calls it**. Today that is the analysis thread (writer) and the
message thread (reader). This record adds a **third** participant, and the
accounting is:

- The API thread does exactly **one** `load()` per request. **The rate that
  governs this accounting is §8's shipped default
  `api.maxRequestsPerSecond = 30`, not the 20 Hz publish rate** — a conforming
  client may poll faster than the data changes, and §6 exposes ten endpoints,
  so the ceiling the argument has to survive is 30 loads per second and not 20.
  Above it the limiter answers 429 *before* the load, so 30 is a hard bound on
  this thread's traffic rather than a typical figure. Two views at 20 Hz plus
  the API at 30 is three readers against one writer at 20 Hz — still the same
  order of traffic the slot already carries, which is why 30 was chosen: above
  the publish rate with headroom, far below anything that could starve the
  message thread. Stated at the shipped default rather than at the convenient
  one, per `memory/a-default-must-be-run-through-the-gate-it-feeds.md`.
- The contended party is the **message thread and the analysis thread**, not
  the audio callback, which never touches the slot. A message-thread stall is a
  late repaint; an analysis-thread stall is a late publish. Neither is a
  dropout.
- On Apple's lock-based fallback the cost is a short mutex on a small table,
  taken for the duration of a pointer copy. The API thread's *expensive* work —
  serialising thousands of floats into JSON — happens entirely **after** the
  load, on its own `shared_ptr` copy, with the slot released. That ordering is
  the whole decision, and it is what makes the fallback path acceptable rather
  than merely tolerated.

**Bounded serialisation cost is a requirement, not an aspiration.** The
serialiser allocates one response buffer whose size is a function of the
snapshot and the request's clamped parameters, and nothing in it is unbounded
by a value the caller supplied (§8). OWASP API4:2023 names the specific hazard
— "server-side validation for … the one that controls the number of records to
be returned" — and in this product it is not a web-server concern: unbounded
work commanded by a remote caller while a show is running is the failure this
whole program exists to prevent.

## 5. Decision: message-thread state is published, never read in place

**Decision.** Anything the API exposes that lives on the **message thread** —
the trace library, the session, later the solver sessions — is published to the
API through a **second `AtomicSharedPtr<const ApiSideState>`**, written by the
message thread and read by the API thread. The API thread never touches
`TraceLibrary`, `SessionDocument`, `EqSession` or `AlignmentWizard` directly.

**Why.** `TraceLibrary` is owned by `MainComponent` (`MainComponent.h:160`), is
mutable, deletes copy and move (`TraceLibrary.h:51-54`), and has a `revision()`
counter but **no atomic publish**. An API thread reading it races every
`rename`, `setVisible`, `setGroup` and `soloOnly`. The `revision()` counter is
exactly the cheap gate that makes republishing affordable: the message thread
rebuilds the published state only when `revision()` has moved.

**Consequence for v1's scope, and it is a real one.** Publishing library and
session state is *new work in `app/`* that nothing else currently needs. Live
measurement needs none of it — `Snapshot` is already published. So §6 marks the
library and session endpoints as **conditional on the owner's answer to
question 3 in §14**, and v1 ships the measurement endpoints regardless.

**Two things cannot ship in v1 at all, and the record says so rather than
pretending:**

- **Solver suggestions.** `EqSession` and `AlignmentWizard` have no caller
  anywhere in `app/` outside their own files and their tests (repo-wide grep at
  `6d9a53d`). They are built and tested; they are not wired into the
  composition root. An endpoint returning "the current EQ suggestions" would be
  reporting the state of an object nobody owns.
- **SPL and Leq.** `Snapshot` carries dBFS only; `calibrationOffsetDb` and
  `LevelUnit` exist on a *stored* `CaptureMeta` (`Trace.h:39-40`), not on the
  live snapshot; `rta::meter::Leq` has no `app/` caller. **The API can carry
  SPL only after the Meters track puts it in the snapshot**, and that sequencing
  constraint belongs to L6a (§12).

## 6. The v1 surface — GET only, versioned in the path

*Amended by §15 **R4** (`absence` is a per-bin array), **R10** (`/transfer` serves `transfer`, not `soloTransfer`), **R11** (`axis.pointCount` is measured, not derived), **R12** (eight endpoints, not six) and **R13** (`/snapshot`'s body schema, which this section never gave).*

**Decision.** Every endpoint is `GET` (with `HEAD` and `OPTIONS` answered).
Every other method returns **405**. The version is a **path segment**, not a
header and not a query parameter.

```
GET /api/v1/status
GET /api/v1/snapshot
GET /api/v1/transfer
GET /api/v1/mtw
GET /api/v1/bands
GET /api/v1/spectrum
GET /api/v1/average
GET /api/v1/positions
GET /api/v1/traces          (conditional -- see §5)
GET /api/v1/session         (conditional -- see §5)
```

**Why read-only is expressed as GET-only, and not as Smaart's verb field.**
Smaart puts the verb in the message: `{sequenceNumber, action, target,
properties}` with `action ∈ get|set|capture|issueCommand`. That makes a
read-only *gateway* trivial (allow `get`, drop the rest) and makes read-only-ness
**inexpressible to anything that is not this program** — not to a firewall, not
to a proxy, not to a browser. REW's HTTP-method split is expressible in all
three, and is weakened only because REW routes real actions through
`POST …/command`. Since v1 has no writes at all, GET-only is a boundary
something other than this code can enforce, and that is worth more than the
elegance of one verb field.

**Why the version is a path segment.** Smaart's `/api/v3/` lets a client fail
fast against an incompatible server; REW's unversioned paths cannot, and OSM —
which ships the build's `git describe` output in a field called `version` — has
no contract version at all, so a client discovers an unsupported field by its
absence. `schemaVersion` is *also* in every body (below), because a body that
travels (saved to a file, pasted into an issue) must carry its own version.

### `GET /api/v1/status`

```json
{
  "schemaVersion": 1,
  "app": "RTA Tool",
  "sequence": 12345,
  "sampleRate": 48000,
  "fftSize": 4096,
  "fraction": 3,
  "hasReference": true,
  "framesAnalysed": 98765,
  "droppedSamples": 0,
  "available": ["transfer", "mtw", "bands", "spectrum", "average", "positions"]
}
```

`available` is the capability list, and it is the honest answer to the two
absences in §5: a client asks what this build actually serves rather than
inferring it from a 404. `"spl"` appears in that list on the day the Meters
track puts SPL in the snapshot, and not a day earlier.

### `GET /api/v1/transfer`

```json
{
  "schemaVersion": 1,
  "sequence": 12345,
  "axis": { "kind": "uniform", "sampleRate": 48000, "fftSize": 4096, "pointCount": 2049 },
  "effectiveAverages": 8.5859375,
  "appliedDelaySamples": 512,
  "magnitudeDb": [ -3.2145123, -3.107789, ... ],
  "phaseDeg":    [ 12.421333, 11.901777, ... ],
  "coherence":   [ 0.9731445, 0.9642334, ... ]
}
```

*The float values in this and every following example are written at
**shortest-round-trip float32 precision**, which is what the serialiser emits
(§6, "Units on the wire"). They are deliberately not `-3.2` / `0.97`: an
implementer who copies a rounded example reproduces exactly the rounding the
wire format forbids. Up to nine significant digits is normal here and is not a
false claim of accuracy — it is the shortest decimal that reads back as the
same `float`.*

- **`coherence` is absent — the key is not present at all — when the engine's
  gate has not opened.** It is never `null`, never an array of `1.0`, never
  zeros. `TransferBlock::coherence` is an `optional` for a stated reason (a
  single frame gives coherence identically 1.0 at every frequency, so a broken
  engine looks perfect), and that reason survives the wire or it was never a
  gate. A client that sees no `coherence` key must draw no coherence.
- **`axis.kind` exists because this project has two kinds of curve.**
  `"uniform"` means bin *i* is at `i·sampleRate/fftSize` and no frequency
  vector is sent — REW's model. `MtwBlock` is the other kind and says so.
  Neither prior art generalises: OSM ships a frequency with every bin even
  though its own averaging cannot use one; REW ships parameters and makes the
  client rebuild the axis. Saying which kind is on the wire costs one string.
- **Phase is in degrees**, matching `TransferBlock::phaseDeg` and the one
  radians→degrees crossing the app already makes, and matching REW.

### `GET /api/v1/mtw`

```json
{
  "schemaVersion": 1,
  "sequence": 12345,
  "axis": { "kind": "explicit", "pointCount": 1536 },
  "frequencyHz": [ 0, 11.71875, ... ],
  "magnitudeDb": [ ... ],
  "phaseDeg":    [ ... ],
  "coherence":   [ ... ],
  "appliedDelaySamples": 512,
  "bands": [
    { "firstIndex": 0, "pointCount": 256, "fftSize": 32768,
      "windowSeconds": 0.6826667, "integrationSeconds": 5.4613333,
      "effectiveAverages": 8.5859375, "seamHz": 0, "coherenceAvailable": false }
  ]
}
```

**`coherenceAvailable` is per band and must cross the wire.** `MtwBlock`'s own
comment is explicit: an index whose band has not passed its gate holds `0.0f`
and "must not be read as a measured zero". A client that ignores this field
draws a bottom band reading zero coherence for five and a half seconds and
reports a fault that does not exist. This is the single field most likely to be
dropped by a well-meaning implementer, so the schema test in §11 asserts it.

### `GET /api/v1/bands`, `GET /api/v1/spectrum`

`bands` is an array of `{centreHz, lowerHz, upperHz, levelDb, underResolved}`;
`underResolved` travels for the same reason `coherenceAvailable` does — it is
the difference between a measurement and a band the FFT physically cannot
resolve. `spectrum` is `{axis, spectrumDb}`.

### `GET /api/v1/average`, `GET /api/v1/positions`

`average` carries `magnitudeDb`, `phaseDeg`, `phaseAgreement`,
`weightedCoherence`, `contributors` and `absence`. Two rules:

- **`phaseAgreement` and `weightedCoherence` are named on the wire exactly as
  they are named in the code, and the schema documents that neither is a
  coherence estimate.** L6b §2 and §4 exist because those two quantities are
  routinely mistaken for one; a wire format that renamed either to something
  friendlier would undo that record.
- **`absence` travels as a string enum** — `"present"`, `"noContributor"`,
  `"noWeight"` — never an integer. OSM flattens enums to their integer value
  with no name (`server.cpp:286-288`), so a renumbering between versions
  silently changes meaning with nothing on the wire to reveal it. That is a
  measured failure mode in a shipping product, not a hypothetical.
  `memory/a-placeholder-for-an-absent-result-erases-its-state.md` is about this
  exact field being rewritten from `NoWeight/2` to `NoContributor/0`; the wire
  format must not reintroduce the confusion it cost a lane to find.

`positions` is an array of `PositionSummary` with `membership` likewise a
string enum (`"member"`, `"excludedDifferentReference"`,
`"excludedOverCapacity"`), and it deliberately carries **no per-bin array**:
L6b §6 fixed that publish cost must be O(1) in N, and an API that re-expanded
it would reintroduce the churn that decision refused.

### Units on the wire

`CLAUDE.md` "Reading out numbers" is a **display** rule: whole hertz, one
decimal of dB, two decimals of coherence. The wire carries **full float32
precision**, and the schema states the display rule as a note. Rounding at the
serialiser would be a lossy transform nobody asked for, applied to numbers a
client may want to re-analyse — and it would put a display decision in the
transport layer, which is the same mistake as putting measurement vocabulary in
`az_ui`. The **viewer** rounds (§12), and the SPL web viewer will round the
same way the app does because it reads the same rule.

What the wire does **not** do is OSM's mistake of widening float32 to double
for printing (`server.cpp:386-390`, `item.cpp:169-175`): the values are float32
and are serialised at float32 precision, because
`memory/float32-fft-precision.md` already says what these numbers are worth.

**Amendment, 2026-09-17 (station 4 wave 1).** Every numeric field on this wire
is **a JSON number or `null`, and a number only when the value is finite**: a
non-finite float emits the literal `null`, never the tokens `nan` or `inf`,
which are not JSON and which a conforming parser rejects outright — taking the
whole document with them, not just the one field. §6 above specified the
precision of the numbers and never said what happens when there is no number
to print; the rule existed only in the plan's Task A row (A4) and in
`app/src/api/ApiJson.h`, which is a decision living in code with no record
behind it. Pinned by `test_api_json.cpp` A4 at the emitter and re-asserted
through a third-party parser by `test_api_schema.cpp` F3 and F7 at the
document level. A client must therefore treat any numeric field as
`number | null`.

## 7. Decision: the response body is JSON numbers in v1, not Base64 float32

**Decision.** v1 ships plain JSON number arrays. Base64-encoded float32 (REW's
model) is recorded as the **first optimisation to reach for**, behind a
`?encoding=base64` parameter, and is not built now.

**Why not now.** A 2049-point float32 array is 8196 bytes raw and roughly 10.9 KB
Base64; as JSON decimals it is roughly 20-40 KB depending on how many digits are
printed. At 20 Hz with one or two clients on loopback that difference is not the
constraint, and JSON numbers are readable in a browser's network tab, greppable
in a bug report, and parseable by a Lua script in a Q-SYS core without a Base64
decoder. **Legibility is worth more than a factor of three on loopback.** If a
LAN bind ever ships, or a client wants eight traces at 20 Hz, the parameter is
the place it goes.

**One trap to avoid if it is ever built**: REW's Base64 payload is
**big-endian** float32. Any implementation here must state its byte order in
the schema, because half the surveyed formats do not and the receiver then
guesses.

## 8. Decision: the settings, with their defaults named

*Amended by §15 **R14**: `api.port` is **4736**, not 4737 — 4737 is IANA-registered as `ipdr-sp`. And **R5**: no preferences store exists in `app/`, so none of these persists in v1. `api.allowLanBind` **does** ship, as a setting that is present and refuses (§14 q.2's default, taken in the plan), contrary to this table's "absent in v1".*

| Setting | Default | Range / values | Why |
|---|---|---|---|
| `api.enabled` | **`false`** | bool | The API does not exist until the operator turns it on. Smaart and REW both ship theirs off by default; a network listener nobody asked for is not a feature |
| `api.bindAddress` | **`127.0.0.1`** (literal, never `localhost`) | `127.0.0.1` in v1 | Owner's ruling. **Literal IP**: cpp-httplib's own README warns that resolving `localhost` on Windows with misconfigured IPv6 can cost *up to 2 seconds per request* — at a 20 Hz poll that is a broken product, not a performance note |
| `api.port` | **`4737`** (proposed — see §14 q.1) | 1024-65535 | No standard applies. Neighbours: REW 4735, Smaart 26000, OSM 49007, GALAXY 25003/25004, Q-SYS 1710/1702 |
| `api.token` | **empty = no token required** | string | On loopback with a `Host` allowlist, a token is defence in depth. It becomes **mandatory** the moment a LAN bind exists. **Bearer header or an explicit parameter — never a cookie** (§9) |
| `api.maxRequestsPerSecond` | **30** | 1-100 | Above the 20 Hz publish rate with headroom, below anything that could starve the message thread. 429 above it |
| `api.maxPointsPerResponse` | **8192** | 256-65536 | Clamps any caller-supplied count. Covers `fftSize` 16384 at full resolution |
| `api.corsOrigins` | **empty = no CORS headers** | list | Not a security control (§9). Populated only when a browser client that is not served from loopback needs it |
| `api.allowLanBind` | **absent in v1** | — | The setting does not exist. See §14 q.2 |

Timeouts, fixed rather than settable: read 2 s, write 2 s, keep-alive idle
30 s, keep-alive max count 1000. cpp-httplib's defaults are 5 s / 5 s / 5 s /
**100**, and 100 requests at 20 Hz is five seconds — a polling client would be
forced through a TCP handshake every five seconds. The thread pool is set
explicitly small (base 2, max 8) via `new_task_queue`, because a pooled worker
in cpp-httplib is occupied for the life of a **keep-alive connection**, not a
request, and the stock base of `max(8, hardware_concurrency()-1)` sizes for a
load this API does not have.

## 9. Decision: the security posture, and what it is actually defending against

*Amended by §15 **R17**: control 3's **406 and 415 are dropped** as machinery with no buyer for a GET-only API with one representation; **413 is kept and tested**, and refusing an oversized body before routing is what makes 415 unreachable.*

**Decision, in order of value:**

1. **`Host`-header allowlist, checked before routing.** Accept only
   `127.0.0.1:<port>`, `localhost:<port>` and `[::1]:<port>`; everything else
   is **403** before any handler runs.
2. **GET/HEAD/OPTIONS only**; **405** for everything else (OWASP REST Security
   Cheat Sheet: an "allowlist of permitted HTTP Methods").
3. **Every query parameter is a bounded integer or a closed enum**, validated
   for length, range, format and type; **413** over the request size limit;
   **429** over the rate limit; **406/415** on an unexpected content type.
4. **Optional Bearer token**, never a cookie (below).
5. **No CORS headers by default**, and no pretence that this is a defence.
6. **No write endpoints at all in v1** — the owner's ruling, and the reason the
   rest of this list is short.

**Why the `Host` check is first, and binding to loopback is not enough.** The
threat is **DNS rebinding**. An attacker page on the operator's laptop uses a
short-TTL domain that flips to `127.0.0.1`; the browser then treats
`http://attacker.example:<port>/…` as **same-origin**, because an origin is a
hostname and not a resolved IP. The same-origin policy does not apply, CORS is
inapplicable, and the attacker's JavaScript reads every response in full.
GitHub's security blog (3 April 2025) states the property that matters: it
"does not require a misconfiguration or bug". NCC Group's Singularity wiki —
the reference implementation of the attack — gives the mitigation verbatim: the
service "should check that all HTTP request 'Host' header values strictly
contain '127.0.0.1:3000' and/or 'localhost:3000'. If the host header contains
anything else, then the request should be denied." It is a handful of lines and
it is the highest-value control in the whole API.

**Why the token must not be a cookie — and it is not the rebinding argument.**
Rebinding is answered by the `Host` allowlist above. The cited GitHub post is
explicit that a rebound request "cannot contain cookies": a cookie jar is keyed
on the host *name*, and rebinding changes only what a name *resolves to*, so the
browser attaches `attacker.example`'s cookies and never the ones this app set
for `127.0.0.1`. Against rebinding alone a cookie would have been adequate, and
a reader who believes otherwise will under-rate how much work the `Host` check
is doing.

The reason to refuse a cookie is **ambient authority**. The browser attaches a
cookie to every request to `127.0.0.1:<port>` regardless of which page issued
it, so any site the operator visits during a show is authenticated to this
listener — textbook CSRF, needing no DNS trick at all, and the next paragraph
is why the absent CORS headers do not stop such a request from being
*executed*. A `Bearer` header is not ambient: nothing attaches it but a caller
that already knows the secret, and the attacker cannot read it. That argument
stands on its own, with no rebinding premise in it.

**Why "we set no CORS headers" is not a posture.** Per MDN, a *simple* request —
`GET` with only safelisted headers — gets **no preflight**. The browser sends
it, this program executes it, and only afterwards does the browser refuse to
hand the response to the script. `GET /api/v1/transfer` is a simple request.
Absence of CORS headers governs who may *read* a reply in the one case where
the origin does not already match; it governs nothing else. If CORS is ever
configured, OWASP's rules apply: allowlist specific origins, never `*`, never
`null`, never reflect `Origin` unvalidated, and never use `Origin` alone for
access control — a non-browser client spoofs it trivially.

**TLS on loopback: declined, and here is the cost.** NCC Group recommends TLS
"on all services including localhost". This record declines it for v1 because a
desktop app has no valid certificate name for `127.0.0.1` without shipping one,
and a self-signed certificate trains the operator to click through a browser
warning — which is its own security regression. **The cost of declining**: a
local process that can read loopback traffic can read this API's responses. That
process could already read the app's own memory, so on a single-user machine the
marginal loss is small; on a shared or managed machine it is not, and this
paragraph is what a future reader should reopen if a LAN bind ships. `?TLS` is
listed in §13 as not decided.

**Chrome's Local Network Access is a bonus, not a control.** LNA (formerly
Private Network Access) shipped in **Chrome 142, 28 October 2025**, enforced
across Chromium browsers, and covers `127.0.0.0/8` and `::1/128`: a public site
fetching loopback now raises a permission prompt. It helps, and it must not be
load-bearing — it is a **browser** control that does nothing against a native
client, a script, or any non-browser HTTP library, and it is less than a year
old.

**The rate limit and the point cap are real-time-safety controls, not
hygiene.** OWASP API4:2023 names the hazard ("server-side validation for … the
one that controls the number of records to be returned"). In this product an
uncapped `?points=` is not a memory amplifier, it is a way for a remote caller
to make this program do unbounded work while a show is running — a dropout,
which is the exact failure this whole tool exists to prevent. Both caps
therefore belong in the same category as the audio-callback rule, and §11 tests
them as such.

## 10. Boundary: what each layer owns

*Amended by §15 **R1** (three files in `app/src/api/`, not two — the validator, the limiter and the `Host` check must be testable in OFF) and **R15** (`ApiServer` is **not** `RTA_BUILD_APP=ON` only; only the composition-root wiring is).*

- **`core/`** — nothing. Untouched. The `core_has_no_framework_deps` guard
  already blocks `asio` by name, which as a side effect keeps every
  Asio-based server library (Beast, Crow, Drogon) out of `core/` and
  `platform/types/` whether or not anyone remembers this record.
- **`platform/`** — nothing. The audio path does not learn that a network
  exists.
- **`ui/`** — nothing. `az_ui` is the portable design system; a network
  listener is not a widget.
- **`app/src/api/`** (new) — split in two, and the split is what makes CI able
  to prove anything:
  - **`ApiSerialise.{h,cpp}`** — pure functions from a `const Snapshot&` (plus
    a validated, clamped request description) to a `std::string` of JSON.
    **No JUCE, no sockets, no `httplib.h`.** Two separate registrations, and
    the second does not follow from the first. It is added to the explicit
    `measure_has_no_framework_deps` glob list — a **textual scan**
    (`check_no_framework_deps.cmake:54` at main `e213202` — the sole
    `if(content MATCHES ...)` line; grep that handle, not the number —
    regex-matches each file's contents and
    compiles nothing), so membership proves the absence of a framework include
    and that alone. It is *separately* added to the `rtatool_analysis_tests`
    target in `app/tests/CMakeLists.txt`, and that is what actually compiles
    and tests it in the `RTA_BUILD_APP=OFF` configuration CI runs on three
    OSes. §11 items 1-9 need the second registration; the glob list on its own
    would not buy it.
  - **`ApiServer.{h,cpp}`** — the one translation unit that includes
    `httplib.h`, owns the thread and the socket, does the `Host` check, the
    method allowlist, the parameter validation and the rate limit, and calls
    the serialiser. `RTA_BUILD_APP=ON` only.
- **The composition root** constructs `ApiServer` with a `SnapshotSource&` and
  the settings, and joins its thread in the right order — the same
  declaration-order discipline `AnalysisThread`'s destructor comment already
  states (trap T-1).

Both new files stay under the 400-line hard cap; if `ApiServer.cpp` grows past
it, the seam is validation-versus-routing, not "split the handlers in half".

## 11. How CI proves this with no sound card, no network and no JUCE

*Amended by §15 **R3**/**R8** (item 10: where the library lives, and the doxygen false positive; the guard also needs a fifth red planted under `core/`, or nothing proves it watches the four non-`app` directories), **R6** (item 11: "rise by two" is arithmetic, not a constant), **R9**/**R15** (item 12 moves into the OFF half and runs on three OSes) and **R16** (nothing here asserted the document is well-formed JSON).*

Every claim below is testable in the **`RTA_BUILD_APP=OFF`** configuration
except where marked ON. Nothing here needs a socket bound, because the
serialiser is a pure function and the validator is a pure function.

1. **Golden JSON.** `makeSyntheticSnapshot(spec)` is documented as
   bit-identical for a given spec — the same property that makes
   `rta-view.png` reviewable as a byte-for-byte diff. Serialise it and compare
   against a committed golden file under `app/tests/golden/`. Per
   `CLAUDE.md`'s verification standard this is a **regression lock, not a
   correctness test**, and it must be labelled as one: it proves the format
   has not drifted, not that any number in it is right. The numbers are
   already proven by the tests that own them.
2. **Absent coherence is an absent key.** Serialise a snapshot whose
   `transfer->coherence` is `nullopt`; assert the string contains no
   `"coherence"` key at all. Mutation: make the serialiser emit `null` or an
   array of `1.0` — the test must go red. This is the gate crossing the wire,
   and a gate nothing enforces is not a gate.
3. **Per-band `coherenceAvailable` survives.** Serialise an MTW snapshot with a
   filling bottom band; assert `bands[0].coherenceAvailable == false` in the
   output **and** that the zeros in `coherence` are still present at those
   indices. Mutation: drop the field — red.
4. **Enums are strings.** Assert `"absence": "noWeight"` and
   `"membership": "excludedOverCapacity"` appear as strings. Mutation: emit the
   integer — red. (This is OSM's documented failure mode, not a hypothetical.)
5. **Axis kind.** Assert `/transfer` emits `"kind": "uniform"` with no
   `frequencyHz`, and `/mtw` emits `"kind": "explicit"` with one.
6. **Point cap, with no server.** The request validator is a pure function:
   `validate({points: 1'000'000})` clamps to `api.maxPointsPerResponse`, and
   the serialiser's output length is bounded by it. Assert both. Mutation:
   remove the clamp — the length assertion goes red.
7. **Rate limit against a fake clock.** The limiter is a pure object taking an
   injected `now()`; feed it 31 requests inside one simulated second and
   assert the 31st is refused with 429, then advance the fake clock and assert
   the next is admitted. No real time, no sleeps, no flakiness.
8. **`Host` allowlist, as a pure function.** `hostIsAllowed("127.0.0.1:4737")`
   true; `"localhost:4737"` true; `"[::1]:4737"` true;
   `"attacker.example:4737"` **false**; `"127.0.0.1:4737.attacker.example"`
   **false** (the substring trap — NCC Group's own wording says "strictly
   contain", and a naive `find()` passes this string); empty **false**. This
   test is the whole DNS-rebinding defence and deserves to be read that way.
9. **Method allowlist.** `methodIsAllowed` accepts `GET`/`HEAD`/`OPTIONS` and
   rejects `POST`/`PUT`/`DELETE`/`PATCH`/an empty string/a lowercase `get`.
10. **Exactly one file in the repository includes a server library.** A new
    ctest reusing the `check_no_std_atomic_shared_ptr.cmake` **DIRS + ALLOW**
    shape: `-DDIRS=core;platform;ui;tools;app`, pattern
    `httplib|civetweb|mongoose`, and an `ALLOW` naming
    `app/src/api/ApiServer.cpp` as the one file permitted to include it. That
    script's own sentinel idiom — fail if the guard has stopped watching its
    allowed file — is copied with it, because a guard that silently scans
    nothing is worse than no guard.

    **There are TWO sentinels in that script, and both get copied.** Taking
    only the first makes the copy weaker than the original it cites
    (`core/tests/check_no_std_atomic_shared_ptr.cmake`, at main `e213202`):

    - `if(NOT ALLOW IN_LIST SOURCES)` (`:65`) — the allowed file is inside the
      scanned set. Its analogue here is the `DIRS` argument below.
    - `if(NOT ALLOW_CODE MATCHES "${SENTINEL_PATTERN}")` (`:145`) — the allowed
      file **still contains** the thing it is the sole exception for. Its
      analogue here: **`ApiServer.cpp` must still contain
      `#include <httplib.h>`.** Without it, deleting the include from
      `ApiServer.cpp` leaves a guard that passes while proving nothing.

    Two mechanical notes for whoever writes the script. `SENTINEL_PATTERN` is
    `set()` inside the script at `:141`, and `PATTERN` at `:134` — neither is a
    `-D` argument, so this is a **new script** modelled on that one, not a
    re-invocation of it. And the sentinel is matched against *comment-stripped*
    source (`rta_strip_comments`, `:128-132`), which is what stops a mention in
    prose from standing in for the code.

    **Write `${CMAKE_SOURCE_DIR}/` on every `DIRS` entry and on `ALLOW`.** The
    shorthand below omits it for readability; the registration must not.
    `file(GLOB_RECURSE)` returns **absolute** paths, so a literally relative
    `ALLOW` dies either at the `if(NOT EXISTS "${ALLOW}")` check or at `:65`.
    The existing registration spells it out —
    `core/tests/CMakeLists.txt:149-154`.

    **`app` must be in `DIRS`, and that is not a detail.** The sentinel is
    `if(NOT ALLOW IN_LIST SOURCES)` → `FATAL_ERROR`
    (`core/tests/check_no_std_atomic_shared_ptr.cmake:65` at main `e213202`),
    and `SOURCES` is
    exactly what `DIRS` globbed. Omit `app` and the allowed file is never
    globbed, `ALLOW IN_LIST SOURCES` is false, and the guard `FATAL_ERROR`s on
    **every** run — the two halves of the specification would be mutually
    exclusive. With `app` in `DIRS` the guard proves two things, and both are
    load-bearing:

    - `core/`, `platform/`, `ui/` and `tools/` contain no server library at
      all — §10's layer boundary, stated as a test rather than as a habit;
    - within `app/`, `ApiServer.cpp` is the **only** file that includes one, so
      **`ApiSerialise.cpp` does not include `httplib.h`** — §10's split, and
      the half a `DIRS` without `app` could not have proved at all. That split
      is the reason items 1-9 can run in `RTA_BUILD_APP=OFF`; a guard that
      never scanned `app/` would leave it resting on nothing but intent.
11. **`ApiSerialise` is in the framework-free glob list.** Adding it to
    `measure_has_no_framework_deps`'s explicit `GLOBS` is itself the test:
    that check prints `OK (N files scanned)` and N must rise by two.
12. **`RTA_BUILD_APP=ON` only, and the only test that binds anything**: start
    the server on `127.0.0.1:0` (ephemeral), issue one `GET /api/v1/status`
    over loopback, assert 200 and a parseable body; issue one `POST`, assert
    405; issue one request with a forged `Host`, assert 403. Three requests,
    no sound card, no device.
13. **The display rule is already one place. Reuse it; do not write a second
    one.** §12 constraint 2 — "the viewer rounds identically" — is the
    load-bearing half of §6's units deviation, and until now nothing asserted
    it *against the wire values*. The rounding functions themselves already
    exist and already satisfy every property an earlier revision of this item
    asked a new `formatHz`/`formatDb`/`formatCoherence` to provide. **No new
    formatter may be added for this lane.** In `app/src/view/Readouts.h`
    (namespace `rta::view`, at main `e213202`):

    ```cpp
    [[nodiscard]] inline std::string formatHz(double hz)              // :72
        { return std::format("{} Hz", std::llround(hz)); }
    [[nodiscard]] inline std::string formatTrim(double trimDb)        // :79
        { return std::format("{:.1f} dB", trimDb); }
    [[nodiscard]] inline std::string formatAgreement(double agreement)// :87
        { return std::format("{:.2f}", agreement); }
    ```

    Grep handle if those numbers drift: `inline std::string format` in that
    file. They are pinned by `app/tests/test_readouts.cpp:100-115`
    (`formatHz(1000.4) == "1000 Hz"`, `formatTrim(-3.0) == "-3.0 dB"`,
    `formatAgreement(0.7071) == "0.71"`), `Readouts.h` is already a named
    entry in `measure_has_no_framework_deps`'s `GLOBS`
    (`app/tests/CMakeLists.txt:282`), and `rta::view::formatHz` already has a
    live caller at `app/src/view/DevicePanel.cpp:102`. So "framework-free,
    therefore testable in `RTA_BUILD_APP=OFF`" is a fact about the repository,
    not a requirement on new code.

    **Three name mappings, because they are not the names an implementer would
    guess:** Hz → `formatHz`; the one-decimal dB rule → **`formatTrim`**;
    the two-decimal 0..1 rule (coherence and `phaseAgreement`) →
    **`formatAgreement`**.

    **The dB and Hz formatters return the unit in the string.** The test must
    assert with the suffix or it fails on the suffix, not on the rounding:

    ```
    formatHz(1000.4)         == "1000 Hz"
    formatTrim(-3.2145123)   == "-3.2 dB"     // magnitudeDb[0], §6's golden
    formatAgreement(0.9731445) == "0.97"      // coherence[0], §6's golden
    ```

    Each argument is the golden JSON's own literal; the parameters are `double`
    and the float32→double widening is exact, so the string is the rounding of
    the *same* value the wire carried. (An earlier revision of this item paired
    the dB rule with `8.5859375` — that literal is `effectiveAverages`, a
    **count**, not a level. `formatTrim(8.5859375)` does return `"8.6 dB"`, but
    a count formatted as dB is a wrong test; use `magnitudeDb`'s own values.)

    So the work this item names is: extend `app/tests/test_readouts.cpp` with
    one `TEST_CASE` that feeds the golden JSON's float32 literals through the
    three existing functions, and say in the test's name that it is the
    desktop half of §12 constraint 2. Mutation: round in the serialiser
    instead, and test 1's golden goes red; change one formatter's precision,
    and this test goes red.

    **What it does not prove, stated plainly:** that L6a's *JavaScript* viewer
    rounds the same way. A C++ test cannot reach it. The constraint is
    dischargeable there and only there — L6a ships the same thresholds as a
    table shared with these functions plus its own test against the same
    golden values, or §12 constraint 2 stays **untested** for the viewer and
    must be labelled so in L6a's record. This lane proves the desktop half and
    hands over a named seam; it does not get to claim the other half.

14. **Every guard above is shown red-then-green** before the lane closes, per
    `docs/GIT-WORKFLOW.md`'s PR checklist and
    `memory/mutation-testing-needs-the-exe-deleted-first.md` — delete the
    binary before re-running a mutation, because `cmake --build` can log
    `-> X.exe` without relinking.

## 12. The SPL web viewer (lane L6a, G7) rides THIS surface

**Decision, stated so that a parallel lane cannot quietly open a second
listener.** L6a's SPL logging/history/alarms/web viewer (G7,
`docs/plans/MASTER-EXECUTION-PLAN.md` L6a row) is a **client of this API**,
served from **this** server, on **this** port, behind **this** `Host` check,
rate limit and token. It must not open a second socket, a second port, a second
bind default or a second auth model.

**The precedent is exact.** Smaart's SPL Web Viewer is plain HTTP served on the
**same port 26000** as its WebSocket API, with the same optional password. One
surface, two representations. SysTune's answer to the same problem was to ship
a whole bundled NGINX inside the product — which is the upper bound on what
this costs if the decision is made casually.

**Three constraints L6a inherits, and one it must resolve:**

1. **SPL is not in the snapshot yet.** `Snapshot` carries dBFS;
   `rta::meter::Leq` has no `app/` caller. The `"spl"` entry in
   `/api/v1/status`'s `available` list appears when the Meters track lands it,
   and L6a is blocked on that, not on this record.
2. **The viewer rounds, the wire does not.** `CLAUDE.md`'s reading rules —
   whole hertz, one decimal of dB, two decimals of coherence — are the
   viewer's job (§6). The web viewer must apply the identical rule the desktop
   UI applies, or the same measurement reads two ways on two screens. **§11
   item 13 makes the desktop half of this a test** — against the three
   formatters that **already exist**, `rta::view::formatHz` / `formatTrim` /
   `formatAgreement` in `app/src/view/Readouts.h`, asserted on the golden
   JSON's own float32 values; this lane writes no new formatter. L6a
   discharges the JavaScript half by shipping the same
   thresholds against the same golden values, or records the constraint as
   untested for the viewer. Two records asserting it and neither testing it is
   the failure mode this note exists to prevent.
3. **Static assets ride the same server** — a tiny HTML/JS bundle served from
   the same origin — because a viewer served from `127.0.0.1:<port>` fetching
   `127.0.0.1:<port>` is same-origin, needs no CORS headers at all, and passes
   the `Host` check by construction.
4. **What L6a must resolve first**: whether a page served *from* `127.0.0.1`
   fetching `127.0.0.1` is exempt from Chrome's Local Network Access prompt.
   It follows from LNA's same-address-space model and was **not found stated
   verbatim** (station-1 UNVERIFIED ledger item 7). It is an afternoon's test
   against Chrome 142+, and it is a precondition for the viewer, not for this
   API.

## 13. What this record does not decide

- **Write access of any kind.** Owner's ruling; `docs/UPGRADE-BACKLOG.md`.
- **TLS**, on loopback or anywhere else (§9 records the decline and its cost).
- **Discovery / mDNS.** Nobody surveyed agrees: OSM broadcasts at 1 Hz forever,
  Smaart uses a UDP broadcast, REW has none. OSC 1.1 already specifies
  `_osc._udp` / `_osc._tcp` with `txtvers`/`version`/`framing`/`uri`/`types`
  if an OSC surface ever needs it.
- **A LAN bind, and the password that must come with it.** The *feature* is
  deferred; §14 q.2 asks whether the disabled *setting* ships.
- **Push (WebSocket or SSE).** §3 rules out REW's webhook shape permanently;
  it does not build the inbound alternative.
- **Base64 float32 bodies** (§7) — named as the first optimisation, not built.
- **An OSC scalar surface** (§1) — named as a candidate, not built.
- **Q-SYS QRC, ECP or QRWC.** Q-SYS's Lua `HttpClient` reaches a plain JSON
  endpoint already; implementing JSON-RPC to serve a client that speaks HTTP
  would be work with no buyer.
- **Dante.** Its public API is GraphQL, gated behind DDM v1.5+, and concerns
  routing and device health rather than measurement. It is L8's G19, not this
  lane, and naming it here is what stops a later session treating it as
  scoped-out work.
- **Solver endpoints** (`/eq`, `/align`) — blocked on those sessions existing
  in the composition root at all (§5), not on a schema question.
- **Trace binary download** (`.bin` field arrays over HTTP). If `/traces`
  ships, it ships the *list*; the arrays are a second decision.

## 14. Open questions for a human

*Amended by §15 **R14** (q.1: 4737 is taken by IANA; the default is now 4736, and the fixed-versus-ephemeral half of the question is still open) and **R12** (q.3: without `/traces` and `/session` v1 is **eight** endpoints, not six). The plan takes a named default for all five; none blocks a builder.*

1. **The port number.** Proposed **4737**, which is adjacent to REW's 4735 and
   currently unclaimed by any surveyed tool (Smaart 26000, OSM 49007, GALAXY
   25003/25004, Q-SYS 1702/1710, X32 10023). The real question is fixed versus
   ephemeral: a fixed port is discoverable and can collide; an ephemeral port
   written to a file the client reads never collides and needs a rendezvous.
   One sentence settles it.
2. **Does `api.allowLanBind` ship in v1 as a disabled setting, or not exist?**
   The backlog defers the *feature*. A setting that exists and refuses is
   honest about the roadmap; a setting that does not exist cannot be turned on
   by accident, or by a support forum post. Either is defensible.
3. **Is `/traces` and `/session` in v1 at all?** They require a new
   message-thread publish (§5) that nothing else in the app needs yet. Live
   measurement needs none of it. If the answer is "not yet", v1 is six
   endpoints and no new publish path, and this record's §5 becomes a
   description of what a later version adds.
4. **Token: ship the setting empty, or ship a token generated on first
   enable?** A generated token that the operator copies out of the preferences
   panel is strictly safer and is one more thing to lose during a show. On
   loopback with the `Host` check the marginal gain is small; the moment a LAN
   bind exists it is mandatory either way.
5. **Should the Smaart API SDK be requested?** It is free and non-NDA per the
   published terms, and it is the only route to the one piece of prior art
   station 1 could not read — how a competitor encodes **coherence**, the field
   REW has no opinion on because REW has no coherence. Its terms forbid
   redistributing the SDK, so nothing from it could ever be quoted into this
   repo's docs; it could only inform. It is a days-long round trip, so the
   answer is worth having before station 3, not after.

## 15. Amendment, 2026-09-17 — the reconciliations station 3 filed

*Added after `docs/plans/2026-09-17-remote-api-impl-plan.md` was written against
the landed code, and after the adversarial verify of that plan on PR #14. Every
item below **supersedes** the section it names. Nothing above this line was
deleted: a record whose wrong sentences are erased teaches the next session
nothing about how they came to be wrong. Read the original, then read this.*

**How to read it.** `API-R1`..`R13` came from station 3 reading the files the
record cites. `R14`..`R17` came from the verifier round on the plan. Each names
the section it overrides and what changes; the plan's own reconciliation section
carries the longer argument.

### R1 — §10 names two files in `app/src/api/`; there must be three

§10 puts the `Host` allowlist, the method allowlist, the parameter validation
and the rate limit inside `ApiServer.cpp`. §11 items 6-9 require all four to be
**pure functions tested in `RTA_BUILD_APP=OFF`**. Both cannot hold at once.

**Amended:** a third file, `app/src/api/ApiPolicy.{h,cpp}`, framework-free and
free of any server library, holds them; `ApiServer.cpp` keeps the socket, the
thread and the routing and calls into it. This is §10's own sentence — "if
`ApiServer.cpp` grows past [400] the seam is validation-versus-routing" —
applied before the fact rather than after.

### R2 — §2's `split.py` is superseded by the amalgamated header

`split.py` exists to amortise the header across **several** translation units.
§11 item 10's guard permits **exactly one** includer, so there is no second TU
to amortise over and the cost is paid once either way. Against that, split
output is *generated*: a reviewer cannot hash it against an upstream release,
while a verbatim `httplib.h` can — verified at v0.56.0, 22875 lines, sha256
`1f99e51881c4c9d0649b27c611442c2f4d9bcfec5a22a14d5fcd1f8106f730b4`.

**Amended:** vendor the amalgamated header verbatim. If a second includer is
ever needed, `split.py` is the answer and the guard's `ALLOW` becomes a list.

### R3 — the vendored library lives at `external/`, and the guard decides that

§11 item 10 scans `core;platform;ui;tools;app`. `file(GLOB_RECURSE "${DIR}/*.h")`
picks up a vendored `httplib.h` placed **anywhere under `app/`**, matches the
pattern, finds it is not the `ALLOW` file, and turns the guard red on the very
library it exists to permit.

**Amended:** `external/cpp-httplib/` at the repository root, outside all five
`DIRS` entries. (`external/` is new; this repo vendored nothing before.)

### R4 — `absence` is a per-bin array, not a scalar

§6 and §11 item 4 read `absence` as one string. `AverageBlock::absence` is
`std::vector<rta::dsp::SpatialAbsence>` (`app/src/measure/Snapshot.h:131`), one
entry per bin, and `contributors` is likewise a vector (`:125`).

**Amended:** `absence` travels as a JSON **array of strings**, and §11 item 4's
assertion is `"noWeight"` present at a named index. The decision it encodes — a
name, never an integer, because OSM's integer flattening is a measured failure
mode — is unchanged.

### R5 — §8's settings have nowhere to persist, and v1 does not build one

`grep` for `PropertiesFile` / `ApplicationProperties` / `getUserSettings` over
`app/src` returns nothing. `SessionCodec` persists sessions at
`kSchemaVersion = 3` and knows nothing about an API.

**Amended:** `ApiSettings` is a plain framework-free struct carrying §8's
defaults, constructed by the composition root. **Persistence is out of v1** and
joins §13's list. No default in §8 is weakened; every test runs against them.

### R6 — §11 item 11's "N must rise by two" is arithmetic, not a constant

The `-DGLOBS=` list ends with `${CMAKE_CURRENT_SOURCE_DIR}/*.h;*.hpp`
(`app/tests/CMakeLists.txt:282`), so **every new header in `app/tests/` also
raises N**.

**Amended:** the acceptance is "N rises by exactly the number of files this task
added, read from the guard's own `OK (N files scanned)` line". The plan adds far
more than two.

### R7 — `/W4` is global, and a vendored header will not be clean under it

Root `CMakeLists.txt:50` applies `/W4 /permissive- /utf-8` to every target, and
every lane's gate is 0 `warning C`.

**Amended:** the one `#include` of `httplib.h` is wrapped in
`#pragma warning(push, 0)` / `#pragma warning(pop)`; fallback is a `SYSTEM`
include directory plus `/external:W0 /external:anglebrackets`. Which one was
needed is **measured and reported**, not assumed.

### R8 — `rta_strip_comments` does not strip a doxygen block, so the new guard can false-positive

`check_no_std_atomic_shared_ptr.cmake:129-130` removes `//` lines and `/* ... */`
bodies **containing no `*`**. A `/** ... */` block survives, so a file outside
`ApiServer.cpp` that mentions the library inside one trips the guard.

**Amended:** every mention of the library outside `ApiServer.cpp` is a `//` line
comment, and the guard's red-then-green includes that exact case, so the rule is
a test rather than folklore.

### R9 — §11 item 12 had no target to live in (and see R15, which moves it)

`app/tests_juce/CMakeLists.txt` builds `rtatool_view_tests`, which links `az_ui`
and the JUCE GUI modules; that file's own header comment argues against folding
unrelated tests into a target with the wrong dependency direction.

**Amended, then superseded by R15:** the loopback test does not go there at all.
It moves into `rtatool_analysis_tests`, in the `RTA_BUILD_APP=OFF` half of
`app/`, because R15 removes the server's only JUCE dependency.

### R10 — §6 does not say whether `/transfer` serves `transfer` or `soloTransfer`

Both are `std::optional<TransferBlock>` (`Snapshot.h:221`, `:257`).

**Amended:** `/transfer` serves `Snapshot::transfer`. `soloTransfer` is not on
the wire in v1 and never appears in `available`. Three further `Snapshot` fields
§6 never mentions — `peakBandLevelDb`, `peakBandCentreHz`, `referenceBands` —
are likewise **decided out**, and are named here so a later reader knows they
were decided rather than forgotten.

### R11 — `axis.pointCount` is measured, never derived

`TransferBlock` carries no point count. §6's example shows `pointCount: 2049`
beside `fftSize: 4096`, and the two agree only when the engine filled the whole
half-spectrum.

**Amended:** `pointCount = transfer->magnitudeDb.size()`. `fftSize` is echoed
from `Snapshot::fftSize` as a separate fact.

### R12 — §14 q.3's "six endpoints" is the length of `available`, not the endpoint count

§6 lists ten paths. Removing `/traces` and `/session` leaves **eight**:
`/status`, `/snapshot`, `/transfer`, `/mtw`, `/bands`, `/spectrum`, `/average`,
`/positions`. Six is the length of `/status`'s `available` capability list,
which does not include `/status` or `/snapshot` themselves.

**Amended:** v1 is **eight** endpoints; `available` keeps its six capability
names.

### R13 — `GET /api/v1/snapshot` is listed in §6 with no body schema

**Amended:** `/snapshot` is the **union** of `/status` and every block that is
present, keyed by block name, with an absent block's key **absent** — the same
absence rule `coherence` gets, for the same reason. It is one round trip for a
client that wants all of it, and it is the endpoint the golden file pins in
full.

---

*The four below came from the adversarial verify of the plan (PR #14).*

### R14 — §8's port `4737` is IANA-registered; the default becomes `4736`

§14 q.1 proposed 4737 as "currently unclaimed by any surveyed tool", which was
true of the surveyed **audio** tools and was never checked against the registry.
The IANA Service Name and Transport Protocol Port Number Registry has
`ipdr-sp,4737,tcp` and `ipdr-sp,4737,udp` (IPDR/SP, registered 2005-08). 4734,
4735 (REW's own) and 4736 are absent from it.

**Amended:** the default port is **4736**. User Ports are not exclusive so 4737
would have worked, but a named default resting on a check nobody ran is the
thing this record's method exists to prevent. §14 q.1's real question — fixed
versus ephemeral-plus-a-rendezvous-file — is **still open** and is still one
sentence.

### R15 — §10's "`RTA_BUILD_APP=ON` only" for `ApiServer` is overturned; the server is JUCE-free and runs on CI

§4 says the server owns one thread created and joined by the composition root;
§10 marks `ApiServer.{h,cpp}` ON-only. The only thing that forced ON was the
choice of `juce::Thread`. cpp-httplib is pure standard C++ — `bind_to_port(host,
port)`, `bind_to_any_port(host)` and `listen_after_bind()` (`httplib.h`
v0.56.0 `:2267`, `:2269`) — and `app/tests` is registered **outside** the
`RTA_BUILD_APP` guard (root `CMakeLists.txt:72`). There is **one** CI job and it is `RTA_BUILD_APP=OFF`
(`.github/workflows/ci.yml:9`, `:21`, three OSes at `:15`), so an ON-only server
is a server proven on **zero** machines except a developer's Windows box, by
hand — including the end-to-end `Host` check, which §9 calls the highest-value
control in the whole API, and shutdown/port-reuse, the two behaviours that
differ most across Windows, Linux and macOS sockets.

**Amended:** `ApiServer` owns a **`std::thread`**, contains no JUCE, compiles
into the `RTA_BUILD_APP=OFF` half of `app/`, and its loopback tests run on all
three CI operating systems. §4's contract is unchanged — the composition root
still constructs it and still joins its thread, and the ownership and ordering
argument is untouched. **Only the composition-root wiring stays ON.**

Two mechanical consequences the plan's first revision got wrong and its second
fixed, recorded here because a later reader will otherwise rediscover them.
**`Server::bind_to_port` returns `bool` and discards the port, and `Server` has
no `port()` accessor** — that member belongs to `Client`. The call that returns
the bound port is **`Server::bind_to_any_port(host)`**, and binding on the
constructing thread before `listen_after_bind()` runs on the server thread is
what lets a test read the port with no sleep and no polling. And **`ApiServer`
must be a pimpl**: a by-value `httplib::Server` member forces
`#include <httplib.h>` into `ApiServer.h`, which §11 item 10's guard — whose
`ALLOW` is `ApiServer.cpp` alone and whose scan covers `app/**/*.h` — would then
turn **red on a correct build**. The header is what bends, never the guard.

Two costs, stated rather than absorbed. (a) CI now compiles the vendored header
on ubuntu, macos and windows; §2's compile measurement was MSVC-only, so the
figures do not transfer and the builder reports the real ones. (b) A loopback
test built on `httplib::Client` would be a **second includer** and would turn
§11 item 10's guard red, so the test drives a **raw socket** instead — forty
lines that send three fixed request lines and read a status line. That is not
"hand-rolling HTTP": §2 rejected hand-rolling a *server*, with its parsing,
keep-alive, timeouts and security surface. A fixed-string test client has none
of those, and it keeps the guard's headline claim — *exactly one* file includes
a server library — literally true.

### R16 — §11's acceptance needs a JSON parser, and the record vendors none

§11 items 1-5 are satisfied by substring matches and a byte-compare against a
file the same serialiser produced. **Nothing in the `RTA_BUILD_APP=OFF`
configuration — the only one CI runs — asserts that the emitted document is
well-formed JSON.** A stable-but-malformed document passes every one of them.

Two ways out were argued. A hand-written validating parser in the test needs no
new dependency — and fails on the principle this project already applies to
golden vectors: **a validator must not share an author with the thing it
validates.** Golden vectors come from NumPy/SciPy rather than from a second
in-house implementation for exactly that reason, and a parser written by the
person who wrote the serialiser reproduces their misunderstanding of JSON on
both sides, where it cancels out and reads as proof.

**Amended:** vendor **nlohmann/json** (MIT, single header) at
`external/nlohmann/json.hpp` with its `LICENSE` and a provenance note, **used
only under `app/tests/`** and never by shipped code. A new guard
(`no_json_parser_in_shipped_code`) scans `core;platform;ui;tools;app/src` with
**no permitted file at all**, and additionally fails if **no** file under
`app/tests/` includes it — a parser nothing tests with has stopped meaning
anything. MIT flows into an AGPLv3 work imposing only notice retention, the same
chain §2 established for cpp-httplib. The compile cost is confined to one test
TU, the same discipline §2 applies to the server header.

### R16a — a WebSocket upgrade is a `GET`, so no method check stops it

Recorded because the plan asserted the opposite twice before it was measured
against the library. At v0.56.0 cpp-httplib compiles WebSocket support in with
**no macro guard**, and an upgrade request is `GET /path HTTP/1.1` carrying
`Upgrade: websocket` — so §9 control 2's method allowlist **accepts** it, and
with no upgrade handler registered the request falls through to ordinary
routing: `200` with the route's normal JSON body, or `404`. Never `405`.

**The conclusion §3 and §13 reach is unchanged** — no WebSocket can be
established — but it rests on **the absence of a registered handler alone**, not
on any check this code performs. The server never emits `101 Switching
Protocols` and never sends `Sec-WebSocket-Accept`. The plan's Task I carries a
case that measures it rather than asserting it.

### R17 — §9 control 3's `406` and `415` are dropped; `413` is kept and tested

Both are content-negotiation answers about a **request body**, and v1 is
GET-only with a single representation. 406 would be machinery with no buyer: a
client that cannot accept `application/json` has nothing to do with this API.
415 is unreachable once any request carrying a body is refused before its
content type is read.

**Amended:** `413` **is** built and tested — the payload limit is set small
(8 KiB) and any request with a body over it is refused before routing, which is
what makes 415 unreachable. **406 and 415 are dropped**, and §13 gains "content
negotiation" as something this record no longer decides. Naming the drop is the
point: a control listed in a record and absent from the code is a control
everyone assumes someone else built.


## Sources

**This repo, at `6d9a53d`:** `app/src/measure/{Snapshot,SnapshotSource,
AtomicSharedPtr,AnalysisThread,RoutingPlan,SyntheticSnapshot}.h`,
`app/src/measure/AnalysisThread.cpp:316-325`, `app/src/view/RtaView.cpp:26`,
`app/src/view/TransferView.cpp:34`, `app/src/MainComponent.h:129,160`,
`app/src/trace/{Trace,TraceLibrary,SessionCodec}.h`,
`app/src/measure/{EqSession,AlignmentWizard}.h`,
`platform/src/AudioIo.cpp:116-148`, `platform/tests/check_callback_shape.cmake`,
`core/tests/check_no_framework_deps.cmake:54` (at main `e213202`),
`core/tests/check_no_std_atomic_shared_ptr.cmake`,
`core/tests/CMakeLists.txt:149-154`, `app/tests/CMakeLists.txt:280` (the
`add_test(NAME measure_has_no_framework_deps ...)`, its `-DGLOBS=` list at
`:282` — both at main `e213202`), `app/src/view/Readouts.h:72,79,87`,
`app/tests/test_readouts.cpp:100-115`,
`CMakeLists.txt:60-72`, `core/include/rta/meter/Leq.h`.

**Records and rulings:** `docs/dsp/2026-09-06-multichannel-l6b.md` §8, §10;
`docs/research/2026-09-06-l6b-station1-research.md` (the prior-art table this
pass corrects); `docs/HUMAN-QA-QUEUE.md` "Từ lane L6b";
`docs/UPGRADE-BACKLOG.md`; `docs/specs/2026-08-28-trace-library-and-session.md`
§3; `docs/plans/MASTER-EXECUTION-PLAN.md` (L6a and L6b rows);
`docs/GIT-WORKFLOW.md`; `memory/float32-fft-precision.md`,
`memory/a-placeholder-for-an-absent-result-erases-its-state.md`,
`memory/mutation-testing-needs-the-exe-deleted-first.md`,
`memory/the-parity-table-is-a-hypothesis.md`.

**External, as reached by station 1 on 2026-09-16** — full URL ledger in
`docs/research/2026-09-16-remote-api-station1-research.md`: Open Sound Meter
`src/remote/` at master `1e08de2` (GPL-3.0-or-later); REW API help;
Rational Acoustics Smaart v8 User Guide pp. 82-83, 98-100, Smaart LE v9.1 guide
p. 55, the SPL Webserver and Client Window support articles, the SDK terms, and
the Bitfocus Companion Smaart v3 module source; Meyer Sound *Galileo GALAXY
Programming Guide* Rev B4; Q-SYS help (External Control APIs, QRC, Lua
`HttpClient`); Audinate Dante Managed API user guide; OSC 1.0 specification and
Freed & Schmeder, NIME 2009; Fraietta, NIME 2008; RFC 768; RFC 8085 §3.2;
cpp-httplib `httplib.h` v0.56.0, `README.md` and `LICENSE`; civetweb
`LICENSE.md`, `src/civetweb.c`, `docs/UserManual.md`, `CMakeLists.txt`;
cesanta/mongoose `LICENSE` and mongoose.ws/licensing; the FSF licence list;
JUCE 9.0.1 `LICENSE.md` and the pinned checkout's module sources; OWASP REST
Security Cheat Sheet; OWASP API Security Top 10 (2023) API4; NCC Group
Singularity wiki; GitHub Security blog, 3 April 2025; MDN CORS; Chrome for
Developers, Local Network Access.
