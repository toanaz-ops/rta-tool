# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for tools/difflines.py -- parsing `git diff -U0` into added
lines. Shared by diffmut.py and orphan_check.py; kept in one file so both
tools' tests exercise the same parse.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import difflines  # noqa: E402


def test_parse_added_lines_basic():
    diff_text = (
        "diff --git a/core/src/Foo.cpp b/core/src/Foo.cpp\n"
        "index 1111111..2222222 100644\n"
        "--- a/core/src/Foo.cpp\n"
        "+++ b/core/src/Foo.cpp\n"
        "@@ -10,0 +11,2 @@ void Foo::bar() {\n"
        "+    int x = 1;\n"
        "+    return x;\n"
    )
    added = difflines.parse_added_lines(diff_text)
    assert len(added) == 2
    assert added[0].path == "core/src/Foo.cpp"
    assert added[0].lineno == 11
    assert added[0].text == "    int x = 1;"
    assert added[1].lineno == 12
    assert added[1].text == "    return x;"


def test_parse_added_lines_ignores_removed_lines():
    diff_text = (
        "diff --git a/core/src/Foo.cpp b/core/src/Foo.cpp\n"
        "--- a/core/src/Foo.cpp\n"
        "+++ b/core/src/Foo.cpp\n"
        "@@ -5,2 +5,1 @@\n"
        "-old line one\n"
        "-old line two\n"
        "+new line\n"
    )
    added = difflines.parse_added_lines(diff_text)
    assert len(added) == 1
    assert added[0].lineno == 5
    assert added[0].text == "new line"


def test_parse_added_lines_skips_new_files():
    diff_text = (
        "diff --git a/dev/null b/core/src/New.cpp\n"
        "--- /dev/null\n"
        "+++ b/core/src/New.cpp\n"
        "@@ -0,0 +1,2 @@\n"
        "+line one\n"
        "+line two\n"
    )
    added = difflines.parse_added_lines(diff_text)
    assert len(added) == 2
    assert added[0].path == "core/src/New.cpp"


def test_parse_added_lines_handles_two_files_in_one_diff():
    # M1's use case: orphan_check.py needs added lines from a MODIFIED
    # header that is one of several files in a real `git diff` -- the path
    # tracker must reset correctly at each new file's "+++" header.
    diff_text = (
        "diff --git a/app/src/measure/A.h b/app/src/measure/A.h\n"
        "--- a/app/src/measure/A.h\n"
        "+++ b/app/src/measure/A.h\n"
        "@@ -3,0 +4 @@\n"
        "+    void newMethodOnA();\n"
        "diff --git a/app/src/measure/B.h b/app/src/measure/B.h\n"
        "--- a/app/src/measure/B.h\n"
        "+++ b/app/src/measure/B.h\n"
        "@@ -7,0 +8 @@\n"
        "+    void newMethodOnB();\n"
    )
    added = difflines.parse_added_lines(diff_text)
    assert [a.path for a in added] == ["app/src/measure/A.h", "app/src/measure/B.h"]
    assert added[0].text == "    void newMethodOnA();"
    assert added[1].text == "    void newMethodOnB();"


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
