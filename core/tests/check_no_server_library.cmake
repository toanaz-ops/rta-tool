# SPDX-License-Identifier: AGPL-3.0-or-later
#
# POLICY FLOOR -- see check_coherence_gate.cmake's own note. `cmake -P` does
# not inherit the root project's cmake_minimum_required, and a script with
# every policy unset behaves differently on different runners.
cmake_minimum_required(VERSION 3.22)
#
# No server library outside app/src/api/ApiServer.cpp. Lane L-API Task K /
# record docs/dsp/2026-09-16-remote-api.md sec.11 item 10, sec.15 R2/R3.
#
# `external/cpp-httplib/httplib.h` is vendored so this project has an HTTP/1.1
# server without hand-rolling one (record sec.2 weighed that and listed the
# machinery: request parsing, chunked transfer, keep-alive accounting, header
# folding, a thread pool, timeout handling). ONE translation unit includes it,
# and that is the whole compile-cost argument for the amalgamated header over
# split.py output: nothing to amortise a split over.
#
# WHAT A SECOND INCLUDER WOULD COST, since "one TU" can read as tidiness. The
# amalgamated header is 22875 lines; record sec.2 measured a 6-7x configure +
# build ratio for a TU that takes it, on MSVC. But the cost this guard is
# really about is architectural: `app/src/api/` is otherwise framework-free
# and server-library-free, which is what lets the serialiser, the request
# policy, the rate limiter and the Host check all be proven in the
# RTA_BUILD_APP=OFF configuration -- the only one CI runs (sec.15 R1, R15). A
# second includer is how that split erodes one file at a time.
#
# MODELLED ON check_no_std_atomic_shared_ptr.cmake, and this is a COPY rather
# than a re-invocation of it: PATTERN (:134) and SENTINEL_PATTERN (:141) are
# `set()` inside that script, not `-D` arguments, so they cannot be overridden
# from a registration. Both of its sentinels and its empty-SOURCES
# FATAL_ERROR come across too -- a copy that took only the first sentinel
# would be weaker than the original it cites.
#
# TWO DELIBERATE DIFFERENCES from "copy and change nothing else", named here
# so a reviewer diffing the two knows they are intended:
#
#  1. **The source glob is widened** to `*.h;*.hpp;*.cpp;*.cc;*.inl`. The
#     model collects only `*.h` and `*.cpp` (its line 52), so `.hpp`, `.cc`
#     and `.inl` are blind spots a faithful copy would inherit. Widening the
#     ORIGINAL is a separate change and belongs in its own PR.
#
#  2. **PATTERN is anchored on the INCLUDE DIRECTIVE**, not on the bare
#     library name -- this repo's own idiom for an include-scanning guard
#     (check_no_framework_deps.cmake:54 is the same shape). That deliberately
#     NARROWS the false-positive surface: a comment that merely NAMES
#     cpp-httplib is fine, and ApiServer.h's class comment names it five
#     times on purpose. What still trips it is a literal `#include
#     <httplib.h>` inside a `/** ... */` doxygen block, because
#     rta_strip_comments removes only `/* ... */` bodies containing no
#     asterisk (model :129-130). That false positive is part of this guard's
#     contract and the acceptance procedure plants it.
#
# DIRS: directories to scan. `app` must be one of them -- omit it and
#       `ALLOW IN_LIST SOURCES` is false and this script FATAL_ERRORs on every
#       run, which is sentinel 1 doing its job.
# ALLOW: the one file permitted to contain the pattern.
#
# Every DIRS entry and ALLOW must be ABSOLUTE: `file(GLOB_RECURSE)` returns
# absolute paths, so a relative ALLOW dies either at the EXISTS check below or
# at sentinel 1. `external/` is at the repo root, outside every DIRS entry,
# which is sec.15 R3's reason for putting it there rather than under `app/`.

if(NOT DEFINED DIRS)
    message(FATAL_ERROR "server-library guard needs -DDIRS=<dir>[;<dir>...]")
endif()
if(NOT DEFINED ALLOW)
    message(FATAL_ERROR "server-library guard needs -DALLOW=<file>")
endif()
if(NOT EXISTS "${ALLOW}")
    message(FATAL_ERROR "server-library guard: ALLOW=${ALLOW} does not exist")
endif()

set(GLOB_PATTERNS "*.h" "*.hpp" "*.cpp" "*.cc" "*.inl")

set(SOURCES "")
foreach(DIR ${DIRS})
    if(NOT IS_DIRECTORY "${DIR}")
        message(FATAL_ERROR "server-library guard: ${DIR} is not a directory")
    endif()
    foreach(PATTERN_GLOB ${GLOB_PATTERNS})
        file(GLOB_RECURSE FOUND "${DIR}/${PATTERN_GLOB}")
        list(APPEND SOURCES ${FOUND})
    endforeach()
endforeach()

# A guard that scanned nothing is the failure this makes loud, exactly as the
# model does (its :56-58).
if(SOURCES STREQUAL "")
    message(FATAL_ERROR "server-library guard scanned nothing -- DIRS=${DIRS} is wrong")
endif()

# SENTINEL 1 (model :65): the allowed file is inside the scanned set. Worth
# having for a reason D3 named against the JSON-parser guard, which cannot
# have it: without this check, a TYPO in any DIRS entry is silent -- the
# remaining directories still glob files, SOURCES is non-empty, and the guard
# prints OK while watching four directories instead of five.
if(NOT ALLOW IN_LIST SOURCES)
    message(FATAL_ERROR
        "server-library guard did not see ${ALLOW} -- it is not watching. Is `app` "
        "in DIRS, and is ALLOW an absolute path?")
endif()

# COMMENTS ARE NOT CODE, and the model's own history is why this is copied
# rather than skipped: its sentinel was once satisfied by the guarded file's
# doc comment, so deleting the real declaration left the guard passing and
# still announcing a proof. Note the known limit that comes with it -- the
# second regex strips only `/* ... */` bodies containing no asterisk, so a
# `/** ... */` doxygen block SURVIVES.
#
# A FUNCTION, NEVER A MACRO. A CMake macro substitutes its parameters
# textually and the result is re-lexed as CMake, so every scanned source
# would be parsed twice -- and any backslash-x escape in any scanned file,
# even inside a comment, would abort the run with `Invalid character escape`
# instead of reporting a finding. The contents are passed BY VARIABLE NAME
# (`IN_VAR`, then `${${IN_VAR}}`), which also keeps megabytes of source text
# off the call line.
function(rta_strip_comments OUT_VAR IN_VAR)
    string(REGEX REPLACE "//[^\n]*" "" STRIPPED "${${IN_VAR}}")
    string(REGEX REPLACE "/\\*[^*]*\\*/" "" STRIPPED "${STRIPPED}")
    set(${OUT_VAR} "${STRIPPED}" PARENT_SCOPE)
endfunction()

# The three alternatives are the C++ HTTP servers a future contributor would
# actually reach for. `mongoose` is named for a second reason: its licence
# offers GPL-2.0 with NO "or any later version", incompatible with this
# AGPL-3.0-or-later work, and its commercial arm conflicts with the AGPL
# source release -- neither arm works, and a guard naming it is cheaper than
# rediscovering that (external/cpp-httplib/PROVENANCE.md carries the full
# paragraph).
set(PATTERN          "#[ \t]*include[ \t]*[<\"](httplib|civetweb|mongoose)")

# SENTINEL 2 (model :145): the allowed file STILL CONTAINS the thing it is the
# sole exception for. Tighter than PATTERN on purpose -- anchored on the exact
# include ApiServer.cpp carries -- because a guard whose one exception has
# stopped including a server library is a guard announcing a proof it no
# longer has: either ApiServer.cpp stopped being the server, or the ALLOW path
# drifted, and the next reader needs telling rather than reassuring.
set(SENTINEL_PATTERN "#[ \t]*include[ \t]*<httplib\\.h>")

file(READ "${ALLOW}" ALLOW_CONTENT)
rta_strip_comments(ALLOW_CODE ALLOW_CONTENT)
if(NOT ALLOW_CODE MATCHES "${SENTINEL_PATTERN}")
    message(FATAL_ERROR
        "server-library guard is not proving anything: ${ALLOW} no longer "
        "INCLUDES the server library it is the sole exception for (looked for "
        "`#include <httplib.h>` in code, comments excluded)")
endif()

set(OFFENDERS "")
foreach(FILE ${SOURCES})
    if(FILE STREQUAL ALLOW)
        continue()
    endif()
    file(READ "${FILE}" CONTENT)
    rta_strip_comments(CODE CONTENT)
    if(CODE MATCHES "${PATTERN}")
        list(APPEND OFFENDERS "${FILE}")
    endif()
endforeach()

list(LENGTH SOURCES SCANNED)
if(OFFENDERS)
    message(FATAL_ERROR
        "a server library is included outside ${ALLOW}. Exactly one translation "
        "unit may take external/cpp-httplib/httplib.h: app/src/api/ is otherwise "
        "server-library-free, which is what lets the serialiser, the request "
        "policy, the rate limiter and the Host check all be proven in the "
        "RTA_BUILD_APP=OFF configuration CI actually runs (record sec.15 R1, "
        "R15). If a header needs the type, use a pimpl -- ApiServer.h is one, "
        "for exactly this reason: ${OFFENDERS}")
endif()
message(STATUS "server-library guard OK (${SCANNED} files scanned)")
