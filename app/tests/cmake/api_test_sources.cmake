# SPDX-License-Identifier: AGPL-3.0-or-later
#
# LOW follow-up batch, item 14: split out of app/tests/CMakeLists.txt -- pure
# relocation. Lane L-API's test surface (docs/plans/2026-09-17-remote-api-
# impl-plan.md): the read-only remote API's pure half, framework-free and
# server-library-free except test_api_server*.cpp's own real loopback socket.
set(RTA_API_TEST_SOURCES
    # --- lane L-API (docs/plans/2026-09-17-remote-api-impl-plan.md) ---
    # The read-only remote API's pure half: the wire's number format, the
    # request-shape policy and the serialiser. All framework-free and all in
    # the RTA_BUILD_APP=OFF target, which is the only configuration CI runs
    # (record sec.15 R15) -- so the whole surface is proven on three OSes.
    test_api_json.cpp
    test_api_policy.cpp
    test_api_limits.cpp
    test_api_serialise.cpp
    test_api_serialise_spatial.cpp
    test_api_schema.cpp
    # test_api_pre_routing.cpp: LOW follow-up batch, item 12 --
    # decidePreRoutingRefusal (ApiPolicy.h/.cpp) is ApiServer.cpp's own
    # pre-routing sequencing (record sec.9 controls 1-5), pulled out of that
    # httplib TU so the ORDER itself -- host header count, then the Host
    # allowlist, then method, then Bearer, then body size -- is provable here
    # rather than only through the real socket in test_api_server_refusals.cpp.
    test_api_pre_routing.cpp
    # test_api_server.cpp: Task I. The server over a REAL loopback socket,
    # here rather than in app/tests_juce because sec.15 R15 put ApiServer on
    # a std::thread -- so the Host check, its ORDERING, the Bearer path, the
    # CORS posture, 413, 304 and shutdown all run on three CI OSes. The test
    # client is app/tests/RawHttpClient.h (raw sockets, ~40 lines of logic):
    # an httplib::Client would be a SECOND includer of <httplib.h> and
    # no_server_library_outside_api permits exactly one.
    test_api_server.cpp
    # test_api_server_bind.cpp: the same task, split along the seam that made
    # the file above long -- HOW it binds and WHEN it refuses, rather than
    # what it answers. It carries the FIXED-port case, which is the branch the
    # shipped configuration takes and which every case in the other file
    # (settings.port == 0) leaves untouched.
    test_api_server_bind.cpp
    # test_api_server_refusals.cpp: the station-5 verify pass on PR #18 --
    # every way OUT of the request path and IN WHAT ORDER, plus the method
    # surface `Allow` advertises. Same seam the repo already uses for
    # test_alignment_wizard_refusals.cpp: "which refusal, and does it cost
    # the legitimate client anything" is a different question from "does the
    # refusal happen at all", and the answer to the first one was no.
    test_api_server_refusals.cpp
)
