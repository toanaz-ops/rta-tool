# SPDX-License-Identifier: AGPL-3.0-or-later
#
# POLICY FLOOR -- see check_coherence_gate.cmake's own note. `cmake -P` does
# not inherit the root project's cmake_minimum_required, and a script with
# every policy unset behaves differently on different runners.
cmake_minimum_required(VERSION 3.22)
#
# Fails if anything outside app/src/measure/AtomicSharedPtr.h names
# `std::atomic<>` over a `shared_ptr` -- directly, or through the
# `SnapshotPtr` / `TracePtr`-style aliases this repo uses for one.
#
# WHY A GREP AND NOT A COMPILE ERROR. `std::atomic<std::shared_ptr<T>>` is
# valid C++20 and compiles fine on MSVC and libstdc++. It is Apple's libc++
# that has not shipped the P0718 specialisation, so there the primary template
# is selected and its `is_trivially_copyable` static_assert fires. That means
# the mistake is INVISIBLE on two of the three CI runners and on every
# developer machine in this project -- which is exactly how it reached main
# and turned macos-latest red on 2026-09-06 (run 34043346767), across three
# translation units at once. A guard that runs everywhere is the only kind
# that would have caught it before the push.
#
# AtomicSharedPtr (app/src/measure/AtomicSharedPtr.h) is the one place allowed
# to name it: inside the feature-test branch that only a library WITH the
# specialisation ever compiles.
#
# WHAT THIS CATCHES, AND WHAT IT DOES NOT. Textual scan, not a C++ parse. It
# matches `std::atomic<` (or a bare `atomic<` after a `using namespace`) whose
# argument mentions `shared_ptr` or ends in `Ptr` before the closing `>`. It
# does NOT see an atomic whose argument is a typedef named something else
# entirely, an atomic built inside a template alias, or `std::atomic_ref` over
# one. It is a tripwire for the obvious form, not a proof -- and the obvious
# form is what was written.
#
# DIRS: directories to scan for *.h and *.cpp.
# ALLOW: one file permitted to contain the pattern.

if(NOT DEFINED DIRS)
    message(FATAL_ERROR "atomic-shared_ptr guard needs -DDIRS=<dir>[;<dir>...]")
endif()
if(NOT DEFINED ALLOW)
    message(FATAL_ERROR "atomic-shared_ptr guard needs -DALLOW=<file>")
endif()
if(NOT EXISTS "${ALLOW}")
    message(FATAL_ERROR "atomic-shared_ptr guard: ALLOW=${ALLOW} does not exist")
endif()

set(SOURCES "")
foreach(DIR ${DIRS})
    if(NOT IS_DIRECTORY "${DIR}")
        message(FATAL_ERROR "atomic-shared_ptr guard: ${DIR} is not a directory")
    endif()
    file(GLOB_RECURSE FOUND "${DIR}/*.h" "${DIR}/*.cpp")
    list(APPEND SOURCES ${FOUND})
endforeach()

if(SOURCES STREQUAL "")
    message(FATAL_ERROR "atomic-shared_ptr guard scanned nothing -- DIRS=${DIRS} is wrong")
endif()

# The sentinel: the guard is worthless if it has stopped watching the one file
# that is SUPPOSED to trip it. AtomicSharedPtr.h must be in the scanned set and
# must still DECLARE the member -- if it does not, either the allow-list path
# drifted or the wrapper no longer uses the specialisation at all, and either
# way the next reader needs to be told rather than reassured.
if(NOT ALLOW IN_LIST SOURCES)
    message(FATAL_ERROR "atomic-shared_ptr guard did not see ${ALLOW} -- it is not watching")
endif()

# COMMENTS ARE NOT CODE, and until a review caught it this script could not
# tell the difference. The sentinel below was satisfied by AtomicSharedPtr.h's
# own doc comment, which spells the pattern out in prose -- so deleting the
# real `std::atomic<Ptr> value_;` declaration left the guard passing, still
# announcing that it was proving something. Strip `//` line comments from
# every file before matching, and the prose stops voting.
#
# Stripping does not weaken the OFFENDER scan either: a commented-out atomic
# compiles on no platform, so flagging one was a false positive. It also frees
# the rest of the tree to DISCUSS the forbidden form in comments, which the
# files being guarded have good reason to do.
#
# Block comments are stripped too, but only the shape this tree actually
# contains. An earlier revision of this comment said the tree "does not use"
# `/* */`; that was wrong -- 16 files carry inline argument annotations like
# `/*fcHz=*/` and `/*checkGeometry=*/`. None of them hides the pattern today,
# so nothing was slipping through, but the comment was asserting something
# untrue about the code it sits next to.
#
# The pattern below matches a `/* ... */` whose body contains no `*`, which is
# every one of those 16 and any ordinary one-sentence block. A body that DOES
# contain a `*` -- a decorated banner, a doxygen `/** ... */` -- survives, so
# text inside one could still satisfy the sentinel. That is accepted rather
# than chased: this guard exists to catch DRIFT, a contributor writing the
# natural thing or deleting the declaration, and neither of those arrives
# disguised inside a doxygen block. A regex that handled every C comment
# correctly would be harder to read than the rule it enforces.
#
# `//` inside a string literal (a URL, say) is stripped too. That costs
# nothing here: the patterns below cannot match a string literal's tail.
#
# A FUNCTION, AND NEVER AGAIN A MACRO. This was a macro until 2026-09-16, and
# it took the file's text as an argument. A CMake macro substitutes its
# parameters TEXTUALLY into its body and the result is re-lexed as CMake, so
# every scanned source was parsed twice: once as C++ by the compiler, once as
# CMake code by this guard. Any backslash-x escape in any scanned file -- in a
# string literal or, just as fatally, inside a comment -- then aborted the run
# with `Invalid character escape '\x'` instead of reporting a finding. The
# guard did not report a false positive; it stopped existing, and the
# contributor was told their C++ was a CMake syntax error. PR #4 hit exactly
# this on a UTF-8 BOM literal and rewrote it as three `static_cast<char>`
# bytes to get a green run
# (https://github.com/toanaz-ops/rta-tool/pull/4#issuecomment-5689190024) --
# a source file bent to fit a guard's bug.
#
# A function takes its arguments by value into its own scope and its body is
# parsed once, when the file is read, so nothing a scanned file contains is
# ever lexed. The contents are passed BY VARIABLE NAME here (`IN_VAR`, then
# `${${IN_VAR}}`) rather than by value, which keeps megabytes of source text
# off the call line as well. `core/tests/guard_fixtures/hex_escape_comment.h`
# is a scanned file carrying the escape in both shapes; it makes the trap fail
# this test rather than return silently.
#
# The sibling guards (check_coherence_gate, check_no_framework_deps,
# check_no_polynomial_form, check_no_conformance_claim,
# check_test_names_are_ascii, platform/tests/check_*) were audited the same
# day: all of them match with `if(CONTENT MATCHES ...)` on a variable NAME and
# never route file text through a macro, so none shared the defect and none
# was changed.
function(rta_strip_comments OUT_VAR IN_VAR)
    string(REGEX REPLACE "//[^\n]*" "" STRIPPED "${${IN_VAR}}")
    string(REGEX REPLACE "/\\*[^*]*\\*/" "" STRIPPED "${STRIPPED}")
    set(${OUT_VAR} "${STRIPPED}" PARENT_SCOPE)
endfunction()

set(PATTERN "(std::)?atomic[ \t]*<[^>]*(shared_ptr|Ptr)")

# Tighter than PATTERN on purpose: the sentinel must be satisfied by the
# DECLARATION and by nothing else, so it is anchored on `<Ptr>` followed by a
# member name and a semicolon -- the exact shape of AtomicSharedPtr.h's
# `std::atomic<Ptr> value_;`. Matching PATTERN here was what let prose stand
# in for code.
set(SENTINEL_PATTERN "std::atomic[ \t]*<[ \t]*Ptr[ \t]*>[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*;")

file(READ "${ALLOW}" ALLOW_CONTENT)
rta_strip_comments(ALLOW_CODE ALLOW_CONTENT)
if(NOT ALLOW_CODE MATCHES "${SENTINEL_PATTERN}")
    message(FATAL_ERROR
        "atomic-shared_ptr guard is not proving anything: ${ALLOW} no longer "
        "DECLARES the member it is the sole exception for (looked for "
        "`std::atomic<Ptr> <name>;` in code, comments excluded)")
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
        "std::atomic over a shared_ptr outside ${ALLOW} -- Apple libc++ has no "
        "C++20 atomic<shared_ptr> specialisation and will not compile it; use "
        "rta::measure::AtomicSharedPtr instead: ${OFFENDERS}")
endif()
message(STATUS "atomic-shared_ptr guard OK (${SCANNED} files scanned)")
