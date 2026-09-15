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
# must still contain the pattern -- if it does not, either the allow-list path
# drifted or the wrapper no longer uses the specialisation at all, and either
# way the next reader needs to be told rather than reassured.
if(NOT ALLOW IN_LIST SOURCES)
    message(FATAL_ERROR "atomic-shared_ptr guard did not see ${ALLOW} -- it is not watching")
endif()

set(PATTERN "(std::)?atomic[ \t]*<[^>]*(shared_ptr|Ptr)")

file(READ "${ALLOW}" ALLOW_CONTENT)
if(NOT ALLOW_CONTENT MATCHES "${PATTERN}")
    message(FATAL_ERROR
        "atomic-shared_ptr guard is not proving anything: ${ALLOW} no longer "
        "contains the pattern it is the sole exception for")
endif()

set(OFFENDERS "")
foreach(FILE ${SOURCES})
    if(FILE STREQUAL ALLOW)
        continue()
    endif()
    file(READ "${FILE}" CONTENT)
    if(CONTENT MATCHES "${PATTERN}")
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
