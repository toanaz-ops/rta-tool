# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The coherence gate (docs/dsp/2026-08-28-dual-fft.md §3) is only real if
# something enforces it. For ONE frame, |X*Y|^2 == |X|^2|Y|^2 identically, so
# coherence is exactly 1.0 at every frequency and a broken engine looks
# flawless. TransferEstimator.cpp's makeSnapshot() is the single place the gate
# is applied, and therefore the single place the field may be written.
#
# WHAT THIS CATCHES, AND WHAT IT DOES NOT. This is a textual grep over the
# whole file, not a parse of C++ -- it enforces a CONVENTION against the direct
# forms of writing the field:
#   - `obj.coherence = ...`  and  `ptr->coherence = ...`
#   - `obj.coherence.emplace(...)`
#   - a reference bound directly to the field, `auto& x = obj.coherence;`,
#     because that alias can be assigned through on any later, unrelated line
#     the assignment-pattern check above would not see.
# It does NOT catch every way C++ can reach the same bytes: an alias built up
# over several statements before the bind (`auto& x = pick(obj); ... x =
# obj.coherence;` with the binding and the field on different sides), a
# pointer-to-member, reflection-style field access, or an aggregate
# initialisation of TransferSnapshot that sets `.coherence` as a designated
# initialiser (`TransferSnapshot{.coherence = ...}` -- this WOULD match the
# assignment pattern above, since `.coherence =` appears literally, but a
# spacing or line-break inside the designator could dodge it). A guard that
# claimed more than this would be lying to the next reader; this one is a
# tripwire for the ordinary and the careless, not a proof.
file(GLOB_RECURSE SOURCES "${CORE_DIR}/src/*.cpp" "${CORE_DIR}/include/*.h")

if(SOURCES STREQUAL "")
    message(FATAL_ERROR "coherence-gate guard scanned nothing -- CORE_DIR=${CORE_DIR} is wrong")
endif()

set(SENTINEL "${CORE_DIR}/src/dsp/TransferEstimator.cpp")
if(NOT SENTINEL IN_LIST SOURCES)
    message(FATAL_ERROR "coherence-gate guard did not see ${SENTINEL} -- it is not watching")
endif()

set(OFFENDERS "")
foreach(FILE ${SOURCES})
    if(FILE STREQUAL SENTINEL)
        continue()
    endif()
    file(READ "${FILE}" CONTENT)
    if(CONTENT MATCHES "(\\.|->)coherence[ \t]*=" OR
       CONTENT MATCHES "coherence[ \t]*=[ \t]*std::vector" OR
       CONTENT MATCHES "coherence[ \t]*\\.[ \t]*emplace" OR
       CONTENT MATCHES "&[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*=[^;]*(\\.|->)coherence")
        list(APPEND OFFENDERS "${FILE}")
    endif()
endforeach()

list(LENGTH SOURCES SCANNED)
if(OFFENDERS)
    message(FATAL_ERROR "coherence assigned outside the gate: ${OFFENDERS}")
endif()
message(STATUS "coherence-gate guard OK (${SCANNED} files scanned)")
