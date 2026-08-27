# SPDX-License-Identifier: AGPL-3.0-or-later
# Fails if the weighting/meter track's OWN files claim IEC 61672-1 "Class 1"
# (or "Class 0") conformance. Modelled on check_no_framework_deps.cmake.
#
# Why this exists: docs/dsp/2026-08-27-weighting-and-meters.md records that
# the analytic weighting curve is verified within 0.05 dB of IEC 61672-1
# Table 3, and the digital filter's approximation error is published there --
# but the full Table 2 tolerance envelope is paywalled and only four points
# (20 Hz / 1 kHz / 10 kHz / 16 kHz) are corroborated. Until that table is
# sourced, no "Class 1" claim may appear in this track's files -- not as an
# identifier, a comment, a doc string, or a test name -- because it would be
# an unverifiable claim shipping as if it were a proven one.
#
# Scope is deliberately narrow -- dsp/Weighting.*, meter/*, and this track's
# three test files -- and NOT all of core/: the filter-bank track's
# BandWeights.h / test_butterworth.cpp / test_filterbank.cpp already carry a
# legitimate, separately-sourced "ANSI S1.11 Class 1" conformance claim for
# the octave-band filters, a different standard this guard has no business
# policing. "No claim ... produced by this work" (the plan's own wording)
# means this track's files, not every file under core/.
if(NOT DEFINED CORE_DIR)
    message(FATAL_ERROR "CORE_DIR is not set")
endif()

file(GLOB sources
    "${CORE_DIR}/include/rta/dsp/Weighting.h"
    "${CORE_DIR}/src/dsp/Weighting.cpp"
    "${CORE_DIR}/include/rta/meter/*.h"
    "${CORE_DIR}/src/meter/*.cpp"
    "${CORE_DIR}/tests/test_weighting.cpp"
    "${CORE_DIR}/tests/test_detector.cpp"
    "${CORE_DIR}/tests/test_leq.cpp"
)

set(violations "")
foreach(file IN LISTS sources)
    file(READ "${file}" content)
    if(content MATCHES "[Cc][Ll][Aa][Ss][Ss][ \t_-]*[01]")
        list(APPEND violations "${file}")
    endif()
endforeach()

list(LENGTH sources n_sources)
if(n_sources EQUAL 0)
    message(FATAL_ERROR "No sources found under ${CORE_DIR} -- check is not actually running")
endif()

if(violations)
    message(FATAL_ERROR
        "core/ must not claim IEC 61672-1 Class 1/Class 0 conformance -- the "
        "full Table 2 tolerance envelope is not sourced yet. See "
        "docs/dsp/2026-08-27-weighting-and-meters.md. Offending files:\n"
        "  ${violations}")
endif()

message(STATUS "core_makes_no_class_1_claim: OK (${n_sources} files scanned)")
