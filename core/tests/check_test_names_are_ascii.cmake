# SPDX-License-Identifier: AGPL-3.0-or-later
#
# POLICY FLOOR -- see check_coherence_gate.cmake's own note. `cmake -P` does
# not inherit the root project's cmake_minimum_required, and a script with
# every policy unset behaves differently on different runners.
cmake_minimum_required(VERSION 3.22)
#
# Fails if any Catch2 test NAME contains a byte outside printable ASCII.
#
# WHY A TEST NAME IS NOT JUST A STRING. catch_discover_tests registers one
# ctest test per Catch2 test case, and ctest runs each by re-invoking the test
# binary with the name as a command-line FILTER argument. That round trip --
# C++ string literal, to the discovery listing, to a ctest file, to a process
# argument, back to Catch2's matcher -- is byte-exact only for ASCII. Every
# other hop has an encoding: the compiler's execution charset, the console
# code page, and on Windows the narrow/wide argv conversion.
#
# It broke exactly there. CI run 34043346767 (2026-09-06), windows-latest:
#
#   Filters: "Publish churn is O(1) in N: ... (record <?>6\, T12)"
#   No test cases matched '"Publish churn ... (record <?>6\, T12)"'
#   No tests ran
#
# Catch2 exited non-zero, so ctest reported test 285 as FAILED. The test's
# assertions were fine -- not one of them ran. ubuntu and macos, whose
# toolchains keep UTF-8 end to end, passed the same test. A failure mode that
# costs a red CI job and points at the wrong file deserves a tripwire.
#
# WHAT THIS CATCHES, AND WHAT IT DOES NOT. This is a textual scan, not a C++
# parse. It looks for a `TEST_CASE(`, `TEST_CASE_METHOD(`, `SCENARIO(` or
# `SCENARIO_METHOD(` whose FIRST string literal contains a byte outside
# 0x20..0x7E. It does NOT see a name built from a macro or a concatenation of
# adjacent literals, nor a name split across lines, nor a non-ASCII byte in a
# SECTION or a tag. Tags and SECTION names travel the same command line when
# someone filters by hand, so keep them ASCII too -- but this guard does not
# claim to enforce that, and a guard that claimed more than it checks would be
# worse than none.
#
# DIRS: a list of directories to scan for *.cpp. Every test directory in the
# tree is listed by the caller, including the ones only built with
# RTA_BUILD_APP=ON, because a name is wrong in the source whether or not this
# configuration compiles it.

if(NOT DEFINED DIRS)
    message(FATAL_ERROR "test-name ASCII guard needs -DDIRS=<dir>[;<dir>...]")
endif()

set(SOURCES "")
foreach(DIR ${DIRS})
    if(NOT IS_DIRECTORY "${DIR}")
        message(FATAL_ERROR "test-name ASCII guard: ${DIR} is not a directory")
    endif()
    file(GLOB_RECURSE FOUND "${DIR}/*.cpp")
    list(APPEND SOURCES ${FOUND})
endforeach()

if(SOURCES STREQUAL "")
    message(FATAL_ERROR "test-name ASCII guard scanned nothing -- DIRS=${DIRS} is wrong")
endif()

# The macro, optional whitespace, the opening quote, then a run of printable
# ASCII that is not itself a quote ([ -!#-~] is 0x20..0x21 plus 0x23..0x7E, so
# it excludes " and every control character including newline -- which is what
# keeps the match inside one line), then one byte that is NOT printable ASCII.
set(OFFENDING_NAME "(TEST_CASE|TEST_CASE_METHOD|SCENARIO|SCENARIO_METHOD)[ \t]*\\([ \t]*\"[ -!#-~]*[^ -~]")

set(OFFENDERS "")
foreach(FILE ${SOURCES})
    file(READ "${FILE}" CONTENT)
    if(CONTENT MATCHES "${OFFENDING_NAME}")
        list(APPEND OFFENDERS "${FILE}")
    endif()
endforeach()

list(LENGTH SOURCES SCANNED)
if(OFFENDERS)
    message(FATAL_ERROR
        "non-ASCII byte in a Catch2 test name -- ctest cannot filter for it on "
        "every runner, and the test reports FAILED without running: ${OFFENDERS}")
endif()
message(STATUS "test-name ASCII guard OK (${SCANNED} files scanned)")
