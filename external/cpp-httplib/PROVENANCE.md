# cpp-httplib — vendored, the amalgamated header

| | |
|---|---|
| upstream | https://github.com/yhirose/cpp-httplib |
| release tag | https://github.com/yhirose/cpp-httplib/releases/tag/v0.56.0 |
| fetched from | `https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.56.0/httplib.h` |
| tag | `v0.56.0` |
| sha256 | `1f99e51881c4c9d0649b27c611442c2f4d9bcfec5a22a14d5fcd1f8106f730b4` |
| `wc -l` | 22875 |
| licence | MIT (`LICENSE`, upstream verbatim, sha256 `4b45cbe16d7b71b89ae6127e26e0d90a029198ca5e958ad8e3d0b8bbed364d8b`) |
| fetched | 2026-09-18 |

`httplib.h` and `LICENSE` are upstream's files byte-for-byte and carry **no
added SPDX line**: editing a vendored file to satisfy a local convention is how
it stops being verifiable against upstream. The **amalgamated** header, not
`split.py` output (record `docs/dsp/2026-09-16-remote-api.md` sec.15 R2) — one
permitted includer leaves nothing to amortise a split over, and a verbatim file
is hash-checkable against the release while a generated one is not.

**A version string is not an identity.** A copy already on this machine
reported the same `CPPHTTPLIB_VERSION "0.56.0"` and measured **22885 lines /
sha256 `a6e65d30…`** against the release's 22875 / `1f99e518…`. The figures
above were measured on the bytes committed here, fetched from the tag.

**MIT into AGPLv3.** MIT is a lax permissive non-copyleft licence the FSF calls
compatible with the GNU GPL; it flows into this AGPL-3.0-or-later work imposing
only notice retention, which is why `LICENSE` is vendored beside the header and
is never deleted. **Mongoose was disqualified for the opposite reason**: its
licence offers GPL-2.0 with no "or any later version", incompatible with
AGPLv3, and its commercial arm conflicts with the AGPL source release. Neither
arm works — do not "simplify" this dependency without re-reading that.

**Every optional backend is left UNDEFINED.** `CPPHTTPLIB_OPENSSL_SUPPORT`,
`_MBEDTLS_SUPPORT`, `_WOLFSSL_SUPPORT` — no TLS in v1 (record sec.2, sec.9), so
no link dependency and no second licence to reason about. `_ZLIB_SUPPORT`,
`_BROTLI_SUPPORT`, `_ZSTD_SUPPORT` — a link dependency for a compression
nothing on loopback needs. `_NO_EXCEPTIONS` — the rest of the app builds with
exceptions, and changing that for one TU is an ODR-shaped hazard. Everything
sec.8 tunes (four timeouts, keep-alive max count, thread pool, payload limit)
is set at **run time** in `app/src/api/ApiServer.cpp`, never by a macro.

## WebSocket support is compiled in, and NO macro guards it

`Server::WebSocket(pattern, handler)` is declared at `httplib.h:2189` and
`Server::process_request` calls `detail::is_websocket_upgrade(req)` at `:14407`
before routing. The record forbids push of any shape (sec.3, sec.13), so this
is a capability the vendored TU carries whatever either document says.

**Judgement: acceptable, and the reason is narrower than it looks.** An upgrade
*is* a `GET` (`is_websocket_upgrade` returns false for any other method,
`:5481`), so the method allowlist passes it and can never answer one with 405.
What makes an upgrade impossible is that **`ApiServer` registers eight `Get`
routes and no `WebSocket` handler**, so `websocket_handlers_` is empty, the loop
at `:14413` matches nothing, and control falls through to ordinary routing. The
server never writes the `101` line at `:14437` and never emits
`Sec-WebSocket-Accept`. `app/tests/test_api_server.cpp` I11 measures that.

**Two mechanical facts I11 recorded against that code.** The comment at
`:14436` says "fall through to 404" and is wrong — the fall-through reaches
routing, so a *known* path answers 200. And `pre_routing_handler_` runs
**twice** for an upgrade, at `:14408` and again from `Server::routing`
(`:13881`), so one such request spends two rate-limiter admissions. Neither
changes the judgement; both are facts a later reader would have to rediscover.
