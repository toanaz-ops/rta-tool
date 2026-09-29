# SPDX-License-Identifier: AGPL-3.0-or-later
"""Direct unit tests for tools/orphan_targets.py's pure `_reason` functions
-- no scratch git repo or fixture .map needed, unlike test_orphan_targets_
check.py's full orphan_check.main() runs (kept apart to leave that file's
line count under this repo's 400-line cap).

Lane-end LOW D4 (docs/HUMAN-QA-QUEUE.md "Lane-end LOW triage -- 2026-09-27"):
`not_in_any_target_reason`'s old message "not compiled into any target" is
LITERALLY FALSE for a file this tool never asked about -- `AlignmentWizard.cpp`
and `EqVerify.cpp` both compile cleanly into the `rtatool_analysis_tests` TEST
target (confirmed in a real `build-off` log) while being absent from
`rtatool_sources.cmake`/`rtatool_snapshot_sources.cmake`, the only two lists
this function reads. The fix is wording only: this function checks exactly
two targets (`rtatool`, `rtatool_snapshot`) and must say so, never claim
ignorance about a third (any app/tests* target) it was never asked to read.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import orphan_targets as ot  # noqa: E402


def test_not_in_any_target_reason_names_the_two_targets_it_actually_checked():
    # The real repo shape (AlignmentWizard.cpp/EqVerify.cpp): a production
    # .cpp absent from BOTH rtatool_sources.cmake and
    # rtatool_snapshot_sources.cmake, but this function is never told about
    # -- and never claims to know about -- app/tests*/CMakeLists.txt's own
    # source list, so the message must not assert "any target" when a TEST
    # target could easily be the true, unchecked answer.
    reason = ot.not_in_any_target_reason("app/src/measure/AlignmentWizard.cpp", set(), set())
    assert reason == "not compiled into rtatool or rtatool_snapshot"
    assert reason != "not compiled into any target"


def test_not_in_any_target_reason_snapshot_only_wording_is_unchanged():
    # The OTHER branch of this function (compiled into rtatool_snapshot only)
    # already names the exact target it found the file in -- D4 does not
    # touch this branch, pinned here so a future edit cannot merge the two
    # messages back into one vague sentence.
    reason = ot.not_in_any_target_reason(
        "app/src/measure/Preview.cpp", set(), {"app/src/measure/preview.cpp"}
    )
    assert reason == "compiled only into rtatool_snapshot, never rtatool"


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
