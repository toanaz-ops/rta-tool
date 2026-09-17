# L-API — the read-only remote API (lane L-API, v1: HTTP/1.1 + JSON over TCP, GET only)

> **For agentic workers:** REQUIRED SUB-SKILL `superpowers:test-driven-development` and `superpowers:executing-plans`. Steps are checkboxes; the failing test is written and *seen to fail* before the code. Station 5 (adversarial verify, a reviewer with **no Write tools**) is not optional.

*2026-09-17, lane L-API, station 3. Written from the decision record `docs/dsp/2026-09-16-remote-api.md` (merged as PR #11 at `a39a02e`, after two verifier rounds on that PR) and the station-1 research `docs/research/2026-09-16-remote-api-station1-research.md`, **after opening every file they cite** — not after reading their description of those files. Worktree `.claude\worktrees\agent-a3c60992e3c5ae938`, branched from `main` at `a39a02e`. Every path below was existence-checked; a path marked **NEW** is not in the tree today. Thirteen places where the record's spelling and the landed code disagree are in "Reconciliations"; the orchestrator amends the record, the builder does not silently re-decide. **This lane ships no DSP**, so it needs no golden vector in the `tools/gen_golden.py` sense; its one golden is a **regression lock on the wire format**, labelled as such per `CLAUDE.md`'s verification standard.*

## 0. Hazard first: the one rule, and what this lane may never touch

**The audio callback is not in this lane's reach, and that is structural, not disciplined.** `AudioIo::audioDeviceIOCallbackWithContext` is `juce::ScopedNoDenormals` followed by exactly two calls — `bus_.pushFromCallback(...)` and `output_.render(...)` (`platform/src/AudioIo.cpp:116-148`, guarded by `platform/tests/check_callback_shape.cmake` and `audioio_callback_has_no_rt_hazards`). Nothing here adds a third call, a socket, an allocation, a lock or a branch to it. The API lives in `app/`; `platform/` cannot see `app/` (grep for `app/src|rtatool` over `platform/src platform/include platform/types` returns nothing), so the callback is unreachable from this code by construction. **A builder who finds themselves editing anything under `platform/` has left the lane.**

**The second rule, and it is the one a builder can actually break.** The API thread does **one** `SnapshotSource::latest()` per request (`app/src/measure/SnapshotSource.h:19`), takes the `shared_ptr<const Snapshot>` copy, and **serialises from that copy with the slot released**. It never holds the pointer across a request, never calls `latest()` twice in one request, and never serialises while the load is in flight. `AtomicSharedPtr` is **not lock-free on this project's own toolchain** — its class comment records the measurement on MSVC 14.51 — so the API thread is a *third* participant on a slot whose safety comes from who calls it, not from lock-freedom (`app/src/measure/AtomicSharedPtr.h`; record §4). The expensive work — thousands of floats into JSON — happens entirely after the load. That ordering **is** the decision.

**The third rule.** The API thread never touches `TraceLibrary`, `SessionDocument`, `EqSession` or `AlignmentWizard`. `TraceLibrary` is owned by `MainComponent` (`app/src/MainComponent.h:160`), is mutable, deletes copy and move (`app/src/trace/TraceLibrary.h:51-54`) and has `revision()` (`:83`) but **no atomic publish**. v1 does not expose it at all (§14 q.3's default, taken below), so no second publish path is built.

## Defaults this plan TAKES, each one named

The record's §14 has five open owner questions and `docs/HUMAN-QA-QUEUE.md` ("Từ lane Remote API") states in its own header that **none blocks station 3**. A plan that left them blank would be a plan nobody could build, so each is taken as a named default. **A default is a decision the owner may overturn in one sentence; it is not a decision this plan is hiding.**

| § | question | **default taken** | what changes if the owner says otherwise |
|---|---|---|---|
| §14 q.1 | port: fixed or ephemeral | **fixed `4737`**, one constant in `ApiSettings.h` | one constant, plus a rendezvous file if ephemeral wins — no design fork |
| §14 q.2 | does `api.allowLanBind` exist | **it ships, as a setting that is present and refuses**: `bool allowLanBind = false`, and `ApiServer` **fails to start** with a named refusal if it is true. Honest about the roadmap, and impossible to turn on by accident because the code path does not exist | delete one field and one refusal |
| §14 q.3 | `/traces` and `/session` in v1 | **no.** v1 is the **eight** measurement endpoints and **no new publish path** (see API-R12 — the record says "six", which is the length of `available`, not the endpoint count) | a whole task, and the second `AtomicSharedPtr<const ApiSideState>` of §5 |
| §14 q.4 | token: empty, or generated on first enable | **ship the setting empty.** `api.token` defaults to `""` = no token required; when non-empty it is checked as `Authorization: Bearer <token>`, **never a cookie**, never a query parameter that lands in a log | a generator and a place to show it — neither is on any path here |
| §14 q.5 | request the Smaart API SDK | **not needed for this lane.** It would inform how a competitor encodes coherence; §6 already decides this project's encoding, and the SDK's terms forbid quoting it into this repo anyway. **Nothing here waits on it** | nothing in this plan |

Settings shipped verbatim from record §8, and **every test below runs against these values, not against convenient ones** (`memory/a-default-must-be-run-through-the-gate-it-feeds.md`): `enabled=false`, `bindAddress="127.0.0.1"` (literal IP, never the string `localhost` — cpp-httplib's own README warns that resolving it on Windows with misconfigured IPv6 can cost up to 2 s per request), `port=4737`, `token=""`, `maxRequestsPerSecond=30`, `maxPointsPerResponse=8192`, `corsOrigins` empty, `allowLanBind=false`. Fixed, not settable: read 2 s, write 2 s, keep-alive idle 30 s, keep-alive max count **1000** (cpp-httplib's stock 100 is five seconds at a 20 Hz poll), thread pool base 2 / max 8 via `new_task_queue`.

## Global constraints

- SPDX header `// SPDX-License-Identifier: AGPL-3.0-or-later` on every new file this repo authors. The **vendored** `external/cpp-httplib/httplib.h` is upstream's file byte-for-byte and gets **no** SPDX line added — editing a vendored file to satisfy a local convention is how a vendored file stops being verifiable against upstream.
- **`core/` is untouched. `platform/` is untouched. `ui/` is untouched.** This lane's whole diff is `app/`, `external/`, `core/tests/CMakeLists.txt` (one guard registration) and the root `CMakeLists.txt` (one interface target).
- **Hard cap 400 lines, aim 300**, headers too. Per-file budgets are given in each task.
- **`RTA_BUILD_APP=OFF` is where the proof lives.** CI builds only OFF, on three OSes (`.github/workflows/ci.yml`), and `app/tests` is registered **outside** the `RTA_BUILD_APP` guard (root `CMakeLists.txt:72`). Everything provable without a socket is provable there: the serialiser, the schema, the validator, the rate limiter, the `Host` allowlist, the conditional-GET decision, and every guard. Only the httplib TU and the composition-root wiring are ON.
- **Never assert a value the implementation produced.** The three sources of truth here are: the record's own §6 schema (a specification, not an output), `std::to_chars`'s shortest-round-trip property (a closed form — the emitted decimal must `from_chars` back to the identical `float` bits), and the three **already-tested** formatters in `app/src/view/Readouts.h`. The golden JSON file is a **regression lock, not a correctness test**, and its test's name must say so.
- Build dirs **`build-lapi`** (OFF) / **`build-lapi-on`** (ON), Visual Studio generator, **never Ninja** (`memory/a-misconfigured-build-goes-99-percent-of-the-way.md`). MSVC `/W4` is the truth: **0 `warning C`** — see API-R7 for the one place that is at risk.

```
cmake -S . -B build-lapi -G "Visual Studio 18 2026" -A x64
cmake --build build-lapi --config Release --parallel
ctest --test-dir build-lapi -C Release --output-on-failure
```

For the ON config append `-DRTA_BUILD_APP=ON -DRTA_JUCE_PATH="D:/DEV CAVE EP3/PROJECT005-AZ-handsfree/external/JUCE"` and use `build-lapi-on`.

---

## Reconciliations made while planning (take to the orchestrator; do not silently re-decide in code)

- **API-R1 — the validator, the limiter and the `Host` check need a THIRD file; §10 names two.** §11 items 6-9 require them to be **pure functions tested in `RTA_BUILD_APP=OFF`**; §10 puts them inside `ApiServer.cpp`, which is the one TU that includes `httplib.h` and is ON-only. Both cannot be true. *Reconciled:* a new `app/src/api/ApiPolicy.{h,cpp}` — framework-free, httplib-free, in the `measure_has_no_framework_deps` GLOBS and in `rtatool_analysis_tests`. `ApiServer.cpp` keeps the socket, the thread and the routing and calls into it. §10's own sentence — "if `ApiServer.cpp` grows past [400] the seam is validation-versus-routing" — is honoured *before* the fact instead of after.
- **API-R2 — vendoring: the amalgamated header verbatim, NOT `split.py` output.** §2 says run cpp-httplib through its own `split.py` "so the header cost is paid in exactly one translation unit". `split.py`'s benefit is amortising the header across **several** TUs — and §11 item 10's guard permits **exactly one** includer, so there is no second TU to amortise over and the cost is paid once either way. Against that, a split output is **generated**: a reviewer cannot hash it against an upstream release tag, while a verbatim `httplib.h` can. *Reconciled:* vendor the single amalgamated header byte-identical to the upstream release, with its `LICENSE` and a provenance note recording the tag and the SHA-256. If a second includer is ever needed, `split.py` is the answer and the guard's `ALLOW` becomes a list — recorded, not built.
- **API-R3 — where the vendored library lives is decided by the guard's `DIRS`, not by taste.** §11 item 10 scans `core;platform;ui;tools;app` for `httplib|civetweb|mongoose` with `ALLOW=app/src/api/ApiServer.cpp`. A vendored `httplib.h` placed **anywhere under `app/`** is picked up by `file(GLOB_RECURSE "${DIR}/*.h")`, matches the pattern, is not the `ALLOW` file, and turns the guard red on the very library it exists to permit. *Reconciled:* `external/cpp-httplib/` at the repository root, outside all five `DIRS` entries. The guard's claim is then exactly true of the five layer directories, which is what §10's boundary is about. (`external/` is **NEW** — this repo has no vendored third party today; JUCE and Catch2 are fetched or reused by path.)
- **API-R4 — `AverageBlock::absence` is a per-bin ARRAY, not a scalar.** §6 and §11 item 4 read as one string (`"absence": "noWeight"`). The landed type is `std::vector<rta::dsp::SpatialAbsence>` (`app/src/measure/Snapshot.h:131`), one entry per bin, and `contributors` likewise (`std::vector<std::uint16_t>`, `:125`). *Reconciled:* `absence` is a JSON **array of strings**, and §11 item 4's assertion becomes `"noWeight"` **present at a named index**. The decision — a name, never an integer, because OSM's integer flattening is a measured failure mode — is unchanged.
- **API-R5 — there is no preferences store, so §8's settings have nowhere to persist.** `grep` for `PropertiesFile` / `ApplicationProperties` / `getUserSettings` over `app/src` returns **nothing**; `SessionCodec` persists sessions at `kSchemaVersion = 3` and knows nothing about an API. *Reconciled:* `ApiSettings` is a plain framework-free struct carrying §8's defaults, constructed by the composition root. **Persistence is out of v1** and is named in "What this plan does NOT include". Nothing in §8 is weakened: the defaults are the shipped values and every test runs against them.
- **API-R6 — §11 item 11's "N must rise by two" is arithmetic that depends on what the task adds.** The GLOBS list ends with `${CMAKE_CURRENT_SOURCE_DIR}/*.h;${CMAKE_CURRENT_SOURCE_DIR}/*.hpp` (`app/tests/CMakeLists.txt:282`), so **every new header in `app/tests/` also raises N**. This plan adds six OFF source files, not two. *Reconciled:* the acceptance everywhere below is "**N rises by exactly the number of files this task added**, read from the guard's own `OK (N files scanned)` line, never predicted here".
- **API-R7 — `/W4` is global and a vendored third-party header will not be clean under it.** Root `CMakeLists.txt:50` applies `/W4 /permissive- /utf-8` to every target, and every lane's gate is 0 `warning C`. *Reconciled:* `ApiServer.cpp` wraps its one `#include <httplib.h>` in `#pragma warning(push, 0)` / `#pragma warning(pop)`. Fallback if that proves insufficient: mark the interface target's include directory `SYSTEM` and add `/external:W0 /external:anglebrackets`. **Measure it** — paste the warning count before and after; do not assume either works.
- **API-R8 — a `/** */` doc comment is NOT stripped, so it can trip the new guard.** `rta_strip_comments` removes `//` lines and `/* ... */` bodies **containing no `*`** (`core/tests/check_no_std_atomic_shared_ptr.cmake:129-130`); a doxygen block survives. A file outside `ApiServer.cpp` that mentions `httplib` inside `/** ... */` is a false positive the next contributor will not understand. *Reconciled:* every mention of the library outside `ApiServer.cpp` is a `//` line comment, and Task J's red-then-green includes that exact case so the rule is a test and not folklore.
- **API-R9 — §11 item 12's ON loopback test has no target to live in.** `app/tests_juce/CMakeLists.txt` builds `rtatool_view_tests`, which links `az_ui` and the JUCE GUI modules; that file's own header comment argues against folding unrelated tests into a target with the wrong dependency direction. *Reconciled:* a new `rtatool_api_tests` target in the same directory, linking `rta_httplib` + `juce_core` + `rta::core` and compiling `app/src/api/*.cpp`. Same registration pattern, one more target.
- **API-R10 — four `Snapshot` fields §6 never mentions, and one ambiguity that matters.** Present and unserialised in v1: `peakBandLevelDb`, `peakBandCentreHz`, `referenceBands`, `soloTransfer`. The last is the ambiguity: `transfer` and `soloTransfer` are **both** `std::optional<TransferBlock>` (`Snapshot.h:221`, `:257`) and §6 does not say which `/transfer` serves. *Reconciled:* `/transfer` serves `Snapshot::transfer`; `soloTransfer` is **not on the wire in v1** and never appears in `available`. The other three are named here so a later reader knows they were decided out, not forgotten.
- **API-R11 — `axis.pointCount` must be measured from the array, not derived from `fftSize`.** `TransferBlock` carries no point count; §6's example shows `pointCount: 2049` beside `fftSize: 4096`, and the two agree only when the engine filled the whole half-spectrum. *Reconciled:* `pointCount = transfer->magnitudeDb.size()`, asserted in Task D; `fftSize` is echoed from `Snapshot::fftSize`. A derived number a client trusts is a number that will be wrong once.
- **API-R12 — §14 q.3's "v1 is six endpoints" miscounts.** §6 lists ten paths; removing the two conditional ones leaves **eight**: `/status`, `/snapshot`, `/transfer`, `/mtw`, `/bands`, `/spectrum`, `/average`, `/positions`. Six is the length of `/status`'s `available` capability list, which does not include `/status` or `/snapshot` themselves. *Reconciled:* the plan builds **eight**, and `available` keeps its six capability names.
- **API-R13 — `GET /api/v1/snapshot` appears in §6's path list with no body schema.** *Reconciled:* `/snapshot` is the **union** of `/status` and every block that is present, keyed by block name (`transfer`, `mtw`, `bands`, `spectrum`, `average`, `positions`), with an absent block's key **absent** — the same absence rule `coherence` gets, for the same reason. It is one round trip for a client that wants all of it, and it is **the** endpoint the golden file pins in full.

---

## The API this lane builds (record §6, §8, §9; names are this plan's)

```cpp
// app/src/api/ApiSettings.h   --  namespace rta::api  --  OFF, framework-free
struct ApiSettings {
    bool        enabled              = false;        // record §8: off until the operator asks
    std::string bindAddress          = "127.0.0.1";  // LITERAL IP, never "localhost" (§8)
    int         port                 = 4737;         // §14 q.1 default taken
    std::string token                = "";           // empty = no token; Bearer only, never a cookie (§9)
    int         maxRequestsPerSecond = 30;           // §8; the rate §4's accounting is stated at
    int         maxPointsPerResponse = 8192;         // §8; clamps any caller-supplied count
    std::vector<std::string> corsOrigins{};          // empty = no CORS headers, and that is not a defence (§9)
    bool        allowLanBind         = false;        // §14 q.2: exists, and refuses (see startRefusal)
};

// app/src/api/ApiPolicy.h   --  namespace rta::api  --  OFF, framework-free, httplib-free
enum class Method { Get, Head, Options, Other };
enum class Verdict { Serve, NotModified };

[[nodiscard]] bool hostIsAllowed(std::string_view host, int port);      // §9 control 1
[[nodiscard]] Method methodOf(std::string_view verb);                   // exact match, case-sensitive
[[nodiscard]] bool methodIsAllowed(Method m);                           // Get|Head|Options
[[nodiscard]] int clampPoints(long long requested, const ApiSettings&); // §11 item 6
[[nodiscard]] std::string etagFor(std::uint64_t sequence);              // `"12345"`, quotes included
[[nodiscard]] Verdict conditionalVerdict(std::string_view ifNoneMatch,
                                         std::optional<std::uint64_t> since,
                                         std::uint64_t sequence);       // §3: 304 or serve

class RateLimiter {                                                     // §11 item 7
public:
    explicit RateLimiter(int maxPerSecond);
    [[nodiscard]] bool admit(std::chrono::steady_clock::time_point now); // false => 429, BEFORE any load()
};

// app/src/api/ApiSerialise.h   --  namespace rta::api  --  OFF, framework-free, httplib-free
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

`ApiServer` (ON only) owns one `juce::Thread`, one `httplib::Server`, an `ApiSettings` and a `measure::SnapshotSource&`. Its whole request path is: rate limit → `Host` check → method check → route → clamp → **one `latest()`** → `conditionalVerdict` → serialise from the copy → respond with `ETag`. The limiter runs **before** the load, so `maxRequestsPerSecond` is a hard bound on this thread's traffic against the publish slot and not a typical figure.

**Absence rules that must survive the wire, and each has a test.** `coherence` absent ⇒ **the key is not present at all** (never `null`, never ones, never zeros). `coherenceAvailable` is **per band** and travels. `underResolved` travels. `absence` and `membership` are **strings**. A `nullopt` block's key is absent from `/snapshot`.

---

## Task A — `ApiJson.h`: the wire's number format (OFF; record §6 "Units on the wire", §7)

The smallest thing, and everything else emits through it. Shortest-round-trip float32 is the one numeric decision in the whole lane, and it is a closed form, so it goes first.

**Files.** Create `app/src/api/ApiJson.h` (**NEW**, ≤ 160, header-only) and `app/tests/test_api_json.cpp` (**NEW**, ≤ 160). Modify `app/tests/CMakeLists.txt` (add the test to `rtatool_analysis_tests`; append `ApiJson.h` to the `measure_has_no_framework_deps` GLOBS at `:282`).

`std::to_chars(first, last, float)` with **no** precision argument produces the shortest decimal that reads back as the identical `float` — that is the standard's own guarantee, and it is why no `printf` format string appears anywhere in this lane. `double` values (`effectiveAverages`, `MtwBlock::frequencyHz`) go through the `double` overload for the same reason.

**RED first.** `test_api_json.cpp` opens with `#include "api/ApiJson.h"`; the build fails at the include. Paste it.

| # | case | closed-form acceptance |
|---|---|---|
| **A1 (first)** | round-trip is the property, not the digits | for every value in `{-3.2145123f, -3.107789f, 12.421333f, 11.901777f, 0.9731445f, 0.9642334f, 0.0f, -1.0f/0.0f}` (the finite ones are §6's own literals): `from_chars` on the emitted string returns **bit-identical** `float`. Asserted with `std::bit_cast<std::uint32_t>`, not `==`, so a `-0.0f` cannot pass by accident |
| A2 | §6's examples are reproduced exactly | `number(-3.2145123f) == "-3.2145123"`, `number(0.9731445f) == "0.9731445"`, `number(8.5859375) == "8.5859375"`. These are the record's own printed values; a mismatch means either the record or the emitter is wrong and the orchestrator hears which |
| A3 | no display rounding leaks into the transport | `number(0.9731445f)` is **not** `"0.97"` and `number(-3.2145123f)` is **not** `"-3.2"`. The rounding rule is the viewer's (§6, §12 constraint 2, Task F) |
| A4 | non-finite is parseable JSON | a `NaN` or `±inf` emits `null`, never the token `nan`/`inf`, which is not JSON. A snapshot should never carry one; if it does, the response must still parse |
| A5 | strings are escaped | a position name containing `"`, `\`, a control byte and a UTF-8 multi-byte sequence survives a JSON round trip; `PositionSummary::name` is `std::string` and comes from a device, i.e. from outside this program |
| A6 | arrays are bounded by the caller's clamp | `array(span, limit)` emits `min(span.size(), limit)` elements and the emitted count is in the object beside it |

- [ ] **Accept:** OFF ctest `base_off + N` (N read from ctest, never predicted). `measure_has_no_framework_deps` green with the risen scanned count **pasted**. Zero `warning C`.
- [ ] **Mutation:** give `number(float)` a `std::to_chars(..., std::chars_format::fixed, 2)` → A1 and A2 go red together; revert.
- [ ] **Commit:** `feat(app): ApiJson -- shortest-round-trip float32 on the wire, no display rounding in the transport`

## Task B — `ApiSettings.h` + the `Host`, method and point-cap checks (OFF; record §8, §9; §11 items 6, 8, 9)

**Files.** Create `app/src/api/ApiSettings.h` (**NEW**, ≤ 120), `app/src/api/ApiPolicy.h` (**NEW**, ≤ 140), `app/src/api/ApiPolicy.cpp` (**NEW**, ≤ 260), `app/tests/test_api_policy.cpp` (**NEW**, ≤ 220). Modify `app/tests/CMakeLists.txt` (test target + three GLOBS entries).

**Every case below runs against `ApiSettings{}` — the shipped defaults** (`memory/a-default-must-be-run-through-the-gate-it-feeds.md`). A test that constructs a convenient 4-point cap proves nothing about the 8192 that ships.

**RED first.** `test_api_policy.cpp` includes `api/ApiPolicy.h`; the build fails at the include. Paste it.

| # | case (record §9, §11) | acceptance |
|---|---|---|
| **B1 (first)** | the whole DNS-rebinding defence | `hostIsAllowed("127.0.0.1:4737", 4737)` **true**; `"localhost:4737"` **true**; `"[::1]:4737"` **true**; `"attacker.example:4737"` **false**; `"127.0.0.1:4737.attacker.example"` **false** — the substring trap NCC Group's "strictly contain" wording exists to catch, and a naive `find()` passes it; `""` **false**; `"127.0.0.1"` with no port **false**; `"127.0.0.1:4738"` **false** (right host, wrong port) |
| B2 | case and whitespace do not open it | `"LOCALHOST:4737"` **true** (a hostname is case-insensitive per RFC), `" 127.0.0.1:4737"` **false**, `"127.0.0.1:4737 "` **false**. Whichever way the builder finds the truth, **the test states it and the code follows**; a disagreement here is reported, not papered over |
| B3 | method allowlist | `methodIsAllowed` accepts `GET`, `HEAD`, `OPTIONS`; rejects `POST`, `PUT`, `DELETE`, `PATCH`, `TRACE`, `""`, and lowercase `get` — the HTTP method token is case-**sensitive** |
| B4 | the point cap is run at the shipped default | `clampPoints(1'000'000, ApiSettings{}) == 8192`; `clampPoints(-1, …) == 8192` (absent/garbage means "as much as allowed", not zero); `clampPoints(0, …) == 8192`; `clampPoints(256, …) == 256`; `clampPoints(<a value that overflows int>, …) == 8192` — the parameter is parsed as `long long` precisely so the overflow is a clamp and not UB |
| B5 | `allowLanBind` refuses rather than silently binding | `startRefusal(ApiSettings{.allowLanBind = true})` returns a named refusal; `startRefusal(ApiSettings{})` returns none. §14 q.2's default made visible: the setting exists and the code path does not |
| B6 | a non-loopback bind address is refused the same way | `ApiSettings{.bindAddress = "0.0.0.0"}` refuses. Two ways to ask for a LAN bind, one refusal |

- [ ] **Accept:** OFF ctest count rises; `measure_has_no_framework_deps` green with the risen count pasted; zero `warning C`; `wc -l` on all four files.
- [ ] **Mutation 1 (the one that matters):** implement `hostIsAllowed` as `host.find("127.0.0.1:4737") != npos` → **B1's substring trap goes red** and nothing else does. Paste it. That single red line is the DNS-rebinding defence proving it is present; revert.
- [ ] **Mutation 2:** drop the clamp's upper branch → B4 red; revert.
- [ ] **Commit:** `feat(app): the API's Host allowlist, method allowlist and point cap -- pure, tested at the shipped defaults`

## Task C — `RateLimiter` and the conditional GET (OFF; record §3, §8, §9; §11 item 7)

Both are pure and both are real-time-safety controls, not hygiene: an uncapped poll rate and an uncapped `?points=` are two ways for a remote caller to make this program do unbounded work while a show is running.

**Files.** Modify `app/src/api/ApiPolicy.{h,cpp}` and `app/tests/test_api_policy.cpp`. No CMake edit.

The limiter takes an **injected** `std::chrono::steady_clock::time_point`. No real time, no sleeps, no flakiness — and no `std::this_thread::sleep_for` anywhere in this lane's tests.

| # | case | closed-form acceptance |
|---|---|---|
| **C1 (first)** | the ceiling is exactly the shipped default | `RateLimiter limiter{ApiSettings{}.maxRequestsPerSecond}`: requests 1..30 at `t0` are admitted, the **31st** is refused. `30` is read from `ApiSettings{}`, never written as a literal in the test |
| C2 | the window advances | after the refusal, advance the fake clock past the window and the next request is admitted; advance by half a window and it is still refused |
| C3 | the limiter admits nothing it did not count | 30 admitted at `t0`, then one at `t0 + 999 ms` refused, then one at `t0 + 1001 ms` admitted — the boundary is stated, not discovered |
| C4 | `ETag` is the sequence, quoted | `etagFor(12345) == "\"12345\""`. The quotes are part of an HTTP entity tag; a bare `12345` is a malformed validator and a conforming client will not echo it |
| C5 | 304 on a matching validator | `conditionalVerdict("\"12345\"", nullopt, 12345) == NotModified`; `("\"12344\"", …, 12345) == Serve`; `("", …) == Serve`; `("*", …, 12345) == NotModified` (`If-None-Match: *` matches any current representation) |
| C6 | `?since=` needs no validator | `conditionalVerdict("", 12345, 12345) == NotModified`; `("", 12344, 12345) == Serve`; `("", 12346, 12345) == **Serve**` — a client ahead of the server is a client that restarted, and serving it is the only recovery |
| C7 | the two agree, and `If-None-Match` wins | with both present and disagreeing, the header wins; the record specifies the header as the primary form and `?since=` as the form that needs no server-issued validator |

- [ ] **Accept:** OFF ctest count rises; zero `warning C`; `grep -rn "sleep_for\|sleep(" app/tests/test_api_policy.cpp` is **empty** — a timing test that sleeps is a test that will be flaky on a CI runner.
- [ ] **Mutation:** change `admit` to compare `> maxPerSecond` instead of `>=` → C1 lets the 31st through; revert.
- [ ] **Commit:** `feat(app): the API rate limiter against an injected clock, and the ETag/304 decision as a pure function`

## Task D — `ApiSerialise`: `/status`, `/transfer`, `/spectrum`, `/bands` (OFF; record §6; §11 items 1, 2, 5, 6)

**Files.** Create `app/src/api/ApiSerialise.h` (**NEW**, ≤ 120), `app/src/api/ApiSerialise.cpp` (**NEW**, ≤ 380 — if it passes 400 the seam is *fixed-axis blocks* versus *spatial blocks*, which is exactly the Task D / Task E split, so split the `.cpp` rather than the tests), `app/tests/ApiFixture.h` (**NEW**, ≤ 120), `app/tests/test_api_serialise.cpp` (**NEW**, ≤ 300), `app/tests/golden/api-v1-snapshot.json` (**NEW** — `app/tests/golden/` does not exist today; `core/tests/golden/` does). Modify `app/tests/CMakeLists.txt`.

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
| D7 | the golden — **a regression lock, not a correctness test** | `serialiseSnapshot(fixture, Request{})` compared byte-for-byte against `app/tests/golden/api-v1-snapshot.json`. The `TEST_CASE` name must contain the words **regression lock**: it proves the format has not drifted, not that any number in it is right. Every number in it is already owned by the test that proved it (`test_analyser_transfer.cpp`, `test_analyser_mtw.cpp`, `test_average_group.cpp`, `test_synthetic_snapshot.cpp`) |
| D8 | the golden is UTF-8, LF, and regenerable | the committed file is byte-identical to what the serialiser emits on this machine; the test says in one line how to regenerate it. `memory/a-gen-script-runs-the-moment-you-invoke-it.md` — if a regeneration helper is added it takes an explicit flag, and `--help` must not overwrite the file |

- [ ] **Accept:** OFF ctest count rises; `measure_has_no_framework_deps` green with the risen scanned count pasted; zero `warning C`; `wc -l` on every new file (all < 400).
- [ ] **Mutation 1:** make the serialiser emit `"coherence": null` when the optional is empty → **D1 red**; revert. **Mutation 2:** emit an array of `1.0` instead → D1 red again (assert the substring, so both shapes fail); revert. **Mutation 3:** drop the clamp in `Request` → D6 red; revert. **Mutation 4:** change one digit in the golden → D7 red, which proves the lock is locked.
- [ ] **Commit:** `feat(app): the API serialiser -- status, transfer, spectrum, bands; absent coherence is an absent key`

## Task E — `ApiSerialise`: `/mtw`, `/average`, `/positions`, `/snapshot` (OFF; record §6; §11 items 3, 4)

The three blocks that carry a state a well-meaning implementer drops, plus the union endpoint API-R13 defines.

**Files.** Modify `app/src/api/ApiSerialise.{h,cpp}` (or add `ApiSerialiseSpatial.cpp` **NEW** if the 400-line cap is reached — the seam is named in Task D), `app/tests/test_api_serialise.cpp`, `app/tests/golden/api-v1-snapshot.json`, `app/tests/CMakeLists.txt` if a file was added.

| # | case (record §6, §11) | acceptance |
|---|---|---|
| **E1 (first)** | per-band `coherenceAvailable` survives | serialise an MTW snapshot with a **filling bottom band**: `bands[0].coherenceAvailable == false` in the output **and** the zeros at those indices are still present in `coherence`. `MtwBandDescriptor`'s own comment says an index whose band has not passed its gate holds `0.0f` and "must not be read as a measured zero"; a client that ignores this draws a bottom band reading zero coherence for five and a half seconds and reports a fault that does not exist. `makeSyntheticMtw()` states a **completed** measurement (its comment says `coherenceAvailable` is true in every band), so this case builds the filling band by hand from `rta::dsp::mtwBands` rather than waiting for a fixture that cannot produce it |
| E2 | MTW's axis is the other kind | `/mtw` emits `"kind":"explicit"` **with** a `frequencyHz` array, and every band descriptor field crosses: `firstIndex`, `pointCount`, `fftSize`, `windowSeconds`, `integrationSeconds`, `effectiveAverages`, `seamHz`, `coherenceAvailable`. Eight fields, none optional |
| E3 | `absence` is an array of **strings** | `"present"` / `"noContributor"` / `"noWeight"`, at named bin indices (API-R4). OSM flattens enums to their integer value with no name, so a renumbering silently changes meaning with nothing on the wire to reveal it — that is a measured failure mode in a shipping product. `memory/a-placeholder-for-an-absent-result-erases-its-state.md` is about this exact field being rewritten from `NoWeight/2` to `NoContributor/0` |
| E4 | `membership` is a string too | `"member"` / `"excludedDifferentReference"` / `"excludedOverCapacity"` in `/positions` |
| E5 | the two quantities keep their code names | `/average` emits `phaseAgreement` and `weightedCoherence` **spelled as the code spells them**, and the schema note says neither is a coherence estimate. L6b §2 and §4 exist because those two are routinely mistaken for one; a wire format that renamed either to something friendlier would undo that record |
| E6 | `/positions` carries no per-bin array | the emitted `/positions` body contains no array longer than the position count. L6b §6 fixed that publish cost must be O(1) in N; an API that re-expanded it would reintroduce the churn that decision refused |
| E7 | `/snapshot` is a union, and absence is absence | with `mtw == nullopt`, `/snapshot` contains **no** `"mtw"` key; with every block present, `/snapshot` contains all six plus the `/status` fields; and `soloTransfer` never appears (API-R10) |
| E8 | mutation-visible field list | for each of the eight band-descriptor fields, dropping it from the emitter turns E2 red. Written as a data-driven loop over the field names so a ninth field added later without a test is visible |

- [ ] **Accept:** OFF ctest count rises; `measure_has_no_framework_deps` green with the risen count pasted; zero `warning C`; golden regenerated and its diff reviewed line by line (a golden that changed for a reason nobody read is not a lock).
- [ ] **Mutation 1:** drop `coherenceAvailable` → E1 red; revert. **Mutation 2:** emit `"absence":2` instead of `"noWeight"` → E3 red; revert. **Mutation 3:** rename `weightedCoherence` to `coherence` in the emitter → E5 red; revert — and note this is the one mutation whose *green* version would have been the most plausible-looking code in the lane.
- [ ] **Commit:** `feat(app): the API serialiser -- mtw with per-band coherenceAvailable, spatial average and positions with string enums, and the union snapshot`

## Task F — the desktop half of §12 constraint 2 (OFF; record §11 item 13, §12 constraint 2)

Twenty minutes, and it is the only thing making §6's units deviation provable rather than merely intended.

**Files.** Modify `app/tests/test_readouts.cpp` **only**. No new formatter, no new file, no CMake edit.

**The three functions already exist and are already tested.** `app/src/view/Readouts.h`: `formatHz` (`:72`), **`formatTrim`** (`:79`), **`formatAgreement`** (`:87`) — grep handle `inline std::string format` if those numbers drift. They are pinned by `app/tests/test_readouts.cpp:100-115`, `Readouts.h` is already in `measure_has_no_framework_deps`'s GLOBS, and `rta::view::formatHz` already has a live caller at `app/src/view/DevicePanel.cpp:102`. **No new formatter may be added for this lane.** The three name mappings, because they are not the ones an implementer would guess: Hz → `formatHz`; the one-decimal dB rule → **`formatTrim`**; the two-decimal 0..1 rule (coherence and `phaseAgreement`) → **`formatAgreement`**.

| # | case | acceptance (the arguments are the golden JSON's own literals) |
|---|---|---|
| **F1** | the desktop readout rounds the wire's own values | `formatHz(1000.4) == "1000 Hz"`; `formatTrim(-3.2145123) == "-3.2 dB"` (`magnitudeDb[0]`, §6's golden); `formatAgreement(0.9731445) == "0.97"` (`coherence[0]`). The dB and Hz functions return the **unit inside the string**, so the assertion carries the suffix or it fails on the suffix rather than on the rounding. The parameters are `double` and the float32→double widening is exact, so the string is the rounding of the *same* value the wire carried |

The `TEST_CASE` name must say it is the **desktop half of §12 constraint 2**. Do **not** pair the dB rule with `8.5859375` — that literal is `effectiveAverages`, a **count**, not a level; `formatTrim(8.5859375)` does return `"8.6 dB"`, but a count formatted as dB is a wrong test.

- [ ] **Accept:** OFF ctest count rises by one; zero `warning C`.
- [ ] **Mutation 1:** round in the serialiser instead → **Task D's golden (D7) goes red**, which is the point. **Mutation 2:** change `formatTrim`'s precision to two decimals → F1 red and `test_readouts.cpp:100-115` red with it; revert both.
- [ ] **What this does NOT prove, and the plan says so rather than pretending:** that L6a's **JavaScript** viewer rounds the same way. A C++ test cannot reach it. L6a either ships the same thresholds against the same golden values plus its own test, or records §12 constraint 2 as **untested for the viewer** and labels it so. This lane proves the desktop half and hands over a named seam.
- [ ] **Commit:** `test(app): the desktop half of the rounding constraint -- the wire's own float32 through the three formatters that already exist`

## Task G — vendor cpp-httplib (ON config; record §2; API-R2, API-R3, API-R7)

**Files.** Create `external/` (**NEW** directory), `external/cpp-httplib/httplib.h` (**NEW**, upstream byte-for-byte, **no SPDX line added**), `external/cpp-httplib/LICENSE` (**NEW**, upstream's MIT text verbatim), `external/cpp-httplib/PROVENANCE.md` (**NEW**, ≤ 40: upstream URL, the release tag vendored, the SHA-256 of `httplib.h`, the date, and the one sentence explaining API-R2 — why the amalgamated header and not `split.py`). Modify the root `CMakeLists.txt` (one `INTERFACE` target inside the `if(RTA_BUILD_APP)` block) and `README.md` (one line under "Licence" naming the vendored MIT dependency — an AGPL work that ships a third party's code says so where a reader looks).

**The licence line, which is the part that must not be got wrong.** This project is AGPL-3.0-or-later. cpp-httplib's `LICENSE` is the MIT text — a lax permissive non-copyleft licence the FSF calls compatible with the GNU GPL — and it flows into an AGPLv3 work imposing only notice retention. That is why the `LICENSE` file is vendored beside the header and never deleted. **Mongoose is disqualified and this is the trap worth naming**: its `LICENSE` offers GPL-2.0 with **no "or any later version"**, which is incompatible with AGPLv3, and the commercial arm conflicts with the AGPL source release this project is committed to. Neither arm works. Do not "simplify" this dependency later without re-reading that paragraph.

**`CPPHTTPLIB_*` macros to leave UNDEFINED, and this is a list, not a preference.**

| macro | left undefined because |
|---|---|
| `CPPHTTPLIB_OPENSSL_SUPPORT` | record §2 and §9: **no TLS in v1**, therefore no OpenSSL link dependency and **no second licence to reason about**. This is the load-bearing one |
| `CPPHTTPLIB_ZLIB_SUPPORT` | a zlib link dependency for a compression nothing on loopback needs |
| `CPPHTTPLIB_BROTLI_SUPPORT` | same, plus a second third-party licence |
| `CPPHTTPLIB_ZSTD_SUPPORT` | same |
| `CPPHTTPLIB_NO_EXCEPTIONS` | the rest of the app builds with exceptions; changing that for one TU is an ODR-shaped hazard, not a tidy-up |

Everything §8 tunes — the four timeouts, the keep-alive max count, the thread pool — is set at **run time** on the `httplib::Server` object (`set_read_timeout`, `set_write_timeout`, `set_keep_alive_timeout`, `set_keep_alive_max_count`, `new_task_queue`), never by a compile-time macro, so the values live next to the record's reasoning in `ApiServer.cpp` instead of in a build file.

- [ ] **Accept:** ON configure succeeds; `sha256sum external/cpp-httplib/httplib.h` **pasted** and matching `PROVENANCE.md`; `grep -c "" external/cpp-httplib/LICENSE` non-zero and the file's first line is the MIT header; **OFF ctest count is unchanged** (vendoring adds nothing to the OFF tree — if it moved, something is wired wrong).
- [ ] **Measure, do not assume (API-R7):** ON build warning count **before** this task and **after**, pasted. If it rose, apply the `#pragma warning(push, 0)` wrap in Task H and re-measure; if that is insufficient, the `SYSTEM` include + `/external:W0` fallback, and say which was needed.
- [ ] **Commit:** `build: vendor cpp-httplib (MIT) as the amalgamated header, TLS and every optional backend left undefined`

## Task H — `ApiServer.{h,cpp}`: the one httplib TU (ON only; record §4, §8, §9, §10; §11 item 12)

**Files.** Create `app/src/api/ApiServer.h` (**NEW**, ≤ 140), `app/src/api/ApiServer.cpp` (**NEW**, ≤ 380 — the seam if it grows is validation-versus-routing, and Task B already took the validation half out), `app/tests_juce/test_api_server.cpp` (**NEW**, ≤ 200). Modify `app/CMakeLists.txt` (add `src/api/*.cpp` to `rtatool`, link `rta_httplib`), `app/tests_juce/CMakeLists.txt` (the new `rtatool_api_tests` target, API-R9).

**The request path, in this order, and the order is the decision.**

1. `RateLimiter::admit(now)` → **429 before any `latest()`**. The limiter is the real-time-safety control, so it runs before the work, not after.
2. `hostIsAllowed(req.get_header_value("Host"), settings.port)` → **403** before routing. Highest-value control in the whole API.
3. `methodIsAllowed` → **405**, with an `Allow: GET, HEAD, OPTIONS` header.
4. Bearer check when `settings.token` is non-empty → **401**. Header only. **Never a cookie** — not because of rebinding (a cookie jar keys on the host *name*, so a rebound request carries the attacker's cookies and never this app's) but because a cookie is **ambient authority**: the browser attaches it to every request to `127.0.0.1:<port>` regardless of which page issued it, so any site the operator opens during a show is authenticated to this listener. A `Bearer` header is not ambient.
5. Route → `clampPoints` → **one** `source.latest()` → null check (no snapshot yet ⇒ 503) → `conditionalVerdict` → **304 with the same `ETag` and no body**, or serialise from the copy → **200** with `ETag: "<sequence>"`.
6. **No CORS headers**, and no pretence that their absence is a defence: a `GET` with only safelisted headers is a *simple* request, gets no preflight, and is **executed** by this program before the browser decides whether the script may read the reply.

The thread is a `juce::Thread` (the idiom `AnalysisThread` and `SyntheticInput` already use), started only when `settings.enabled`. `~ApiServer` calls `svr_.stop()` **then** joins — the belt-and-braces second guarantee `AnalysisThread`'s destructor comment already argues for, because getting shutdown wrong is a crash that happens once, at exit, on a customer's machine.

| # | case (record §11 item 12) | acceptance |
|---|---|---|
| **H1 (first)** | it binds nothing it was not told to | with `settings.enabled == false`, constructing and destroying `ApiServer` starts no thread and opens no socket; `netstat`-free assertion — the thread object reports not running |
| H2 | one real request over loopback | bind `127.0.0.1:0` (**ephemeral**, so the test never collides with a developer's own 4737), `GET /api/v1/status` → **200**, body parses, `schemaVersion == 1`, and the `ETag` header equals `etagFor(sequence)` |
| H3 | the method boundary is real over the wire | `POST /api/v1/status` → **405** with an `Allow` header; `HEAD` → 200 with no body |
| H4 | the `Host` boundary is real over the wire | one request with `Host: attacker.example:<port>` → **403**, and the body is not served. This is B1 proven end to end rather than only as a pure function |
| H5 | 304 round trip | `GET /api/v1/status`, then repeat with `If-None-Match` set to the returned `ETag` → **304**, empty body, same `ETag` |
| H6 | shutdown is clean and repeatable | construct/start/stop/destroy in a loop ten times: no hang, no leaked thread, and the port is reusable each time |

Three requests were the record's floor; six is what it costs to prove the four boundaries plus shutdown, and none of them needs a sound card or a device.

- [ ] **Accept:** ON ctest `base_on + N` (read from ctest); zero `warning C` (API-R7 — paste the count); `wc -l` on both new files.
- [ ] **Mutation 1:** move the `Host` check **after** routing → H4 still 403s but the handler ran; assert instead that the *rate-limit counter did not advance for a forged host* — or, simpler and stronger, delete the `Host` check → H4 red. Paste; revert. **Mutation 2:** call `latest()` twice in one handler → not test-visible, so it is guarded by review and by the file's own comment, and that limit is stated here rather than implied.
- [ ] **Commit:** `feat(app): ApiServer -- one juce::Thread, one httplib TU, limiter then Host then method then one latest()`

## Task I — composition-root wiring (ON only; record §4, §10)

**Files.** Modify `app/src/MainComponent.h` (one member, declared in the right place) and `app/src/MainComponent.cpp` (construction). Nothing else.

**Declaration order is load-bearing and this is trap T-1's third case.** `audioIo_` is declared before `analysisThread_` so the bus outlives the thread reading it (`MainComponent.h:127-131`, and the class comment at `:36-54`). `apiServer_` reads `analysisThread_.latest()`, so **`apiServer_` is declared AFTER `analysisThread_`** — declared later means destroyed **first**, which is exactly what is needed: the server stops and joins while the `SnapshotSource` it holds a reference to is still alive. Reversing the two is a shutdown crash nobody sees in development.

```cpp
// MainComponent.h, inside the trap T-1 block
rta::platform::AudioIo audioIo_;
rta::measure::AnalysisThread analysisThread_;
std::unique_ptr<rta::measure::SyntheticInput> syntheticInput_;
// apiServer_ reads analysisThread_ as a SnapshotSource&, so it is declared
// AFTER it and therefore destroyed BEFORE it. See the class comment, T-1.
std::unique_ptr<rta::api::ApiServer> apiServer_;
```

- [ ] **Accept:** ON builds; ON ctest count unchanged by this task (wiring, not behaviour); the app starts and exits cleanly with `api.enabled = false` (the shipped default), i.e. **nothing observable changes for an operator who did not ask for the API** — which is the whole point of the default.
- [ ] **Manual check the owner can repeat, with the exact command:** build ON, set `enabled = true` in the constructed settings, run `rtatool`, and in a terminal `curl -s http://127.0.0.1:4737/api/v1/status`. Expect a JSON body with `schemaVersion`, `sequence` and a six-name `available`. Then `curl -s -H "Host: attacker.example:4737" http://127.0.0.1:4737/api/v1/status` — expect **403**.
- [ ] **Commit:** `feat(app): the composition root owns the API server; declaration order keeps the snapshot source alive across its shutdown`

## Task J — prove the guards still GUARD (both configs; record §11 items 10, 11, 14)

No new behaviour; a procedure whose output goes in the PR body. Every count is **read from the guard's own line**, never predicted here.

**Files.** Create `core/tests/check_no_server_library.cmake` (**NEW**, ≤ 170). Modify `core/tests/CMakeLists.txt` (one `add_test`, beside `no_std_atomic_over_shared_ptr` at `:149-154`, which is the exact registration to copy).

**This is a NEW script modelled on `check_no_std_atomic_shared_ptr.cmake`, not a re-invocation of it.** `PATTERN` is `set()` inside that script at `:134` and `SENTINEL_PATTERN` at `:141` — neither is a `-D` argument, so they cannot be overridden from a registration. Copy the file and change those two literals, the message text, and nothing else. **Copy `rta_strip_comments` (`:128-132`) with it**, because the sentinel is matched against comment-stripped source and that is what stops a mention in prose from standing in for the code.

**Both sentinels get copied. A copy that takes only the first is weaker than the original it cites.**

- `if(NOT ALLOW IN_LIST SOURCES)` (`:65`) — the allowed file is inside the scanned set. Its analogue here is `app` being in `DIRS`.
- `if(NOT ALLOW_CODE MATCHES "${SENTINEL_PATTERN}")` (`:145`) — the allowed file **still contains** the thing it is the sole exception for. Its analogue here: **`ApiServer.cpp` must still contain `#include <httplib.h>`**. Without it, deleting the include leaves a guard that passes while proving nothing.

```cmake
# core/tests/CMakeLists.txt, beside no_std_atomic_over_shared_ptr
add_test(NAME no_server_library_outside_api
    COMMAND ${CMAKE_COMMAND}
            "-DDIRS=${CMAKE_SOURCE_DIR}/core;${CMAKE_SOURCE_DIR}/platform;${CMAKE_SOURCE_DIR}/ui;${CMAKE_SOURCE_DIR}/tools;${CMAKE_SOURCE_DIR}/app"
            -DALLOW=${CMAKE_SOURCE_DIR}/app/src/api/ApiServer.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_server_library.cmake
)
```

**`${CMAKE_SOURCE_DIR}/` on every `DIRS` entry and on `ALLOW`** — `file(GLOB_RECURSE)` returns **absolute** paths, so a relative `ALLOW` dies either at the `if(NOT EXISTS "${ALLOW}")` check or at `:65`. **`app` must be in `DIRS`** and that is not a detail: omit it and `ALLOW IN_LIST SOURCES` is false and the guard `FATAL_ERROR`s on every run. With `app` present the guard proves two things and both are load-bearing — that `core/`, `platform/`, `ui/` and `tools/` contain no server library at all, and that within `app/` **`ApiSerialise.cpp` does not include `httplib.h`**, which is what lets Tasks A-F run in `RTA_BUILD_APP=OFF` at all.

- [ ] **GREEN, the new guard, in BOTH configs.** `ctest -R no_server_library_outside_api` green in `build-lapi` **and** `build-lapi-on`. Paste both.
- [ ] **RED once, sentinel 1:** drop `app` from the registration's `DIRS` → `FATAL_ERROR` naming `ApiServer.cpp` as unwatched. Paste; restore.
- [ ] **RED once, sentinel 2:** delete `#include <httplib.h>` from `ApiServer.cpp` → the guard fails saying it is not proving anything. Paste; restore. **Then rebuild from the current tree and hash-check against HEAD** — `memory/mutation-testing-needs-the-exe-deleted-first.md`: restoring a header leaves objects built from the mutated one, and a header mutation is not recompiled at all unless a dependent `.cpp` is touched.
- [ ] **RED once, the offender scan:** add `#include <httplib.h>` to `app/src/api/ApiSerialise.cpp` → the guard names that file. Paste; remove. **This is the mutation that proves the OFF-testability split is enforced and not merely intended.**
- [ ] **RED once, the false-positive case (API-R8):** put the word `httplib` inside a `/** ... */` doxygen block in `ApiPolicy.h` → the guard goes red, because that comment shape is not stripped. Paste it, then rewrite the comment as `//` lines and show green. A guard's false positives are part of its contract.
- [ ] **GREEN, `measure_has_no_framework_deps`, with the risen count.** All six OFF files (`ApiJson.h`, `ApiSettings.h`, `ApiPolicy.h`, `ApiPolicy.cpp`, `ApiSerialise.h`, `ApiSerialise.cpp`, plus any `.cpp` Task E split out) appended to the GLOBS at `app/tests/CMakeLists.txt:282`; paste the `OK (N files scanned)` line and say by how much N rose and why (API-R6 — `app/tests/*.h` is globbed, so `ApiFixture.h` raises it too). **RED once:** `#include <juce_core/juce_core.h>` atop `ApiSerialise.h` → paste the failure naming the file → remove.
- [ ] **GREEN, `no_std_atomic_over_shared_ptr`.** It already globs `app/`, so the new files are in its scope from the moment they land; paste it green.
- [ ] **GREEN, `test_names_are_ascii`.** New `TEST_CASE` names in `app/tests` and `app/tests_juce` are pure ASCII; paste it green.
- [ ] **GREEN, `audioio_scoped_no_denormals_is_first` and both RT-hazard guards, unchanged.** `git diff main --stat -- platform/ core/src core/include` is **empty**. This lane touched no layer below `app/`, and the diff is the proof, not the claim.
- [ ] **Commit:** `test(ci): the server-library guard -- app in DIRS, both sentinels copied, five reds pasted`

---

## Numbers the builder must measure, not copy

Every figure below is a prediction to falsify. Measure the baselines on `main` at `a39a02e` **before** Task A; if a measurement disagrees with this plan, **the plan is wrong and the orchestrator hears about it** — do not bend the code to hit it.

| quantity | how |
|---|---|
| ctest OFF baseline `base_off` | `--clean-first` build of `a39a02e`, `build-lapi` |
| ctest ON baseline `base_on` | `--clean-first` ON build, `build-lapi-on`, `RTA_JUCE_PATH` set |
| OFF after each of A-F | read from ctest; this plan predicts no count |
| ON after H, I | read from ctest |
| `measure_has_no_framework_deps` scanned count | the guard prints `(N files scanned)`; it rises once per task and the task says by how much and why (API-R6). It read **67** at PR #13 (`8818ad2`) — **re-measure at `a39a02e` rather than starting from that number** |
| MSVC `/W4` warning count, ON, before and after Task G | `grep -c "warning C" ` over the build log. A warning is a defect, and the vendored header is the one place it is at risk (API-R7) |
| `sha256sum external/cpp-httplib/httplib.h` | must match `PROVENANCE.md` and the upstream release |
| forced-fallback config | `-DRTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK=ON -DRTA_BUILD_APP=OFF` still green — this lane adds a third reader to the publish slot, so the branch only Apple takes is worth one run |
| `wc -l` on every new file | all < 400; the record requires the two `api/` originals under the cap and this plan budgets each one |

## Build sequence and acceptance gate

1. **A** (`ApiJson.h`, OFF) — everything emits through it.
2. **B** (`ApiSettings` + `Host`/method/point cap, OFF) — no dependency on A.
3. **C** (`RateLimiter` + conditional GET, OFF) — extends B's files.
4. **D** (serialiser: status/transfer/spectrum/bands + the golden, OFF) — needs A and B.
5. **E** (serialiser: mtw/average/positions/snapshot, OFF) — extends D, finishes the golden.
6. **F** (the three existing formatters against the golden's literals, OFF) — needs E's golden.
7. **G** (vendor cpp-httplib) — first ON-touching task.
8. **H** (`ApiServer`, ON) — needs B, C, D, E, G.
9. **I** (composition root, ON) — needs H.
10. **J** (guards, both configs) — needs H (the guard's `ALLOW` file must exist).

**A-F are all OFF and all independent of the network**, which is deliberate: if the lane stopped after F, six of the record's fourteen acceptance items would already be proven on three operating systems with no sound card and no socket.

**Acceptance gate.** OFF and ON ctest both green at the measured counts; **0 `warning C`** in both; the new `no_server_library_outside_api` guard green in both configs and shown red **four** times (two sentinels, one offender, one false positive); `measure_has_no_framework_deps` green with the risen scanned count and shown red once; `test_names_are_ascii` and `no_std_atomic_over_shared_ptr` green; forced-fallback OFF config green; every new file < 400 lines; `git diff main --stat -- platform/ core/src core/include ui/` empty; the golden JSON's diff reviewed line by line.

## What this lane does NOT include (deferred; record §5, §12, §13)

- **Write access of any kind.** Owner's ruling; `docs/UPGRADE-BACKLOG.md`. A remote write to routing during a live show is near-irreversible and needs authentication first.
- **TLS**, on loopback or anywhere (§9 records the decline *and its cost*: a local process that can read loopback traffic can read these responses).
- **`/traces` and `/session`**, and therefore the second `AtomicSharedPtr<const ApiSideState>` publish path of §5 (§14 q.3's default).
- **Solver endpoints** (`/eq`, `/align`). `EqSession` and `AlignmentWizard` have no caller anywhere in `app/` outside their own files and their tests — an endpoint returning "the current EQ suggestions" would report the state of an object nobody owns.
- **SPL and Leq.** `Snapshot` carries dBFS only; `rta::meter::Leq` has no `app/` caller. `"spl"` joins `available` when the Meters track lands it in the snapshot. **This blocks L6a's G7, not this lane.**
- **The SPL web viewer itself (L6a, G7).** It is a **client of this surface**, on **this** port, behind **this** `Host` check, rate limit and token, with its static assets served from the same origin. It must not open a second socket, a second port, a second bind default or a second auth model (§12; Smaart serves its SPL Web Viewer on the same port 26000 as its API, while SysTune shipped a bundled NGINX — the upper bound on getting this casually wrong).
- **Push of any shape** — no WebSocket, no SSE, and **never** REW's callback-URL webhook, which turns the analyser into an HTTP client aimed at an address an untrusted caller chose (§3).
- **Base64 float32 bodies** behind `?encoding=base64` (§7) — named as the first optimisation, not built. If it is ever built, **state the byte order in the schema**: REW's is big-endian and half the surveyed formats do not say.
- **An OSC scalar surface** (§1), **discovery / mDNS** (§13), **a LAN bind and the mandatory password that must come with it**, **settings persistence** (API-R5), **Q-SYS QRC / ECP / QRWC**, **Dante** (L8's G19).

## Open owner questions carried forward

All five of §14 are **answered by a named default above** and none blocks the build. The two worth a sentence from the owner **before** station 4 commits the wire format, because changing them afterwards changes a shipped schema rather than a constant:

1. **§14 q.1, the port.** `4737` is taken. The real question is fixed versus ephemeral-plus-a-rendezvous-file, and it is one sentence.
2. **§14 q.3, `/traces` and `/session`.** "Not yet" is taken. If the answer is "yes", it is a whole extra task and a second publish path, and it is cheaper to hear now.

The other three (`allowLanBind` ships and refuses; the token setting ships empty; the Smaart SDK is not requested) are safe to leave as defaults: each is one field or nothing at all, and none of them is on the wire.

## For the station-4 builder, first read

1. **The record `docs/dsp/2026-09-16-remote-api.md` is binding.** This plan implements its §6 surface and §8/§9 posture and nothing past it, and flags **API-R1..R13**, which the orchestrator amends in the record **first**. Do not silently re-decide any of them in code.
2. **Read the two verifier threads on PR #11** (`gh pr view 11 --comments`). Two CONFIRMED defects were fixed there — the cookie argument (it is **ambient authority / CSRF**, *not* DNS rebinding) and §11 item 10's `DIRS` — and five non-blocking fixes were folded in. A builder working from a cached memory of the record will reintroduce the first one.
3. **Order is fixed by dependency: A → B → C → D → E → F → G → H → I → J.** One commit per task. A-F need no socket and no JUCE.
4. **The failing test first, seen to fail** (the missing `#include`, the missing member), then the header, then the body. Paste the command and its output for every "done": a green build proves it compiles, not that the numbers are right (`CLAUDE.md`, "Verification standard").
5. **`app/src/view/Readouts.h` is a seam to reuse, not to rebuild.** Three functions, already tested, with names an implementer would not guess: `formatHz`, **`formatTrim`**, **`formatAgreement`**. No fourth formatter ships in this lane.
6. **`tools/snapshot.cpp:181-190` is the fixture template**, not an edit target — it already composes the full `Snapshot` this lane's golden needs.
7. **Every count in this plan is a prediction you are expected to falsify if it is wrong.**
