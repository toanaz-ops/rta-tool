# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for tools/msvc_decorate.py -- each case checked against the real
fragment MSVC's own linker produced for the same symbol (see that module's
docstring and the orphan_check.py v2 PR body for the source greps)."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import msvc_decorate  # noqa: E402


def test_free_function_in_nested_namespace():
    assert (
        msvc_decorate.decorated_fragment("sessionFolderName", ["measure", "rta"])
        == "?sessionFolderName@measure@rta@@"
    )


def test_free_function_at_global_scope():
    assert msvc_decorate.decorated_fragment("main", []) == "?main@@"


def test_member_function():
    assert (
        msvc_decorate.decorated_fragment("enableSplLogging", ["AnalysisThread", "measure", "rta"])
        == "?enableSplLogging@AnalysisThread@measure@rta@@"
    )


def test_member_function_with_no_enclosing_namespace():
    assert (
        msvc_decorate.decorated_fragment("calibrationStartClicked", ["MainComponent"])
        == "?calibrationStartClicked@MainComponent@@"
    )


def test_constructor():
    assert (
        msvc_decorate.decorated_fragment(
            "AnalysisThread", ["AnalysisThread", "measure", "rta"], is_constructor=True
        )
        == "??0AnalysisThread@measure@rta@@"
    )


def test_static_member_function():
    # F1's exact false-positive class in v1 (`Foo::make()`-style qualified
    # static calls discarded by the old regex heuristic): the fragment itself
    # does not distinguish static from instance methods -- that lives in the
    # signature encoding this module deliberately never reconstructs -- but
    # it must still resolve to the right scope chain.
    assert (
        msvc_decorate.decorated_fragment("computeSomething", ["Helpers", "measure", "rta"])
        == "?computeSomething@Helpers@measure@rta@@"
    )


def test_constructor_requires_a_class_in_scope():
    with pytest.raises(ValueError):
        msvc_decorate.decorated_fragment("Foo", [], is_constructor=True)


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
