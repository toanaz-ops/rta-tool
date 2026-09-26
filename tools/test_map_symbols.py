# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for tools/map_symbols.py -- MSVC .map parsing and fragment
liveness, against a checked-in fixture (tools/test_fixtures/sample.map).

The fixture's decorated names were checked, before being hand-written, against
the ACTUAL fragments a real /OPT:REF /MAP build of rtatool produces for the
same symbols (`?enableSplLogging@AnalysisThread@measure@rta@@...`,
`??0AnalysisThread@measure@rta@@...`, `?sessionFolderName@measure@rta@@...`
all matched exactly) -- see the orphan_check.py v2 PR body for the real map
greps this fixture is modelled on.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import map_symbols  # noqa: E402

_FIXTURE = Path(__file__).resolve().parent / "test_fixtures" / "sample.map"


def _fixture_names() -> set[str]:
    return map_symbols.extract_decorated_names(_FIXTURE.read_text(encoding="utf-8"))


def test_extracts_a_constructor():
    names = _fixture_names()
    assert any(n.startswith("??0AnalysisThread@measure@rta@@") for n in names)


def test_extracts_a_member_function():
    names = _fixture_names()
    assert any(n.startswith("?enableSplLogging@AnalysisThread@measure@rta@@") for n in names)


def test_extracts_a_free_function_in_a_namespace():
    names = _fixture_names()
    assert any(n.startswith("?sessionFolderName@measure@rta@@") for n in names)


def test_extracts_a_qualified_static_member_call_target():
    # F1's exact false-positive class in v1: a static member, called as
    # `Helpers::computeSomething()`, must resolve via the LINKER (present in
    # the map) rather than a text-grep heuristic that discarded qualified
    # calls.
    names = _fixture_names()
    assert any(n.startswith("?computeSomething@Helpers@measure@rta@@") for n in names)


def test_extracts_from_the_static_symbols_table_too():
    names = _fixture_names()
    assert any(n.startswith("?writerLoop@SplLogWriter@export@rta@@") for n in names)


def test_ignores_the_segment_length_table():
    # The "Start Length Name Class" table's second column is a hex LENGTH
    # ending in `H` (e.g. `0002ac30H`), never a symbol -- must not be read as
    # one, and `.text$mn` / `CODE` must never appear as "symbol names".
    names = _fixture_names()
    assert not any(".text" in n or n == "CODE" or n.endswith("H") for n in names)


def test_a_mutual_reference_cycle_of_dead_code_is_simply_absent():
    # F3's exact bug in v1: two unwired components that reference only each
    # other (CycleA::pingB <-> CycleB::pingA) are never emitted to the map at
    # all under /OPT:REF, because reachability is decided from the app's
    # entry point, not by counting in-degree. The fixture deliberately does
    # NOT include either symbol -- this test is the acceptance proof that
    # "absent from the map" is exactly the right sentinel for that case.
    names = _fixture_names()
    assert not any("pingB@CycleA@" in n or "pingA@CycleB@" in n for n in names)
    assert not map_symbols.is_fragment_live("?pingB@CycleA@measure@rta@@", names)
    assert not map_symbols.is_fragment_live("?pingA@CycleB@measure@rta@@", names)


def test_is_fragment_live_true_for_a_real_prefix():
    names = _fixture_names()
    assert map_symbols.is_fragment_live("?enableSplLogging@AnalysisThread@measure@rta@@", names)


def test_is_fragment_live_false_when_only_a_different_scope_matches():
    # The fragment must match as a full scope-chain prefix, not a bare
    # substring of the name -- a symbol with the same short name in a
    # DIFFERENT class must not count.
    names = _fixture_names()
    assert not map_symbols.is_fragment_live("?enableSplLogging@OtherClass@measure@rta@@", names)


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
