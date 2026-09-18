# 008 — Remote API, read-only (lane L-API)

*2026-09-18, lane closeout. L-API ran as four pull requests over three days:
stations 1+2 (research + record) on 2026-09-16, station 3 (the implementation
plan) on 2026-09-17, then station 4 in two waves — Wave 1 the pure half
(tasks A–G), Wave 2 the server (tasks H–K) — each built against an Opus plan
and refuted by an independent verifier with no `Write`/`Edit`. Research
`docs/research/2026-09-16-remote-api-station1-research.md`, record
[`docs/dsp/2026-09-16-remote-api.md`](../dsp/2026-09-16-remote-api.md)
including its §15 amendment `R1..R20`, plan
[`docs/plans/2026-09-17-remote-api-impl-plan.md`](../plans/2026-09-17-remote-api-impl-plan.md).*

**Lane state: BUILT and merged.** The last piece, Wave 2, merged as **PR #18 at
`91367a8`**. Nothing in L-API is waiting on an agent for the surface it set out
to build. Two things *are* waiting: a person, for §14 q.1 (§9 of this report),
and a follow-up session, for one CI failure this closeout found and did not fix
(§5 and §8).

**Read "BUILT" precisely.** It means eleven tasks proven by ctest, ten of them
in `RTA_BUILD_APP=OFF` — the only configuration CI runs — including the whole
request path over a real loopback socket, and re-measured by an **independent
rebuild** at the merged tree (§5). It does **not** mean an operator sees
anything: `api.enabled` ships **`false`** and there is no preferences UI, so
turning the API on is a one-line source edit and a rebuild (§7). That is the
shipped default working as designed, not an omission.

---

## 1. How it landed — four PRs, all merged

| PR | branch | merge commit | what |
|---|---|---|---|
| #11 | `remote-api/stations-1-2` | `a39a02e` | stations 1+2: research + decision record. Docs-only |
| #14 | `remote-api/station-3-plan` | `a02fb29` | station 3: eleven tasks A–K, reconciliations `API-R1..R17` + `R16a`. Docs-only |
| #16 | `remote-api/wave1-serialise` | `7b4773f` | station 4 Wave 1, tasks A–G: the pure half |
| #18 | `remote-api/wave2-server` | `91367a8` | station 4 Wave 2, tasks H–K: the server, the wiring, the guard |

Every comment on all four threads is authored by the repository owner's
account, so **builder and verifier are distinguishable only from the prose**:
verifier comments open with "Adversarial verification", declare no
`Edit`/`Write`, and say they re-measured in a throwaway worktree. A later
session reading `gh pr view N --comments` should not expect two GitHub
identities.

Two upstream merges are part of the lane's history because the tallies change
across them: PR #16's branch absorbed `main` at `00276cb` (PR #15, L6a's
station-3 plan), and PR #18's branch absorbed `main` at `b1e14a9` (PR #17, L6a
Wave 0). The second one matters for more than arithmetic — see §7.

**One thing this lane owed another lane and paid separately.** Resolving PR
#18's conflict in `app/tests/CMakeLists.txt` surfaced **mojibake on `main`**: a
section sign encoded as UTF-8 twice, four bytes where one belongs. The builder
kept it **byte-identical** through the resolution rather than repairing it
inside a conflict, on the grounds that silently fixing another lane's line in a
merge is how the trace of a defect disappears — and flagged it for a commit of
its own. That commit exists: **PR #19 at `d071269`**, `fix(cmake): repair a
double-encoded section sign in a comment`. It is the one-line fix CLAUDE.md
rule 6 exists to make possible.

---

## 2. What shipped

### The transport

**HTTP/1.1 + JSON over TCP, GET-only, version in the path.** OSC was rejected
at station 1 and the reason is a measurement, not a preference: a 2049-point
curve does not fit a 1472-byte datagram, and neither blobs nor bundles rescue
it. Read-only is expressed as **GET-only** rather than as Smaart's `action`
field, because a method is a boundary a firewall or a proxy can enforce and a
verb inside a message body is a boundary only this program can enforce.

`schemaVersion` travels **in every body**, not only in the path, because a body
that gets saved to a file or pasted into an issue must carry its own version.
As shipped it is a JSON field with the literal value **`1`** — there is no
named C++ constant; it is written out at `ApiSerialise.cpp:62`, `:133`, `:147`,
`:157` and `ApiSerialiseSpatial.cpp:187`, `:198`, `:209`. A later version
bump touches seven lines, which is worth knowing before someone assumes one.

### The library, its licence and its provenance

**cpp-httplib, MIT**, vendored as the **amalgamated header** at
`external/cpp-httplib/httplib.h`. Not `split.py` output, which the record
originally specified: one permitted includer has nothing to amortise over, and
a verbatim release file is **hash-checkable** where a generated one is not
(`API-R2`). It lives at `external/` rather than under `app/` because the guard
that permits exactly one includer globs `app/` (`API-R3`) — a vendored copy
inside `app/` would turn that guard red on the very library it exists to allow.

**Mongoose was disqualified on licence**, not on merit: GPL-2.0-**only**, with
no "or any later version", is incompatible with AGPLv3, and buying the
commercial licence does not fix the direction of the incompatibility.

| | cpp-httplib | nlohmann/json (**test-only**) |
|---|---|---|
| tag | `v0.56.0` | `v3.12.0` |
| sha256 | `1f99e51881c4c9d0649b27c611442c2f4d9bcfec5a22a14d5fcd1f8106f730b4` | `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63` |
| `wc -l` | 22875 | 25526 |
| licence | MIT, `LICENSE` sha256 `4b45cbe16d7b71b89ae6127e26e0d90a029198ca5e958ad8e3d0b8bbed364d8b` | MIT, `LICENSE.MIT` sha256 `46a65cffd1ea955132d95a8dd921640714a8d6b537d2e4e482d31145ae95b603` |
| includers | exactly one: `app/src/api/ApiServer.cpp` | exactly one: `app/tests/test_api_schema.cpp` |
| provenance | `external/cpp-httplib/PROVENANCE.md` | `external/nlohmann/PROVENANCE.md` |

Both hashes were measured **on the committed bytes** and against the release
asset, and the cpp-httplib hash was also checked against
`git show HEAD:external/cpp-httplib/httplib.h`. That discipline paid for itself
inside this lane: **a copy of `httplib.h` already on the build machine reported
the same `CPPHTTPLIB_VERSION "0.56.0"` and was a different file** — 22885 lines,
sha256 `a6e65d30…`. It was not used. **A version string is not an identity**,
and that sentence is now in the provenance file rather than in a session's
memory.

Every optional cpp-httplib backend is left **undefined** — no OpenSSL, mbedTLS,
wolfSSL, zlib, Brotli or zstd — so TLS is not merely declined in prose, it is
absent from the build. Everything the record tunes (timeouts, thread pool,
payload cap) is set at run time in `ApiServer.cpp`.

### The v1 surface — eight endpoints

`app/src/api/ApiRoutes.cpp:53-62`, a `std::array<RouteEntry, 8>`:

```
GET /api/v1/status      GET /api/v1/bands
GET /api/v1/snapshot    GET /api/v1/spectrum
GET /api/v1/transfer    GET /api/v1/average
GET /api/v1/mtw         GET /api/v1/positions
```

**Eight endpoints, and `/status`'s `available` list has six names.** Those are
different counts of different things and the confusion cost a record
correction (`API-R12`): `available` reports which *representations* this build
serves and does not name `/status` or `/snapshot`, which always exist.
`ApiRoutes.h:36-37` now says so where a reader will hit it.

The wire carries **full float32 precision** and does no display rounding —
CLAUDE.md's whole-hertz / one-decimal-dB rule is the *viewer's* job, because
rounding at the serialiser is a lossy transform applied to numbers a client may
want to re-analyse, and it would put a display decision in the transport layer.
Numbers are emitted by `std::to_chars` with no precision argument, i.e. the
**shortest round-trip decimal**. A non-finite value emits the literal `null`,
never `nan` or `inf`, which are not JSON and which take the whole document with
them when a conforming parser meets them. So **a client must treat every
numeric field as `number | null`** — a rule that lived only in code until a
verifier round put it in the record (§6 amendment).

### The security posture, as shipped

Not as designed — as coded, with the file and line, in the order the code runs
them:

| # | Control | Answer | Where |
|---|---|---|---|
| 0 | bind | `127.0.0.1` literal, port **4736** | `ApiSettings.h:35`, `:43`; bind at `ApiServer.cpp:384-389` |
| 1 | more than one `Host` field | **400** | `ApiServer.cpp:199-202` |
| 2 | `Host` allowlist, **pre-routing** | **403** | `ApiServer.cpp:219-222`, inside `set_pre_routing_handler` at `:187` |
| 3 | method allowlist | **405** + `Allow` | `ApiServer.cpp:236-240` |
| 4 | Bearer token | **401** + `WWW-Authenticate` | `ApiServer.cpp:245-249` |
| 5 | declared body size | **413** | `ApiServer.cpp:254-257` |
| 6 | rate limit | **429** | `ApiServer.cpp:279-285` |
| — | then routing; unknown path | **404** | httplib, after all six |
| 7 | no snapshot published yet | **503** | `ApiServer.cpp:312` |
| 8 | `If-None-Match` matches | **304**, no body | `ApiServer.cpp:319-323` |

**The `Host` allowlist is the primary defence, and the bind is not.** The
threat is DNS rebinding: an origin is a host *name*, not a resolved IP, so a
short-TTL domain that flips to `127.0.0.1` is same-origin as far as the browser
is concerned, the same-origin policy does not apply, CORS is inapplicable, and
the attacker's JavaScript reads every response in full. `hostIsAllowed`
(`ApiPolicy.cpp:69-106`) accepts exactly `127.0.0.1`, `localhost` and `[::1]`
(`:99`) with a **whole-token** port match (`portEquals`, `:51-56`), which is
what defeats the `127.0.0.1:4736.attacker.example` substring trap a naive
`find()` passes. It is a handful of lines and it is the highest-value control
in the API.

It compares against the port actually **bound**, not `settings.port`
(`ApiServer.cpp:219`). That is a plan correction the builder measured: with an
ephemeral bind `settings.port == 0`, while the `Host` header carries the port
the client really reached, so comparing against 0 would refuse **every request
that ever arrives**. For the shipped fixed port the two are identical.

**A forged `Host` on a path that does not exist answers 403, not 404** — the
ordering is the point, and it is asserted over a socket by I5
(`test_api_server.cpp:200`), not by reading the source.

**GET / HEAD / OPTIONS only**, and the `Allow` value is **one constant**,
`kAllowedMethods = "GET, HEAD, OPTIONS"` at `ApiServer.cpp:61`, read by the 405
path at `:238` and by every OPTIONS response at `:343` and nowhere else. Two
spellings of a method list are two lists that can disagree, and this lane
shipped that defect once before fixing it (§4). HEAD needs no registration —
httplib dispatches HEAD into `get_handlers_` — and OPTIONS is registered
explicitly, answering **204** with `Allow` and **reading no snapshot**, because
OPTIONS describes a resource and must answer identically before the first
publish, which is exactly where a GET correctly answers 503.

**No CORS, and no pretence that this is a posture.** `settings.corsOrigins`
exists and is read **nowhere**; a repo-wide grep for `Access-Control` in `app/`
and `core/` finds only two comments and two negative assertions
(`test_api_server.cpp:260-261`). The reasoning is in `ApiServer.cpp:287-293`
and it is worth keeping: a *simple* request — GET with safelisted headers —
gets no preflight, so the browser sends it and **this program executes it**;
only afterwards does the browser refuse to hand the response to the script.
Absent CORS headers govern who may *read* a reply. They govern nothing about
what runs.

**The rate limiter is a real-time-safety control, not hygiene.** One-second
sliding window (`ApiPolicy.cpp:206`), boundary `>=` (`:212`), default
`maxRequestsPerSecond = 30` (`ApiSettings.h:58`). It sits **last**, immediately
before the single snapshot load, and **OPTIONS is exempt** because OPTIONS
performs no load (`ApiServer.cpp:279`, argument at `:263-271`). **HEAD is not
exempt, and that half is the point**: HEAD routes to the same `Get` handler, so
it does the whole `latest()` and the whole serialisation — httplib strips the
body on the way out, *after* the work. "No body, so no cost" is the plausible
and wrong reading, and `ApiServer.cpp:273-278` says so where someone would
otherwise re-derive it.

**ETag / 304.** The version token is `Snapshot::sequence`, read once at
`ApiServer.cpp:316`; `etagFor` returns it **with the quotes included**
(`ApiPolicy.cpp:173-175`). `ETag` is set before the conditional check so a 304
still carries it. With both `If-None-Match` and `?since=` present and
disagreeing, **`If-None-Match` wins** (`ApiPolicy.cpp:177-197`).

**413 is enforced in two layers and the second is not redundant.** Layer one
reads `Content-Length` in pre-routing and refuses **before the body is read**,
which is what the record's control 3 actually meant. Layer two is
`set_payload_max_length` (`ApiServer.cpp:157`), for a **chunked** body that
declares no length and can only be discovered while reading. Dropping layer one
is how a GET declaring 2 GB gets read.

**The point cap** is `kDefaultMaxPointsPerResponse = 8192`
(`ApiSettings.h:16`), and `Request::points` **defaults to that constant rather
than to zero** (`ApiSerialise.h:32`) — a verifier finding, because a
default-constructed `Request` used to mean *uncapped*, and a cap that vanishes
when a caller forgets to clamp is not a cap.

### The real-time contract, as shipped

One `std::thread`, created and joined by the composition root, behind a
**pimpl**. `struct ApiServer::Impl` at `ApiServer.cpp:115`, `std::thread thread`
at `:138`, started at `:395`. The bind happens on the **calling** thread
(`:384-389`) before the thread starts, which removes the startup race and is
why `boundPort()` is knowable the moment the constructor returns. The pimpl is
not style: a by-value `httplib::Server` member would force `<httplib.h>` into
`ApiServer.h` and turn the server-library guard **red on a correct build**.

`std::thread` rather than `juce::Thread` is the single largest change any
verifier round made to this lane, and the reasoning is arithmetic: the
repository has **exactly one CI job and it is `RTA_BUILD_APP=OFF`**, so the
first draft — which put the whole network layer behind `ON` — proved the server,
including the `Host` check the record calls the highest-value control in the
API, on **zero machines**. cpp-httplib is pure std. Ten of eleven tasks are now
OFF and only the composition-root wiring is ON (`API-R15`).

**Exactly one `latest()` per request.** The only call site anywhere under
`app/src/api/` is `ApiServer.cpp:306`; the nine other occurrences in that tree
are comments. OPTIONS reads no snapshot at all. The expensive work —
thousands of floats into JSON — runs at `:326`, **after** the load, from the
`shared_ptr` copy, with the publish slot released. That ordering *is* the
decision.

**And the honest statement about it, which a later summary must not round up.**
"The API thread never blocks the analysis thread" is **wrong**. What is true is
that it does not hold a lock **across serialisation**. `AtomicSharedPtr` is
**not lock-free on this project's own toolchain** — its class comment records
the measurement at `app/src/measure/AtomicSharedPtr.h:92-101`: on MSVC 14.51
the C++20 path's `is_lock_free()` returns **false** and the specialisation spins
on a bit in the control block, and an earlier revision of that very comment
claimed otherwise and was wrong on the one toolchain every developer here uses
daily. So the atomic load itself **can** contend with the publish. The bound is
**arithmetic, not structural**: at most `maxRequestsPerSecond` loads per second.
That is why the limiter is a genuine safety control, and why it sits
immediately before the load rather than anywhere after it. Corrected in
`ApiServer.h:56-58`.

Nothing in this lane adds a call, a socket, an allocation, a lock or a branch
to the audio callback, and it could not: the server lives in `app/`, and
`platform/` cannot see `app/`. `git diff origin/main --stat -- platform/
core/src core/include ui/` is **empty** for the whole lane.

---

## 3. The files

`app/src/api/`, eleven files, 1767 lines, every one inside CLAUDE.md's 400-line
hard cap:

| file | lines | what |
|---|---|---|
| `ApiJson.h` | 121 | `number`, `stringValue`, `emittedCount`, `array` over `std::to_chars` |
| `ApiSettings.h` | 82 | the nine shipped defaults, and the point-cap constant |
| `ApiPolicy.h` | 146 | `hostIsAllowed`, `methodOf`, `bearerAccepted`, `clampPoints`, `bodyIsAcceptable`, `etagFor`, `conditionalVerdict`, `RateLimiter` |
| `ApiPolicy.cpp` | 223 | those, framework-free and server-library-free |
| `ApiRoutes.h` | 91 | `RouteEntry`, and which methods a resource answers |
| `ApiRoutes.cpp` | 66 | the eight-entry table and its adapters |
| `ApiSerialise.h` | 94 | `Request` and the eight declarations |
| `ApiSerialise.cpp` | 198 | `/status`, `/transfer`, `/spectrum`, `/bands`, `/snapshot` |
| `ApiSerialiseSpatial.cpp` | 216 | `/mtw`, `/average`, `/positions` |
| `ApiServer.h` | 113 | pimpl-only surface plus the RT contract |
| `ApiServer.cpp` | 417 | the only TU including `<httplib.h>` |

`ApiRoutes.{h,cpp}` exists because the PR #18 fix round pushed `ApiServer.cpp`
to **440 lines**, past the hard cap, and the cap was **not waived**. The split
follows the seam the plan had already named — validation versus routing — so
which paths exist and what each serialises is now asserted with **no server in
the picture**. An `ApiServerImpl.h` would have been the wrong split: it would
have had to carry `httplib::Server` and would turn the guard red on a correct
build, which is the same trap the pimpl exists for.

Tests: nine files under `app/tests/test_api_*.cpp`, 2007 lines, **76
`TEST_CASE`s**. Plus `app/tests/RawHttpClient.h` (218 lines) — a raw socket,
and that is not a preference: `httplib::Client` would be a **second** file
including `<httplib.h>`, which the guard permits exactly one of. It sends a
fixed string and reads until the peer closes; no parsing, no keep-alive, no
timeout. It does handle `reset`, because at the 413 case the server closes with
the body unread and TCP answers RST, and the bytes already received are what the
assertions read.

---

## 4. The decisions corrected along the way

A record that is never amended is a record nobody checked. This lane amended
its record **twenty-one times** (`API-R1..R17`, `R16a`, `R18..R20`) and the
following are the ones a later session would otherwise re-derive or re-break.

1. **The cookie argument was inverted, in four places, and the conclusion
   survived anyway.** The record, the research document and the owner's queue
   all said a rebound request "**is** same-origin and **would** carry cookies
   for that origin, so a cookie-based scheme fails against exactly the attack
   it was added for." That is backwards, and **backwards against the source it
   cited**: a cookie jar is keyed on the host *name*, and rebinding changes only
   what a name *resolves to*, so the browser attaches `attacker.example`'s
   cookies and never the ones this app set for `127.0.0.1`. The GitHub Security
   blog (3 April 2025) says rebinding "cannot contain cookies" — i.e. cookies
   *defeat* rebinding rather than fail against it. **"Bearer, never a cookie"
   still ships**, on the argument that was always the real one: **ambient
   authority / CSRF.** The browser attaches a cookie to every request to
   `127.0.0.1:<port>` regardless of which page issued it, so any site the
   operator visits during a show is authenticated to this listener, with no DNS
   trick at all — and §9's CORS paragraph is why absent CORS headers do not stop
   such a request from being *executed*. A `Bearer` header is not ambient.
   The fourth site was the serious one: it was the **question the owner was
   being asked**, and its framing asserted a fact its own source denied. It was
   **re-posed on the corrected premise with an explicit retraction paragraph**,
   because the owner may already have read the old version.
2. **Port 4737 → 4736.** §14 q.1 proposed 4737 as "currently unclaimed by any
   surveyed tool" — true of the surveyed *audio* tools, and **the one check
   that would have caught it was never run**. The IANA Service Name and
   Transport Protocol Port Number Registry carries `ipdr-sp,4737,tcp` and
   `ipdr-sp,4737,udp` (IPDR/SP, registered 2005-08); the registry jumps
   **4733 → 4737**, so 4734, 4735 (REW's own) and **4736** appear nowhere in it.
   Verified twice, the second time against a live 3,644,963-byte fetch. User
   Ports are not exclusive so 4737 would have worked — but a named default
   resting on an unperformed check is precisely what this project's method
   exists to prevent. `API-R14`. **The number is settled; the shape is not**
   (§9).
3. **A JSON parser had to be vendored, test-only, and finding that out is the
   highest-value thing any verifier did here.** Before it, **nothing in the OFF
   configuration asserted the emitted document was well-formed JSON**: every
   check was a substring match or a byte-compare against a file the same
   serialiser produced. **A stable but malformed document passed all of them.**
   Measured, with a trailing comma injected: `D1`–`D8` and `E1`–`E8` gave
   **16/16 PASSED**, while `F1`–`F5` failed. Sixteen assertions could not see a
   malformed document, in the only configuration CI runs. The principle behind
   the fix is the reusable part: **a validator must not share an author with the
   thing it validates**, so nlohmann/json is vendored rather than hand-written,
   test-only, confined to one TU, behind its own guard. `API-R16`.
4. **Task F's F3 row is impossible, and the code was not bent to fit it.** The
   plan asked that `static_cast<float>(v)` round-trip to itself on every
   numeric leaf. It went red on six of eight endpoints, and it **cannot hold
   against correct code**:

   ```
   float32 25.118864f              -> exactly 25.118864059448242 as a double
   shortest round-trip decimal     -> "25.118864"              (Task A emits this)
   "25.118864" parsed as a double  -> 25.118864                (exactly)
   (double)(float)25.118864        -> 25.118864059448242       (differs by 5.9e-8)
   ```

   `std::to_chars` with no precision emits the **shortest** decimal inside the
   float's rounding interval — by construction *not* the float's exact double
   value. Demanding a fixed point of float-narrowing demands the very thing Task
   A decided against: satisfying F3 as written would mean printing
   `25.118864059448242`, which is **exactly OSM's defect the row exists to
   catch** (`server.cpp:386-390`). The plan was wrong twice: `effectiveAverages`
   and `frequencyHz` are genuine `double`s on `Snapshot`, not float32 at all.
   As shipped, F3 asserts finiteness everywhere and, for the keys whose source
   really is `float` — **listed by name, because a document cannot say what C++
   type a number was born as** — narrows to float32, re-emits, and requires a
   byte match. Proven to have teeth by mutation: widening float32 to double
   before printing makes **F3 red on its own**.
5. **A Catch2 `[.]` tag is not a gate, and believing it was made a regression
   lock self-healing.** The golden regenerator was tagged `[.][api][golden-write]`
   and three places in the tree claimed "no wildcard filter can reach it".
   Measured, by truncating the golden to an 8-byte sentinel and watching its
   size: spec `[]` left it alone, but `[*]` **rewrote it to 198045 bytes**, and
   so did **`[api]`** — the natural way to run this lane. With a real regression
   live, the sequence was: run 1 under `"[api]"` → `D7` **FAILED** and the
   golden silently rewritten; run 2 → **PASSED**; run 3 → **PASSED**. *One*
   filtered run converts a caught regression into a permanently green lock and
   leaves a modified 198 KB golden in the working tree for the next
   `git add -A` to commit. **A tag cannot fix this, because the thing being
   attacked is the tag matcher.** The gate is now a condition no test spec can
   supply — the environment variable `RTA_API_GOLDEN_WRITE=1` — the case
   **lost its `[api]` tag**, it `SKIP`s with a message when the gate is closed,
   and **`D9` asserts the gate is closed while running under the very filter
   that used to trip it**. (`ctest` was always clean; that half of the claim
   held.)
6. **The rate limiter ran before the refusals, so a forged `Host` spent the
   legitimate client's quota.** Measured at a limit of 3: three forged-`Host`
   requests answered 403, 403, 403, and the **legitimate fourth answered 429**.
   A caller outside the allowlist, holding no token, unable to read one byte of
   this API, could **lock the operator out during a show** — the exact class of
   failure this whole tool exists to prevent. §4's "limiter before the work"
   never required it to be first: the bound was always a bound on the number of
   `latest()` **loads**, and no refusal above touches the publish slot. The
   limiter now runs **last**. The trade-off is stated rather than glossed: it no
   longer bounds *inbound* traffic, only *served* traffic — a hostile caller can
   send forged requests as fast as it likes, each costing an accept and a header
   parse. `R19`. The red was louder than the finding: the limiter was not only
   spending the quota, it was **masking the refusal that should have been the
   answer** (429 where 403, 405 and 401 belonged).
7. **`Allow` advertised OPTIONS and nothing served it.** `OPTIONS
   /api/v1/status` answered **404, with no `Allow` header at all** — the server
   named a method it then treated as an unknown resource. Now **served**: `204`
   plus `Allow`, reading no snapshot. `204` rather than `200` because there is
   no representation to return and a `200` with an empty body would claim one
   exists. And the `Allow` value became **one constant**, which is the durable
   half: two spellings of a method list are two lists that can disagree, and
   this defect is what that looks like. `R18`.
8. **Nothing bounded how many `Host` fields a request may carry.** `httplib`'s
   `get_header_value("Host")` reads index 0, so a **good `Host` followed by a
   forged one passed the allowlist and was served**, while every proxy, cache
   and log downstream might read the other one. More than one `Host` field is
   now **400**, refused on **count** per RFC 9112 §3.2 rather than on
   "the two disagree" — the rule is *one field line*, so two identical `Host`
   fields are refused too, deliberately, while a **missing** `Host` stays 403.
   `R20`.
9. **Coverage had drifted onto the branch that does not ship.** Every
   over-the-wire case bound ephemerally, for good test reasons — which left
   `bind_to_port`, **the branch the shipped configuration always takes**,
   exercised by nothing, while the branch only a test takes ran twelve times.
   `test_api_server_bind.cpp` now binds ephemerally to learn a free port,
   releases it, and binds that number **fixed**. The mutation proves the shape:
   pointing the `Host` check at `settings.port` reddens 11 cases and the new
   fixed-port case stays **green** — which is the lesson, not the fix. Memory:
   `a-test-convenience-can-move-coverage-off-the-shipped-branch.md`.
10. **Six more, shorter.** `API-R1`: the validator, limiter and `Host` check
    need a **third** framework-free file, because §10 put them in the ON-only TU
    while §11 required them in OFF, and both could not be true — resolved as
    `ApiPolicy.{h,cpp}`. `API-R4`: `AverageBlock::absence` is a **per-bin
    array**, not a scalar, so it travels as an array of strings. `API-R11`:
    `axis.pointCount` is **measured** from the array, never derived from
    `fftSize`. `API-R13`: `/snapshot` had no body schema in §6 at all, and is
    defined as the union. `API-R17`: **406 and 415 are dropped** — for a
    GET-only API with one representation, 406 is machinery with no buyer and 415
    is unreachable once an oversized body is refused with 413 — and 413 is
    built and tested at both layers. `API-R5`: §8's settings have **nowhere to
    persist**, because `app/` has no preferences store and this lane did not
    build one (§7).
11. **`R16a`, and why the WebSocket reasoning had to be corrected twice.** A
    WebSocket upgrade **is a `GET`**, so the method allowlist does not stop it.
    What makes 101 unreachable is that **no handler registers an upgrade**.
    Measured by I11: a `GET` carrying `Upgrade: websocket`, `Connection:
    Upgrade` and `Sec-WebSocket-Key` answers **200 with the ordinary JSON body**
    on a known path and **404** on an unknown one — never 405, never 101, no
    `Sec-WebSocket-Accept`. Two mechanical facts came with it, both now in
    `PROVENANCE.md`: the comment at `httplib.h:14487` saying "fall through to
    404" **is wrong** (the fall-through reaches routing, so a known path answers
    200), and `pre_routing_handler_` runs **twice** for an upgrade request
    (`:14408`, then from `Server::routing` at `:13881`), so one such request
    spends **two** rate-limiter admissions. Measured at a limit of 4: upgrade 1
    → 200, upgrade 2 → 200, upgrade **3 → 429**, while three plain requests all
    answer 200. Not a denial-of-service finding — the amplification points away
    from the publish slot, not at it.

A twelfth item is smaller and worth one line, because it is the shape of a trap
rather than a bug: **`stringValue`, not `quoted`.** The plan named the function
`quoted`; `std::quoted` is found by ADL when the argument is a `std::string` and
wins overload resolution, and the first build failed with C2678 on
`std::_Quote_out`. Measured, not anticipated. Do not rename it back.

---

## 5. The numbers

**The figures at the merged tree are VERIFIER-MEASURED**, by an independent
rebuild run in parallel with this closeout at **`d071269`** — which is
`91367a8` plus PR #19's one-character comment repair, `git diff --stat` between
them being one line of `app/tests/CMakeLists.txt`. They confirm the two numbers
the builder reported and add a third configuration. Generator `Visual Studio 18
2026`, MSVC 14.51, Release, JUCE 9.0.1 via `RTA_JUCE_PATH` for the ON
configuration.

```
INDEPENDENT REBUILD AT d071269  (= merged tree of PR #18, + PR #19's comment fix)
  RTA_BUILD_APP=OFF                                            -> 774/774, 0 failed
  RTA_BUILD_APP=ON                                             -> 848/848, 0 failed
  forced fallback (-DRTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK=ON)  -> 774/774, 0 failed
  "warning C" across every build log                           -> 0
  guards green                                                 -> 13 of 13 (ON) / 11 of 11 (OFF)
  rtatool_snapshot                                             -> 8 PNGs, exit 0
  git diff origin/main --stat -- platform/ core/src core/include ui/  -> EMPTY
```

The forced-fallback configuration matters more in this lane than in most: it is
the path where `AtomicSharedPtr` takes a lock, and this lane added a **third**
participant to the publish slot. 774/774 there says the API thread's one load
per request is fine on the lock-based path too.

**`rtatool_snapshot` renders 8 PNGs and one of them does not do what its
arguments say.** `main-live.png` comes out **39853 bytes at 1280×800** and
**ignores the requested size** — the other seven honour it (`preview-phase.png`
45851 bytes at the requested 1100×760). That is `MainComponent` asserting its
own size rather than a snapshot bug, and it is worth knowing before someone
reads a size mismatch as a broken render. `shots/` is gitignored; nothing under
it is committed.

The per-wave ladder, each figure pasted from `ctest` at the commit named:

```
baseline a02fb29 (main, = PR #14)          OFF 649/649   ON 717/717
Wave 1 at 73148a9                          OFF 698/698   ON 766/766   fallback OFF 698
Wave 1 after the PR #16 fix round f95436f  OFF 700/700   ON 768/768   fallback OFF 700
Wave 2 as reviewed 0e0d974                 OFF 718/718   ON 786/786   fallback OFF 718
Wave 2 after the PR #18 fix round 4a65c2d  OFF 725/725   ON 793/793   fallback OFF 725
Wave 2 merged with main 6073fce            OFF 774/774   ON 848/848
```

Both configurations rise by the same amount at every step, and that is not a
coincidence to re-derive: **`app/tests` is `add_subdirectory`-ed outside the
`RTA_BUILD_APP` guard**, so the JUCE-free tests compile in OFF too. It is also
the whole point of `API-R15`.

**Three of the six rows above were independently re-measured in a throwaway
worktree**, and all three confirmed: Wave 1 at `e2fc4b3` (OFF 698, ON 766,
fallback 698, three zero warning counts, `measure_has_no_framework_deps` at 75
with the arithmetic re-derived independently as 71 + 4), and Wave 2 at both
`0e0d974` (718/786/718) and `4a65c2d` (**725/793/725** and three zeros) — plus
the merged tree itself, at the top of this section. **One row nobody
rebuilt:** the post-fix Wave 1 figures at `f95436f` (700/768/700). It is
bracketed on both sides by measured trees, so nothing rests on it.

The builder's own arithmetic for the merged tree is worth keeping because it
explains a gap rather than hiding one: the verifier predicted 772 and 846, the
measurement read **774 and 848**, and the difference is exactly the two limiter
cases added for the OPTIONS-exemption residual after the 725/793 figures were
taken — `727 + (747−700) = 774` and `793 + 2 + (821−768) = 848`. The
independent rebuild then read the same 774 and 848 off `d071269`.

### And then CI, which this closeout found running — and red

**`docs/GIT-WORKFLOW.md` rule 3's merge gate is enforceable again, and it is
not being met.** Every PR body and handoff entry in this lane says "GitHub
Actions is billing-blocked at the account level, so these are local runs". That
was true when the lane opened. It stopped being true on **2026-09-17T17:31Z**,
when a push to `main` ran the three-OS matrix and passed. Measured now with
`gh run list`: every run since has executed. So **PRs #16, #17, #18 and #19
merged with a live, visibly failing matrix rather than with none** — which is a
worse position than the one the PR bodies describe, and nobody looked.

At `main` `d071269`, reproduced across four consecutive runs:

```
rta_core (ubuntu-latest)   100% tests passed, 0 tests failed out of 774
rta_core (windows-latest)  100% tests passed out of 774
rta_core (macos-latest)     99% tests passed, 4 tests failed out of 774
```

**The good half is genuinely good**: ubuntu and windows are the **first
confirmation of OFF 774 by anything other than this machine**, on two
toolchains this lane never built on. The four macOS failures:

| test | file | whose lane |
|---|---|---|
| `D7 REGRESSION LOCK: the golden /snapshot body has not drifted` | `app/tests/test_api_serialise.cpp:193` | **L-API** |
| `E1 the two conventions differ by kFullScaleSineOffsetDb and nothing else` | `app/tests/test_spl_seam.cpp:85` | L6a Wave 0 |
| `E3 with a calibration offset the metric reads 94 dB and the band still reads 0 dBFS` | `app/tests/test_spl_seam.cpp:164` | L6a Wave 0 |
| `B0c AllocationProbe resets on construction so one case cannot read another's bytes` | `app/tests/test_average_group.cpp:376,389` | L6a Wave 0 (shared probe) |

**L-API's one is diagnosed, and it is a test-portability defect rather than a
wire-format defect.** `D7` is `CHECK(emitted == buffer.str())` — a byte-compare
of the whole `/snapshot` body against the committed golden. The first
divergence, at character 5084 of a 198 KB document:

```
emitted : …,-49.21796,-49.341915,-49.18101,…
golden  : …,-49.21796,-49.34192, -49.18101,…
```

`-49.341915` and `-49.34192` are **adjacent float32 values, about one ULP
apart** (the ULP near 49 is ≈3.8e-6; these differ by ≈5e-6). Both are correct
shortest-round-trip decimals — of **two different floats**. So the number the
DSP computes differs in the last bit between MSVC/x64 and Apple clang/arm64, and
a byte-for-byte lock over computed floats cannot survive that. What is *not*
broken: `F1`–`F7`, the third-party-parser checks, **pass on all three OSes**, so
the document is well-formed, correctly typed and finite everywhere. Only the
byte lock is machine-specific. §8 has the options; this closeout does not pick
one, because picking one changes code and this is a docs-only pass.

### Guards, and each was made red in the shape that trips it

**13 guards green in ON, 11 in OFF**, counted by the independent rebuild at
`d071269`. Counts below are that rebuild's.

| guard | scanned at `d071269` | before L-API | reds pasted |
|---|---|---|---|
| `no_server_library_outside_api` | **409** files, both configs | **new in L-API** (Wave 2, Task K) | **6** — both sentinels, an offender in `app/`, the doxygen-block false positive, plus offenders planted under `core/` **and** `platform/` |
| `no_json_parser_in_shipped_code` | **333** files, **2** witnesses | **new in L-API** (Wave 1, Task F) | **4** — offenders in `app/src`, `core/` and `platform/`, plus the witness's include removed |
| `measure_has_no_framework_deps` | **86** files | 67 | green throughout; the count is the evidence it grew with the lane |
| `no_std_atomic_over_shared_ptr` | **409** files | 393 at `4a65c2d` | unchanged by this lane |
| `core_has_no_framework_deps` | **164** files | 161 | unchanged by this lane; `git diff … -- core/` is empty |

**And a gap in what CI can possibly prove, which this lane's own `API-R15`
argument makes newly relevant.** The two RT-hazard guards on the audio callback
— `audioio_callback_has_no_rt_hazards` and
`audioio_scoped_no_denormals_is_first` — are registered **only in the ON
configuration**, and **CI runs only OFF**. So the grep that asserts the audio
callback is still `ScopedNoDenormals` followed by exactly two calls **never
runs on any CI machine**; it runs on a developer's box or not at all. Nothing
in this lane touches that function and `git diff origin/main --stat --
platform/` is empty, so the property holds today by construction. But the whole
force of `API-R15` was "a control proven on zero machines is not proven", and by
that standard these two are in the same position the server was in before the
plan was corrected. Named here rather than left to be rediscovered; it is not
L-API's to move.

Both new guards carry **sentinels**, and the difference between them is the
lesson. `no_server_library_outside_api` has an `ALLOW`
(`app/src/api/ApiServer.cpp`) and therefore two sentinels: the allowed file must
be **inside the scanned set** (else a typo'd `DIRS` prints OK while watching
four directories instead of five), and the allowed file must **still contain the
thing being guarded** (else the exemption outlives the reason for it).
`no_json_parser_in_shipped_code` has **no `ALLOW` at all** — shipped code may
never include a parser — so the first sentinel has no analogue, and a
**witness** replaces it: at least one file under `app/tests` must include the
parser or the script `FATAL_ERROR`s. A verifier caught that gap in the plan
before it shipped, which is why the parser guard's reds had to be planted
outside `app/` too.

One known, documented false positive that is part of the contract rather than a
hole: a literal `#include <httplib.h>` inside a `/** … */` doxygen block
survives `rta_strip_comments` (`check_no_server_library.cmake:47-51`). Named in
the script, and one of the six reds.

**A scope fact worth knowing before trusting the parser guard:**
`no_json_parser_in_shipped_code` scans `app/src`, **not `app`**, so `app/tests`
is out of range **by construction** rather than by exemption. That is why the
witness exists at all.

---

## 6. What the verifiers refuted

Wave by wave, the findings that changed shipped code or shipped claims. The
full inventory is in the four PR threads; these are the ones with teeth.

- **PR #11 (record).** Verdict SOUND-WITH-FIXES, station 3 opened. Two
  confirmed defects: the cookie inversion at four sites (§4.1), and **§11
  acceptance test 10 specified in a configuration that cannot pass** —
  `-DDIRS=core;platform;ui;tools` omits `app` while the sentinel it copies
  requires the `ALLOW` file to be inside the scanned set, so it would
  `FATAL_ERROR` on **every** run. One token. Plus the JUCE `*Server*` grep being
  **three** hits and not two (`HubPipeServer` is declared `struct`, so a
  `class …Server` grep misses it) and three off-by-one citations. Round 2
  **partially refuted** the citation fix: one of four sites was still wrong, and
  two of the line numbers had moved again because `main` changed underneath —
  which is why every fragile citation now carries "(at main `e213202`)" **and a
  grep handle**, so the next reader does not have to trust a number.
- **PR #14 (plan).** Twelve findings in round 1, five more in round 2, and the
  two that reshaped the lane were `V2` (the whole network layer proven on zero
  machines) and `V4` (no JSON parser, called "the one item I would call a hard
  blocker"). Round 2 found four **mechanical** facts that would each have cost a
  build: `bind_to_port` returns `bool` and `class Server` has no `port()` — the
  `port()` at `:2800` belongs to `ClientImpl` — so an ephemeral-port test needs
  `bind_to_any_port`; a by-value `httplib::Server` member turns the new guard
  red on a correct build, so pimpl; the parser guard needs reds outside
  `app/src`; and a WebSocket upgrade is a `GET`. It also caught a
  budget arithmetic error — `test_api_policy.cpp` at 26 lines per case across
  C1–C8 lands past the 400-line cap — with the durable phrasing: **a task that
  extends a file without restating its cap is a task that discovers the cap by
  breaking it.** One verifier finding was its own: "my own round-1 comment is
  where `bind_to_port(…, 0)` was first written. My error, inherited."
- **PR #16 (Wave 1).** One confirmed defect, and it was the `[.]` tag (§4.5).
  Three more that changed code or claims: `Request{}` meant **uncapped**
  (`points = 0` mapping to `SIZE_MAX`), fixed as `D6b` with a red reading
  `0 == 8192`; the **F3 comment overstated F3** — it compares the float32
  re-emission against the double re-emission of the parsed value, not against
  the literal token on the wire, so a hypothetical non-shortest token would pass
  — generalised by the builder into **"a comment that overstates an assertion
  is the same defect class as an assertion that proves nothing"**; and the
  null-for-non-finite rule **existed nowhere in the record**, fixed by a dated
  §6 amendment. The verifier also reported the PR body **under-reporting** one
  of its own mutations, and swept the `Host` allowlist with 17 rejected shapes
  and 5 accepted, naming its one leniency: `127.0.0.1:04736` is accepted because
  `std::from_chars` takes leading zeros — and admits nothing, because the *name*
  is still a loopback literal and rebinding turns on the name.
- **PR #18 (Wave 2), two rounds.** Round 1: three defects (§4.6–§4.8), all
  **measured over a socket and none visible by reading**, plus five
  observations. Round 2 confirmed all three fixes by independent probe — at the
  exact limit, not just at the shipped margin — and found a **residue on the
  same principle as the one just fixed**: a *served* `OPTIONS` still spent
  quota while performing no load. The verifier asked only for prose; the builder
  took the stronger option and exempted OPTIONS, then added the case that says
  **HEAD is not exempt** and why. Round 1's finding 8 (six Vietnamese
  diacritic typos in the new handoff section) survived a whole round unfixed and
  is noted here because "the builder's reply did not claim otherwise" is the
  honest description of what happened.

---

## 7. Known gaps, stated rather than hidden

- **Nothing an operator can reach is different, and enabling the API is a
  source edit.** `api.enabled` ships `false` (`ApiSettings.h:27`). There is **no
  preferences store anywhere in `app/`** (`API-R5`), so `ApiSettings` is a plain
  struct constructed by hand in the composition root at
  `MainComponent.cpp:120-121`. **The dotted names `api.enabled`, `api.port` and
  the rest are documentation, not keys a user can set** — the only two literal
  `api.*` strings in shipped code are refusal messages at `ApiPolicy.cpp:160`
  and `:166`. Flipping the API on means editing `MainComponent.cpp:120` and
  rebuilding. Whoever ships a settings surface owns the persistence decision,
  and it is not a small one: a persisted `enabled = true` is a network listener
  that survives a restart.
- **Task J's manual `curl` check was NOT run.** It needs a running GUI with
  `enabled` flipped, and no closeout session has started one. It reads **not
  verified**, not done. What *is* proven is the whole request path over a real
  socket in OFF, on three OSes, plus both bind branches. The exact commands are
  in `docs/HANDOFF.md`'s top section, marked as unrun.
- **`D7` fails on macOS and the lane merged anyway** (§5). It is the one L-API
  claim this report cannot make.
- **The `"spl"` trigger has fired and `available` did not follow.**
  `ApiSerialise.cpp:74` still reads `"spl" joins it the day the Meters track
  puts SPL in the Snapshot and not a day earlier`, and `:77` still emits the
  hardcoded six-name literal. That day was **PR #17**, which merged *before*
  this lane's own PR #18. Nothing is wrong on the wire — no endpoint
  serialises SPL, so advertising `"spl"` would promise a representation nothing
  returns — but the stated gate is stale, and the real remaining decision is
  whether `"spl"` means a field on `/snapshot` or an endpoint of its own. One
  comment, one string literal, and one decision.
- **Twenty-six of the seventy-six API test cases carry no tag at all**, so no
  tag filter reaches them — and **`ctest -R "api"` reaches none of them**
  (§10). Every case in `test_api_server.cpp`, `test_api_server_bind.cpp` and
  `test_api_server_refusals.cpp` — which is to say **every socket-level case,
  the ones `API-R15` exists to make runnable on CI** — is reachable only by
  running the whole binary or by naming the cases. Naming them is awkward in
  its own right: **nine of the twenty-six names contain a comma**, and a comma
  is Catch2's test-spec separator, so `-f <file-of-specs>` cannot be used for
  them either. §10 gives the `ctest -R` alternation that does work. A one-line
  fix — add `"[api][server]"` to twenty-six `TEST_CASE`s — would retire the
  whole problem. `test_names_are_ascii` guards the *character set* of a test
  name; nothing guards that a lane's tests carry the lane's tag.
- **Layer two of the 413 defence is covered by no test.** The chunked-body path
  (`set_payload_max_length`) was exercised only by a verifier's manual wire
  probe: a chunked 40 KiB body with no `Content-Length` answered **413**. Layer
  one has `B10` and `I9`; layer two has a comment and one probe nobody can
  re-run from ctest.
- **Two file-length budgets were exceeded, both declared, both under the hard
  cap.** `test_api_server.cpp` 366 against a planned ≤ 280, `RawHttpClient.h`
  218 against ≤ 120, and `external/cpp-httplib/PROVENANCE.md` 70 against ≤ 50.
- **`ApiServer.h:63-65` says out loud what nothing tests:** "Nothing
  test-visible catches a second `latest()` inside one handler — that constraint
  is held by review and by this comment." One `latest()` per request is true
  today by reading, not by a guard.
- **The two audio-callback RT-hazard guards never run on CI** (§5), because
  they are ON-only and CI is OFF-only. Not L-API's to move, but it is the same
  argument `API-R15` won.
- **Cosmetic tech debt, noticed by the independent rebuild and not this lane's
  doing:** `juce_add_console_app(rtatool_snapshot …)` at
  `app/CMakeLists.txt:188` warns at configure time because the bundle
  identifier JUCE derives from `PRODUCT_NAME "RTA Tool Snapshot"` /
  `COMPANY_NAME "AZ Soundtech"` contains spaces. It affects a dev-only console
  tool, not `rtatool`, and the fix is an explicit `BUNDLE_ID`. Logged in
  `docs/HUMAN-QA-QUEUE.md` rather than fixed here.

---

## 8. The macOS failure, and what the options are

Not a decision this report takes — it changes code — but the next session should
not have to re-derive the shape.

`D7` locks **198 KB of computed float32 numbers** byte-for-byte. The lock's
value is real: it is what catches a format drift that no schema assertion would
see, and it did catch one during this lane (the trailing-comma mutation reddened
`D7` alongside `F1`–`F5`). Its cost is that it is only valid on the toolchain
that generated the golden.

Three shapes, with what each gives up:

1. **Generate the golden from a fixture with no DSP in it** — exactly
   representable inputs whose outputs are exact in `float` on every toolchain.
   Keeps a byte lock, keeps CI portable, and gives up the incidental coverage of
   locking realistic values. This is the shape `CLAUDE.md`'s verification
   standard already prefers ("a closed-form identity"), and it is what the L6a
   Wave 0 tolerance corrections converged on for the same reason.
2. **Compare structurally rather than bytewise** — parse both documents and
   compare keys, types, array lengths and numbers to a stated float32 tolerance.
   Portable, but it stops being a *format* lock, which is the one thing `F1`–`F7`
   do not provide.
3. **Keep the byte lock and mark it platform-specific.** Honest, cheap, and it
   means the three-OS matrix can never be green — which makes rule 3's gate
   permanently unsatisfiable and is therefore the worst of the three.

The other three macOS failures are **L6a Wave 0's**, not this lane's, and the
two in `test_spl_seam.cpp` look like the same class of problem (a tolerance or a
float identity that holds on MSVC/x64 and not on Apple clang/arm64) —
`memory/two-builds-disagreeing-is-not-evidence-one-is-wrong.md` is the right
thing to read before assuming which side is wrong. That is for whoever picks up
L6a Wave 1, and it is named in the handoff.

---

## 9. Open, and it is the owner's

None of these is an agent's to close.

1. **§14 q.1 — a fixed port or an ephemeral one.** **The number is settled:
   4736**, because 4737 is IANA `ipdr-sp` (§4.2). What is open is the *shape*: a
   fixed port is discoverable and can collide; an ephemeral port written to a
   file the client reads never collides and needs a rendezvous. **The cost of
   flipping is now known and small** — the code ships fixed, and **both bind
   branches have tests**, so it is one constant in `ApiSettings.h` plus a
   rendezvous file. One sentence settles it. Carried in
   `docs/HUMAN-QA-QUEUE.md`.
2. **Should the Smaart API SDK be requested?** Free and non-NDA per the
   published terms, and the only route to the one piece of prior art station 1
   could not read: **how a competitor encodes coherence** on the wire. REW
   teaches nothing here — REW is swept-sine, single-channel, and its API has no
   coherence anywhere. The terms forbid redistributing the SDK, so nothing from
   it could ever be quoted into this repository; it could only inform. It is a
   days-long round trip. Still unasked.
3. **GitHub Actions is running again, and the matrix is red** (§5). Whether to
   hold merges on it — rule 3 says yes and the last four merges say no — is the
   owner's, and it is now a real choice rather than a blocked one.
4. **The other four §14 questions are closed by the build, not by an answer**,
   and the owner may still overrule any of them: `allowLanBind` ships present
   and refusing; the token setting ships empty (so the Bearer control is **off**
   at the shipped default, deliberately, and `B7` asserts exactly that);
   `/traces` and `/session` are **not** in v1, so no second publish path was
   built; the Smaart SDK was not requested. Each was taken as a **named
   default** with its flip cost stated, not silently.

---

## 10. What a human can run

In `docs/HANDOFF.md`, the top section **"2026-09-18 — L-API (Remote API) CLOSED
OUT"**: every runnable thing this lane produced, one PowerShell 7 command per
block, with what should appear on screen and each one marked `[verified]` or
`[not run here]`. It opens with the thing a closeout is most tempted to skip —
**what of L-API is visible in `rtatool.exe` at all**, which is nothing, because
`api.enabled` ships `false` and there is no settings UI — then covers the two
ctest configurations, the manual `curl` sequence for a running GUI (a 200, a
forged-`Host` 403, an OPTIONS 204 and a 304), the golden regenerator and its
environment gate, and the three guards.

**One thing measured while writing that section, because the obvious command
does not work.** `ctest -R "api" -N` selects **exactly one test, and it is not
an API test** — it is the guard `no_server_library_outside_api`, whose *name*
happens to end in the substring. `ctest -R` is a **case-sensitive** regex over
**ctest test names**; `catch_discover_tests` is called with no `TEST_PREFIX`
(`app/tests/CMakeLists.txt:284`), so each ctest name is the raw `TEST_CASE`
string; and **not one of the 837 `TEST_CASE` names in this repository contains
a lowercase `api`**
`[verified: 1 — 0 of 837 TEST_CASE names, 1 of 13 add_test names]`.

Case-folding barely helps: six names match in total, and only two of them are
this lane's — `regenerate the API golden`, which is the **hidden** regenerator,
and `I1 a disabled ApiServer binds nothing…`. The other three are unrelated
(`test_readouts.cpp`, `core/tests/test_detector.cpp`,
`ui/tests/test_grid_panel.cpp`), and they live in **three different test
executables**.

**What does work**, and it is in the handoff verbatim because nobody should
have to reconstruct it: the tag filter `"[api]"` on the test binary reaches
**49** cases `[verified: 49]`, and the remaining **26** socket cases need an
anchored `ctest -R` alternation over their names — which matches **exactly
those 26 of all 837** `TEST_CASE` names `[verified: 26]`, and which the
independent rebuild ran in the OFF configuration: **26 ran, 26 passed, 1.24 s**.
Catch2's `-f <specfile>` is not an alternative, because **nine of the 26 names
contain a comma** and a comma is Catch2's spec separator.

That section also carries rule 12's third item: the handoff for the next lane,
**L6a (SPL-pro) Waves 1–4** — what to read first, which of the four macOS CI
failures are its own, and the fact that its Wave 4b viewer now has gate 1 met
and gate 2 unrun.
