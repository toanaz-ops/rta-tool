# SPDX-License-Identifier: AGPL-3.0-or-later
#
# POLICY FLOOR -- see check_coherence_gate.cmake's own note. `cmake -P` does
# not inherit the root project's cmake_minimum_required, and a script with
# every policy unset behaves differently on different runners.
cmake_minimum_required(VERSION 3.22)
#
# source_files_are_under_400_lines -- CLAUDE.md "File length": hard cap 400
# lines, aim 300, "applies to headers too". Enforced here as a real ctest
# because a norm nobody's build breaks on is a norm that gets broken quietly:
# the process-tooling PR that added this guard also found (and split) three
# files that had already crossed 400 lines with nothing red anywhere.
#
# WHAT IS SCANNED. Every *.h, *.hpp, *.cpp under DIRS (recursively), plus
# every CMakeLists.txt and *.cmake under DIRS -- the task spec calls those out
# by name because a 600-line CMakeLists.txt is exactly as unreadable as a
# 600-line .cpp, and nothing else was watching it. TOOLS_DIR (optional) is
# scanned for *.cpp ONLY: tools/ is mostly *.py generator/probe scripts this
# guard does not govern (they are not C++ and were never in scope here).
#
# EXTRA_FILES (optional): semicolon-separated exact file paths scanned in
# addition to DIRS/TOOLS_DIR -- for a file that lives outside every scanned
# directory, such as the root CMakeLists.txt (a sibling of core/app/platform/
# ui, not inside any of them, and otherwise invisible to this guard: fix
# round 1, item L4). Each path MUST exist or this script FATAL_ERRORs.
#
# EXCLUDES (optional): semicolon-separated file(GLOB_RECURSE) patterns exempt
# from the cap -- for external/, vendored third-party sources, or a file whose
# split is judged not worth the risk (with a dated expiry comment at the
# exclusion site in the REGISTERING CMakeLists, per CLAUDE.md's verification
# standard). Each pattern MUST match at least one file in the scanned set or
# this script FATAL_ERRORs: an exclusion that matches nothing is not
# exempting anything, it is a dead line pretending to (memory
# a-naming-grep-that-bans-a-word-bans-its-own-justification.md -- the same
# "prove the sentinel" shape as check_no_std_atomic_shared_ptr.cmake's ALLOW).
# As of this guard's introduction the tree needs NO exclusions at all -- every
# file that was over the cap got split instead -- so do not add a
# placeholder EXCLUDES "just in case"; add one only when a real file needs it.
#
# HOW LINES ARE COUNTED. Not file(STRINGS): CMake's STRINGS command treats
# `;` as a list separator when it builds the result, and C++ source is mostly
# semicolons -- a single statement-per-line file would be split into several
# list entries per line and wildly overcount. Counting `\n` bytes in the raw
# file(READ) content sidesteps that: the content is a STRING argument to
# string(REGEX MATCHALL), never turned into a list, so embedded semicolons
# are inert. A file with a trailing newline (the normal case) has exactly as
# many `\n` bytes as it has lines; a file missing the final newline is
# corrected by the one-line patch below.
if(NOT DEFINED DIRS)
    message(FATAL_ERROR "file-length guard needs -DDIRS=<dir>[;<dir>...]")
endif()
if(NOT DEFINED MAX_LINES)
    set(MAX_LINES 400)
endif()

set(SOURCES "")
foreach(DIR ${DIRS})
    if(NOT IS_DIRECTORY "${DIR}")
        message(FATAL_ERROR "file-length guard: ${DIR} is not a directory")
    endif()
    file(GLOB_RECURSE FOUND
        "${DIR}/*.h" "${DIR}/*.hpp" "${DIR}/*.cpp"
        "${DIR}/*.cmake" "${DIR}/CMakeLists.txt")
    list(APPEND SOURCES ${FOUND})
endforeach()

if(DEFINED TOOLS_DIR)
    if(NOT IS_DIRECTORY "${TOOLS_DIR}")
        message(FATAL_ERROR "file-length guard: TOOLS_DIR=${TOOLS_DIR} is not a directory")
    endif()
    file(GLOB_RECURSE TOOLS_FOUND "${TOOLS_DIR}/*.cpp")
    list(APPEND SOURCES ${TOOLS_FOUND})
endif()

if(DEFINED EXTRA_FILES)
    foreach(EXTRA ${EXTRA_FILES})
        if(NOT EXISTS "${EXTRA}")
            message(FATAL_ERROR "file-length guard: EXTRA_FILES entry '${EXTRA}' does not exist")
        endif()
        list(APPEND SOURCES "${EXTRA}")
    endforeach()
endif()

if(SOURCES)
    list(REMOVE_DUPLICATES SOURCES)
endif()

# Exclusions, each proving it exempts a real file before it exempts anything.
if(DEFINED EXCLUDES)
    foreach(PATTERN ${EXCLUDES})
        file(GLOB_RECURSE MATCHED ${PATTERN})
        list(LENGTH MATCHED n_matched)
        if(n_matched EQUAL 0)
            message(FATAL_ERROR
                "file-length guard: exclusion '${PATTERN}' matched no file -- "
                "remove it or fix the pattern; an exclusion that watches "
                "nothing is not proving anything")
        endif()
        foreach(M ${MATCHED})
            list(REMOVE_ITEM SOURCES "${M}")
        endforeach()
    endforeach()
endif()

list(LENGTH SOURCES n_sources)
if(n_sources EQUAL 0)
    message(FATAL_ERROR "file-length guard scanned nothing -- DIRS=${DIRS} is wrong")
endif()

set(OFFENDERS "")
foreach(FILE ${SOURCES})
    file(READ "${FILE}" CONTENT)
    string(REGEX MATCHALL "\n" NEWLINES "${CONTENT}")
    list(LENGTH NEWLINES n_lines)
    string(LENGTH "${CONTENT}" content_len)
    if(content_len GREATER 0)
        math(EXPR last_index "${content_len} - 1")
        string(SUBSTRING "${CONTENT}" ${last_index} 1 LAST_CHAR)
        if(NOT LAST_CHAR STREQUAL "\n")
            math(EXPR n_lines "${n_lines} + 1")
        endif()
    endif()
    if(n_lines GREATER MAX_LINES)
        list(APPEND OFFENDERS "${FILE} (${n_lines} lines)")
    endif()
endforeach()

if(OFFENDERS)
    list(LENGTH OFFENDERS n_offenders)
    string(REPLACE ";" "\n  " OFFENDERS_MSG "${OFFENDERS}")
    message(FATAL_ERROR
        "file-length guard: ${n_offenders} file(s) exceed the ${MAX_LINES}-line "
        "cap (CLAUDE.md \"File length\"):\n  ${OFFENDERS_MSG}\n"
        "Split along a real seam, or add a dated, self-proving exclusion "
        "(see this script's own header comment).")
endif()

message(STATUS "file-length guard OK (${n_sources} files scanned, cap ${MAX_LINES})")
