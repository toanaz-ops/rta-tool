# The remote API, read-only (lane L-API)

*2026-09-16. Station-2 decision record. Station-1 research:
`docs/research/2026-09-16-remote-api-station1-research.md`. Repo at `6d9a53d`.*

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
the pinned checkout for a `*Server*` class returns `SVGPaintServer` and
`InterprocessConnectionServer`, and the latter speaks JUCE's own length-prefixed
framing that no browser, `curl` or Lua script can talk to. Hand-rolling means an
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

**Decision.** v1 is **poll-only**. Every response carries the snapshot's
`sequence`; a client that wants change detection sends
`If-None-Match: "<sequence>"` (or `?since=<sequence>`) and receives **304 Not
Modified** with no body when nothing has advanced. There is **no** WebSocket,
no SSE and no webhook in v1.

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

**Decision.** The server owns **one** thread, created and joined by the
composition root. It calls `SnapshotSource::latest()`, takes a
`shared_ptr<const Snapshot>` copy, and serialises **from that copy**. It never
holds the pointer across a request. Serialisation happens on the API thread,
never on the analysis thread and never on the message thread.

**The one rule about the audio callback.** The callback is
`juce::ScopedNoDenormals` followed by exactly two calls —
`bus_.pushFromCallback(...)` and `output_.render(...)`
(`platform/src/AudioIo.cpp:117-148`) — and
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

- The API thread does exactly **one** `load()` per request, at most 20 times a
  second, matching what one view already does. Two views plus the API is three
  readers at 20 Hz against one writer at 20 Hz — the same order of traffic the
  slot already carries.
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
  "effectiveAverages": 8.59,
  "appliedDelaySamples": 512,
  "magnitudeDb": [ -3.2, -3.1, ... ],
  "phaseDeg":    [ 12.4, 11.9, ... ],
  "coherence":   [ 0.97, 0.96, ... ]
}
```

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
  "frequencyHz": [ 0, 11.7, ... ],
  "magnitudeDb": [ ... ],
  "phaseDeg":    [ ... ],
  "coherence":   [ ... ],
  "appliedDelaySamples": 512,
  "bands": [
    { "firstIndex": 0, "pointCount": 256, "fftSize": 32768,
      "windowSeconds": 0.683, "integrationSeconds": 5.461,
      "effectiveAverages": 8.59, "seamHz": 0, "coherenceAvailable": false }
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

**Why the token must not be a cookie.** The token defence works because the
attacker cannot *read* the secret — not because credentials are blocked. A
rebound request **is** same-origin and **would** carry cookies for that origin.
A cookie-based scheme fails against exactly the attack it was added for.

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
    **No JUCE, no sockets, no `httplib.h`.** Added to the explicit
    `measure_has_no_framework_deps` glob list, and therefore compiled and
    tested in the `RTA_BUILD_APP=OFF` configuration CI runs on three OSes.
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
10. **No server library below `app/`.** A new ctest reusing the
    `check_no_std_atomic_shared_ptr.cmake` **DIRS + ALLOW** shape:
    `-DDIRS=core;platform;ui;tools`, pattern `httplib|civetweb|mongoose`, and
    an `ALLOW` naming `app/src/api/ApiServer.cpp` as the one file permitted to
    include it. That script's own sentinel idiom — fail if the guard has
    stopped watching its allowed file — is copied with it, because a guard
    that silently scans nothing is worse than no guard.
11. **`ApiSerialise` is in the framework-free glob list.** Adding it to
    `measure_has_no_framework_deps`'s explicit `GLOBS` is itself the test:
    that check prints `OK (N files scanned)` and N must rise by two.
12. **`RTA_BUILD_APP=ON` only, and the only test that binds anything**: start
    the server on `127.0.0.1:0` (ephemeral), issue one `GET /api/v1/status`
    over loopback, assert 200 and a parseable body; issue one `POST`, assert
    405; issue one request with a forged `Host`, assert 403. Three requests,
    no sound card, no device.
13. **Every guard above is shown red-then-green** before the lane closes, per
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
   UI applies, or the same measurement reads two ways on two screens.
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

## Sources

**This repo, at `6d9a53d`:** `app/src/measure/{Snapshot,SnapshotSource,
AtomicSharedPtr,AnalysisThread,RoutingPlan,SyntheticSnapshot}.h`,
`app/src/measure/AnalysisThread.cpp:316-325`, `app/src/view/RtaView.cpp:26`,
`app/src/view/TransferView.cpp:34`, `app/src/MainComponent.h:129,160`,
`app/src/trace/{Trace,TraceLibrary,SessionCodec}.h`,
`app/src/measure/{EqSession,AlignmentWizard}.h`,
`platform/src/AudioIo.cpp:117-148`, `platform/tests/check_callback_shape.cmake`,
`core/tests/check_no_framework_deps.cmake:50`,
`core/tests/check_no_std_atomic_shared_ptr.cmake`,
`core/tests/CMakeLists.txt:149-154`, `app/tests/CMakeLists.txt:274`,
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
