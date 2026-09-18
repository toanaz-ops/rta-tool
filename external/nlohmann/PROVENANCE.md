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

**One TU.** `app/tests/test_api_schema.cpp` is the only translation unit in the
repository that includes this header — the same one-TU discipline record §2
applies to the server header, and what keeps the compile cost paid once.
