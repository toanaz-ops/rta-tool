# SPDX-License-Identifier: AGPL-3.0-or-later
#
# POLICY FLOOR -- required, not decorative. This file runs under `cmake -P`
# (script mode), which does NOT inherit the root CMakeLists.txt's
# cmake_minimum_required. Without a floor here the script starts with every
# policy unset, and CMake's behaviour then depends on the CMake build on the
# machine: `if(x IN_LIST list)` (CMP0057, introduced in 3.3) is a hard
# "Unknown arguments specified" error under OLD. That is exactly how CI's
# ubuntu-latest job went red on 2026-09-06 while windows and macos passed --
# same script, different CMake, different policy defaults. Keep this line.
cmake_minimum_required(VERSION 3.22)
# Fails if the weighting/meter track's OWN files claim IEC 61672-1 "Class 1"
# (or "Class 0") conformance. Modelled on check_no_framework_deps.cmake.
#
# Why this exists: docs/dsp/2026-08-27-weighting-and-meters.md records that
# the analytic weighting curve is verified within 0.05 dB of IEC 61672-1
# Table 3, and the digital filter's approximation error is published there --
# but the full Table 3 tolerance envelope is paywalled and only four points
#
# TABLE 3, NOT TABLE 2 (record §13 Q10, lane L6a task G2). Both the comment
# below and the FATAL_ERROR text at the bottom of this file used to say
# "Table 2". In IEC 61672-1:2013, Table 2 is "Acceptance limits for deviations
# of directional response from the design goal"; the frequency-weighting
# tolerances -- design goal and acceptance limits together -- are Table 3,
# "Frequency weightings and acceptance limits". Verified from the IEC official
# preview of Ed 2.0 2013-09 (complete Contents with every clause and table
# title), read 2026-09-16; provenance in
# docs/research/2026-09-16-l6a-spl-pro-station1-research.md §A1.8. This is not
# cosmetic: a purchaser acting on the guard's own wording would buy against
# the wrong reference.
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
    # Lane L6a (record docs/dsp/2026-09-16-spl-pro-l6a.md; SPL-R9 defect 8).
    # These are the DENSEST quotations of IEC 61672-1, IEC 61252, ISO 1996-1,
    # NIOSH 98-126 and 29 CFR 1910.95 anywhere in this repository -- Wave 1
    # alone transcribes 222 rows of two regulators' tables -- and until they
    # were named here they were entirely UNGUARDED while the three files above
    # were policed. A file that quotes a standard is exactly the file most
    # likely to acquire a conformance claim beside the quotation.
    #
    # Added BY NAME, not by a glob over tests/, because a glob would also pull
    # in the filter-bank track's test_butterworth.cpp and test_filterbank.cpp,
    # which carry a legitimate and separately sourced ANSI S1.11 Class 1 claim
    # this guard has no business policing. And because a misspelled name in
    # such a list shrinks coverage SILENTLY, the risen `OK (N files scanned)`
    # count is the only proof the addition took effect -- read it, never
    # predict it (memory/core-must-not-include-frameworks.md).
    "${CORE_DIR}/tests/test_block.cpp"
    "${CORE_DIR}/tests/test_window_energy.cpp"
    "${CORE_DIR}/tests/test_level_histogram.cpp"
    "${CORE_DIR}/tests/test_level_histogram_span.cpp"
    "${CORE_DIR}/tests/test_alarm.cpp"
    "${CORE_DIR}/tests/test_dose.cpp"
    "${CORE_DIR}/tests/test_dose_constants.cpp"
    "${CORE_DIR}/tests/test_dose_tables.cpp"
    "${CORE_DIR}/tests/test_dose_table12.cpp"
    "${CORE_DIR}/tests/DoseTableFixtures.h"
    "${CORE_DIR}/tests/BlockFixtures.h"
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
        "full Table 3 tolerance envelope is not sourced yet. See "
        "docs/dsp/2026-08-27-weighting-and-meters.md. Offending files:\n"
        "  ${violations}")
endif()

message(STATUS "core_makes_no_class_1_claim: OK (${n_sources} files scanned)")
