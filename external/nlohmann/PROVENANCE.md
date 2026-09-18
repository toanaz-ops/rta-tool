# nlohmann/json — vendored, TEST-ONLY

| | |
|---|---|
| upstream | https://github.com/nlohmann/json |
| release asset | https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp |
| tag | `v3.12.0` |
| sha256 | `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63` |
| `wc -l` | 25526 |
| licence | MIT (`LICENSE.MIT`, upstream verbatim) |
| fetched | 2026-09-17 |

**Test-only; shipped code must never include it, and
`no_json_parser_in_shipped_code` enforces that.**

`json.hpp` and `LICENSE.MIT` are upstream's files byte-for-byte and carry **no
added SPDX line**. Editing a vendored file to satisfy a local convention is how
a vendored file stops being verifiable against upstream. The header carries
upstream's own `SPDX-License-Identifier: MIT` at its line 7.

**Why it is here at all** (record `docs/dsp/2026-09-16-remote-api.md` §15 R16).
Before this, every assertion in the `RTA_BUILD_APP=OFF` configuration — the
only one CI runs — was a substring match or a byte-compare against a file the
same serialiser produced. **A stable but malformed document passes all of
them.** A hand-written validator would need no dependency and would fail on the
principle this project already applies to golden vectors: a validator must not
share an author with the thing it validates. Golden vectors come from
NumPy/SciPy rather than from a second in-house implementation for exactly that
reason, and a parser written by the author of the serialiser reproduces their
misunderstanding of JSON on both sides, where it cancels out and reads as proof.

**The hash was measured against the release asset, twice, not copied.** A copy
of a vendored header already sitting on a machine can report the same version
string and be a different file — that has been observed once on this project
for `httplib.h` (22885 lines / `a6e65d30…` against the release's 22875 /
`1f99e518…`). A version string is not an identity.

**MIT into AGPLv3.** MIT is a lax permissive non-copyleft licence the FSF calls
compatible with the GNU GPL; it flows into this AGPL-3.0-or-later work imposing
only notice retention, which is why `LICENSE.MIT` is vendored beside the header
and is never deleted.

**Two test TUs, both under `app/tests`.** Corrected 2026-09-18 at the L-API
closeout: this paragraph said "One TU" and named only
`app/tests/test_api_schema.cpp`. Measured, the includers are
`app/tests/test_api_schema.cpp:25` **and** `app/tests/test_api_server.cpp:23`,
the second of which uses the parser at `:149`, `:165` and `:338`. That is
deliberate rather than drift — a body proven well-formed inside the
serialiser's own test says nothing about what the **server** wrote to the
socket, so the over-the-wire cases parse what they received — but the count in
this file was wrong, and the count is the whole point of a provenance file.

**The guard is the authority on that number, not this prose.**
`no_json_parser_in_shipped_code` prints its witness count, and it reports
**2 witnesses**. If a third TU ever includes this header, that number moves and
this paragraph does not; read the guard's output first.

Both includers are under `app/tests`, so the **test-only** property this file
exists to record is unaffected: no file under `core/`, `platform/`, `ui/`,
`tools/` or `app/src` includes it, and the guard fails the build if one does.
Note that the one-TU compile-cost discipline record §2 applies to the *server*
header is therefore **not** claimed here: two TUs pay for this header, and at
25526 lines of mostly-templates that is a cost worth knowing before a third
joins them.
