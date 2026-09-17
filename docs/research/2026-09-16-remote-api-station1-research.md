# The remote API — station 1 research

*2026-09-16. Station 1 research, lane L-API (remote read-only API). Repo at
`main` = `6d9a53d`.*

Owner rulings this pass starts from, both already recorded and both **not
reopened here**:

- **Read-only first.** `docs/HUMAN-QA-QUEUE.md` ("Từ lane L6b", 2026-09-06,
  item (b)) and `docs/UPGRADE-BACKLOG.md` "Remote API: write access to
  routing": the API exposes `SnapshotSource` — measurements and traces — and
  cannot change routing or config. A remote write to routing during a live
  show is near-irreversible and needs authentication and a deliberate design
  before it exists.
- **Localhost by default** (REW's model), with LAN bind plus a password itself
  an opt-in later feature.

What is open, and what `docs/dsp/2026-09-06-multichannel-l6b.md` §10 explicitly
deferred to this pass: **the transport**, **the dependency**, **the threading
contract against the publish model**, **the security posture of "read-only on
localhost"**, and **the data model and its schema**.

## Decision → evidence table

| Candidate decision | Evidence | Source |
|---|---|---|
| Transport is **HTTP/1.1 + JSON over TCP**, not OSC, not a bespoke protocol | A 2049-point float32 curve is 8196 bytes; one non-fragmented Ethernet UDP datagram holds 1472 bytes (368 floats); a fragmented datagram reassembles all-or-nothing, so one lost fragment discards the whole 20 Hz frame with no retransmit and no notification | RFC 768 (16-bit Length → 65507 ceiling), RFC 8085 §3.2, `Snapshot.h` (`fftSize/2+1`) |
| OSC's blob type and bundles do **not** rescue OSC for spectra | Blob is an encoding, not a transport; "the contents of an OSC packet must be either an *OSC Message* or an *OSC Bundle*" — a bundle is inside one packet. No sequence number, no fragment index, no reassembly anywhere in OSC | OSC 1.0 spec |
| A second reason not to pick OSC: **OSC-over-TCP has two incompatible framings in the wild** | 1.0 prefixes each packet with an int32 size; 1.1 "requires" SLIP (RFC 1055) double-END encoding instead | OSC 1.0 spec; Freed & Schmeder, NIME 2009 |
| The library is **cpp-httplib**, MIT, TLS left undefined | `LICENSE` is MIT; every OpenSSL include is inside `#ifdef CPPHTTPLIB_OPENSSL_SUPPORT`; `listen("127.0.0.1", port)` binds loopback in one call | `httplib.h` v0.56.0, `LICENSE`, both read 2026-09-16 |
| **Mongoose is disqualified**, and buying a licence does not fix it | `LICENSE` offers "GNU General Public License version 2" with **no "or any later version"**; FSF: "GPLv2 is, by itself, not compatible with GPLv3". The commercial arm conflicts with the AGPL source release | cesanta/mongoose `LICENSE`, mongoose.ws/licensing, gnu.org licence list |
| The compile-time charge against cpp-httplib is real, measurable, and fixable in one TU | Measured on MSVC 19.51.36248 `/std:c++20 /O2 /c`: empty TU 0.13 s, `<regex>`+`<thread>`+`<iostream>` 0.96 s, httplib TU **9.55 s cold**; preprocessed 210,318 lines. `split.py` moves it to one TU | Measured this pass, 2026-09-16 |
| **JUCE ships no HTTP server** — hand-rolling on `StreamingSocket` is the alternative, and it is several hundred lines whose bugs are security bugs | Grep of all 24 modules of the pinned 9.0.1 checkout for a `*Server*` class returns `SVGPaintServer` and `InterprocessConnectionServer` only; `waitForNextConnection()` blocks and you supply the thread | JUCE 9.0.1 checkout; `docs.juce.com` `StreamingSocket` |
| The API thread reads the **same published snapshot the views read**, through `SnapshotSource::latest()` | Publish is one `AtomicSharedPtr` store at ≤ 20 Hz; the views already poll `latest()` at exactly 20 Hz | `AnalysisThread.cpp:316-325`, `RtaView.cpp:26`, `TransferView.cpp:34` |
| …and that slot is **not lock-free on this project's own toolchain**, so the API thread is a third participant on a spin/lock, not a free read | Measured on MSVC 14.51: `is_lock_free()` false on the C++20 path; the Apple libc++ fallback takes a lock from an address-keyed table | `app/src/measure/AtomicSharedPtr.h` class comment |
| Nothing in this lane may touch the audio callback | The callback is `ScopedNoDenormals` then exactly two calls, and a brace-counting grep enforces the first | `platform/src/AudioIo.cpp:116-148`, `platform/tests/check_callback_shape.cmake` |
| The trace library **cannot** be read from an API thread as it stands | `TraceLibrary` is owned by `MainComponent`, mutable, copy and move deleted, with a `revision()` counter and no atomic publish | `MainComponent.h:160`, `TraceLibrary.h:51-54, 83` |
| An endpoint for solver suggestions would return the state of an object nobody owns | `grep -rl` over `app/` finds `EqSession` and `AlignmentWizard` only in their own files and their tests — no composition-root instance exists | Repo-wide grep at `6d9a53d` |
| **SPL cannot ship in v1**, which directly constrains L6a's web viewer | `Snapshot` carries dBFS only; `calibrationOffsetDb`/`LevelUnit` exist on stored `CaptureMeta`, not live; `rta::meter::Leq` has no `app/` caller | `Snapshot.h`, `Trace.h:39-40`, repo-wide grep |
| The bind default is **127.0.0.1** and it is the only surveyed default under which "no auth" is not a contradiction | REW binds `localhost`:4735 with no auth; Smaart binds **all adapters including wireless** with an optional blank password; OSM broadcasts and multicasts link-local with none; GALAXY has no authentication at all | REW API help; Smaart v8 guide p. 98 + support article; OSM `server.cpp`; GALAXY Programming Guide |
| Binding loopback is **not** sufficient — a `Host`-header allowlist is the highest-value control | Under DNS rebinding the origin genuinely matches, so the same-origin policy does not apply and CORS is inapplicable; it "does not require a misconfiguration or bug" | NCC Group Singularity wiki; GitHub Security blog, 3 Apr 2025 |
| Any token must be a **Bearer/header token, never a cookie** | A cookie is an *ambient* credential: the browser attaches it to any request to `127.0.0.1:<port>` whichever page issued it, so any site the operator visits is authenticated to the listener (CSRF, no DNS trick needed). A `Bearer` header is attached only by a caller that knows the secret. **Not** a rebinding argument — rebinding is answered by the `Host` allowlist, and cookie jars key on host *name*, so a rebound request carries the attacker's cookies, not this app's | GitHub Security blog, 3 Apr 2025 (rebinding "cannot contain cookies") |
| Absence of CORS headers is **not** a posture — the request is still sent and executed | A simple `GET` gets no preflight; the browser rejects the *response* after the server has run it | MDN CORS |
| A record-count parameter must be clamped **server-side**, and here that is a real-time control | API4:2023 names "server-side validation for … the one that controls the number of records to be returned"; in this product unbounded work during a show is a dropout | OWASP API Security Top 10 (2023) |
| Read-only should be expressed as **GET-only over HTTP**, not as a verb field in a message | Smaart's `action` field makes read-only inexpressible to a firewall, a proxy or a browser; REW's method split is expressible in all three (and is weakened only by REW's own `POST …/command` endpoints) | Smaart Companion `src/index.js`; REW API help |
| Push in v1 must not be REW's model | REW's subscription is an outbound POST to a caller-supplied URL — for a read-only API that is an SSRF pivot | REW API help |
| The client should declare its own rate budget, and 23 FPS is the published ceiling of the nearest competitor | Smaart v8 guide p. 100: Spec/TF Stream FPS "is set to the maximum allowable value (23 FPS) by default"; Command Timeout default 2000 ms | Smaart v8 User Guide pp. 98-100 |
| A per-snapshot version token is what prevents the documented stale-state bug | Smaart clients "cannot detect setting changes made by the host or by other clients"; `Snapshot::sequence` already exists for exactly this and says so | Smaart v8 guide; `Snapshot.h` (`sequence`) |
| Positional, unlabelled, unversioned arrays rot — do not ship them | OSM's `ftdata` column 5's meaning survives only in a commented-out line; enums are flattened to integers with no name | OSM `server.cpp:383-399, 286-292`, `item.cpp:174-175` |
| The SPL web viewer must ride this surface, and the nearest competitor already does exactly that | Smaart's SPL Web Viewer is plain HTTP on the **same port 26000** with the same optional password | Rational Acoustics SPL Webserver article |
| Q-SYS integration needs no protocol work from us | Q-SYS Control Scripting ships a Lua `HttpClient` with `Download{}`, TLS, basic/digest auth, a `Headers` table and an async handler — it can poll a plain JSON endpoint on a timer | help.qsys.com, `HttpClient` |
| Dante is not a consumer and not a publication route | The Dante Managed API is GraphQL and works only on DDM v1.5+-managed systems; it covers routing and device health, not measurement | dev.audinate.com Managed API guide; Audinate third-party page |
| Meyer GALAXY is **public**, and is the nearest design analogue in this product's own world | A free Programming Guide specifies ASCII 25003 / OSC 25004, a URL-shaped control-point namespace, subscribe-with-rate (default 30 ms, range 0-100 ms), 30 s UDP subscription expiry, and **no authentication** | Galileo GALAXY Programming Guide, Rev B4 |

## Part 0 — Corrections to the brief

Three statements the project already holds are wrong or too coarse. Station 1
opens with them, per `memory/the-parity-table-is-a-hypothesis.md`.

1. **OSM's discovery is UDP *broadcast*, not multicast.** The L6b research
   table (`docs/research/2026-09-06-l6b-station1-research.md:210`) records
   "UDP multicast 239.255.42.42:49007" as OSM's transport. Reading
   `src/remote/server.cpp:512-518` against `src/remote/network.cpp:98`: the
   1 Hz `hello` beacon calls `sendUDP` with the host argument omitted, and
   that defaults to `QHostAddress::Broadcast` — 255.255.255.255:49007. The
   multicast group carries only the *change* notifications
   (`Server::sendMulticast`, `server.cpp:482-490`), and data and control run
   over **TCP** on the same port (`network.cpp:55`). So OSM is three channels
   on one port, not one.
2. **OSM has no schema version.** The L6b table's "envelope
   `{api, version, host, message, uuid, time}`" is right, but `version` is
   `APP_GIT_VERSION`, defined in `OpenSoundMeter.pro:306-307` as the output of
   `git describe --tags` at build time — a build string, not a contract
   version. `NO_API_REVISION` (`src/meta/metabase.h:21`) is a "do not spam the
   network with this property" filter, not version negotiation. A client
   cannot ask the far end what it speaks.
3. **OSM's write surface is worse than "writable routing".** `command`
   (`server.cpp:420-454`) takes a **method name as a string off the network**
   and hands it to `QMetaObject::invokeMethod`. The server does not restrict
   the name to the three the client emits, and `src/source/measurement.h:125`
   declares `Q_INVOKABLE void destroy()`. This is a code-reading conclusion,
   not a demonstrated exploit — but it is the specific pattern this project
   must not copy, and it sharpens *why* the owner's read-only ruling is
   cheap insurance rather than a limitation.
4. **Meyer Sound's Galileo GALAXY protocol is not proprietary.** The brief
   asked for "notes" on it, expecting an undocumented system. Meyer publishes
   a complete free Programming Guide specifying a third-party ASCII + OSC
   control API. It is the nearest design analogue this project has, and it is
   citable. (What *is* proprietary: Compass's own session layer, RMS, and
   Compass Go / Spacemap Go.)
5. **Q-SYS does not need us to implement QRC.** The brief listed Q-SYS and
   Dante as "consumers". Q-SYS Control Scripting ships a Lua `HttpClient`
   that polls arbitrary URLs — plain HTTP+JSON buys the Q-SYS integration with
   no protocol work here at all. **Dante is not a consumer in any sense**: its
   public API is GraphQL, gated behind a DDM v1.5+ licence, and is about
   routing and device health, not measurement. It belongs to L8's G19, and
   this record should say so rather than leave it looking scoped-out.
6. **`https://www.roomeqwizard.com/api/`, named in the brief, is a 404.** The
   REW API documentation lives at
   `roomeqwizard.com/help/help_en-GB/html/api.html`.

## Part A — Prior art, read as code where code exists

### Open Sound Meter — `src/remote/`, master `1e08de2` (authored 2025-09-13)

Read from source on 2026-09-16, file and line cited. GPL-3.0-or-later
(`LICENSE` is the GPLv3 text; every source header adds "or … any later
version", e.g. `src/remote/network.cpp:6-8`).

| Question | What the code says |
|---|---|
| Port | `DEFAULT_PORT = 49007` (`network.h:37-39`), `constexpr`, **no setter anywhere in the remote layer** — not configurable |
| Discovery | UDP **broadcast** 255.255.255.255:49007, 1 Hz (`server.cpp:512-518`, `network.cpp:98`, `server.h:40` `TIMER_INTERVAL = 1000`) |
| Notifications | UDP multicast 239.255.42.42, `MulticastTtlOption = 1` → link-local (`network.cpp:25-26, 31`, `server.cpp:482-490`) |
| Data / control | TCP on `AnyIPv4:49007`, one request per connection, server disconnects after answering (`network.cpp:55, 133`) |
| Framing | 4-byte **little-endian** length prefix (`tcpreciever.cpp:51-57`), `TIMEOUT = 10000` ms (`tcpreciever.h:31`), writes chunked at 32767 bytes |
| Compression | **Asymmetric**: server `qCompress`es the reply, client sends plain JSON (`network.cpp:120, 167, 194-195`). `qCompress` is Qt's own framing — a non-Qt client must reimplement it |
| Auth | **None.** `password\|token\|auth\|secret\|tls\|ssl\|allowlist` across the whole `src/remote/` tree: zero matches. The peer address is taken and marked `[[maybe_unused]]` (`server.cpp:208`). Identity is a self-declared `name` string (`server.cpp:229`) |
| Schema version | **None** — see Part 0 item 2 |
| Write surface | `update` writes any `Q_PROPERTY` except `objectName`/`active` (`server.cpp:312-374`): `polarity`, `gain`, `offset`, `dataChanel`, `referenceChanel`, `delay`, `average`, `window`, `calibration`, and the generator's `enabled`/`type`/`frequency`/`gain`/`duration` (`generatorremote.h:35-41`). Plus `command` — Part 0 item 3 |
| Client rate | 250 ms tick, **one outstanding request at a time**, oldest-first (`remoteclient.h:41`, `remoteclient.cpp:126-152`) → ceiling of 4 full fetches per second across *all* remote sources |
| Payload | `ftdata`: one array-of-5 per bin, positional and unlabelled — `[frequency, module, magnitudeRaw, phase(radians), coherence]` (`server.cpp:383-399`). `timeData`: `[impulseTime, impulseValue]` per sample |

Three details worth carrying into a decision:

- **The wire is float data in double-formatted decimal text.** Everything is
  `float` internally, `static_cast<double>` on the way out
  (`server.cpp:386-390`), `static_cast<float>(row[n].toDouble())` on the way
  in (`item.cpp:169-175`). The JSON is wider than the information it carries.
  `memory/float32-fft-precision.md` already says what this project's numbers
  are worth; printing them at double width is pure payload cost.
- **Positional arrays with no schema rot in place.** Slots 5 and 6
  (`peakSquared`, `meanSquared`) are commented out on the server
  (`server.cpp:391-392`) while the client still parses them if present
  (`item.cpp:174-175`). The meaning of column 5 lives only in a comment.
- **Enums are flattened to their integer value with no name**
  (`server.cpp:286-288`), so a renumbering between versions silently changes
  meaning and nothing on the wire would reveal it.

**Licence direction matters and only one way.** GPLv3 §13 permits combining
GPLv3 code with AGPLv3 code, so OSM source *could* be taken into this tree
(the combination then carries the AGPL network clause); this tree's code
cannot flow back into a plain-GPL project. Reading the design costs nothing.
Any file lifted would have to be labelled with its origin.

### REW — HTTP + JSON, `localhost:4735`, no auth (help page read 2026-09-16)

The owner's bind ruling cites REW, so this pass read REW's own help page rather
than the summary the L6b table carries.

- **Transport** HTTP/1.1 + JSON. Default `localhost` : **4735**. Both
  overridable on the command line: `-port` (ports below 1024 ignored) and
  `-host "0.0.0.0"`. Started by a preferences button, a start-with-REW
  preference, or the `-api` launch argument.
- **The same page contradicts itself**: one sentence says the API cannot be
  reached from outside the machine, and a few sentences later documents
  `-host "0.0.0.0"`. Read as: localhost is the default, remote is a flag, and
  the prose has not caught up. There is no documented GUI "allow remote"
  control.
- **Auth: none, at all.** With `-host 0.0.0.0` that is full write control to
  anyone on the LAN, including `POST /application` shutdown.
- **Schema version: not in the path.** REW serves its own OpenAPI spec at
  `/doc.json` and `/doc.yaml` with Swagger UI at the root; `info.version`
  there is the authoritative number and is **UNVERIFIED** (it needs a running
  REW). There is a `GET /version`, and measurement summaries carry
  `rewVersion`.
- **Read/write split is by HTTP method**, but weakened in practice: the real
  actions sit behind `POST …/command` endpoints, so "POST" does not mean "this
  changes a measurement" and "GET" is the only honest half of the split.
- **Payload shape.** A `FrequencyResponse` is JSON carrying a smoothing
  setting, a start frequency, either points-per-octave or a linear frequency
  step, and **Base64 strings of 32-bit floats, big-endian**, for magnitude and
  phase. **Frequencies are not transmitted** — the client rebuilds the axis.
  Phase is in **degrees**. Magnitude defaults to SPL, `?unit=dBFS` overrides;
  `?smoothing=` and `?ppo=` override the rest.
- **`/rta/captured-data` carries a nanosecond timestamp and a running count of
  samples processed since the RTA last started** — a change-detection token,
  and the one idea in REW's API worth taking outright.
- **Coherence does not appear anywhere in REW's API.** REW is a single-channel
  swept-sine analyser. So on the one field a dual-FFT tool most needs to
  publish, REW is not prior art at all.
- **Subscriptions are an outbound POST to a caller-supplied URL.** A client
  posts `{"url": "http://…"}` and REW then POSTs updates *to that URL*,
  cancelling if it does not get an OK back. For an API whose whole premise is
  "read-only", this is not a read-only primitive: it turns the analyser into
  an HTTP client aimed at an address the caller chose — the textbook SSRF
  pivot. Anything push-shaped in this project must keep the connection
  **inbound**.
- **Rate: no documented limit and no recommended interval.** The documented
  pattern is instead "long commands return Accepted; poll a result endpoint,
  or set `/application/blocking`". An independent open-source client
  (`rew-iqc`) reports blocking mode unreliable for measurement commands and
  polls a count instead.

### Smaart — JSON over WebSocket, `ws://host:26000/api/v3/`, all adapters

Read from Rational Acoustics' own docs and the **Smaart v8 User Guide PDF**
(pp. 82-83, 98-100), plus the open-source Bitfocus Companion module, which is
the only public source for the wire shape.

- **Transport** JSON over WebSocket at `ws://<host>:<port>/api/v3/`; discovery
  is a separate **UDP broadcast** so clients auto-find servers. Default port
  **26000** (v8 guide p. 98).
- **Bind: all network adapters, including wireless** — the opposite of REW,
  and the reason the first enable raises an OS firewall prompt. Smaart **LE
  v9.1** (p. 55) exposes an IP/Hostname/Port setting, which implies v9 lets
  you choose; the v9 **Suite** guide does not exist yet, so that is
  **UNVERIFIED** for the full product.
- **Auth: optional password, blank by default** — v8 guide p. 98: "If you
  leave this field blank, no password is required to connect." The handshake
  is visible in the Companion source: the server's first response carries
  `authenticationRequired`, the client answers
  `{action:'set', properties:[{password}]}`. So the password crosses an
  unencrypted `ws://` on an all-adapters bind, in cleartext.
- **Version is a path segment** — `/api/v3/`. A client fails fast against an
  incompatible server. This is the cleaner of the two precedents; REW's
  unversioned paths cannot do it.
- **The verb is in the message, not the URL**:
  `{sequenceNumber, action, target, properties}` with `action` ∈
  `get | set | capture | issueCommand`. `capture` *feels* like a read and is
  not — the v8 guide says the file is created on the server and uploaded to
  the client.
- **Rate is negotiated by the client at connect, and the numbers are
  published** (v8 guide p. 100): **Spec / TF Stream FPS**, "set to the maximum
  allowable value (23 FPS) by default"; **Command Timeout**, default
  **2000 ms**; **Live IR Range**, whose width is explicitly a bandwidth
  trade. A client declaring its own budget at handshake is the best rate prior
  art in any surveyed tool.
- **The SPL Web Viewer is served over plain HTTP on the same port 26000, with
  the same optional password.** One surface, two representations. That is
  direct prior art for this project's L6a G7 requirement, and it is the reason
  §12 of the decision record exists.
- **Documented client limitations, useful as a "what a remote view cannot do"
  list**: no peak hold, no coherence blanking, no unwrapped phase, no
  phase-as-group-delay, no SPL metering; "Captured traces from the data
  library on the host machine are not available to the client"; averaging is
  server-side while banding and smoothing are client-side and may differ; and
  **clients cannot detect setting changes made by the host or by other
  clients** — a stale-state bug in the protocol that a per-snapshot version
  token prevents by construction.
- **The data message format for magnitude / phase / coherence is
  UNVERIFIED.** It is in the SDK, which is free but request-gated behind a
  form. The published SDK terms grant no right to redistribute the SDK and do
  allow commercial closed-source products built with it. So the format is
  obtainable and this project may not republish it. If the encoding of
  coherence ever becomes contentious, requesting the SDK is a days-long round
  trip that should be started early, not at design freeze.

### Meyer Sound Galileo GALAXY — the brief was wrong: it is fully public

The brief called this "network protocol notes", expecting something
proprietary. Meyer Sound publishes a complete **Galileo GALAXY Programming
Guide** (PN 05.230.008.01 Rev B4, © 2021) as a free PDF, and it specifies a
third-party control API in full. GALAXY is "the server in a client/server
relationship"; Compass is one client among several.

This is the nearest analogue to what this lane is building, from a vendor whose
users overlap this project's almost completely, so its choices are citable
precedent rather than trivia:

- **Two protocols over one address namespace.** ASCII on TCP **25003**, OSC on
  **25004**, both over TCP *and* UDP. Control points are URL-shaped:
  `/processing/input/1/mute`. ASCII set is `path=value`; ASCII query is the
  bare path; the response echoes `path='value'`.
- **UDP is chosen for a stated reason this lane shares**: it "has less latency
  and jitter than TCP, which is an advantage when communicating with meters or
  other functions that require real-time updates".
- **Subscriptions carry a rate.** `+/processing/input/1/mute 100` subscribes at
  100 ms. Default **30 ms**, allowed range **0-100 ms**, and a value is sent
  only when it actually changes if the point changes slower than the rate.
- **Keepalive: a UDP subscription expires after 30 s of client silence**, held
  open by an empty `/ping`. (Q-SYS uses 60 s for the same purpose.)
- **Regex in the address** — `/project/snapshot/7/.*` returns every control
  point under that prefix, one response line each. An alternative to
  array-shaped responses worth knowing about, and worth rejecting for a
  measurement API: a regex evaluated against a namespace is an unbounded
  amount of server work chosen by the caller.
- **OSC argument types accepted are `i f s F T h`** — note **no `b` (blob)**.
  GALAXY's surface is scalars.
- **Security: none.** No authentication anywhere in the protocol. Meyer's own
  disclaimer pushes the problem to the operator, noting GALAXY "can provide
  very loud sound pressure levels to the audience" and recommending network
  discipline before remote control.

What *is* proprietary: the Compass ↔ GALAXY session layer beyond the published
control plane, the RMS/RMServer monitoring protocol, and Compass Go /
Spacemap Go. No public spec found — **UNVERIFIED**.

### Who would actually consume this API in a rig

- **Q-SYS — and it does not need us to speak QRC.** Q-SYS publishes three
  control surfaces (ECP, ASCII, TCP 1702; QRC, JSON-RPC 2.0 NUL-terminated,
  TCP 1710; QRWC, WebSocket 443, beta). But Q-SYS Control Scripting ships a
  Lua **`HttpClient`** with `Download{}` / `Upload{}` against arbitrary URLs,
  HTTP and HTTPS, TLS 1.0-1.3, optional `User`/`Password` with
  `Auth = any|basic|digest|digest_ie`, a `Timeout`, a `Headers` table, and an
  async `EventHandler(table, code, data, error, headers)`. **A Q-SYS
  integrator can poll a plain HTTP+JSON endpoint on a timer with no protocol
  work on our side at all**, and a Bearer token is expressible through the
  `Headers` table. Implementing QRC here would mean building a JSON-RPC server
  to serve a client that already speaks HTTP. Two Q-SYS ideas are worth
  copying conceptually: the **60-second liveness contract** and
  **`ChangeGroup.Poll`, which returns only what changed since the last poll**.
- **Dante / Audinate — out of scope, and here is why.** The **Dante Managed
  API** is real and documented: **GraphQL** at `https://<ddm>/graphql` or
  `https://api.director.dante.cloud/graphql`, authenticated with **API keys**
  minted in the DDM/Director UI (non-expiring, revocable, inheriting the
  creating user's privileges). But it works only on systems managed by
  **DDM v1.5+**; an unmanaged Dante network has no API access, and legacy-mode
  devices are unsupported. It is also about routing and device health, not
  measurement. There is an Audinate "Dante API for Desktop Platforms" whose
  existence is visible only through a third-party-licence notice —
  **UNVERIFIED** as to capability or terms. Nothing here consumes or produces
  measurement data. Rule it out and say so; it is L8's G19, not this lane's.
- **OSC's real audience is real**: QLab (its own dictionary plus Custom OSC
  cues), Reaper, TouchOSC, Lemur, Behringer/Midas X32 and X-Air (UDP 10023),
  and GALAXY itself. The downsides are equally real and sourced: OSC 1.0 has
  **no discovery and no schema** (which is why OSCQuery and Libmapper exist as
  bolt-ons); **no request/response** — no correlation id, no status code, no
  error channel, so every implementation invents one; and UDP gives **no
  delivery or ordering guarantee** (Fraietta, NIME 2008, gives the worked
  example of a direction message and a start message arriving reversed,
  "worse than not receiving the information at all"). A concrete interop
  hazard: TouchOSC reportedly does not send bundles while Lemur does.

### OSC over UDP cannot carry a spectrum, and the arithmetic says so

| Quantity | Value |
|---|---|
| UDP payload in one non-fragmented Ethernet frame | 1500 − 20 (IPv4) − 8 (UDP) = **1472 bytes** = **368 float32** and nothing else |
| Absolute UDP payload ceiling (RFC 768's 16-bit Length field) | 65535 − 8 − 20 = **65507 bytes** |
| This project's default curve | `fftSize/2+1` = **2049** points → **8196 bytes** for one float32 field |

RFC 8085 §3.2 is explicit: an application "SHOULD NOT send UDP datagrams that
result in IP packets that exceed" the path MTU, MUST subtract the IP and
8-byte UDP headers, and where PMTU is unknown should fall back below EMTU_S —
**576 bytes for IPv4**, 1280 for IPv6.

The failure mode decides it, not the number. A fragmented datagram reassembles
**all-or-nothing**: lose one of six fragments on a shared show network and the
whole 20 Hz frame is discarded, with no retransmit and no way for the receiver
to know. The effective loss rate is worse than the link's own.

**The `b` blob type does not rescue it, and neither do bundles.** OSC 1.0's
blob (int32 count, bytes, 0-3 pad) is the *correct* encoding for a float array
and far better than 2000 `f` arguments — but it is an encoding, not a
transport. And OSC 1.0 says "the contents of an OSC packet must be either an
*OSC Message* or an *OSC Bundle*": a bundle lives **inside** one packet. Five
400-float messages in a bundle is one 8000-byte datagram that fragments
exactly as before. OSC has no sequence number, no fragment index and no
reassembly layer; any chunking is application-invented, and once the receiver
handles out-of-order and missing chunks it has rebuilt a worse TCP.

One more interop hazard if OSC is ever shipped here: **OSC-over-TCP has two
incompatible framings in the wild** — 1.0's int32 size preamble, and 1.1's
required SLIP (RFC 1055) double-END encoding (Freed & Schmeder, NIME 2009).
OSC 1.1 also defines DNS-SD discovery as `_osc._udp` / `_osc._tcp` with TXT
keys `txtvers`, `version`, `framing`, `uri`, `types`, which is a ready-made
mDNS advertisement if discovery is ever built.

**Where that leaves OSC**: a reasonable *secondary* surface for **scalars** —
broadband SPL, Leq, a coherence figure, a delay result, a handful of band
levels — which fit one datagram at 20 Hz with room to spare, for an audience
(QLab, TouchOSC, GALAXY-literate integrators) that genuinely exists. It is the
wrong transport for spectra.

### SysTune — carried from the L6b pass, not re-read here

`docs/research/2026-09-06-l6b-station1-research.md:209-213`: HTTP served by a
**bundled NGINX**, bound to the host IP on a port such as 9000, **no auth**, no
push. This pass did not re-read AFMG's material, so treat it as the L6b pass
left it. It is worth one sentence anyway: SysTune's answer to "we need an HTTP
server" was **to ship a whole general-purpose web server inside the product**,
which is the upper bound on what this decision could cost if it is made
casually.

### Where the sources disagree, and what each disagreement decides

1. **Bind default: localhost (REW) vs every adapter including wireless
   (Smaart) vs link-local broadcast+multicast (OSM).** Three products, three
   answers, and **none of the three authenticates by default**. The owner has
   already ruled localhost, so this pass does not reopen it — but the reason
   matters for the record: localhost is the only one of the three where
   "no auth by default" is not a contradiction.
2. **Where the read/write boundary is expressed: in the HTTP method (REW), in
   a message field (Smaart), or nowhere at all (OSM).** This is the real
   decision. Smaart's single `action` field makes a read-only *gateway*
   trivial (allow `get`, drop the rest) and makes read-only-ness
   inexpressible to a firewall, a proxy, or a browser. REW's method split is
   expressible in all three and is undermined only because REW put real
   actions behind `POST …/command`. For an API whose entire v1 is read-only,
   **GET-only over HTTP is the stronger boundary**, precisely because it can
   be enforced by something that is not this program.
3. **Push: none (Smaart), outbound webhook (REW), multicast spray (OSM).**
   Every one of the three is a different failure mode. Smaart's absence
   produces the documented stale-state bug. REW's webhook is an SSRF pivot.
   OSM's multicast has no rate limit and no coalescing — change notifications
   scale with source count times analysis rate.
4. **Rate: published and client-negotiated (Smaart, 23 FPS max, 2000 ms
   command timeout) vs undocumented (REW) vs hard-coded in two constants
   nobody can change (OSM, 1000 ms server / 250 ms client).** Smaart is the
   only one that treats bandwidth as the client's budget to declare.
5. **Frequency axis on the wire: explicit per row (OSM) vs reconstructed from
   parameters (REW).** OSM ships a frequency with every bin even though its
   own averaging cannot use one; REW ships `startFreq` + `ppo` and makes the
   client rebuild the axis. This project has **both kinds of curve at once** —
   `TransferBlock`, whose axis is `i·sampleRate/fftSize`, and `MtwBlock`,
   which carries an explicit `frequencyHz` vector precisely because its axis
   is not uniform (`Snapshot.h`, `MtwBlock`'s own comment). So neither prior
   art generalises, and the wire format has to say which kind it is holding.
6. **Numeric encoding: JSON numbers (OSM) vs Base64 float32 (REW).** OSM
   widens float to double to print it; REW ships the bytes. At this project's
   sizes — `fftSize/2+1` = 2049 points at the default 4096, three fields —
   this is the difference between roughly 40 KB and roughly 8 KB per curve per
   poll.

## Part B — Transport candidates under JUCE 9.0.1

### JUCE ships no HTTP server. Verified against the 9.0.1 checkout, not the docs

A grep across all 24 modules of the pinned checkout
(`D:\DEV CAVE EP3\PROJECT005-AZ-handsfree\external\JUCE`, `JUCE_MAJOR_VERSION 9`
/ `MINOR 0` / `BUILDNUMBER 1`) for a `class` **or `struct`** name containing
`Server` returns exactly **three** hits: `SVGPaintServer` (an SVG internal),
`juce::InterprocessConnectionServer`, and `HubPipeServer`
(`modules/juce_graphics/native/juce_Direct2DMetrics_windows.h:264`,
`struct HubPipeServer : public InterprocessConnection` — a Direct2D
debug-metrics named pipe, which a `class ...Server` grep misses because it is
declared `struct`). There is no HTTP server class in any module. What JUCE does
ship:

| Class | What it actually is |
|---|---|
| `juce::StreamingSocket` | Raw TCP. `createListener(port, localHostName)`, `bindToPort(port, localAddress)`, and `waitForNextConnection()` which **blocks** — you supply the thread. No HTTP parsing, no routing, no keep-alive, no timeouts |
| `juce::DatagramSocket` | UDP |
| `juce::URL` / `juce::WebInputStream` | HTTP **client** only |
| `juce::InterprocessConnection(Server)` | A real server, speaking JUCE's own length-prefixed framing — **not** HTTP. No browser, `curl` or JS client can talk to it |
| `juce::NetworkServiceDiscovery` | UDP broadcast advertise/discover. Announces a port; does not serve one |
| `juce_osc` — `OSCSender`, `OSCReceiver`, `OSCMessage`, `OSCBundle`, `OSCAddress` | OSC over UDP |
| `WebBrowserComponent::Options::withResourceProvider` (JUCE 8+) | Serves resources to an **embedded** WebView. Not a socket; no external process can reach it |

`juce_osc.h` declares `version: 9.0.1`, `license: AGPLv3/Commercial`,
`minimumCppStandard: 17`, `dependencies: juce_events` — the same dual licence
as every other JUCE 9 module, so it costs this AGPL project nothing new.
`OSCReceiver` **runs its own thread** (`Pimpl : Thread`, owning a
`DatagramSocket`, started by `connect()`), and its listener dispatch is
templated on a callback policy: `MessageLoopCallback` is the **default**
("called on the application's message loop"), `RealtimeCallback` runs "directly
on the network thread" and is documented for callbacks that "don't do much, but
are realtime-critical". Both `addListener` overloads exist, so the choice is
explicit at registration — which matters, because the message-thread default is
the one that would queue behind a repaint.

### The library comparison, licences read from the LICENSE files

| Library | Licence (read 2026-09-16) | AGPLv3-compatible? | Verdict |
|---|---|---|---|
| **cpp-httplib** 0.56.0 | **MIT** | Yes | Candidate |
| **civetweb** | **MIT** (default build; Lua/SQLite arms are opt-in and OFF) | Yes | Fallback |
| **Mongoose** | **GPL-2.0-only** + paid commercial | **NO** | **Disqualified** |
| **llhttp** | MIT | Yes | Parser only — no sockets, no threads |
| **Boost.Beast** | BSL-1.0 | Yes | Drags Boost + Asio into a project with neither |
| **uWebSockets** | Apache-2.0 (uSockets **UNVERIFIED**) | Yes (v3 family only) | Built for tens of thousands of sockets |
| **Crow** | BSD-3-Clause | Yes | Web framework; pulls Asio |
| **Drogon** | MIT | Yes | Full stack with ORM and sessions |

**Mongoose is the dual-licence trap the brief asked for, and it is worse than
it looks.** Its `LICENSE` offers "the GNU General Public License version 2"
with **no "or any later version" clause**, or a commercial licence. GPL-2.0-only
is incompatible with GPLv3 and therefore with AGPLv3 (FSF: "GPLv2 is, by
itself, not compatible with GPLv3"), and the usual escape — the "or later"
option — is not granted. Buying the commercial licence does not fix it either:
the commercial arm conflicts with the AGPL source release this project is
committed to. Neither arm works. Note also that **civetweb exists because of
this**: it is the MIT fork of Mongoose, taken in August 2013 from the last MIT
Mongoose, precisely when Mongoose relicensed.

**TLS is where a second licence sneaks in.** cpp-httplib compiles every
OpenSSL include inside `#ifdef CPPHTTPLIB_OPENSSL_SUPPORT` and hard-errors
below OpenSSL 3.0.0 if you ask for it; leave the macro undefined and there is
no third-party link dependency at all. civetweb's `CIVETWEB_ENABLE_SSL` is
**ON by default** and has to be turned off deliberately.

### Arguing against the obvious choice (cpp-httplib), as the brief requires

Four charges, each answered with a measured or read fact rather than an
impression:

1. **Compile cost — the charge lands, and the number was measured on this
   machine's own toolchain.** MSVC 19.51.36248, `/std:c++20 /EHsc /O2 /c`,
   wall clock: an empty TU is 0.13 s; a TU including only `<regex>`,
   `<thread>` and `<iostream>` is 0.96 s; a TU including `httplib.h` and
   instantiating `httplib::Server` is **9.55 s cold, 5.97-6.83 s warm**.
   Preprocessed output is **210,318 lines / 9.5 MB**. The header is 22,885
   lines with 94 `#include`s including `<regex>`. Single runs, so ±20% — the
   durable finding is the **6-7× ratio against a `<regex>`-heavy TU**, not the
   seconds. Mitigation is built in and documented: `./split.py` emits
   `httplib.h` + `httplib.cc`, and the cost is then paid in exactly **one**
   translation unit. This project's own file-length culture already says one
   file, one job; one TU, one dependency is the same discipline.
2. **Thread model — the charge half-lands.** It is a thread **pool**, not
   thread-per-connection: one accept loop on the `listen()` thread, each
   accepted socket enqueued. But the pooled worker runs
   `process_server_socket_core`, which *loops while keep-alive holds* — a
   pooled thread is occupied for the life of a **connection**, not a request.
   Defaults: `THREAD_POOL_COUNT = max(8u, hardware_concurrency()-1)`,
   `MAX_COUNT = COUNT*4`, `IDLE_TIMEOUT = 3 s`, `LISTEN_BACKLOG = 128`. At one
   or two polling clients this is a non-issue; with many idle browser tabs it
   is exactly what would exhaust the pool. `svr.new_task_queue` overrides it
   at runtime, so the answer is to set a small explicit pool, not to reject
   the library.
3. **TLS — the charge does not land**, because v1 does not want TLS. See
   above: undefined macro, no dependency. (NCC Group's "use TLS even on
   localhost" is a real recommendation and is answered in Part D, not here.)
4. **Binary size — not measured, and deliberately not asserted.** What *was*
   read: it is HTTP/1.1 only, blocking socket I/O, no HTTP/2 or HTTP/3. For a
   localhost read-only API that is a feature. Anyone who wants a size number
   must measure it; this pass did not.

Two operational facts worth carrying forward because they are foot-guns:

- **Bind with the literal `"127.0.0.1"`, never `"localhost"`.** cpp-httplib's
  own README warns that on Windows with misconfigured IPv6, resolving
  `localhost` can cost **up to 2 seconds per request**. At a 20 Hz poll that
  is not a performance note, it is a broken product.
- **Keep-alive defaults recycle the connection every 5 s at this rate.**
  `KEEPALIVE_TIMEOUT_SECOND = 5`, `KEEPALIVE_MAX_COUNT = 100`, server read and
  write timeouts 5 s, `PAYLOAD_MAX_LENGTH = 100 MB`, `MAX_LINE_LENGTH = 32768`.
  100 requests at 20 Hz is five seconds, so a polling client is forced through
  a TCP handshake every five seconds unless `set_keep_alive_max_count()` is
  raised.

civetweb's equivalents, for the fallback: `num_threads` **50**,
`prespawn_threads` **0** (despite the name, nothing is pre-spawned — workers
spawn lazily), `listen_backlog` 200, `connection_queue` 20,
`enable_keep_alive` **"no"** (off by default), `request_timeout_ms` 30000,
`keep_alive_timeout_ms` 500. Binding loopback only is
`listening_ports = "127.0.0.1:8080"` (`[::1]:8080` for IPv6 loopback). Its
CMake needs four defaults changed for this use: `ENABLE_SSL` OFF,
`BUILD_TESTING` OFF, `ENABLE_SERVER_EXECUTABLE` OFF, and
`SERVE_NO_FILES` / `DISABLE_CGI` **ON** — **CGI is enabled by default in an
embedded server**, which is attack surface a JSON-only API has no use for.

### Hand-rolling HTTP on `StreamingSocket` — costed, and rejected

The honest cost is not "parse a request line". It is: an accept thread, per-
connection read buffering with a header-size cap, request-line and header
parsing with the whitespace and folding rules, `Host` validation, chunked vs
`Content-Length` response framing, keep-alive state and its idle timer, read
and write timeouts, 400/404/405/413/429/500 paths, and graceful shutdown that
does not leave `waitForNextConnection()` blocked. That is several hundred lines
of code whose bugs are security bugs, against a **400-line hard cap per file**
and a global rule that says reuse existing code and prefer the standard library
to a new dependency (`CLAUDE.md`; global rule 10). The rule argues **for** the
MIT header here, not against it: the smallest change that satisfies the task is
the one that does not reimplement HTTP.

## Part C — This repo's surface, as of `6d9a53d`, re-checked at main `e213202`

*Line numbers below were read at `6d9a53d` and re-checked after merging
`origin/main` = `e213202` (PRs #10 and #13). Where the two disagree the
`e213202` number is the one written, and it says so inline. Two citations
moved: the framework-deps regex (`:49` → `:54`) and
`measure_has_no_framework_deps` (`app/tests/CMakeLists.txt:274` → `:280`).*

### There is still no network code, and the guard already names one library

`grep` for `juce_osc` / `StreamingSocket` / `DatagramSocket` /
`WebInputStream` / `InterprocessConnection` / `httplib` / `civetweb` / `asio`
across `app/ platform/ core/ tools/ ui/` returns nothing outside
`app/CMakeLists.txt`'s optional ASIO **audio** SDK probe and two unrelated
prose comments. `rtatool` links `juce_audio_utils`, `juce_gui_extra`,
`juce_opengl` (`app/CMakeLists.txt:143-152`) and nothing else. The finding the
L6b pass recorded at `60ba99c` still holds at `6d9a53d`.

`core/tests/check_no_framework_deps.cmake:54` at main `e213202` — the sole
`if(content MATCHES ...)` in the file, so grep for `content MATCHES` rather
than trusting the number — matches
`#include <(juce|JuceHeader|Q[A-Z]|portaudio|RtAudio|asio)`. That regex already
blocks **standalone or Boost Asio** from `core/` and `platform/types/` by name,
which silently eliminates every server library built on Asio (Boost.Beast,
Crow, Drogon) from those two layers — not as a policy anyone wrote for this
lane, but as a fact the lane inherits.

### The publish model the API thread must read through

- `rta::measure::SnapshotSource::latest()` returns
  `std::shared_ptr<const Snapshot>` (`app/src/measure/SnapshotSource.h:19`).
  `AnalysisThread` implements it (`AnalysisThread.h:57, 84`) and is the live
  source; `StaticSnapshotSource` is the offline one.
- The publish is one `AtomicSharedPtr` store, at most **20 Hz**:
  `AnalysisThread::publishIfDue` gates on `kMinPublishIntervalMs`
  (`AnalysisThread.cpp:316-321`), and its own comment fixes the rate — "at
  most 20 times a second -- never in the audio callback"
  (`AnalysisThread.cpp:323-325`).
- The views already poll at the same rate: `kTimerHz = 20` in both
  `app/src/view/RtaView.cpp:26` and `app/src/view/TransferView.cpp:34`.
  `MainComponent` itself runs a 2 Hz timer (`MainComponent.cpp:105`).
- **Neither `AtomicSharedPtr` path is lock-free on this project's own
  toolchain.** `AtomicSharedPtr.h`'s class comment records the measurement:
  on MSVC 14.51 the C++20 specialisation's `is_lock_free()` returns false and
  `is_always_lock_free` is false — it spins on a bit in the control block. The
  Apple libc++ fallback takes a lock out of an address-keyed table. The design
  is safe because of **who calls it** — analysis thread writes, message thread
  reads, the audio callback never touches it — not because the swap is free.
  An API thread is a **third** participant on that slot, and that is the fact
  the threading decision has to be made against.
- The audio callback is literally two calls after `ScopedNoDenormals`
  (`platform/src/AudioIo.cpp:116-148` at main `e213202` — the
  `audioDeviceIOCallbackWithContext` body, signature line to closing brace):
  `bus_.pushFromCallback(...)` and `output_.render(...)`.
  `platform/tests/check_callback_shape.cmake` greps that function. Nothing in this lane may add a third.

### What a read-only client could actually be served today

| Data | Where it lives | Reachable from an API thread? |
|---|---|---|
| Per-bin magnitude / phase / coherence, `effectiveAverages`, `appliedDelaySamples` | `Snapshot::transfer` (`Snapshot.h`, `TransferBlock`) | **Yes** — inside the published snapshot |
| MTW stitched curve + per-band descriptors (`seamHz`, `windowSeconds`, `integrationSeconds`, `coherenceAvailable`) | `Snapshot::mtw` (`MtwBlock`, `MtwBandDescriptor`) | **Yes** |
| Fractional-octave bands, spectrum, peak band, dropped samples | `Snapshot::bands` / `spectrumDb` / `peakBand*` / `droppedSamples` | **Yes** |
| Spatial average + `phaseAgreement`, `weightedCoherence`, `contributors`, `absence` | `Snapshot::average` (`AverageBlock`) | **Yes** |
| Per-position summaries incl. `membership`, `gatePassed`, `overloaded` | `Snapshot::positions` (`PositionSummary`) | **Yes** |
| Trace library: names, groups, visibility, shade | `rta::trace::TraceLibrary`, owned by `MainComponent` (`MainComponent.h:160`) | **No, not directly** — see below |
| Session id / routing / presets (schema 3) | `rta::trace::SessionDocument` (`SessionCodec.h:91-99`), `kSchemaVersion = 3` (`:30`) | **No, not directly** |
| EQ suggestions, alignment verdicts | `EqSession`, `AlignmentWizard` | **No — no live instance exists** |
| SPL / Leq | `core/include/rta/meter/Leq.h` | **No — no caller in `app/`** |

Two of those rows are load-bearing and both were checked by grep at `6d9a53d`:

- **`TraceLibrary` is message-thread-owned, mutable, and has no atomic
  publish.** It carries a `revision()` counter (`TraceLibrary.h:83`) and
  deletes copy and move (`:51-54`). An API thread reading it directly races
  every `rename` / `setVisible` / `soloOnly`. Anything the API exposes from the
  library has to be *published* the way a snapshot is, by the message thread.
- **`EqSession` and `AlignmentWizard` have no caller outside their own files
  and their tests.** `grep -rl` over `app/` hits only
  `app/src/measure/{EqSession,AlignmentWizard,AlignmentWizardSignals,EqVerify}.*`
  and `app/tests/*`. They are built and tested; they are not wired into the
  composition root. An endpoint that claims to return "the current EQ
  suggestions" would today be returning the state of an object nobody owns.
- **The three readout-rounding rules are already code, and already tested.**
  `CLAUDE.md`'s reading rules — whole hertz, one decimal of dB, two decimals
  for a 0..1 figure — live in `app/src/view/Readouts.h` (namespace
  `rta::view`, at main `e213202`) as `formatHz` (`:72`), **`formatTrim`**
  (`:79`) and **`formatAgreement`** (`:87`); grep handle
  `inline std::string format`. They are pinned by
  `app/tests/test_readouts.cpp:100-115`, `Readouts.h` is already a named entry
  in `measure_has_no_framework_deps`'s `GLOBS`
  (`app/tests/CMakeLists.txt:282`), and `formatHz` already has a live caller
  (`app/src/view/DevicePanel.cpp:102`). **This is a seam to reuse, not to
  rebuild**: the dB and Hz functions return the unit inside the string
  (`formatTrim(-3.2145123) == "-3.2 dB"`, `formatHz(1000.4) == "1000 Hz"`,
  `formatAgreement(0.9731445) == "0.97"`), and the names are not the ones an
  implementer would guess, so the record's §11 item 13 names all three
  explicitly and forbids a fourth formatter.
- **SPL is not in the live snapshot at all.** `Snapshot` carries dBFS
  (`BandReading::levelDb`, `kLevelFloorDb`); `calibrationOffsetDb` and
  `LevelUnit` exist only on a *stored* `CaptureMeta` (`Trace.h:39-40`).
  `rta::meter::Leq` has no `app/` caller. This matters directly to lane L6a,
  whose SPL web viewer (G7) is meant to ride this surface: **the surface can
  carry SPL only after the Meters track lands it in the snapshot.**

### Guard precedents this lane can reuse verbatim

- `core_has_no_framework_deps` / `platform_types_has_no_framework_deps` /
  `measure_has_no_framework_deps` — one script,
  `core/tests/check_no_framework_deps.cmake`, parameterised by `CORE_DIR` or an
  explicit `GLOBS` list plus `LABEL`.
- `no_std_atomic_over_shared_ptr` — the **allowlist** shape:
  `-DDIRS=<core>;<app>;<platform>;<ui>;<tools>` plus `-DALLOW=<one file>`
  (`core/tests/CMakeLists.txt:149-154` at main `e213202` — the
  `add_test(NAME no_std_atomic_over_shared_ptr ...)` block;
  **note the `${CMAKE_SOURCE_DIR}/` prefix on every `DIRS` entry and on
  `ALLOW`**, because `file(GLOB_RECURSE)` returns absolute paths and a
  relative `ALLOW` dies at the `if(NOT EXISTS "${ALLOW}")` check —
  `check_no_std_atomic_shared_ptr.cmake`).
  Its own comment states the tripwire doctrine: "a textual scan, not a C++
  parse … a tripwire for the obvious form, not a proof". That is exactly the
  shape a "no server library below `app/`" guard needs, and it also carries a
  **sentinel** check that fails if the guard has stopped watching its allowed
  file. **There are two sentinels, not one**, and a copy that takes only the
  first is weaker than the original it cites: `ALLOW IN_LIST SOURCES` (`:65`)
  proves the allowed file is inside the scanned set, and
  `ALLOW_CODE MATCHES "${SENTINEL_PATTERN}"` (`:145`, with `SENTINEL_PATTERN`
  `set()` at `:141` and `PATTERN` at `:134` — both literals in the script, not
  `-D` arguments) proves the allowed file **still contains** the thing it is
  the sole exception for. Both at main `e213202`.
- `app/tests` is registered **outside** the `RTA_BUILD_APP` guard
  (root `CMakeLists.txt:72`, `app/tests/CMakeLists.txt:3-9`, both at main
  `e213202`), so a JUCE-free file
  under `app/src` is testable in the `RTA_BUILD_APP=OFF` configuration — the
  one CI runs on three OSes. A serialiser that is JUCE-free and socket-free is
  therefore provable on CI; a server that owns a socket is not.
- `makeSyntheticSnapshot(const SyntheticSpec&)`
  (`app/src/measure/SyntheticSnapshot.h:44`) is **bit-identical for a given
  spec** — the precondition that already makes `rta-view.png` reviewable as a
  byte-for-byte diff. The same property makes a **golden JSON** possible with
  no sound card.

## Part D — Security posture for a read-only API bound to localhost

### The threat is DNS rebinding, and CORS is not a defence against it

This is the correction that most changes the design. A localhost-bound HTTP
API with no auth is **not** protected by the browser's same-origin policy:

1. The operator's laptop loads any attacker page.
2. The attacker's domain has a short-TTL DNS record that flips to `127.0.0.1`.
3. The page fetches `http://attacker.example:<port>/…`. The browser considers
   that **same-origin** — the origin is the hostname, not the resolved IP — so
   the same-origin policy does not apply and **CORS never enters the
   picture**. The attacker's JavaScript reads every response in full.

GitHub's security blog (3 April 2025) states the property that matters: DNS
rebinding "does not require a misconfiguration or bug". It works against a
correctly configured server.

**And CORS does not even stop the request.** Per MDN, a **simple request** —
`GET`/`HEAD`/`POST` with only safelisted headers and no upload listeners —
gets **no preflight**. The browser sends it, our server executes it, and only
*afterwards* does the browser refuse to hand the response to the script. A
plain `GET /api/v1/transfer` is a simple request. So "we set no
`Access-Control-Allow-Origin`" is not a security posture; it is at most a
posture about who may *read* the reply in the one case where the origin does
not already match.

The mitigations that do work, on which NCC Group's Singularity wiki (the
reference implementation of the attack) and the GitHub writeup agree:

1. **Validate the `Host` header against a strict allowlist, before routing.**
   NCC Group: the service "should check that all HTTP request 'Host' header
   values strictly contain '127.0.0.1:3000' and/or 'localhost:3000'. If the
   host header contains anything else, then the request should be denied."
   For this project: allow exactly `127.0.0.1:<port>`, `localhost:<port>` and
   `[::1]:<port>`, reject everything else with 403 before any handler runs.
   **A handful of lines, and the single highest-value control in the whole
   API.**
2. **Require a token — and it must not be a cookie.** GitHub: "adding simple
   authentication for all sensitive and critical endpoints will prevent this
   attack". The mechanism matters: it works because the attacker cannot *read*
   the token.

   **Against rebinding alone, a cookie would have worked too** — the same page
   says rebinding "cannot contain cookies". A cookie jar is keyed on the host
   *name*, and rebinding changes only what a name *resolves to*, so the browser
   attaches `attacker.example`'s cookies, never the ones this app set for
   `127.0.0.1`. Whatever defends against rebinding here, it is item 1's `Host`
   allowlist, not the token's format.

   The reason to refuse a cookie is a different and larger one: a cookie is an
   **ambient** credential. The browser attaches it to *every* request to
   `127.0.0.1:<port>` regardless of which page issued it, so any site the
   operator happens to visit during a show is authenticated to this listener —
   textbook CSRF, needing no DNS trick at all, and Part D's CORS note explains
   why the absence of CORS headers does not stop such a request from being
   *executed*. A `Bearer` header is not ambient: nothing attaches it but a
   caller that already knows the secret. So: Bearer header or an explicit token
   parameter, **never a cookie or a session**.
3. **NCC Group also recommends TLS even on localhost.** For a desktop app this
   is awkward — self-signed certificate warnings, and no valid name for
   `127.0.0.1` without shipping a certificate. Declining it is defensible;
   **the decline and its cost belong in the record**, not in silence.
4. NCC Group explicitly warns off relying on **DNS response filtering**
   (dropping loopback answers) as a primary defence.
5. A **named pipe / Unix domain socket** instead of a TCP port is the
   strongest isolation — no browser can reach it at all. It also makes the
   Q-SYS Lua `HttpClient` integration impossible, which is the main reason to
   serve HTTP in the first place.

### Chrome Local Network Access — real, new, and not load-bearing

Formerly Private Network Access. Shipped in **Chrome 142, released
28 October 2025**, enforced across Chromium browsers; available behind
`chrome://flags#local-network-access-check` from Chrome 138. It restricts "the
ability of websites to send requests to servers on a user's local network
(including servers running locally on the user's machine)", covering
`127.0.0.0/8`, `::1/128`, RFC1918 ranges and `.local`. The trigger is a public
site → local IP or loopback, or a local site → loopback. Behaviour is a
permission prompt ("Look for and connect to any device on your local
network"); denial fails the request. It applies to `fetch()`, XHR, subresource
loads and iframe embeds (an iframe additionally needs
`allow="local-network-access"`); WebSocket, WebTransport and WebRTC coverage is
stated as future work. `fetch(url, { targetAddressSpace: "local" })` exempts a
request from mixed-content blocking on an HTTPS page.

Two reasons it cannot be load-bearing here: it is a **browser** control and
does nothing against a native client, a script, or any non-browser HTTP
library; and it is under a year old, so it is not universal across whatever
browser a touring engineer is carrying. **UNVERIFIED**: whether a page served
*from* 127.0.0.1 fetching 127.0.0.1 is exempt follows logically from the
same-address-space model but was not found stated verbatim — it must be tested
before any browser UI depends on it. That test is a precondition for L6a's SPL
web viewer.

### OWASP, mapped onto this API

From the **REST Security Cheat Sheet**: HTTPS-only endpoints (the one item
this project knowingly deviates from on loopback — see above); an **allowlist
of permitted HTTP methods** with **405** for everything else, which for a
read-only API is `GET`, `HEAD`, `OPTIONS` and is itself a meaningful CSRF
reduction; "Do not trust input parameters/objects. Validate input: length /
range / format and type"; a request size limit with **413**; **406**/**415**
on unexpected content type; **429** when requests arrive too quickly. Its
positive CORS guidance, for when headers *are* set: allowlist specific
origins, never `*`, never `null`, never reflect `Origin` back unvalidated, and
never use `Origin` alone for access control — a non-browser client spoofs it
trivially.

From **API4:2023 Unrestricted Resource Consumption**: rate-limit per
timeframe, throttle per operation, cap the size of every incoming parameter,
set **execution timeouts**, and — the item that bites hardest here — add
"proper server-side validation for query string and request body parameters,
specifically the one that controls the number of records to be returned".

**That last one is a real-time-safety control in this product, not security
hygiene.** An endpoint shaped like `/spectrum?bins=N` must clamp `N`
server-side. Uncapped, it is not merely a memory amplifier: it is a way for a
remote caller to make **this program** do unbounded work while a show is
running. The whole product exists to prevent dropouts at a live show. So the
bin cap and the rate limit belong in the same category as the audio-callback
rule, and the record has to place them on the publish path rather than only in
an HTTP handler.

## Part E — What a read-only client needs, and in what units

Constraints that are not negotiable, from `CLAUDE.md` "Reading out numbers":
**frequency is a whole number of hertz**, **dB keeps one decimal**,
**coherence is 0..1 with two decimals**. These are display rules, and the
research question they raise is whether the *wire* should carry rounded values
or full precision. Both prior arts disagree with each other and with
themselves: OSM widens float32 to double to print it (pure payload cost, no
information), REW ships raw big-endian float32 bytes in Base64 and leaves
rendering entirely to the client.

The one thing the evidence settles is what **not** to do: OSM's positional,
unlabelled, un-versioned arrays rot exactly as predicted — column 5's meaning
lives only in a commented-out line, and enums are flattened to integers with
no name, so a renumbering silently changes meaning with nothing on the wire to
reveal it.

What a client would want, ordered by whether this repo can actually serve it
today (Part C's table is the evidence):

| Want | Serveable at `6d9a53d`? |
|---|---|
| Live magnitude / phase / coherence, per bin, with `effectiveAverages` and `appliedDelaySamples` | Yes, from `Snapshot::transfer` |
| MTW stitched curve with its explicit frequency vector and per-band descriptors | Yes, from `Snapshot::mtw` — and the per-band `coherenceAvailable` **must** cross the wire, or a client will read a filling bottom band as a measured zero |
| Fractional-octave bands, spectrum, peak band | Yes |
| Spatial average with `phaseAgreement`, `weightedCoherence`, `contributors` and per-bin `absence` | Yes — and `absence` is an enum, which is exactly the field OSM's integer-flattening would have corrupted |
| Position summaries with `membership`, `gatePassed`, `overloaded` | Yes |
| Session id, trace library list | Only through a **new** message-thread publish — `TraceLibrary` is mutable and unpublished |
| Solver suggestions from EQ / DELAY / ALIGN | **No** — `EqSession` and `AlignmentWizard` have no live instance |
| SPL / Leq | **No** — not in `Snapshot`; `rta::meter::Leq` has no `app/` caller |

Two change-detection precedents are worth more than they look:
`Snapshot::sequence` already "increments by exactly one on every
`Analyser::publish`" and is documented as "the only field a reader needs to
notice 'there is something new' without comparing pointers"; REW's
`/rta/captured-data` carries a nanosecond timestamp plus a running sample
count for the same purpose; Q-SYS's `ChangeGroup.Poll` returns only what
changed since the last poll. All three say the same thing: **publish the
counter, and let the client decide whether to re-fetch.**

## URL ledger — everything read for this pass, all on 2026-09-16

| Source | What it established |
|---|---|
| `api.github.com/repos/psmokotnin/osm/git/trees/master?recursive=1` + `/commits/master` | OSM tree; HEAD `1e08de2`, authored 2025-09-13 |
| `raw.githubusercontent.com/psmokotnin/osm/master/src/remote/{network,server,remoteclient,tcpreciever,item}.{h,cpp}`, `src/remote/items/measurementitem.cpp`, `src/remote/generatorremote.h`, `src/meta/metabase.h`, `src/source/measurement.h`, `src/abstract/source.h`, `OpenSoundMeter.pro`, `LICENSE` | Every OSM fact in Part A and Part 0 |
| `roomeqwizard.com/help/help_en-GB/html/api.html` | REW transport, `localhost`/4735, `-api`/`-port`/`-host`, endpoint list, Base64 big-endian float payloads, phase in degrees, subscription-by-callback-URL, blocking mode, Swagger at `/doc.json` and `/doc.yaml`, no auth, no CORS statement |
| `roomeqwizard.com/api/` | **404** — the path named in the brief does not exist |
| `avnirvana.com/threads/rew-api-tips-tricks.13101/` | Corroborates port and enabling; forum, not official |
| `github.com/audiomacgyver/rew-iqc` | Independent client: blocking mode unreliable for measurement commands; polls instead |
| `rationalacoustics.com/pages/integration-with-3rd-party-products` | Smaart API is JSON over WebSocket; SDK free on request |
| `support.rationalacoustics.com/…/150000092223-…` | Enable under Options > Preferences > API; listens on **all** adapters including wireless; UDP broadcast discovery |
| `support.rationalacoustics.com/…/150000183495-…` (Client Window) | Port 26000; optional password; client cannot read the host's captured-trace library |
| `support.rationalacoustics.com/…/150000090545-…` (SPL Webserver) | **The SPL Web Viewer is plain HTTP on the same port 26000, same optional password** |
| `github.com/bitfocus/companion-module-rationalacoustics-smaart3` — `src/index.js`, `companion/HELP.md` | The only public source for Smaart's wire shape: `ws://host:port/api/v3/`, default port 26000, `authenticationRequired` → `{action:'set',properties:[{password}]}`, `{sequenceNumber, action, target, properties}`, actions `get`/`set`/`capture`/`issueCommand` |
| `rationalacoustics.com/pages/smaart-api-sdk-terms-and-conditions` | SDK free, no redistribution of the SDK, derived applications may be commercial and closed |
| `downloads.rationalacoustics.com/documentation/smaart-v8/Smaart-v8-User-Guide.pdf` pp. 82-83, 98-100 | Port 26000 default; blank password = none; firewall prompt; client-or-server-not-both; **Spec/TF Stream FPS max and default 23**; Command Timeout default 2000 ms; Live IR Range bandwidth note; client feature limitations |
| `downloads.rationalacoustics.com/documentation/smaart-v9/Smaart_LE_v9.1_User_Guide.pdf` p. 55 | Smaart LE exposes a configurable IP / Hostname / Port — implies v9 allows choosing the bind address |
| `support.rationalacoustics.com/…/150000070760-…`, `…/150000069118-…` | Smaart Suite/RT user guides listed "coming soon" — **no v9 Suite API chapter exists to check** |
| `docs.meyersound.com/products/docs/galileo-galaxy_pg_b4.pdf` | The whole GALAXY section: ports 25003/25004, TCP and UDP, control-point namespace, regex addressing, subscribe with rate (default 30 ms, range 0-100 ms), 30 s UDP subscription expiry with `/ping`, accepted OSC types `i f s F T h`, bundles accepted, ASCII `:` built-ins and CR/LF, **no authentication**, the loud-SPL disclaimer |
| `docs.meyersound.com/products/en/user-guide---galileo-galaxy.html`, `meyersound.com/news/compass-4-6/` | Milan certification; AMX/Crestron integration is this same surface; Compass 4.6 first with Milan |
| `help.qsys.com` — External Control APIs overview, QRC overview, QRC commands, Remote Connectivity, **Lua `HttpClient`** | ECP TCP 1702 ASCII; QRC TCP 1710 JSON-RPC 2.0 NUL-terminated; QRWC WebSocket 443 beta; 60 s keepalive with `NoOp`; `ChangeGroup.Poll`/`AutoPoll` delta subscription; **Lua `HttpClient` can poll arbitrary URLs with headers, basic/digest auth, timeout and an async handler** |
| `dev.audinate.com/GA/managed-api/userguide/pdf/latest/`, `global.audinate.com/products/dante-enabled/third-party-applications`, `audinate.com/legal/…/dante-api-for-desktop-platforms-open-source-licenses/` | Dante Managed API is GraphQL with revocable API keys, **only on DDM v1.5+-managed systems**; the desktop SDK's terms are UNVERIFIED |
| `github.com/CNMAT/OpenSoundControl.org/blob/master/spec-1_0.md` | OSC 1.0: blob `b` = int32 count + bytes + pad; a packet is one message **or** one bundle; UDP datagram is the natural representation; TCP needs an int32 size prefix |
| `opensoundcontrol.stanford.edu/files/2009-NIME-OSC-1.1.pdf` | `i f s b` still required; **TCP/serial now require SLIP (RFC 1055)** — two incompatible stream framings in the wild; DNS-SD `_osc._udp`/`_osc._tcp` with `txtvers`/`version`/`framing`/`uri`/`types` |
| `nime.org/proceedings/2008/nime2008_019.pdf` (Fraietta) | OSC does not accommodate lower-layer failure; UDP guarantees neither delivery nor ordering, with a worked reordering example; no discovery in base OSC |
| `rfc-editor.org/rfc/rfc8085.html` §3.2, `rfc-editor.org/rfc/rfc768.txt` | SHOULD NOT exceed path MTU; MUST subtract IP + 8-byte UDP headers; EMTU_S fallback 576 (IPv4) / 1280 (IPv6); 16-bit Length field → 65507-byte payload ceiling |
| `raw.githubusercontent.com/yhirose/cpp-httplib/master/{httplib.h,README.md,LICENSE}` | v0.56.0; MIT; 22,885 lines / 796,325 bytes; thread-pool defaults; keep-alive and timeout defaults; `listen(host, port)`; the `localhost`-on-Windows 2-second warning; HTTP/1.1 only, blocking I/O; OpenSSL optional and ≥ 3.0.0 |
| `raw.githubusercontent.com/civetweb/civetweb/master/{LICENSE.md,src/civetweb.c,docs/UserManual.md,CMakeLists.txt,README.md}` | MIT; option defaults at `civetweb.c:2117-2135`; `listening_ports = "127.0.0.1:8080"`; `ENABLE_SSL` ON by default; CGI on by default; the MIT-fork-of-Mongoose history |
| `raw.githubusercontent.com/cesanta/mongoose/master/LICENSE`, `mongoose.ws/licensing/` | **GPL-2.0-only**, no "or later", plus a paid commercial tier |
| LICENSE files for llhttp (MIT), Crow (BSD-3), Drogon (MIT), uWebSockets (Apache-2.0), Beast (BSL-1.0) | The compatibility table in Part B |
| `gnu.org/licenses/license-list.en.html` | MIT/BSD/BSL "compatible with the GNU GPL"; Apache-2.0 compatible with v3 but not v2; "GPLv2 is, by itself, not compatible with GPLv3" |
| `raw.githubusercontent.com/juce-framework/JUCE/9.0.1/LICENSE.md` + the local 9.0.1 checkout (`juce_osc.h`, `juce_Socket.h`, `juce_OSCReceiver.cpp`) | JUCE 9 modules are AGPLv3/commercial; `juce_osc` version 9.0.1; **no HTTP server class in any module**; `OSCReceiver` owns a thread; `MessageLoopCallback` is the default dispatch policy |
| `cheatsheetseries.owasp.org/cheatsheets/REST_Security_Cheat_Sheet.html` | HTTPS-only; method allowlist with 405; validate length/range/format/type; 413; 406/415; 429 |
| `api-security.owasp.org/editions/2023/en/0xa4-unrestricted-resource-consumption/` | Rate limiting, per-operation throttling, max parameter sizes, execution timeouts, **server-side validation of the record-count parameter** |
| `github.com/nccgroup/singularity/wiki/Preventing-DNS-Rebinding-Attacks` | Strict `Host` allowlist containing only `127.0.0.1:<port>`/`localhost:<port>`; require authentication; TLS even on localhost; do not rely on DNS response filtering |
| `github.blog/security/application-security/localhost-dangers-cors-and-dns-rebinding/` (3 Apr 2025) | Rebinding "does not require a misconfiguration or bug"; simple requests are sent without CORS headers; mitigations are auth + a `Host` check |
| `developer.mozilla.org/en-US/docs/Web/HTTP/CORS` | The simple-request conditions; the browser rejects the *response*, having already sent the request |
| `developer.chrome.com/blog/local-network-access` + Chrome 142 release notes | LNA covers `127.0.0.0/8` and `::1/128`; Chrome 138 flag, Chrome 142 (28 Oct 2025) launch; permission prompt; iframe `allow=`; `targetAddressSpace: "local"` |
| In-repo, at `6d9a53d` | Every claim in Part C, by file and line |

## UNVERIFIED ledger — what this pass could not read

1. **Smaart's data message format** for magnitude / phase / coherence — field
   names, units, encoding, framing. It lives in the SDK, which is free but
   request-gated behind a form; no form was submitted. **Nothing public
   documents it.** This is the one gap that touches the field REW cannot teach
   (coherence). If the encoding ever becomes contentious, requesting the SDK is
   a days-long round trip and should start early. Note the terms: the SDK may
   not be redistributed, so its contents could never be quoted into this repo's
   docs — only used.
2. **REW's CORS behaviour.** Undocumented either way. `curl -i -H "Origin:
   http://example.com" http://127.0.0.1:4735/version` against a running REW
   settles it. Browser-from-`file://` evidence does not, because a `file://`
   page has a `null` origin.
3. **REW's OpenAPI `info.version`** — served only from a running instance at
   `localhost:4735/doc.json`.
4. **Smaart v9 Suite bind-address configurability** — inferred from the LE 9.1
   guide; the Suite guide does not exist yet.
5. **Whether Smaart API v3 added change notifications**, or whether the
   documented "clients cannot detect changes made by the host or by other
   clients" still holds.
6. **SysTune's remote surface** — carried from the L6b pass, not re-read here.
7. **Whether a page served from `127.0.0.1` fetching `127.0.0.1` is exempt
   from Chrome's LNA prompt.** Logically implied, not found stated. A
   precondition for L6a's SPL web viewer, and testable in an afternoon.
8. **No canonical CWE id for DNS rebinding** was confirmed. Do not cite one
   without checking it.
9. **Binary-size cost of cpp-httplib or civetweb** — not measured. The
   compile-time numbers in Part B were measured; size was not, and is not
   asserted.
10. **uSockets' licence** (uWebSockets' dependency) — not fetched. Moot unless
    uWebSockets is reconsidered.
11. **Audinate's "Dante API for Desktop Platforms"** — visible only through a
    third-party-licence notice page; capabilities and terms unknown.
12. **Meyer's Compass ↔ GALAXY session layer, RMS/RMServer, Compass Go and
    Spacemap Go** — no public spec found.

## Open questions for a human

1. **Port number.** Nobody's default is free of collisions and no standard
   applies. The surveyed values are 4735 (REW), 26000 (Smaart), 49007 (OSM),
   25003/25004 (GALAXY), 1710/1702 (Q-SYS), 10023 (X32). Any unassigned
   high port works; the question is whether the owner wants a *fixed* default
   (discoverable, collides) or an ephemeral port written to a file the client
   reads (no collision, needs a rendezvous). This is a preference, not a
   finding.
2. **Does the LAN-bind opt-in ship in v1 as a disabled setting, or not exist
   at all until the later version?** The backlog defers the *feature*; it does
   not say whether the setting appears greyed out. A setting that exists and
   refuses is honest; a setting that does not exist cannot be turned on by
   accident.
3. **Is the read-only API allowed to expose the trace library at all in v1?**
   Doing so requires a *new* message-thread publish (Part C), which is real
   work in `app/` that nothing else needs yet. Live measurement alone needs no
   such thing.
4. **TLS on loopback**: NCC Group recommends it; it costs a shipped
   certificate and a warning the operator must click through. Declining is
   defensible and must be recorded as a decision, not an omission.
5. **Should the SDK request to Rational Acoustics be made now?** It is free,
   non-NDA per the published terms, and the only route to the one piece of
   prior art this pass could not read. It cannot be quoted into this repo
   either way.
