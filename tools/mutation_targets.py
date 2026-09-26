#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""mutation_targets.py -- map a mutated source path to its ctest target(s).

Split out of diffmut.py (process-tooling PR, fix round 1, item L3: diffmut.py
was 535 lines against the project's 400-line file cap) along the seam
between "which target covers this file" and "run a mutation campaign".

core/, platform/types/, the rest of platform/, and ui/ each map to exactly
one ctest target by the project's own module-boundary convention (see
CLAUDE.md "Module boundaries"). app/src/* is ambiguous by directory alone --
e.g. app/src/export/ holds both SplLogWriter.cpp (compiled into the JUCE-free
rtatool_analysis_tests) and SplReport*.cpp (exercised by the JUCE-only
rtatool_export_tests) -- so for app/src/* this module PARSES the real CMake
source lists (app/tests/cmake/*.cmake for the OFF target, app/tests_juce/
CMakeLists.txt for the three ON targets) at run time instead of guessing.
That means the mapping cannot silently drift from the CMakeLists it mirrors.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Directory-prefix rules that are NOT ambiguous: each of these layers builds
# into exactly one test target by construction. Order matters --
# platform/types/ must be checked before the more general platform/ prefix.
STATIC_PREFIX_TARGETS: list[tuple[str, str]] = [
    ("core/", "rta_core_tests"),
    ("platform/types/", "rta_platform_tests"),
    ("platform/", "rta_platform_juce_tests"),  # ON-only: needs JUCE
    ("ui/", "az_ui_tests"),  # ON-only: needs JUCE
]

# app/src/* is ambiguous by directory alone -- these are the real CMake
# source lists this module parses to resolve it. Paths are relative to
# REPO_ROOT.
APP_OFF_SOURCE_LISTS = [
    "app/tests/cmake/base_test_sources.cmake",
    "app/tests/cmake/api_test_sources.cmake",
    "app/tests/cmake/spl_test_sources.cmake",
    "app/tests/cmake/impl_test_sources.cmake",
]
APP_OFF_TARGET = "rtatool_analysis_tests"
APP_ON_CMAKELISTS = "app/tests_juce/CMakeLists.txt"
APP_ON_TARGETS = ("rtatool_view_tests", "rtatool_routing_live_tests", "rtatool_export_tests")

# Fallback ONLY for an app/src/ file this module's CMake parse did not find in
# any target's source list (e.g. a brand new file not yet wired into a test).
# Printed as a WARNING, never used silently.
APP_SRC_FALLBACK_PREFIXES: list[tuple[str, str]] = [
    ("app/src/measure/", APP_OFF_TARGET),
    ("app/src/api/", APP_OFF_TARGET),
    ("app/src/trace/", APP_OFF_TARGET),
    ("app/src/export/", APP_OFF_TARGET),
    ("app/src/view/", "rtatool_view_tests"),
    ("app/src/dev/", "rtatool_view_tests"),
]

_CMAKE_SOURCE_PATH_RE = re.compile(r"([\w./${}]*\.(?:cpp|h|hpp))\b")


def _basenames_from_cmake(text: str) -> set[str]:
    names: set[str] = set()
    for match in _CMAKE_SOURCE_PATH_RE.finditer(text):
        token = match.group(1)
        # Strip a leading CMake variable reference like
        # ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Analyser.cpp
        names.add(Path(token).name)
    return names


def build_app_src_target_map() -> dict[str, str]:
    """basename -> target, for every .cpp/.h this module found listed in the
    app/ test CMakeLists. Later entries do not overwrite earlier ones: the
    OFF list is read first, so a name appearing in both an OFF and an ON list
    (should not happen, but silence here would be a bug hiding a bug) keeps
    the OFF verdict and a subsequent duplicate is simply redundant, not
    corrective.
    """
    mapping: dict[str, str] = {}
    for rel in APP_OFF_SOURCE_LISTS:
        p = REPO_ROOT / rel
        if not p.is_file():
            continue
        for name in _basenames_from_cmake(p.read_text(encoding="utf-8")):
            mapping.setdefault(name, APP_OFF_TARGET)

    on_path = REPO_ROOT / APP_ON_CMAKELISTS
    if on_path.is_file():
        text = on_path.read_text(encoding="utf-8")
        # Split into add_executable(...) blocks by target name so a file
        # listed under rtatool_view_tests is not confused with one listed
        # under rtatool_export_tests.
        for target in APP_ON_TARGETS:
            block_match = re.search(
                r"add_executable\(\s*" + re.escape(target) + r"\b(.*?)\)", text, re.DOTALL
            )
            if not block_match:
                continue
            for name in _basenames_from_cmake(block_match.group(1)):
                mapping.setdefault(name, target)
    return mapping


def test_targets_for(path: str, app_src_map: dict[str, str]) -> list[str]:
    """Resolve a repo-relative path (forward slashes) to the ctest target(s)
    that cover it. Returns [] if nothing maps -- the caller must treat that
    mutant as unresolved, not silently drop it.
    """
    for prefix, target in STATIC_PREFIX_TARGETS:
        if path.startswith(prefix):
            return [target]
    if path.startswith("app/src/"):
        name = Path(path).name
        if name in app_src_map:
            return [app_src_map[name]]
        for prefix, target in APP_SRC_FALLBACK_PREFIXES:
            if path.startswith(prefix):
                print(
                    f"WARNING: {path} not found in any app/ test CMakeLists; "
                    f"falling back to subdirectory heuristic -> {target}",
                    file=sys.stderr,
                )
                return [target]
    return []
