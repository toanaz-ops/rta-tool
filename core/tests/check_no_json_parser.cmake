# SPDX-License-Identifier: AGPL-3.0-or-later
#
# No JSON parser in shipped code. Lane L-API Task F / record
# docs/dsp/2026-09-16-remote-api.md sec.15 R16.
#
# `external/nlohmann/json.hpp` is vendored so the RTA_BUILD_APP=OFF tests --
# the only configuration CI runs -- can assert that the API's emitted
# documents are WELL-FORMED JSON. Every other OFF assertion is a substring
# match or a byte-compare against a file the same serialiser produced, and a
# stable-but-malformed document passes all of them.
#
# The parser is a TEST tool and nothing else. The wire format is written by
# hand through app/src/api/ApiJson.h, deliberately: shipped code that took a
# parser dependency would be paying its compile cost and its licence
# paragraph in every build of the app, to gain nothing the serialiser needs.
#
# MODELLED ON check_no_std_atomic_shared_ptr.cmake, with three deliberate
# differences, each named here so a reviewer diffing the two knows it is
# intended:
#
#  1. **No ALLOW, and therefore no sentinel 1.** `DIRS` is
#     core;platform;ui;tools;app/src -- note `app/src`, NOT `app`, so
#     `app/tests` is outside the scan BY CONSTRUCTION. There is no permitted
#     file at all, which is a strictly stronger claim than an allowlist:
#     shipped code never includes a JSON parser, full stop.
#
#  2. **A WITNESS replaces sentinel 2.** The model's second sentinel asks
#     "does the allowed file still contain the thing it is the sole exception
#     for?". With no allowed file there is nothing to ask that of, so instead:
#     at least one file under WITNESS_DIR must include the parser, or this
#     script fails. A vendored parser nothing tests with has stopped meaning
#     anything, and a guard that keeps printing OK over that is sentinel 2's
#     failure mode running in the other direction.
#
#     Losing sentinel 1 costs something specific: without an
#     `ALLOW IN_LIST SOURCES` check, a TYPO in any DIRS entry is silent -- the
#     remaining directories still glob files, SOURCES is non-empty, the
#     witness still passes, and this prints OK while watching four
#     directories instead of five. The replacement is not another sentinel
#     but more REDS: the acceptance procedure plants an offender under
#     `core/` and under `platform/`, not only under `app/src`.
#
#  3. **The source glob is widened** to `*.h;*.hpp;*.cpp;*.cc;*.inl`. The
#     model collects only `*.h` and `*.cpp` (its line 52), so `.hpp`, `.cc`
#     and `.inl` are blind spots a faithful copy would inherit -- and the
#     vendored parser is itself a `.hpp`. Widening the ORIGINAL is a separate
#     change and belongs in its own PR.

if(NOT DEFINED DIRS)
    message(FATAL_ERROR "json-parser guard needs -DDIRS=<dir>[;<dir>...]")
endif()
if(NOT DEFINED WITNESS_DIR)
    message(FATAL_ERROR "json-parser guard needs -DWITNESS_DIR=<dir>")
endif()
if(NOT IS_DIRECTORY "${WITNESS_DIR}")
    message(FATAL_ERROR "json-parser guard: WITNESS_DIR=${WITNESS_DIR} is not a directory")
endif()

set(GLOB_PATTERNS "*.h" "*.hpp" "*.cpp" "*.cc" "*.inl")

set(SOURCES "")
foreach(DIR ${DIRS})
    if(NOT IS_DIRECTORY "${DIR}")
        message(FATAL_ERROR "json-parser guard: ${DIR} is not a directory")
    endif()
    foreach(PATTERN_GLOB ${GLOB_PATTERNS})
        file(GLOB_RECURSE FOUND "${DIR}/${PATTERN_GLOB}")
        list(APPEND SOURCES ${FOUND})
    endforeach()
endforeach()

# A guard that scanned nothing is the failure this makes loud, exactly as the
# model does.
if(SOURCES STREQUAL "")
    message(FATAL_ERROR "json-parser guard scanned nothing -- DIRS=${DIRS} is wrong")
endif()

# COMMENTS ARE NOT CODE. Copied from the model (its lines 128-132) with the
# same reasoning: without it, a file's own doc comment mentioning the parser
# would vote, and prose would stand in for code. Note the known limit the
# model carries too -- the second regex strips only `/* ... */` bodies
# containing no asterisk, so a `/** ... */` doxygen block SURVIVES. The
# PATTERN below is anchored on an include directive precisely to narrow that
# false-positive surface: merely naming nlohmann in prose is fine, and only a
# literal include trips it.
function(rta_strip_comments OUT_VAR IN_VAR)
    string(REGEX REPLACE "//[^\n]*" "" STRIPPED "${${IN_VAR}}")
    string(REGEX REPLACE "/\\*[^*]*\\*/" "" STRIPPED "${STRIPPED}")
    set(${OUT_VAR} "${STRIPPED}" PARENT_SCOPE)
endfunction()

# Anchored on the INCLUDE DIRECTIVE, not on the bare word -- this repo's own
# idiom for an include-scanning guard (check_no_framework_deps.cmake:54 is the
# same shape). The four alternatives cover the parsers a future contributor
# would actually reach for.
set(PATTERN "#[ \t]*include[ \t]*[<\"]([^>\"]*/)?(json\\.hpp|nlohmann|rapidjson|picojson|json/json\\.h)")

# The witness, and it is matched against COMMENT-STRIPPED source for the same
# reason the offender scan is: a `// TODO: parse this with nlohmann/json` in
# any test file would otherwise satisfy it, which is the exact
# prose-standing-in-for-code defect this project has already hit once.
set(WITNESS_FOUND "")
foreach(PATTERN_GLOB ${GLOB_PATTERNS})
    file(GLOB_RECURSE FOUND "${WITNESS_DIR}/${PATTERN_GLOB}")
    foreach(FILE ${FOUND})
        file(READ "${FILE}" CONTENT)
        rta_strip_comments(CODE CONTENT)
        if(CODE MATCHES "${PATTERN}")
            list(APPEND WITNESS_FOUND "${FILE}")
        endif()
    endforeach()
endforeach()

if(WITNESS_FOUND STREQUAL "")
    message(FATAL_ERROR
        "json-parser guard is not proving anything: no file under ${WITNESS_DIR} "
        "includes a JSON parser, so the vendored external/nlohmann/json.hpp is "
        "asserting nothing about the API's output. Either a test lost its "
        "include, or the vendored header should be deleted along with its "
        "licence and provenance files")
endif()

set(OFFENDERS "")
foreach(FILE ${SOURCES})
    file(READ "${FILE}" CONTENT)
    rta_strip_comments(CODE CONTENT)
    if(CODE MATCHES "${PATTERN}")
        list(APPEND OFFENDERS "${FILE}")
    endif()
endforeach()

list(LENGTH SOURCES SCANNED)
list(LENGTH WITNESS_FOUND WITNESSES)
if(OFFENDERS)
    message(FATAL_ERROR
        "a JSON parser is included by SHIPPED code. external/nlohmann/json.hpp is "
        "vendored test-only (record sec.15 R16): the wire format is written by "
        "hand through app/src/api/ApiJson.h, and shipped code that took a parser "
        "dependency would pay its compile cost and its licence paragraph in every "
        "build to gain nothing the serialiser needs: ${OFFENDERS}")
endif()
message(STATUS "json-parser guard OK (${SCANNED} files scanned, ${WITNESSES} test witness(es))")
