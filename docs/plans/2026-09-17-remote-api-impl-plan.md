# L-API — the read-only remote API (lane L-API, v1: HTTP/1.1 + JSON over TCP, GET only)

> **For agentic workers:** REQUIRED SUB-SKILL `superpowers:test-driven-development` and `superpowers:executing-plans`. Steps are checkboxes; the failing test is written and *seen to fail* before the code. Station 5 (adversarial verify, a reviewer with **no Write tools**) is not optional.

*2026-09-17, lane L-API, station 3. Written from the decision record `docs/dsp/2026-09-16-remote-api.md` and the station-1 research `docs/research/2026-09-16-remote-api-station1-research.md`, **after opening every file they cite** — not after reading their description of those files. Revised twice on 2026-09-17 against two adversarial verify rounds on **PR #14**: round 1 (SOUND-WITH-FIXES, twelve defects `V1`..`V12`) and round 2 (eleven of twelve fixes confirmed, station 4 GO, five defects `D1`..`D5` — four of them mechanical facts about cpp-httplib and CMake that only a reader with the library open could have caught). Every one is addressed below and named where it changed something. Worktree `.claude\worktrees\agent-a3c60992e3c5ae938`, branched from `main` at `a39a02e`, merged up to `af8a9d0` (PR #12, docs-only — `git diff a39a02e af8a9d0 -- core/ app/ platform/ ui/ tools/ CMakeLists.txt .github/` is **empty**, so every file:line citation below still holds). Every path was existence-checked; a path marked **NEW** is not in the tree today. **The reconciliations `API-R1..R17` plus `R16a` are landed in the record as its §15 amendment**, with inline pointers at §2, §4, §6, §8, §9, §10, §11 and §14 (V12) — station 4 is no longer waiting on that gate.*

## 0. Hazard first: the one rule, and what this lane may never touch

**The audio callback is not in this lane's reach, and that is structural, not disciplined.** `AudioIo::audioDeviceIOCallbackWithContext` is `juce::ScopedNoDenormals` followed by exactly two calls — `bus_.pushFromCallback(...)` and `output_.render(...)` (`platform/src/AudioIo.cpp:116-148`, guarded by `platform/tests/check_callback_shape.cmake` and `audioio_callback_has_no_rt_hazards`). Nothing here adds a third call, a socket, an allocation, a lock or a branch to it. The API lives in `app/`; `platform/` cannot see `app/` (grep for `app/src|rtatool` over `platform/src platform/include platform/types` returns nothing), so the callback is unreachable from this code by construction. **A builder who finds themselves editing anything under `platform/` has left the lane.**

**The second rule, and it is the one a builder can actually break.** The API thread does **one** `SnapshotSource::latest()` per request (`app/src/measure/SnapshotSource.h:19`), takes the `shared_ptr<const Snapshot>` copy, and **serialises from that copy with the slot released**. It never holds the pointer across a request, never calls `latest()` twice in one request, and never serialises while the load is in flight. `AtomicSharedPtr` is **not lock-free on this project's own toolchain** — its class comment records the measurement on MSVC 14.51 — so the API thread is a *third* participant on a slot whose safety comes from who calls it, not from lock-freedom (`app/src/measure/AtomicSharedPtr.h`; record §4). The expensive work — thousands of floats into JSON — happens entirely after the load. That ordering **is** the decision.

**The third rule.** The API thread never touches `TraceLibrary`, `SessionDocument`, `EqSession` or `AlignmentWizard`. `TraceLibrary` is owned by `MainComponent` (`app/src/MainComponent.h:160`), is mutable, deletes copy and move (`app/src/trace/TraceLibrary.h:51-54`) and has `revision()` (`:83`) but **no atomic publish**. v1 does not expose it at all (§14 q.3's default, taken below), so no second publish path is built.

## Defaults this plan TAKES, each one named

The record's §14 has five open owner questions and `docs/HUMAN-QA-QUEUE.md` ("Từ lane Remote API") states in its own header that **none blocks station 3**. A plan that left them blank would be a plan nobody could build, so each is taken as a named default. **A default is a decision the owner may overturn in one sentence; it is not a decision this plan is hiding.**

| § | question | **default taken** | what changes if the owner says otherwise |
|---|---|---|---|
| §14 q.1 | port: fixed or ephemeral | **fixed `4736`** — *not* the record's 4737 (**V1**): the IANA Service Name and Transport Protocol Port Number Registry has `ipdr-sp,4737,tcp` and `ipdr-sp,4737,udp` (IPDR/SP, registered 2005-08), while **4734, 4735** (REW's own) **and 4736 are absent from it**. The record's justification was "unclaimed by any surveyed tool", which was true of the surveyed *audio* tools and never checked against the registry. User Ports are not exclusive, so 4737 would have worked — but a named default resting on a check nobody ran is exactly what this project's method exists to prevent. One constant in `ApiSettings.h` | one constant, plus a rendezvous file if ephemeral wins. **The fixed-versus-ephemeral half of q.1 is still open and still one sentence** |
| §14 q.2 | does `api.allowLanBind` exist | **it ships, as a setting that is present and refuses**: `bool allowLanBind = false`, and `ApiServer` **fails to start** with a named refusal if it is true. Honest about the roadmap, and impossible to turn on by accident because the code path does not exist | delete one field and one refusal |
| §14 q.3 | `/traces` and `/session` in v1 | **no.** v1 is the **eight** measurement endpoints and **no new publish path** (API-R12 — the record says "six", which is the length of `available`, not the endpoint count) | a whole task, and the second `AtomicSharedPtr<const ApiSideState>` of §5 |
| §14 q.4 | token: empty, or generated on first enable | **ship the setting empty.** `api.token` defaults to `""` = no token required; when non-empty it is checked as `Authorization: Bearer <token>`, **never a cookie**, never a query parameter that lands in a log. **Because the shipped default disables this control, it is the one thing in the lane that must be tested with a non-default setting** — see Task B and Task I, and `memory/a-fixed-defect-returns-through-the-silent-fallback.md` | a generator and a place to show it — neither is on any path here |
| §14 q.5 | request the Smaart API SDK | **not needed for this lane.** It would inform how a competitor encodes coherence; §6 already decides this project's encoding, and the SDK's terms forbid quoting it into this repo anyway. **Nothing here waits on it** | nothing in this plan |

Settings shipped as §8 lists them **except where §15 amends §8**, which is the honest phrasing (the first draft said "verbatim from §8" and two of the eight are not §8 values): `enabled=false`, `bindAddress="127.0.0.1"` (literal IP, never the string `localhost` — cpp-httplib's own README warns that resolving it on Windows with misconfigured IPv6 can cost up to 2 s per request, verbatim at its README `:1441`), **`port=4736`** (API-R14, not §8's 4737), `token=""`, `maxRequestsPerSecond=30`, `maxPointsPerResponse=8192`, `maxRequestBodyBytes=8192` (API-R17 — new; §9 control 3's 413), `corsOrigins` empty, **`allowLanBind=false` and present** (§14 q.2's default; §8's table says "absent in v1"). Fixed, not settable: read 2 s, write 2 s, keep-alive idle 30 s, keep-alive max count **1000** (cpp-httplib's stock 100 is five seconds at a 20 Hz poll), thread pool base 2 / max 8 via `new_task_queue`.

## Global constraints

- SPDX header `// SPDX-License-Identifier: AGPL-3.0-or-later` on every new file this repo authors. The **vendored** `external/cpp-httplib/httplib.h` and `external/nlohmann/json.hpp` are upstream's files byte-for-byte and get **no** SPDX line added — editing a vendored file to satisfy a local convention is how a vendored file stops being verifiable against upstream.
- **`core/src`, `core/include`, `platform/`, `ui/` are untouched.** The lane's diff is `app/`, `external/`, **`core/tests/`** (two new guard scripts and their registrations), the root `CMakeLists.txt` and `README.md`. The precise formulation, and the one Task K asserts, is that `git diff main --stat -- platform/ core/src core/include ui/` is **empty** (V-nit: the first draft said "core/ is untouched" and then edited `core/tests/CMakeLists.txt` in the same bullet).
- **Hard cap 400 lines, aim 300**, headers too. Per-file budgets are given in each task.
- **`RTA_BUILD_APP=OFF` is where the proof lives, and after `API-R15` that is almost the whole lane.** CI has exactly one job, `core`, configured `-DRTA_BUILD_APP=OFF` on three OSes (`.github/workflows/ci.yml:9`, `:15`, `:21`); there is **no ON job anywhere in the repository**. `app/tests` is registered outside the `RTA_BUILD_APP` guard (root `CMakeLists.txt:72`). So **ten of the eleven tasks run on CI, including the whole server and the end-to-end `Host` check** — §9's "highest-value control in the whole API" is proven on ubuntu, macos and windows rather than on one developer's box by hand. **Only Task J, the composition-root wiring, is ON.**
- **Never assert a value the implementation produced.** Four sources of truth: the record's own §6 schema (a specification, not an output); `std::to_chars`'s shortest-round-trip property (a closed form — the emitted decimal must `from_chars` back to the identical `float` bits); a **third-party** JSON parser for well-formedness (API-R16 — a validator must not share an author with the thing it validates, which is the same principle that makes golden vectors come from SciPy rather than from a second in-house implementation); and the three **already-tested** formatters in `app/src/view/Readouts.h`. The golden JSON file is a **regression lock, not a correctness test**, and its test's name must say so.
- Build dirs **`build-lapi`** (OFF) / **`build-lapi-on`** (ON), Visual Studio generator, **never Ninja** (`memory/a-misconfigured-build-goes-99-percent-of-the-way.md`). MSVC `/W4` is the truth: **0 `warning C`** — see API-R7 for the one place that is at risk.

```
cmake -S . -B build-lapi -G "Visual Studio 18 2026" -A x64
cmake --build build-lapi --config Release --parallel
ctest --test-dir build-lapi -C Release --output-on-failure
```

For the ON config append `-DRTA_BUILD_APP=ON -DRTA_JUCE_PATH="D:/DEV CAVE EP3/PROJECT005-AZ-handsfree/external/JUCE"` and use `build-lapi-on`.

---

## Reconciliations (now landed in the record as §15; this list is the short form)

**The gate V12 named is closed**: `docs/dsp/2026-09-16-remote-api.md` carries §15 in this same PR, with inline "Amended by §15" pointers at §2, §4, §6, §8, §9, §10, §11 and §14. Station 4 does not need to re-derive any of this from here.

| | supersedes | what changes |
|---|---|---|
| **R1** | §10 | Three files in `app/src/api/`, not two. §11 items 6-9 need the validator, the limiter and the `Host` check testable in OFF; §10 put them in the ON-only httplib TU. → `ApiPolicy.{h,cpp}` |
| **R2** | §2 | The **amalgamated** `httplib.h`, not `split.py` output. One permitted includer means nothing to amortise over; a verbatim file is hash-checkable against the release (v0.56.0, 22875 lines, sha256 `1f99e518…0b4`), a generated one is not |
| **R3** | §11 item 10 | `external/` at the repo root — a vendored `.h` anywhere under `app/` is caught by the guard's recursive glob and reddens it on the library it exists to permit |
| **R4** | §6, §11 item 4 | `absence` is a per-bin **array** of strings (`Snapshot.h:131` is a `std::vector`), not one string |
| **R5** | §8 | No preferences store exists in `app/`; `ApiSettings` is a struct and **nothing persists in v1** |
| **R6** | §11 item 11 | "N must rise by two" is arithmetic, not a constant — `app/tests/*.h` is globbed too (`app/tests/CMakeLists.txt:282`) |
| **R7** | §2 | `/W4` is global (root `CMakeLists.txt:50`); the one httplib include is wrapped in `#pragma warning(push, 0)`, fallback `SYSTEM` + `/external:W0` — **measured, not assumed** |
| **R8** | §11 item 10 | `rta_strip_comments` does **not** strip a doxygen block (`check_no_std_atomic_shared_ptr.cmake:129-130`), so a mention of the library in one false-positives the new guard |
| **R9** | §11 item 12 | The loopback test had no target; R15 then moves it into `rtatool_analysis_tests` entirely |
| **R10** | §6 | `/transfer` serves `transfer`, not `soloTransfer`; `peakBand*` and `referenceBands` are decided out by name |
| **R11** | §6 | `axis.pointCount` is `magnitudeDb.size()`, never `fftSize/2+1` |
| **R12** | §14 q.3 | Eight endpoints, not six; six is the length of `available` |
| **R13** | §6 | `/snapshot` is the union of `/status` and every present block, absent keys absent |
| **R14** | §8, §14 q.1 | **Port 4736.** 4737 is IANA `ipdr-sp`; 4734/4735/4736 are absent from the registry (**V1**) |
| **R15** | §4, §10, §11 item 12 | **`std::thread`, not `juce::Thread`; the server is JUCE-free and runs on CI's three OSes.** Only the composition-root wiring is ON (**V2**) |
| **R16** | §11 | **A JSON parser is vendored, test-only.** Nothing in the OFF configuration asserted the emitted document is well-formed; every OFF assertion was a substring match or a byte-compare against the same serialiser's output (**V4**) |
| **R16a** | §3, §13 | **A WebSocket upgrade is a `GET`**, so no method check stops it; what makes one impossible is the absence of a registered handler, and the server never emits `101` (**D4**) |
| **R17** | §9 control 3 | **413 is built and tested; 406 and 415 are dropped** with the reason, rather than left listed in a record and absent from the code (**V10**) |

---

## The API this lane builds (record §6, §8, §9 as amended by §15; names are this plan's)

```cpp
// app/src/api/ApiSettings.h   --  namespace rta::api  --  OFF, framework-free
struct ApiSettings {
    bool        enabled              = false;        // record §8: off until the operator asks
    std::string bindAddress          = "127.0.0.1";  // LITERAL IP, never "localhost" (§8)
    int         port                 = 4736;         // API-R14; 4737 is IANA ipdr-sp
    std::string token                = "";           // empty = no token; Bearer only, never a cookie (§9)
    int         maxRequestsPerSecond = 30;           // §8; the rate §4's accounting is stated at
    int         maxPointsPerResponse = 8192;         // §8; clamps any caller-supplied count
    int         maxRequestBodyBytes  = 8192;         // API-R17; over it is 413, refused before routing
    std::vector<std::string> corsOrigins{};          // empty = no CORS headers, and that is not a defence (§9)
    bool        allowLanBind         = false;        // §14 q.2: exists, and refuses (see startRefusal)
};

// app/src/api/ApiPolicy.h   --  namespace rta::api  --  OFF, framework-free, server-library-free
enum class Method { Get, Head, Options, Other };
enum class Verdict { Serve, NotModified };

[[nodiscard]] bool hostIsAllowed(std::string_view host, int port);      // §9 control 1
[[nodiscard]] Method methodOf(std::string_view verb);                   // exact match, case-sensitive
[[nodiscard]] bool methodIsAllowed(Method m);                           // Get|Head|Options
[[nodiscard]] bool bearerAccepted(std::string_view authorizationHeader,
                                  const ApiSettings&);                  // §9 control 4 -> 401
[[nodiscard]] int clampPoints(long long requested, const ApiSettings&); // §11 item 6
[[nodiscard]] std::optional<std::string> startRefusal(const ApiSettings&);
[[nodiscard]] std::string etagFor(std::uint64_t sequence);              // `"12345"`, quotes included
[[nodiscard]] Verdict conditionalVerdict(std::string_view ifNoneMatch,
                                         std::optional<std::uint64_t> since,
                                         std::uint64_t sequence);       // §3: 304 or serve

/// SLIDING window, not a fixed one, and the choice is load-bearing (API-R15,
/// record §4): a fixed window admits 2*max across a window boundary, which
/// breaks the bound the real-time-safety argument actually needs -- at most
/// `maxPerSecond` loads in EVERY one-second interval, not in aligned ones.
class RateLimiter {
public:
    explicit RateLimiter(int maxPerSecond);
    [[nodiscard]] bool admit(std::chrono::steady_clock::time_point now); // false => 429, BEFORE any load()
};

// app/src/api/ApiSerialise.h   --  namespace rta::api  --  OFF, framework-free, server-library-free
struct Request { int points = 0; };   // already validated and clamped by ApiPolicy
[[nodiscard]] std::string serialiseStatus   (const measure::Snapshot&, const ApiSettings&);
[[nodiscard]] std::string serialiseSnapshot (const measure::Snapshot&, const Request&);  // API-R13
[[nodiscard]] std::string serialiseTransfer (const measure::Snapshot&, const Request&);
[[nodiscard]] std::string serialiseMtw      (const measure::Snapshot&, const Request&);
[[nodiscard]] std::string serialiseBands    (const measure::Snapshot&);
[[nodiscard]] std::string serialiseSpectrum (const measure::Snapshot&, const Request&);
[[nodiscard]] std::string serialiseAverage  (const measure::Snapshot&, const Request&);
[[nodiscard]] std::string serialisePositions(const measure::Snapshot&);
```

`ApiServer` (OFF, after API-R15) owns one `std::thread`, one `httplib::Server`, an `ApiSettings` and a `measure::SnapshotSource&` — **all of them behind a pimpl**, so `ApiServer.h` includes nothing from httplib and Task K's guard stays green on a correct build (D2). It exposes `running()` and **`boundPort()`**, the latter because `httplib::Server::bind_to_port` returns `bool` and keeps the port to itself (D1). Its whole request path, **in this order, because the order is the decision**: rate limit → body-size cap → `Host` check → method check → Bearer → route → clamp → **one `latest()`** → `conditionalVerdict` → respond with `ETag`. The limiter runs **before** the work, so `maxRequestsPerSecond` is a hard bound on this thread's traffic against the publish slot and not a typical figure.

**Absence rules that must survive the wire, and each has a test.** `coherence` absent ⇒ **the key is not present at all** (never `null`, never ones, never zeros). `coherenceAvailable` is **per band** and travels. `underResolved` travels. `absence` and `membership` are **strings**. A `nullopt` block's key is absent from `/snapshot`.

---

## Task A — `ApiJson.h`: the wire's number format (OFF; record §6 "Units on the wire", §7)

The smallest thing, and everything else emits through it. Shortest-round-trip float32 is the one numeric decision in the whole lane, and it is a closed form, so it goes first.

**Files.** Create `app/src/api/ApiJson.h` (**NEW**, ≤ 160, header-only) and `app/tests/test_api_json.cpp` (**NEW**, ≤ 160). Modify `app/tests/CMakeLists.txt`: add the test to `rtatool_analysis_tests`' **source list** *and* append `ApiJson.h` to the `measure_has_no_framework_deps` GLOBS at `:282`. **Those are two separate registrations and the second does not follow from the first** — record §10 says so explicitly, and the glob list is a textual scan that compiles nothing.

`std::to_chars(first, last, float)` with **no** precision argument produces the shortest decimal that reads back as the identical `float` — the standard's own guarantee, and why no `printf` format string appears anywhere in this lane. `double` values (`effectiveAverages`, `MtwBlock::frequencyHz`) go through the `double` overload for the same reason.

**RED first.** `test_api_json.cpp` opens with `#include "api/ApiJson.h"`; the build fails at the include. Paste it.

| # | case | closed-form acceptance |
|---|---|---|
| **A1 (first)** | round-trip is the property, not the digits | for every value in `{-3.2145123f, -3.107789f, 12.421333f, 11.901777f, 0.9731445f, 0.9642334f, 0.0f, -0.0f}` (the first six are §6's own literals): `from_chars` on the emitted string returns **bit-identical** `float`. Asserted with `std::bit_cast<std::uint32_t>`, not `==`, so `-0.0f` cannot pass by accident |
| A2 | §6's examples are reproduced exactly | `number(-3.2145123f) == "-3.2145123"`, `number(0.9731445f) == "0.9731445"`, `number(8.5859375) == "8.5859375"`. These are the record's own printed values; a mismatch means either the record or the emitter is wrong and the orchestrator hears which |
| A3 | no display rounding leaks into the transport, **including Hz** | `number(0.9731445f)` is not `"0.97"`; `number(-3.2145123f)` is not `"-3.2"`; and `number(11.71875)` — `/mtw`'s own `frequencyHz[1]` — is **not** `"12"`. **V8** caught that the first draft pinned dB and coherence and left Hz unpinned, which is the one quantity `CLAUDE.md` rounds hardest. The rounding rule is the viewer's (§6, §12 constraint 2, Task G) |
| A4 | non-finite emits the literal `null` | `number(NaN)` and `number(±inf)` emit exactly `null`, never the tokens `nan`/`inf`, which are not JSON. **Asserted as an exact string, not by parsing** — the parser does not exist until Task F, and Task F re-asserts this at document level |
| A5 | strings are escaped, asserted as exact strings | a name containing `"`, `\`, `\n`, a `0x01` control byte and a UTF-8 multi-byte sequence produces a named exact output. `PositionSummary::name` is a `std::string` that comes from a device, i.e. from outside this program. Task F re-asserts it by round-tripping through the parser |
| A6 | arrays are bounded by the caller's clamp | `array(span, limit)` emits `min(span.size(), limit)` elements and the emitted count is in the object beside it |

- [ ] **Accept:** OFF ctest `base_off + N` (N read from ctest, never predicted). `measure_has_no_framework_deps` green with the risen scanned count **pasted**. Zero `warning C`.
- [ ] **Mutation:** give `number(float)` a `std::to_chars(..., std::chars_format::fixed, 2)` → A1, A2 **and A3's Hz case** go red together; revert.
- [ ] **Commit:** `feat(app): ApiJson -- shortest-round-trip float32 on the wire, no display rounding in the transport`

## Task B — `ApiSettings.h` + `Host`, method, Bearer, point cap, body cap (OFF; record §8, §9; §11 items 6, 8, 9; API-R14, R17)

**Files.** Create `app/src/api/ApiSettings.h` (**NEW**, ≤ 120), `app/src/api/ApiPolicy.h` (**NEW**, ≤ 150), `app/src/api/ApiPolicy.cpp` (**NEW**, ≤ **200** — Task C extends this file and its budget is restated there), `app/tests/test_api_policy.cpp` (**NEW**, ≤ **280**, and it holds **B1-B10 only**: at this suite's measured ~26 lines per case, B's ten plus C's eight in one file reaches ≈468 and blows the 400-line cap, so **Task C opens its own test file** rather than discovering the cap halfway through). Modify `app/tests/CMakeLists.txt`: **`ApiPolicy.cpp` goes into `rtatool_analysis_tests`' source list** *and* all three headers/sources go into the GLOBS — again two registrations, not one.

**Every case below runs against `ApiSettings{}` — the shipped defaults** (`memory/a-default-must-be-run-through-the-gate-it-feeds.md`) — **except B7-B9, and the exception is the finding.** The shipped token is empty, so at the defaults the Bearer control never executes; a suite that only ever used the defaults would ship an untested authentication path that looks tested (`memory/a-fixed-defect-returns-through-the-silent-fallback.md`). Those three cases construct a settings object with a token **and say in the test name why**.

**RED first.** `test_api_policy.cpp` includes `api/ApiPolicy.h`; the build fails at the include. Paste it.

| # | case (record §9, §11) | acceptance |
|---|---|---|
| **B1 (first)** | the whole DNS-rebinding defence | `hostIsAllowed("127.0.0.1:4736", 4736)` **true**; `"localhost:4736"` **true**; `"[::1]:4736"` **true**; `"attacker.example:4736"` **false**; `"127.0.0.1:4736.attacker.example"` **false** — the substring trap NCC Group's "strictly contain" wording exists to catch, and a naive `find()` passes it; `""` **false**; `"127.0.0.1"` with no port **false**; `"127.0.0.1:4737"` **false** (right host, wrong port) |
| B2 | case and whitespace do not open it | `"LOCALHOST:4736"` **true** (a hostname is case-insensitive, RFC 4343 — and accepting `localhost` costs nothing against rebinding, because a rebound request carries `Host: attacker.example`; the `Host` header is the *requested* name, never the resolved address); `" 127.0.0.1:4736"` **false**; `"127.0.0.1:4736 "` **false** |
| B3 | method allowlist | `methodIsAllowed` accepts `GET`, `HEAD`, `OPTIONS`; rejects `POST`, `PUT`, `DELETE`, `PATCH`, `TRACE`, `""`, and lowercase `get` — the HTTP method token is case-**sensitive** |
| B4 | the point cap is run at the shipped default | `clampPoints(1'000'000, ApiSettings{}) == 8192`; `clampPoints(-1, …) == 8192` (absent or garbage means "as much as allowed", not zero); `clampPoints(0, …) == 8192`; `clampPoints(256, …) == 256`; a value that would overflow `int` clamps rather than wrapping — the parameter is parsed as `long long` precisely so the overflow is a clamp and not UB |
| B5 | `allowLanBind` refuses rather than silently binding | `startRefusal(ApiSettings{.allowLanBind = true})` returns a named refusal; `startRefusal(ApiSettings{})` returns none. §14 q.2's default made visible: the setting exists and the code path does not |
| B6 | a non-loopback bind address is refused the same way | `ApiSettings{.bindAddress = "0.0.0.0"}` refuses; so does `"192.168.1.10"`. Two ways to ask for a LAN bind, one refusal |
| **B7** | **at the shipped default the token control is OFF, and that is asserted** | `bearerAccepted("", ApiSettings{}) == true` and `bearerAccepted("Bearer anything", ApiSettings{}) == true`. The test's name says the shipped default disables the control, so a reader is not misled into thinking 401 is live out of the box |
| **B8** | **with a token set, the control is real** (V6) | against `ApiSettings{.token = "s3cr3t"}`: `"Bearer s3cr3t"` **true**; `""` **false**; `"Bearer wrong"` **false**; `"bearer s3cr3t"` **false** (the scheme token is case-insensitive per RFC 7235 — **whichever way the builder finds the truth, the test states it and the code follows**; a disagreement here is reported, not papered over); `"Basic czNjcjN0"` **false**; `"Bearer  s3cr3t"` with two spaces **false**; a `Cookie:`-shaped value **false** |
| **B9** | **the forbidden carriers are forbidden** (V6) | `bearerAccepted` reads **only** the `Authorization` header: a token supplied as `?token=s3cr3t` or in a `Cookie` header is rejected, asserted by the function having no other input. A cookie is **ambient authority** — the browser attaches it to every request to `127.0.0.1:<port>` regardless of which page issued it, so any site the operator opens during a show is authenticated to this listener. Not a rebinding argument: a cookie jar keys on the host *name*, so a rebound request carries the attacker's cookies and never this app's |
| **B10** | the body cap is a number, not a hope (API-R17) | `bodyIsAcceptable(0, ApiSettings{})` true; `(8192, …)` true; `(8193, …)` **false** → 413. GET requests should carry no body at all; the cap is what makes §9's 415 unreachable rather than unimplemented |

- [ ] **Accept:** OFF ctest count rises; `measure_has_no_framework_deps` green with the risen count pasted; zero `warning C`; `wc -l` on all four files.
- [ ] **Mutation 1 (the one that matters):** implement `hostIsAllowed` as `host.find("127.0.0.1:4736") != npos` → **B1's substring trap goes red** and nothing else does. Paste it. That single red line is the DNS-rebinding defence proving it is present; revert.
- [ ] **Mutation 2:** drop the clamp's upper branch → B4 red; revert. **Mutation 3:** make `bearerAccepted` return `true` whenever the header is non-empty → B8 red; revert.
- [ ] **Commit:** `feat(app): the API's Host allowlist, method allowlist, Bearer check, point cap and body cap -- pure, tested at the shipped defaults and, for the token, deliberately not`

## Task C — `RateLimiter` and the conditional GET (OFF; record §3, §8, §9; §11 item 7)

Both are pure and both are real-time-safety controls, not hygiene: an uncapped poll rate and an uncapped `?points=` are two ways for a remote caller to make this program do unbounded work while a show is running.

**Files.** Modify `app/src/api/ApiPolicy.h` (≤ 150 unchanged — three declarations and the `RateLimiter` class) and `app/src/api/ApiPolicy.cpp` (**restated budget: ≤ 340**, from Task B's ≤ 200 plus roughly 90 lines of limiter, `etagFor` and `conditionalVerdict`; if it passes 400 the seam is *admission control* versus *request shape*, and the limiter is the half that moves). Create `app/tests/test_api_limits.cpp` (**NEW**, ≤ 240) holding **C1-C8**; modify `app/tests/CMakeLists.txt` in both places for it. **This split is why Task B's test file is capped at B1-B10** — eighteen cases in one file overruns the cap (D5).

**The window model is pinned, and V7 is why.** The first draft's C1-C3 all passed under either a fixed or a sliding window, so "30 requests per second" had no shipped meaning. **It is a sliding window.** A fixed window admits 30 at the end of one window and 30 at the start of the next — sixty loads inside one real second — and §4's accounting is that 30 is a *hard bound on this thread's traffic against the publish slot*. A bound that holds only on aligned seconds is not that bound.

The limiter takes an **injected** `std::chrono::steady_clock::time_point`. No real time, no sleeps: `grep -rn "sleep_for\|sleep(" app/tests/test_api_policy.cpp` must be empty, because a timing test that sleeps is a test that will be flaky on a CI runner.

| # | case | closed-form acceptance |
|---|---|---|
| **C1 (first)** | the ceiling is exactly the shipped default | `RateLimiter limiter{ApiSettings{}.maxRequestsPerSecond}`: requests 1..30 at `t0` are admitted, the **31st** is refused. `30` is read from `ApiSettings{}`, never written as a literal in the test |
| C2 | the window advances | after the refusal, advance past the window and the next request is admitted; advance by half a window and it is still refused |
| **C3** | **the boundary, including the value that discriminates** (V7) | 30 admitted at `t0`, then: `t0+999ms` **refused**; **`t0+1000ms` exactly** — the only value that separates `elapsed >= window` from `elapsed > window`, and the one the first draft skipped — asserted at **admitted**, with `>=` as the shipped comparison stated in the test's name; `t0+1001ms` admitted |
| **C4** | **sliding, not fixed** (V7) | 15 admitted at `t0`, 15 more at `t0+900ms` (30 in flight), then one at `t0+950ms` **refused** — under a fixed window aligned at `t0+1000ms` this would be the 31st of a window that is about to reset, and under a **fixed** implementation a request at `t0+1001ms` would admit 30 more for a real-second total of 45. Assert that at `t0+1001ms` exactly **15** are admitted before the next refusal, which only a sliding window produces |
| C5 | `ETag` is the sequence, quoted | `etagFor(12345) == "\"12345\""`. The quotes are part of an HTTP entity tag; a bare `12345` is a malformed validator and a conforming client will not echo it |
| C6 | 304 on a matching validator | `conditionalVerdict("\"12345\"", nullopt, 12345) == NotModified`; `("\"12344\"", …, 12345) == Serve`; `("", …) == Serve`; `("*", …, 12345) == NotModified` (`If-None-Match: *` matches any current representation) |
| C7 | `?since=` needs no validator | `conditionalVerdict("", 12345, 12345) == NotModified`; `("", 12344, 12345) == Serve`; `("", 12346, 12345) == **Serve**` — a client ahead of the server is a client that restarted, and serving it is the only recovery |
| C8 | the two agree, and `If-None-Match` wins | with both present and disagreeing, the header wins; the record specifies the header as the primary form and `?since=` as the form that needs no server-issued validator |

- [ ] **Accept:** OFF ctest count rises; zero `warning C`; the `sleep_for` grep pasted **empty**.
- [ ] **Mutation 1:** change `admit`'s comparison to `>` at the boundary → **C3's `t0+1000ms` case goes red and nothing else does**; revert. **Mutation 2:** reimplement as a fixed window (reset the counter when the second rolls over) → **C4 red**; revert. That mutation is the most natural way to write this class, which is why C4 exists.
- [ ] **Commit:** `feat(app): the API rate limiter -- sliding window against an injected clock -- and the ETag/304 decision as a pure function`

## Task D — `ApiSerialise`: `/status`, `/transfer`, `/spectrum`, `/bands` (OFF; record §6; §11 items 1, 2, 5, 6)

**Files.** Create `app/src/api/ApiSerialise.h` (**NEW**, ≤ 120), `app/src/api/ApiSerialise.cpp` (**NEW**, ≤ **300** — the fixed-axis blocks only; the spatial blocks get their own `.cpp` in Task E, and that split is now **unconditional** rather than "if the cap is reached", because discovering a cap mid-task is how a file ends up at 399 lines with two jobs in it), `app/tests/ApiFixture.h` (**NEW**, ≤ 120), `app/tests/test_api_serialise.cpp` (**NEW**, ≤ **260**, **D1-D8 only** — Task E opens its own, D5), `app/tests/golden/api-v1-snapshot.json` (**NEW** — `app/tests/golden/` does not exist today; `core/tests/golden/` does).

**Modify `app/tests/CMakeLists.txt` in three named places** (V11 — the first draft said only "modify", while every other CMake edit in this plan is named to the line):
1. `ApiSerialise.cpp` and `test_api_serialise.cpp` into `rtatool_analysis_tests`' source list (`:10-167`).
2. **A new compile definition** beside the existing `RTA_REPO_ROOT` at `:173-174`: `RTA_API_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"`. The precedent is `core/tests/CMakeLists.txt:76`, which is the identical `RTA_GOLDEN_DIR` definition on the core test target, and the reasoning is the same — a golden the test finds by a compiled-in absolute path runs from any working directory on any of the three CI OSes.
3. The GLOBS at `:282`.

**The device-free fixture, and it already has a precedent in the tree.** `tools/snapshot.cpp:181-190` composes a full `Snapshot` exactly the way this fixture must: `makeSyntheticSnapshot(SyntheticSpec{})` for bands and spectrum, then `makeSyntheticTransfer(fftSize, sampleRate, 18)`, then `makeSyntheticMtw()`, then `makeSyntheticAverage(fftSize, sampleRate, 4)` for `average` and `positions`. Every one is documented bit-identical for a given spec (`app/src/measure/SyntheticSnapshot.h:44` and the comments at `:57`, `:76`, `:90`) — the same property that makes `rta-view.png` reviewable as a byte-for-byte diff. `ApiFixture.h` lifts that composition into `app/tests/` so both serialiser tasks share one snapshot. **No sound card, no microphone, no thread, no device.**

**RED first.** `test_api_serialise.cpp` includes `api/ApiSerialise.h`; the build fails at the include. Paste it.

| # | case (record §6, §11) | acceptance |
|---|---|---|
| **D1 (first)** | absent coherence is an **absent key** | serialise a snapshot whose `transfer->coherence` is `nullopt`: the output contains **no** `"coherence"` substring at all. `TransferBlock::coherence` is an `optional` because a single frame gives coherence identically 1.0 at every frequency, so a broken engine looks perfect — that gate survives the wire or it was never a gate |
| D2 | axis kind, and `pointCount` is measured | `/transfer` emits `"kind":"uniform"` and **no** `frequencyHz` key; `axis.pointCount == transfer->magnitudeDb.size()` (API-R11), asserted against the fixture's actual size, not against `fftSize/2+1` |
| D3 | the schema version travels in the body as well as the path | every body carries `"schemaVersion":1`. A body that is saved to a file or pasted into an issue has left the URL behind |
| D4 | `/status` reports what this build actually serves | `available` is exactly `["transfer","mtw","bands","spectrum","average","positions"]` — six names (API-R12) — and contains **no** `"spl"`. `Snapshot` carries dBFS only: `grep -ni "spl\|leq\|calibration" app/src/measure/Snapshot.h` returns zero lines, and `rta::meter::Leq` has no `app/` caller. `"spl"` appears the day the Meters track puts it in the snapshot and not a day earlier |
| D5 | `underResolved` travels | `/bands` emits `underResolved` per band. It is the difference between a measurement and a band the FFT physically cannot resolve, and a client that drops it draws a fault that does not exist |
| D6 | the point cap bounds the output **length** | serialise with `Request{.points = clampPoints(1'000'000, ApiSettings{})}`: the emitted array has exactly 8192 entries and the response length is bounded by it. This is §11 item 6's second half, and it is the assertion that goes red when the clamp is removed |
| D7 | the golden — **a regression lock, not a correctness test** | `serialiseSnapshot(fixture, Request{})` compared byte-for-byte against `RTA_API_GOLDEN_DIR "/api-v1-snapshot.json"`. The `TEST_CASE` name must contain the words **regression lock**: it proves the format has not drifted, not that any number in it is right. Every number in it is already owned by the test that proved it (`test_analyser_transfer.cpp`, `test_analyser_mtw.cpp`, `test_average_group.cpp`, `test_synthetic_snapshot.cpp`) |
| D8 | the golden is UTF-8, LF, and regenerable | the committed file is byte-identical to what the serialiser emits; the test says in one line how to regenerate it. `memory/a-gen-script-runs-the-moment-you-invoke-it.md` — if a regeneration helper is added it takes an explicit flag, and `--help` must not overwrite the file |

- [ ] **Accept:** OFF ctest count rises; `measure_has_no_framework_deps` green with the risen scanned count pasted; zero `warning C`; `wc -l` on every new file (all < 400).
- [ ] **Mutation 1:** emit `"coherence": null` when the optional is empty → **D1 red**; revert. **Mutation 2:** emit an array of `1.0` instead → D1 red again (assert the substring, so both shapes fail); revert. **Mutation 3:** drop the clamp in `Request` → D6 red; revert. **Mutation 4:** change one digit in the golden → D7 red, which proves the lock is locked.
- [ ] **Commit:** `feat(app): the API serialiser -- status, transfer, spectrum, bands; absent coherence is an absent key`

## Task E — `ApiSerialise`: `/mtw`, `/average`, `/positions`, `/snapshot` (OFF; record §6; §11 items 3, 4)

The three blocks that carry a state a well-meaning implementer drops, plus the union endpoint API-R13 defines.

**Files.** Create `app/src/api/ApiSerialiseSpatial.cpp` (**NEW**, ≤ 300 — the split is unconditional, per Task D) and `app/tests/test_api_serialise_spatial.cpp` (**NEW**, ≤ 280, **E1-E8 only**). Modify `app/src/api/ApiSerialise.h` (the four new declarations; still ≤ 120), `app/tests/golden/api-v1-snapshot.json`, and `app/tests/CMakeLists.txt` in both places for both new files. **Budgets are restated here rather than inherited** (D5): a task that extends a file without restating its cap is a task that discovers the cap by breaking it.

| # | case (record §6, §11) | acceptance |
|---|---|---|
| **E1 (first)** | per-band `coherenceAvailable` survives | serialise an MTW snapshot with a **filling bottom band**: `bands[0].coherenceAvailable == false` in the output **and** the zeros at those indices are still present in `coherence`. `MtwBandDescriptor`'s own comment says an index whose band has not passed its gate holds `0.0f` and "must not be read as a measured zero"; a client that ignores this draws a bottom band reading zero coherence for five and a half seconds and reports a fault that does not exist. `makeSyntheticMtw()` states a **completed** measurement (its comment says `coherenceAvailable` is true in every band), so this case builds the filling band by hand from `rta::dsp::mtwBands` rather than waiting for a fixture that cannot produce it |
| E2 | MTW's axis is the other kind | `/mtw` emits `"kind":"explicit"` **with** a `frequencyHz` array, and every band descriptor field crosses: `firstIndex`, `pointCount`, `fftSize`, `windowSeconds`, `integrationSeconds`, `effectiveAverages`, `seamHz`, `coherenceAvailable`. Eight fields, none optional |
| E3 | `absence` is an array of **strings** | `"present"` / `"noContributor"` / `"noWeight"`, at named bin indices (API-R4). OSM flattens enums to their integer value with no name, so a renumbering silently changes meaning with nothing on the wire to reveal it — a measured failure mode in a shipping product. `memory/a-placeholder-for-an-absent-result-erases-its-state.md` is about this exact field being rewritten from `NoWeight/2` to `NoContributor/0` |
| E4 | `membership` is a string too | `"member"` / `"excludedDifferentReference"` / `"excludedOverCapacity"` in `/positions` |
| E5 | the two quantities keep their code names | `/average` emits `phaseAgreement` and `weightedCoherence` **spelled as the code spells them**, and the schema note says neither is a coherence estimate. L6b §2 and §4 exist because those two are routinely mistaken for one; a wire format that renamed either to something friendlier would undo that record |
| E6 | `/positions` carries no per-bin array | the emitted `/positions` body contains no array longer than the position count. L6b §6 fixed that publish cost must be O(1) in N; an API that re-expanded it would reintroduce the churn that decision refused |
| E7 | `/snapshot` is a union, and absence is absence | with `mtw == nullopt`, `/snapshot` contains **no** `"mtw"` key; with every block present, `/snapshot` contains all six plus the `/status` fields; and `soloTransfer` never appears (API-R10) |
| E8 | mutation-visible field list | for each of the eight band-descriptor fields, dropping it from the emitter turns E2 red. Written as a data-driven loop over the field names so a ninth field added later without a test is visible |

- [ ] **Accept:** OFF ctest count rises; `measure_has_no_framework_deps` green with the risen count pasted; zero `warning C`; golden regenerated and its diff reviewed line by line (a golden that changed for a reason nobody read is not a lock).
- [ ] **Mutation 1:** drop `coherenceAvailable` → E1 red; revert. **Mutation 2:** emit `"absence":2` instead of `"noWeight"` → E3 red; revert. **Mutation 3:** rename `weightedCoherence` to `coherence` in the emitter → E5 red; revert — and note this is the one mutation whose *green* version would have been the most plausible-looking code in the lane.
- [ ] **Commit:** `feat(app): the API serialiser -- mtw with per-band coherenceAvailable, spatial average and positions with string enums, and the union snapshot`

## Task F — vendor a JSON parser, test-only, and assert the schema's invariants (OFF; API-R16; V4, V8)

**This task exists because of the single hardest finding against the first draft.** Every OFF assertion up to here is a substring match or a byte-compare against a file the same serialiser produced. **A stable but malformed document passes all of them**, and OFF is the only configuration CI runs.

**Why a third-party parser and not one written here.** A hand-written validator needs no dependency and fails on the principle this project already applies to golden vectors: **a validator must not share an author with the thing it validates.** Golden vectors come from NumPy/SciPy rather than a second in-house implementation for exactly that reason. A parser written by the person who wrote the serialiser reproduces their misunderstanding of JSON on *both* sides, where it cancels out and reads as proof. Against that, the honest cost of vendoring: a fourth-party header, a second licence paragraph, a second provenance entry, a second guard, and real compile time. **The cost is paid once, in one TU, and bought a class of defect no substring match can reach.**

**Files.** Create `external/nlohmann/json.hpp` (**NEW**, upstream byte-for-byte, **no SPDX line added**), `external/nlohmann/LICENSE.MIT` (**NEW**, upstream verbatim), `external/nlohmann/PROVENANCE.md` (**NEW**, ≤ 30: URL, release tag, SHA-256, date, and the sentence "test-only; shipped code must never include it, and `no_json_parser_in_shipped_code` enforces that"), `app/tests/test_api_schema.cpp` (**NEW**, ≤ 260 — **the only TU in the repository that includes `json.hpp`**, the same one-TU discipline record §2 applies to the server header). Modify `app/tests/CMakeLists.txt` (source list, the GLOBS, and an include directory for `external/`) and `README.md` (the vendored-dependency line gains its second entry).

| # | case | acceptance |
|---|---|---|
| **F1 (first)** | every endpoint emits **well-formed JSON** | `nlohmann::json::parse` succeeds on all eight bodies from the Task D/E fixture, and on the empty-optional variants. This is the assertion the whole task exists for; it goes first and it must be seen to fail against a deliberately broken emitter |
| F2 | the golden is well-formed too | parse `RTA_API_GOLDEN_DIR "/api-v1-snapshot.json"` from disk. A regression lock on a malformed document locks in the malformation |
| **F3** | **every number in every document is a finite float32 value** (V8) | walk the parsed document; for every numeric leaf assert it is not NaN, not infinite, and that `static_cast<float>` of it round-trips to itself — i.e. the wire carries nothing a float32 consumer will silently widen or lose. The first draft's A4 covered the *emitter primitive*; this covers the *document* |
| **F4** | **`sequence` is monotonic across two polls** (V8) | serialise two snapshots whose `sequence` differs by one and assert the parsed `sequence` rises by exactly one and that `etagFor` tracks it. `Snapshot::sequence` (`Snapshot.h:205`) is both the ETag source and the `?since=` token, so a non-monotonic sequence breaks every conditional-GET client; nothing asserted it before |
| F5 | absence is absence, at the document level | with `coherence == nullopt`, `doc.contains("coherence")` is **false** — the structural form of D1, which a substring match can only approximate |
| F6 | enums are JSON strings, at the document level | `doc["absence"]` is an array and every element `is_string()`; `doc["positions"][i]["membership"].is_string()`. E3/E4 asserted the spelling; this asserts the **type**, which is what a renumbering would change |
| F7 | A4 and A5 re-asserted through the parser | a document containing a non-finite value parses with a JSON `null` at that key; a position name carrying `"`, `\`, a control byte and a multi-byte UTF-8 sequence parses back to the **identical** `std::string` |

- [ ] **Accept:** OFF ctest count rises; `sha256sum external/nlohmann/json.hpp` pasted and matching `PROVENANCE.md`; zero `warning C` (the parser header is included the same way the server header is — `#pragma warning(push, 0)` or a `SYSTEM` include directory; **measure and say which**); the OFF build's wall time before and after, because this header is known to be expensive and CI runs on three machines.
- [ ] **Mutation:** emit a trailing comma before the closing brace of `/status` → **F1 red and every substring test still green**, which is the whole argument for this task in one paste; revert.
- [ ] **Commit:** `test(app): vendor nlohmann/json (MIT, test-only) and assert what no substring match can -- well-formed JSON, finite float32 numbers, a monotonic sequence`

## Task G — the desktop half of §12 constraint 2 (OFF; record §11 item 13, §12 constraint 2)

Twenty minutes, and it is the only thing making §6's units deviation provable rather than merely intended.

**Files.** Modify `app/tests/test_readouts.cpp` **only**. No new formatter, no new file, no CMake edit.

**The three functions already exist and are already tested** — re-verified at `af8a9d0`, which matters because `main` landed a commit the same week whose subject is *"two of the three formatters I told the next lane to reuse do not exist"* (`171cd53`, L6a's record naming `formatDb`/`formatCoherence`). **The names this plan gives are the ones that exist**, and that is exactly the trap it was written to avoid. `app/src/view/Readouts.h`: `formatHz` (`:72`), **`formatTrim`** (`:79`), **`formatAgreement`** (`:87`) — grep handle `inline std::string format` if those numbers drift. They are pinned by `app/tests/test_readouts.cpp:100-115`, `Readouts.h` is already in `measure_has_no_framework_deps`'s GLOBS, and `rta::view::formatHz` already has a live caller at `app/src/view/DevicePanel.cpp:102`. **No new formatter may be added for this lane.** The three name mappings, because they are not the ones an implementer would guess: Hz → `formatHz`; the one-decimal dB rule → **`formatTrim`**; the two-decimal 0..1 rule (coherence and `phaseAgreement`) → **`formatAgreement`**.

| # | case | acceptance (the arguments are the golden JSON's own literals) |
|---|---|---|
| **G1** | the desktop readout rounds the wire's own values | `formatHz(1000.4) == "1000 Hz"`; `formatTrim(-3.2145123) == "-3.2 dB"` (`magnitudeDb[0]`, §6's golden); `formatAgreement(0.9731445) == "0.97"` (`coherence[0]`). The dB and Hz functions return the **unit inside the string**, so the assertion carries the suffix or it fails on the suffix rather than on the rounding. The parameters are `double` and the float32→double widening is exact, so the string is the rounding of the *same* value the wire carried |

The `TEST_CASE` name must say it is the **desktop half of §12 constraint 2**. Do **not** pair the dB rule with `8.5859375` — that literal is `effectiveAverages`, a **count**, not a level; `formatTrim(8.5859375)` does return `"8.6 dB"`, but a count formatted as dB is a wrong test.

- [ ] **Accept:** OFF ctest count rises by one; zero `warning C`.
- [ ] **Mutation 1:** round in the serialiser instead → **Task D's golden (D7) goes red**, which is the point. **Mutation 2:** change `formatTrim`'s precision to two decimals → G1 red and `test_readouts.cpp:100-115` red with it; revert both.
- [ ] **What this does NOT prove, and the plan says so rather than pretending:** that L6a's **JavaScript** viewer rounds the same way. A C++ test cannot reach it. L6a either ships the same thresholds against the same golden values plus its own test, or records §12 constraint 2 as **untested for the viewer** and labels it so. This lane proves the desktop half and hands over a named seam.
- [ ] **Commit:** `test(app): the desktop half of the rounding constraint -- the wire's own float32 through the three formatters that already exist`

## Task H — vendor cpp-httplib (OFF after API-R15; record §2; API-R2, R3, R7, V9)

**Files.** Create `external/cpp-httplib/httplib.h` (**NEW**, upstream byte-for-byte, **no SPDX line added**), `external/cpp-httplib/LICENSE` (**NEW**, upstream's MIT text verbatim), `external/cpp-httplib/PROVENANCE.md` (**NEW**, ≤ 50). Modify the root `CMakeLists.txt` (one `INTERFACE` target, **outside** the `if(RTA_BUILD_APP)` block now that the server is OFF-built) and `README.md`.

**The licence line, which is the part that must not be got wrong.** This project is AGPL-3.0-or-later. cpp-httplib's `LICENSE` is the MIT text — a lax permissive non-copyleft licence the FSF calls compatible with the GNU GPL — and it flows into an AGPLv3 work imposing only notice retention. That is why the `LICENSE` file is vendored beside the header and never deleted. **Mongoose is disqualified and this is the trap worth naming**: its `LICENSE` offers GPL-2.0 with **no "or any later version"**, incompatible with AGPLv3, and the commercial arm conflicts with the AGPL source release this project is committed to. Neither arm works. Do not "simplify" this dependency later without re-reading that paragraph.

**`CPPHTTPLIB_*` macros to leave UNDEFINED. This list was incomplete in the first draft (V9) and the two additions are in the class the plan itself calls load-bearing.**

| macro | left undefined because |
|---|---|
| `CPPHTTPLIB_OPENSSL_SUPPORT` | record §2 and §9: **no TLS in v1**, therefore no OpenSSL link dependency and **no second licence to reason about** |
| **`CPPHTTPLIB_MBEDTLS_SUPPORT`** | **added after V9** — upstream README `:70-71` lists it as a TLS backend in exactly the same class. A list presented as complete that omits two of its own category is worse than no list |
| **`CPPHTTPLIB_WOLFSSL_SUPPORT`** | **added after V9**, same reason |
| `CPPHTTPLIB_ZLIB_SUPPORT` | a zlib link dependency for a compression nothing on loopback needs |
| `CPPHTTPLIB_BROTLI_SUPPORT` | same, plus a second third-party licence |
| `CPPHTTPLIB_ZSTD_SUPPORT` | same |
| `CPPHTTPLIB_NO_EXCEPTIONS` | the rest of the app builds with exceptions; changing that for one TU is an ODR-shaped hazard, not a tidy-up |

**One thing the macro list cannot cover, and `PROVENANCE.md` must say so (V9).** At v0.56.0 **WebSocket support is compiled in with no macro guard** (upstream README `:1673-1713`). The record forbids push of any shape (§3, §13) and this plan repeats it — yet the vendored TU carries the upgrade machinery whatever either document says. **Judgement: acceptable — and the first draft of this paragraph gave the wrong mechanism, which is worth more than the judgement (D4).** It said the method allowlist answers the upgrade with 405. **It does not.** A WebSocket upgrade *is* a `GET` — `GET /path HTTP/1.1` carrying `Upgrade: websocket` and `Connection: Upgrade` — so `methodIsAllowed` accepts it, and with no upgrade handler registered httplib falls through to **ordinary routing**: `200` with the route's normal JSON body, or `404` on an unknown path. Never `405`.

**What actually makes it unreachable is narrower and worth stating precisely:** the server registers eight `Get` routes and **nothing that can upgrade a connection**, so it never emits `101 Switching Protocols` and never sends a `Sec-WebSocket-Accept` header. The client's upgrade offer is simply ignored and answered as an ordinary request. The judgement stands — no WebSocket can be established — but it rests on the absence of a handler alone, not on any check this code performs. **Task I's I11 turns that from a claim into a test**; this paragraph and `PROVENANCE.md` carry the reasoning, and the correction is named here so the next reader does not inherit the plausible-sounding 405 story.

Everything §8 tunes — the four timeouts, the keep-alive max count, the thread pool, and API-R17's payload limit — is set at **run time** on the `httplib::Server` object (`set_read_timeout`, `set_write_timeout`, `set_keep_alive_timeout`, `set_keep_alive_max_count`, `new_task_queue`, `set_payload_max_length`), never by a compile-time macro, so the values live next to the record's reasoning in `ApiServer.cpp` instead of in a build file.

**Station-4 hazard, and it has already been observed once.** A copy of `httplib.h` lying around in a scratch directory or a package cache can report the **same version string** and still be a different file — one such copy measured **22885 lines / sha256 `a6e65d30…`** against the release's 22875 / `1f99e518…`. A version string is not an identity. **Fetch from the release tag, verify the SHA-256 against it, and commit that file — never the one already on the machine**, and never copy either figure out of this plan without re-measuring. The whole argument for the amalgamated header over `split.py` (API-R2) is that it is hash-checkable; a vendored file nobody hashed throws that away and leaves a dependency whose provenance is a guess.

- [ ] **Accept:** OFF **and** ON configure succeed; `sha256sum external/cpp-httplib/httplib.h` **pasted**, matching `PROVENANCE.md` **and matching the upstream release tag** (the verifier read v0.56.0 as 22875 lines, sha256 `1f99e51881c4c9d0649b27c611442c2f4d9bcfec5a22a14d5fcd1f8106f730b4` — **confirm against the tag, do not copy, and see the hazard above**); `wc -l external/cpp-httplib/httplib.h` pasted beside it; `external/cpp-httplib/LICENSE`'s first line is the MIT header.
- [ ] **Measure, do not assume (API-R7, and now on three platforms):** the OFF build's `warning C` count **before** and **after**, pasted. If it rose, apply the `#pragma warning(push, 0)` wrap in Task I and re-measure; if insufficient, the `SYSTEM` include + `/external:W0 /external:anglebrackets` fallback, and say which was needed. **Record §2's compile figures were MSVC-only; CI now compiles this header on ubuntu and macos too, so report the real per-OS cost from the CI logs rather than transferring the Windows number.**
- [ ] **Commit:** `build: vendor cpp-httplib (MIT) as the amalgamated header, TLS and every optional backend left undefined`

## Task I — `ApiServer`: one `std::thread`, one httplib TU, tested over a real socket on three OSes (OFF; API-R15; record §4, §8, §9, §10; V2, V5, V6)

**Files.** Create `app/src/api/ApiServer.h` (**NEW**, ≤ 140), `app/src/api/ApiServer.cpp` (**NEW**, ≤ 380 — the seam if it grows is validation-versus-routing, and Task B already took the validation half out), `app/tests/RawHttpClient.h` (**NEW**, ≤ 120 — see below), `app/tests/test_api_server.cpp` (**NEW**, ≤ 280). Modify `app/tests/CMakeLists.txt` (source list, GLOBS, link `ws2_32` on `WIN32` — cpp-httplib needs Winsock and the test target has never linked it).

**`ApiServer.h` is a pimpl, and the guard is why (D2).** A by-value `httplib::Server` member would force `#include <httplib.h>` into `ApiServer.h` — and Task K's guard scans `app/**/*.h` with `ALLOW` naming **`ApiServer.cpp` and nothing else**, so a *correct* build would turn the guard red. That is the guard punishing the design rather than protecting it, and the fix is one line of C++ rather than a weaker guard:

```cpp
// app/src/api/ApiServer.h  --  includes NOTHING from httplib
class ApiServer {
public:
    ApiServer(measure::SnapshotSource& source, ApiSettings settings);
    ~ApiServer();                                   // out of line: Impl is incomplete here
    ApiServer(const ApiServer&) = delete;
    ApiServer& operator=(const ApiServer&) = delete;

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] int  boundPort() const noexcept;  // -1 until a successful bind; see D1
private:
    struct Impl;                                    // holds httplib::Server and the std::thread
    std::unique_ptr<Impl> impl_;
};
```

The out-of-line destructor is **required**, not stylistic: `std::unique_ptr<Impl>` cannot be destroyed where `Impl` is incomplete, and letting the compiler generate it in the header is exactly the mistake that drags the include back in. `ApiServer.cpp` is then the one and only file in the repository that includes `httplib.h`, which is what §11 item 10's title claims and what Task K's sentinel 2 checks.

**Why the test client is a raw socket and not `httplib::Client` (V2's second-order note).** A client built on `httplib::Client` would be a **second file including `httplib.h`**, and Task K's guard permits exactly one. Three ways out were weighed. Making `ALLOW` a list weakens the guard's headline claim from *exactly one* file to two and makes sentinel 2 ambiguous about which file must still carry the include. Exposing a pure `respond(RequestView)` seam and testing that instead would keep the guard intact but **would not test a socket at all**, which is the entire point of V2. So: `RawHttpClient.h` sends three fixed request lines and reads a status line — about forty lines, no parsing, no keep-alive, no timeouts, no security surface. **That is not the hand-rolling §2 rejected**; §2 rejected hand-rolling a *server*, and listed exactly the machinery a fixed-string test client does not contain.

**The request path, in this order, and the order is the decision.**

1. `RateLimiter::admit(now)` → **429 before any `latest()`**. The limiter is the real-time-safety control, so it runs before the work.
2. Body-size cap → **413** (API-R17).
3. `hostIsAllowed(req.get_header_value("Host"), settings.port)` → **403 before any handler runs**. Highest-value control in the whole API.
4. `methodIsAllowed` → **405**, with an `Allow: GET, HEAD, OPTIONS` header. **This is *not* what answers a WebSocket upgrade** — an upgrade is a `GET` and passes this check (D4); see I11 and Task H.
5. `bearerAccepted` when `settings.token` is non-empty → **401**. Header only, never a cookie, never a query parameter.
6. Route → `clampPoints` → **one** `source.latest()` → null check (no snapshot yet ⇒ 503) → `conditionalVerdict` → **304 with the same `ETag` and no body**, or serialise from the copy → **200** with `ETag: "<sequence>"`.
7. **No CORS headers**, and no pretence that their absence is a defence: a `GET` with only safelisted headers is a *simple* request, gets no preflight, and is **executed** by this program before the browser decides whether the script may read the reply.

The thread is a **`std::thread`** (API-R15). `ApiServer` starts it only when `settings.enabled`, and `~ApiServer` calls `impl_->svr.stop()` **then** joins — the belt-and-braces second guarantee `AnalysisThread`'s destructor comment already argues for, because getting shutdown wrong is a crash that happens once, at exit, on a customer's machine.

**Binding: `bind_to_any_port`, not `bind_to_port`, and the first draft was wrong about this (D1).** `Server::bind_to_port(const std::string& host, int port, int socket_flags = 0)` returns **`bool`** and **discards** the port it bound; `Server` has **no `port()` accessor** — that member is on `Client`, not `Server` (verifier-confirmed against `httplib.h` v0.56.0; the builder re-confirms both signatures in the vendored header at Task H before writing a line of this). So "bind to 0 and read the port back" does not work as written. The one that returns the port is **`Server::bind_to_any_port(const std::string& host, int socket_flags = 0)`**, which returns the bound port or `-1`.

```cpp
// ApiServer::Impl, on the CONSTRUCTING thread -- before the std::thread starts
boundPort = (settings.port == 0) ? svr.bind_to_any_port(settings.bindAddress)
          : (svr.bind_to_port(settings.bindAddress, settings.port) ? settings.port : -1);
// then, on the server thread: svr.listen_after_bind();
```

**Splitting bind from listen is what removes the startup race, and that is the reason it is used here rather than `listen()`.** The bind completes on the constructing thread, so `boundPort()` is already valid when the constructor returns and **before** the server thread has run at all. Every over-the-wire case below therefore reads `server.boundPort()` — **no `sleep_for`, no polling on `is_running()`, no retry loop, and no fixed port a developer's own running instance could already hold.** `grep -rn "sleep_for\|sleep(" app/tests/test_api_server.cpp` must come back empty, exactly as it must for Task C.

| # | case | acceptance |
|---|---|---|
**Every over-the-wire case reads its port from `server.boundPort()`** (D1). `<port>` below is that value, never a literal and never `settings.port`, because the fixture binds with `settings.port == 0`.

| **I1 (first)** | it binds nothing it was not told to | with `settings.enabled == false`, constructing and destroying `ApiServer` starts no thread and opens no socket, and **`boundPort() == -1`**; asserted on the object's own state, not with `netstat` |
| **I1b** | the port is knowable before the first request (D1) | with `settings.port == 0` and `enabled == true`, **`boundPort()` is > 0 the moment the constructor returns**, before the server thread has run. This is the case that would have caught the first draft's `bind_to_port`-returns-bool error, and it is the reason no test in this file sleeps |
| I2 | one real request over loopback | `GET http://127.0.0.1:<port>/api/v1/status` → **200**, body parses with the Task F parser, `schemaVersion == 1`, and the `ETag` header equals `etagFor(sequence)` |
| I3 | the method boundary is real over the wire | `POST /api/v1/status` → **405** with an `Allow` header; `HEAD /api/v1/status` → 200 with no body |
| I4 | the `Host` boundary is real over the wire | `Host: attacker.example:<port>` on a **valid** path → **403**, body not served |
| **I5** | **the `Host` check runs BEFORE routing, and this is the test that tells the difference** (V5) | `Host: attacker.example:<port>` on a path that **does not exist** (`/api/v1/nope`) → **403**, not 404. Pre-routing gives 403; a per-handler check gives 404, because routing ran first and found nothing. The first draft settled for "delete the check, I4 goes red", which tests **presence, not order**, and §9 control 1's wording is "before any handler runs" |
| **I6** | **the token path executes** (V6) | against a server constructed with `ApiSettings{.token = "s3cr3t"}`: no `Authorization` header → **401**; `Bearer wrong` → **401**; `Bearer s3cr3t` → **200**; the same token as `?token=s3cr3t` → **401**; the same token in a `Cookie` header → **401**. The shipped default is an empty token, so this is the one server case that must not use it, and the test's name says why |
| **I7** | **the CORS posture is asserted, not assumed** (V6) | a 200 response carries **no** `Access-Control-Allow-Origin`, no `Access-Control-Allow-Credentials` and no `Vary: Origin`, at the shipped defaults and with an `Origin: https://evil.example` request header present |
| I8 | 304 round trip | `GET /api/v1/status`, then repeat with `If-None-Match` set to the returned `ETag` → **304**, empty body, same `ETag` |
| I9 | 413 over the wire (API-R17) | a `GET` carrying a body larger than `maxRequestBodyBytes` → **413**, and the handler never ran |
| I10 | shutdown is clean and repeatable | construct / start / stop / destroy in a loop ten times: no hang, no leaked thread, and a fresh ephemeral bind succeeds each time with a **different** `boundPort()` being acceptable. **This is one of the two behaviours that differ most across Windows, Linux and macOS sockets, and after API-R15 it runs on all three** |
| **I11** | **no WebSocket can be established, and the reason is measured** (D4) | `GET /api/v1/status` carrying `Upgrade: websocket`, `Connection: Upgrade` and a `Sec-WebSocket-Key` → **200 with the ordinary JSON body**; the response status is **not `101`** and carries **no `Sec-WebSocket-Accept`** header. The same request to `/api/v1/nope` → **404**. Both are *not* 405: an upgrade is a `GET`, so the method allowlist passes it (the first draft claimed 405 — D4). What makes an upgrade impossible is that **no handler registers one**, and this case is what turns that from a claim into an observation. **If the measured behaviour differs — if the vendored header answers an upgrade itself — that is a finding for the orchestrator and a change to Task H's judgement, not a number to bend** |

- [ ] **Accept:** OFF ctest `base_off + N` on **all three CI operating systems**, not only this machine — that is what API-R15 bought and the PR body must show it. Zero `warning C` (API-R7 — paste the count); `wc -l` on all four new files; `grep -n "httplib" app/src/api/ApiServer.h` comes back **empty** (D2 — the pimpl is what keeps Task K green on a correct build); the `sleep_for` grep over `test_api_server.cpp` pasted **empty** (D1).
- [ ] **Mutation 1 (the order mutation, and it is the important one):** move the `Host` check **after** routing → **I5 returns 404 and goes red while I4 stays green**; paste both, revert. **Mutation 2:** delete the `Host` check entirely → I4 and I5 both red; revert. **Mutation 3:** emit `Access-Control-Allow-Origin: *` → I7 red; revert.
- [ ] **Stated limit, not implied:** nothing test-visible catches a second `latest()` inside one handler. That constraint is held by review and by the file's own comment, and this line is where the plan says so instead of leaving a reader to assume a test exists.
- [ ] **Commit:** `feat(app): ApiServer -- one std::thread, one httplib TU, limiter then body cap then Host then method then Bearer then one latest()`

## Task J — composition-root wiring (**ON only — the only ON task in the lane**; record §4, §10)

**Files.** Modify `app/src/MainComponent.h` (one member, declared in the right place) and `app/src/MainComponent.cpp` (construction). Nothing else.

**Declaration order is load-bearing and this is trap T-1's third case.** `audioIo_` is declared before `analysisThread_` so the bus outlives the thread reading it (`MainComponent.h:127-131`, class comment at `:36-54`). `apiServer_` reads `analysisThread_.latest()`, so **`apiServer_` is declared AFTER `analysisThread_`** — declared later means destroyed **first**, which is exactly what is needed: the server stops and joins while the `SnapshotSource` it holds a reference to is still alive. Reversing the two is a shutdown crash nobody sees in development.

```cpp
// MainComponent.h, inside the trap T-1 block
rta::platform::AudioIo audioIo_;
rta::measure::AnalysisThread analysisThread_;
std::unique_ptr<rta::measure::SyntheticInput> syntheticInput_;
// apiServer_ reads analysisThread_ as a SnapshotSource&, so it is declared
// AFTER it and therefore destroyed BEFORE it. See the class comment, T-1.
std::unique_ptr<rta::api::ApiServer> apiServer_;
```

- [ ] **Accept:** ON builds; ON ctest count unchanged by this task (wiring, not behaviour); the app starts and exits cleanly with `api.enabled = false` (the shipped default), i.e. **nothing observable changes for an operator who did not ask for the API** — the whole point of the default.
- [ ] **Manual check the owner can repeat, with the exact command:** build ON, set `enabled = true` in the constructed settings, run `rtatool`, and in a terminal `curl -s http://127.0.0.1:4736/api/v1/status`. Expect a JSON body with `schemaVersion`, `sequence` and a six-name `available`. Then `curl -s -o NUL -w "%{http_code}" -H "Host: attacker.example:4736" http://127.0.0.1:4736/api/v1/nope` — expect **403**, not 404.
- [ ] **Commit:** `feat(app): the composition root owns the API server; declaration order keeps the snapshot source alive across its shutdown`

## Task K — prove the guards still GUARD (both configs; record §11 items 10, 11, 14; API-R16; V3, V8)

No new behaviour; a procedure whose output goes in the PR body. Every count is **read from the guard's own line**, never predicted here.

**Files.** Create `core/tests/check_no_server_library.cmake` (**NEW**, ≤ 180) and `core/tests/check_no_json_parser.cmake` (**NEW**, ≤ 140). Modify `core/tests/CMakeLists.txt` (two `add_test` blocks, beside `no_std_atomic_over_shared_ptr` at `:149-154`, which is the exact registration to copy).

**These are NEW scripts modelled on `check_no_std_atomic_shared_ptr.cmake`, not re-invocations of it.** `PATTERN` is `set()` inside that script at `:134` and `SENTINEL_PATTERN` at `:141` — neither is a `-D` argument, so they cannot be overridden from a registration. Copy the file, change those two literals and the message text. **Copy `rta_strip_comments` (`:128-132`) with it**, because the sentinel is matched against comment-stripped source and that is what stops a mention in prose from standing in for the code.

**One deliberate deviation from "copy and change nothing else" (V3).** The original's source glob at `:51-52` collects only `*.h` and `*.cpp`, so `.hpp`, `.cc` and `.inl` are blind spots the copy would inherit. **The copies glob `*.h;*.hpp;*.cpp;*.cc;*.inl`.** One line, strictly more coverage, and it is named here so a reviewer diffing the two scripts knows the difference is intended. (Widening the *original* is a separate change and belongs in its own PR.)

**The two literals, written out rather than described (D2).** They are `set()` inside the script, never `-D` arguments:

```cmake
# check_no_server_library.cmake -- anchored on the INCLUDE DIRECTIVE, not on the
# bare word, which is this repo's own idiom for an include-scanning guard:
# check_no_framework_deps.cmake:54 is
#   "#[ \t]*include[ \t]*[<\"](juce|JuceHeader|Q[A-Z]|portaudio|RtAudio|asio)"
# Anchoring deliberately narrows the false-positive surface: a comment that
# merely NAMES cpp-httplib is fine, and only a literal include directive trips.
set(PATTERN          "#[ \t]*include[ \t]*[<\"](httplib|civetweb|mongoose)")
set(SENTINEL_PATTERN "#[ \t]*include[ \t]*<httplib\\.h>")
```

**Both sentinels get copied into `check_no_server_library.cmake`. A copy that takes only the first is weaker than the original it cites.**

- `if(NOT ALLOW IN_LIST SOURCES)` (`:65`) — the allowed file is inside the scanned set. Its analogue here is `app` being in `DIRS`.
- `if(NOT ALLOW_CODE MATCHES "${SENTINEL_PATTERN}")` (`:145`) — the allowed file **still contains** the thing it is the sole exception for. Its analogue: **`ApiServer.cpp` must still contain `#include <httplib.h>`**.
- And the original's `if(SOURCES STREQUAL "")` → `FATAL_ERROR` (`:56-58`) comes across too. A guard that scanned nothing is the failure both copies exist to make loud.

**`ALLOW` is `ApiServer.cpp` and nothing else, which is precisely why `ApiServer.h` must be a pimpl (D2).** The scan covers `app/**/*.h`, so a by-value `httplib::Server` member — forcing the include into the header — would turn this guard red on a *correct* build. The guard is not weakened to accommodate that; the header is.

```cmake
# core/tests/CMakeLists.txt, beside no_std_atomic_over_shared_ptr
add_test(NAME no_server_library_outside_api
    COMMAND ${CMAKE_COMMAND}
            "-DDIRS=${CMAKE_SOURCE_DIR}/core;${CMAKE_SOURCE_DIR}/platform;${CMAKE_SOURCE_DIR}/ui;${CMAKE_SOURCE_DIR}/tools;${CMAKE_SOURCE_DIR}/app"
            -DALLOW=${CMAKE_SOURCE_DIR}/app/src/api/ApiServer.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_server_library.cmake
)

# API-R16: the parser is a TEST tool. Note app/src, NOT app -- app/tests is
# outside the scan by construction, so there is no ALLOW file at all and the
# guard's claim is the strong one: shipped code never includes a JSON parser.
add_test(NAME no_json_parser_in_shipped_code
    COMMAND ${CMAKE_COMMAND}
            "-DDIRS=${CMAKE_SOURCE_DIR}/core;${CMAKE_SOURCE_DIR}/platform;${CMAKE_SOURCE_DIR}/ui;${CMAKE_SOURCE_DIR}/tools;${CMAKE_SOURCE_DIR}/app/src"
            -DWITNESS_DIR=${CMAKE_SOURCE_DIR}/app/tests
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_json_parser.cmake
)
```

`check_no_json_parser.cmake` has no `ALLOW`, so it cannot carry sentinel 1 (*"is the allowed file inside the scanned set?"* — there is no allowed file) and it replaces sentinel 2 with a **witness**: at least one file under `WITNESS_DIR` must include the parser, or the guard `FATAL_ERROR`s. A parser nothing tests with has stopped meaning anything, and a guard that keeps printing OK over that is the failure mode sentinel 2 exists to prevent, in its other direction. Its `PATTERN` is
`"#[ \t]*include[ \t]*[<\"]([^>\"]*/)?(json\\.hpp|nlohmann|rapidjson|picojson|json/json\\.h)"`, and it keeps the empty-`SOURCES` `FATAL_ERROR` too.

**Losing sentinel 1 costs something specific, and D3 named it: without an `ALLOW IN_LIST SOURCES` check, a typo in any `DIRS` entry is silent** — the remaining directories still glob files, `SOURCES` is non-empty, the witness still passes, and the guard prints OK while watching four directories instead of five. **The replacement is not another sentinel but two more reds** (below), one planted under `core/` and one under `platform/`, which is the same thing V3 demanded of the server guard and for the same reason.

**`${CMAKE_SOURCE_DIR}/` on every `DIRS` entry and on `ALLOW`** — `file(GLOB_RECURSE)` returns **absolute** paths, so a relative `ALLOW` dies either at the `if(NOT EXISTS "${ALLOW}")` check or at `:65`. **`app` must be in the first guard's `DIRS`** and that is not a detail: omit it and `ALLOW IN_LIST SOURCES` is false and the guard `FATAL_ERROR`s on every run.

- [ ] **GREEN, both new guards, in BOTH configs.** `ctest -R "no_server_library_outside_api|no_json_parser_in_shipped_code"` green in `build-lapi` **and** `build-lapi-on`. Paste both.
- [ ] **RED 1, sentinel 1:** drop `app` from the first registration's `DIRS` → `FATAL_ERROR` naming `ApiServer.cpp` as unwatched. Paste; restore.
- [ ] **RED 2, sentinel 2:** delete `#include <httplib.h>` from `ApiServer.cpp` → the guard fails saying it is not proving anything. Paste; restore. **Then rebuild from the current tree and hash-check against HEAD** — `memory/mutation-testing-needs-the-exe-deleted-first.md`: restoring a header leaves objects built from the mutated one, and a header mutation is not recompiled at all unless a dependent `.cpp` is touched.
- [ ] **RED 3, the offender scan inside `app/`:** add `#include <httplib.h>` to `app/src/api/ApiSerialise.cpp` → the guard names that file. Paste; remove. **This proves the OFF-testability split is enforced and not merely intended.**
- [ ] **RED 4, the false-positive case (API-R8), restated for the anchored `PATTERN` (D2):** the pattern matches an **include directive**, not the bare word, so merely naming cpp-httplib in a comment is now fine — which is a deliberate narrowing, and the plan says so where the literal is given. What still trips it is a **literal `#include <httplib.h>` line inside a `/** ... */` doxygen block** — a usage example in a doc comment, which is an ordinary thing to write — because `rta_strip_comments` removes only `/* ... */` bodies **containing no `*`** (`check_no_std_atomic_shared_ptr.cmake:129-130`). Put exactly that in `ApiPolicy.h` → red. Paste, then move the example into `//` lines and show green. **A guard's false positives are part of its contract, and this one is narrower than the first draft's but has not gone away.**
- [ ] **RED 5 — the one the first draft did not have (V3): plant the offender OUTSIDE `app/`.** Add `#include <httplib.h>` to a file under **`core/`** → the guard names it. Paste; remove. All four earlier reds exercise `app/` or the `ALLOW` file, so the guard's *headline* claim — that `core/`, `platform/`, `ui/` and `tools/` contain no server library at all — was the half nothing turned red. **A typo in any of those four `DIRS` entries leaves `SOURCES` non-empty, `ALLOW IN_LIST SOURCES` true, sentinel 2 satisfied, and every `app/` mutation still red: the guard would silently watch one directory out of five.**
- [ ] **RED 6, the parser guard inside `app/src`:** add `#include <nlohmann/json.hpp>` to `app/src/api/ApiSerialise.cpp` → `no_json_parser_in_shipped_code` names it. Paste; remove.
- [ ] **RED 7 and RED 8 — the parser guard OUTSIDE `app/src` (D3).** Plant the same include in a file under **`core/`**, then in a file under **`platform/`**; the guard must name each. Paste both; remove both. Reds 6 alone would have left this guard in exactly the state V3 condemned in the server guard — a `DIRS` typo dropping four of five directories, silently, with every `app/src` mutation still red. This guard has no `ALLOW`, so it has no sentinel 1 to catch that, and these two reds are what stand in for it.
- [ ] **RED 9, its witness:** remove the include from `app/tests/test_api_schema.cpp` → the guard fails for the opposite reason, saying nothing is testing with a parser any more. Paste; restore.
- [ ] **GREEN, `measure_has_no_framework_deps`, with the risen count.** Every OFF source file appended to the GLOBS at `app/tests/CMakeLists.txt:282`; paste the `OK (N files scanned)` line and say by how much N rose **and why** (API-R6 — `app/tests/*.h` is globbed, so `ApiFixture.h` and `RawHttpClient.h` raise it too). **RED once:** `#include <juce_core/juce_core.h>` atop `ApiSerialise.h` → paste the failure naming the file → remove.
- [ ] **GREEN, `no_std_atomic_over_shared_ptr`.** It already globs `app/`, so the new files are in its scope from the moment they land; paste it green.
- [ ] **GREEN, `test_names_are_ascii`.** New `TEST_CASE` names are pure ASCII; paste it green.
- [ ] **GREEN, `audioio_scoped_no_denormals_is_first` and both RT-hazard guards, unchanged.** `git diff main --stat -- platform/ core/src core/include ui/` is **empty**. This lane touched no layer below `app/` except `core/tests/`, and the diff is the proof, not the claim.
- [ ] **Commit:** `test(ci): the server-library and JSON-parser guards -- nine reds pasted, three of them planted outside app/`

---

## Numbers the builder must measure, not copy

Every figure below is a prediction to falsify. Measure the baselines on `main` at `af8a9d0` **before** Task A; if a measurement disagrees with this plan, **the plan is wrong and the orchestrator hears about it** — do not bend the code to hit it.

| quantity | how |
|---|---|
| ctest OFF baseline `base_off` | `--clean-first` build of `af8a9d0`, `build-lapi` |
| ctest ON baseline `base_on` | `--clean-first` ON build, `build-lapi-on`, `RTA_JUCE_PATH` set |
| OFF after each of A-I, K | read from ctest; this plan predicts no count |
| ON after J | read from ctest |
| **OFF ctest on ubuntu and macos** | from the CI run on the PR. After API-R15 the server and its `Host` check run there; **a PR body that shows only Windows numbers has not banked what R15 bought** |
| `measure_has_no_framework_deps` scanned count | the guard prints `(N files scanned)`; it rises once per task and the task says by how much and why (API-R6). It read **67** at PR #13 (`8818ad2`) — **re-measure at `af8a9d0` rather than starting from that number** |
| MSVC `/W4` warning count, OFF, before and after Tasks F and H | `grep -c "warning C"` over the build log. A warning is a defect, and the two vendored headers are the places it is at risk (API-R7) |
| OFF configure + build wall time, before and after Tasks F and H, **per OS** | both vendored headers are expensive; §2's 6-7× ratio was measured on MSVC only and does not transfer to gcc or clang |
| `sha256sum` of both vendored headers | must match each `PROVENANCE.md` and the upstream release |
| forced-fallback config | `-DRTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK=ON -DRTA_BUILD_APP=OFF` still green — this lane adds a third reader to the publish slot, so the branch only Apple takes is worth one run |
| `wc -l` on every new file | all < 400 |

## Build sequence and acceptance gate

1. **A** `ApiJson.h` (OFF) — everything emits through it.
2. **B** `ApiSettings` + `Host`/method/Bearer/point cap/body cap (OFF).
3. **C** `RateLimiter` (sliding) + conditional GET (OFF) — extends B's files.
4. **D** serialiser: status/transfer/spectrum/bands + the golden (OFF) — needs A, B.
5. **E** serialiser: mtw/average/positions/snapshot (OFF) — extends D.
6. **F** vendor nlohmann/json + the parse gate and the schema invariants (OFF) — needs D, E.
7. **G** the three existing formatters against the golden's literals (OFF) — needs E's golden.
8. **H** vendor cpp-httplib (OFF).
9. **I** `ApiServer` + the raw-socket test client (OFF) — needs B, C, D, E, F, H.
10. **J** composition root (**ON**) — needs I.
11. **K** guards, both configs — needs I (the guard's `ALLOW` file must exist).

**Ten of the eleven tasks are OFF**, so the serialiser, the schema, the validator, the limiter, the `Host` check *including its ordering*, the Bearer path, the CORS posture, shutdown and every guard run on ubuntu, macos and windows. **That is the single biggest change this revision makes** (API-R15 / V2): in the first draft the entire network layer was proven on zero CI machines.

**Acceptance gate.** OFF green on all three CI OSes and ON green locally, at the measured counts; **0 `warning C`** in both; `no_server_library_outside_api` green in both configs and shown red **five** times (two sentinels, one offender inside `app/`, one doxygen false positive, **one offender planted under `core/`**); `no_json_parser_in_shipped_code` green and shown red **four** times (an offender in `app/src`, **one under `core/` and one under `platform/`** standing in for the sentinel 1 it cannot have, and a missing witness); `measure_has_no_framework_deps` green with the risen scanned count and shown red once; `test_names_are_ascii` and `no_std_atomic_over_shared_ptr` green; forced-fallback OFF config green; every new file < 400 lines; `git diff main --stat -- platform/ core/src core/include ui/` empty; the golden JSON's diff reviewed line by line.

## What this lane does NOT include (deferred; record §5, §12, §13, §15)

- **Write access of any kind.** Owner's ruling; `docs/UPGRADE-BACKLOG.md`. A remote write to routing during a live show is near-irreversible and needs authentication first.
- **TLS**, on loopback or anywhere (§9 records the decline *and its cost*: a local process that can read loopback traffic can read these responses).
- **Content negotiation — `406` and `415`** (API-R17). For a GET-only API with one representation, 406 is machinery with no buyer, and 415 is unreachable once any oversized body is refused with **413**, which *is* built and tested (Task B B10, Task I I9). Named here rather than left listed in a record and absent from the code.
- **Settings persistence** (API-R5) — no preferences store exists in `app/`, and this lane does not build one.
- **`/traces` and `/session`**, and therefore the second `AtomicSharedPtr<const ApiSideState>` publish path of §5 (§14 q.3's default).
- **Solver endpoints** (`/eq`, `/align`). `EqSession` and `AlignmentWizard` have no caller anywhere in `app/` outside their own files and their tests — an endpoint returning "the current EQ suggestions" would report the state of an object nobody owns.
- **SPL and Leq.** `Snapshot` carries dBFS only; `rta::meter::Leq` has no `app/` caller. `"spl"` joins `available` when the Meters track lands it in the snapshot. **This blocks L6a's G7, not this lane.**
- **The SPL web viewer itself (L6a, G7).** It is a **client of this surface**, on **this** port, behind **this** `Host` check, rate limit and token, with its static assets served from the same origin. It must not open a second socket, a second port, a second bind default or a second auth model (§12; Smaart serves its SPL Web Viewer on the same port 26000 as its API, while SysTune shipped a bundled NGINX — the upper bound on getting this casually wrong).
- **Push of any shape** — no WebSocket, no SSE, and **never** REW's callback-URL webhook, which turns the analyser into an HTTP client aimed at an address an untrusted caller chose (§3). Note what makes this true in the code, stated correctly on the second attempt (D4): the vendored header **does** carry WebSocket machinery unguarded at v0.56.0 (Task H), and the method allowlist does **not** stop an upgrade — an upgrade is a `GET` and passes it. What makes it unreachable is that **no handler registers one**, so the request is answered by the ordinary route (200) or 404 and the server never emits `101`. Task I's I11 measures it.
- **Base64 float32 bodies** behind `?encoding=base64` (§7) — named as the first optimisation, not built. If it is ever built, **state the byte order in the schema**: REW's is big-endian and half the surveyed formats do not say.
- **An OSC scalar surface** (§1), **discovery / mDNS** (§13), **a LAN bind and the mandatory password that must come with it**, **Q-SYS QRC / ECP / QRWC**, **Dante** (L8's G19).

## Open owner questions carried forward

All five of §14 are **answered by a named default above** and none blocks the build. Two are worth a sentence from the owner **before** station 4 commits the wire format, because changing them afterwards changes a shipped schema rather than a constant:

1. **§14 q.1, fixed versus ephemeral.** The *number* is settled — **4736**, because 4737 is IANA `ipdr-sp` (API-R14). What is still open is the shape: a fixed port is discoverable and can collide; an ephemeral port written to a file the client reads never collides and needs a rendezvous.
2. **§14 q.3, `/traces` and `/session`.** "Not yet" is taken. If the answer is "yes", it is a whole extra task and a second publish path, and it is cheaper to hear now.

The other three (`allowLanBind` ships and refuses; the token setting ships empty; the Smaart SDK is not requested) are safe to leave as defaults: each is one field or nothing at all, and none of them is on the wire.

## For the station-4 builder, first read

1. **The record `docs/dsp/2026-09-16-remote-api.md` is binding — and it now carries its own §15 amendment.** Read §15 first; the sections above it keep their original wrong sentences on purpose, each with an inline pointer. This plan implements the record as amended and nothing past it.
2. **Read both verifier threads on PR #11 and the one on PR #14** (`gh pr view 11 --comments`, `gh pr view 14 --comments`). PR #11 fixed the cookie argument (it is **ambient authority / CSRF**, *not* DNS rebinding) and §11 item 10's `DIRS`. PR #14 found that the whole network layer was proven on zero CI machines (R15) and that nothing asserted the output was well-formed JSON (R16). A builder working from a cached memory of the first draft will reintroduce both.
3. **Order is fixed by dependency: A → B → C → D → E → F → G → H → I → J → K.** One commit per task. Everything except J needs no JUCE.
4. **The failing test first, seen to fail** (the missing `#include`, the missing member), then the header, then the body. Paste the command and its output for every "done": a green build proves it compiles, not that the numbers are right (`CLAUDE.md`, "Verification standard").
5. **`app/src/view/Readouts.h` is a seam to reuse, not to rebuild.** Three functions, already tested, with names an implementer would not guess: `formatHz`, **`formatTrim`**, **`formatAgreement`** — and `main` already carries a commit about a neighbouring lane naming two formatters that do not exist. No fourth formatter ships here.
6. **`tools/snapshot.cpp:181-190` is the fixture template**, not an edit target — it already composes the full `Snapshot` this lane's golden needs.
7. **Every CMake edit is named to the line, and the `.cpp`-into-the-target and file-into-the-GLOBS registrations are two separate acts.** Record §10 says so; the glob list is a textual scan that compiles nothing.
8. **Every count in this plan is a prediction you are expected to falsify if it is wrong.**
