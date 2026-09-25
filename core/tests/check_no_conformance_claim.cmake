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
# Fails if the weighting/meter track's OWN files claim IEC 61672-1 "Class 1",
# "Class 2" (or "Class 0") conformance. Modelled on check_no_framework_deps.cmake.
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
# sourced, no "Class 1" or "Class 2" claim may appear in this track's files --
# not as an identifier, a comment, a doc string, or a test name -- because it
# would be an unverifiable claim shipping as if it were a proven one. "Class
# 2" is in scope by the same reasoning as "Class 1" (plan "Build sequence and
# acceptance gate": "Any Class 1 or Class 2 claim, anywhere, in any
# artefact"); "Class 0" was already covered.
#
# Scope is deliberately narrow -- dsp/Weighting.*, meter/*, and this track's
# three test files -- and NOT all of core/: the filter-bank track's
# BandWeights.h / test_butterworth.cpp / test_filterbank.cpp already carry a
# legitimate, separately-sourced "ANSI S1.11 Class 1" conformance claim for
# the octave-band filters, a different standard this guard has no business
# policing. "No claim ... produced by this work" (the plan's own wording)
# means this track's files, not every file under core/.
#
# SPL-R9: two ways to select what gets scanned, mirroring
# check_no_framework_deps.cmake's CORE_DIR/GLOBS split -- exactly one of
# CORE_DIR or GLOBS must be given:
#   CORE_DIR   this track's fixed list under core/ (unchanged below).
#   GLOBS      an explicit, semicolon-separated file list for a layer with no
#              such directory convention -- app/'s report/export templates,
#              named by app/tests/CMakeLists.txt rather than core/tests/, so a
#              change to one scope's file list cannot silently shrink the
#              other's. Pass TEST_NAME and SCOPE_LABEL alongside it: without
#              them the messages below would say "core_makes_no_class_1_claim"
#              and "core/" while scanning app/, which is what SPL-R9 calls out
#              by name as wrong.
if(NOT DEFINED CORE_DIR AND NOT DEFINED GLOBS)
    message(FATAL_ERROR "Neither CORE_DIR nor GLOBS is set")
endif()
if(DEFINED CORE_DIR AND DEFINED GLOBS)
    message(FATAL_ERROR "Both CORE_DIR and GLOBS are set -- pass exactly one")
endif()

if(NOT DEFINED TEST_NAME)
    set(TEST_NAME "core_makes_no_class_1_claim")
endif()
if(NOT DEFINED SCOPE_LABEL)
    set(SCOPE_LABEL "core/")
endif()

if(DEFINED GLOBS)
    file(GLOB sources ${GLOBS})
else()
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
endif()

# SPL-R10 / memory/a-naming-grep-that-bans-a-word-bans-its-own-justification.md:
# this guard's own two ctest names -- "core_makes_no_class_1_claim" and
# "report_makes_no_class_1_claim" -- each contain the literal shape
# "class_1" or "class_1_claim" that the regex below hunts for. A file that
# explains *why* a false positive exists (SplReportStyle.h's SPL-R10 comment,
# for one) must name the guard it is talking about, and naming it in a
# backtick-quoted comment is not a conformance claim. Strip just these two
# known self-references before matching -- not comments in general, because
# an actual "Class 1" claim written INSIDE a comment is exactly the shape
# this guard exists to catch (its own docstring above: "not as an
# identifier, a comment, a doc string, or a test name"), so blanket
# comment-stripping would open a hole in the guard it is supposed to be
# closing. If this list ever stops matching anything in the scanned corpus,
# that is a sign the guard's own name changed and this list is stale --
# re-check it rather than assuming it is still needed.
set(self_reference_exemptions
    "core_makes_no_class_1_claim"
    "report_makes_no_class_1_claim"
)

set(violations "")
foreach(file IN LISTS sources)
    file(READ "${file}" content)

    # PR #30 fix round (MEDIUM): a real claim split across two adjacent C++
    # string literals -- e.g. "IEC 61672-1 Class " followed by a line break
    # and "1 ..." -- concatenates into one claim at compile time but was
    # invisible to a regex that only looks within a single line. Join
    # exactly that shape (a literal's closing quote, only whitespace/a line
    # break, the next literal's opening quote) before matching -- mirroring
    # what the compiler itself does: nothing is inserted, so a literal's own
    # trailing/leading spacing (already there for word boundaries -- see the
    # honesty sentence at SplReportSections.cpp:148-153, which this exact
    # join exercises on every scan) is preserved rather than duplicated.
    set(content_to_scan "${content}")
    string(REGEX REPLACE "\"[ \t]*\r?\n[ \t]*\"" "" content_to_scan "${content_to_scan}")

    foreach(exemption IN LISTS self_reference_exemptions)
        string(REPLACE "${exemption}" "" content_to_scan "${content_to_scan}")
    endforeach()
    if(content_to_scan MATCHES "[Cc][Ll][Aa][Ss][Ss][ \t_-]*[012]")
        list(APPEND violations "${file}")
    endif()
endforeach()

list(LENGTH sources n_sources)
if(n_sources EQUAL 0)
    message(FATAL_ERROR "No sources found for ${TEST_NAME} (scope: ${SCOPE_LABEL}) -- check is not actually running")
endif()

if(violations)
    message(FATAL_ERROR
        "${SCOPE_LABEL} must not claim IEC 61672-1 Class 0/Class 1/Class 2 "
        "conformance -- the full Table 3 tolerance envelope is not sourced "
        "yet. See docs/dsp/2026-08-27-weighting-and-meters.md. Offending "
        "files:\n"
        "  ${violations}")
endif()

message(STATUS "${TEST_NAME}: OK (${n_sources} files scanned)")
