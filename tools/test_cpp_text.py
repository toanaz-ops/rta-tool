# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for tools/cpp_text.py -- comment stripping, respecting string
and char literals (fix round 1, item M2)."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cpp_text  # noqa: E402


def test_strips_line_comment():
    assert cpp_text.strip_comments("int x; // trailing note\n") == "int x; \n"


def test_strips_whole_line_comment_leaving_blank_line():
    result = cpp_text.strip_comments("// SplAlarms is the one producer\nint x;\n")
    assert "SplAlarms" not in result
    assert "int x;" in result


def test_strips_self_contained_block_comment():
    assert cpp_text.strip_comments("int x; /* note */ int y;\n") == "int x;  int y;\n"


def test_strips_multiline_block_comment_preserving_newline_count():
    text = "int a;\n/* start\nmiddle\nend */\nint b;\n"
    result = cpp_text.strip_comments(text)
    assert "start" not in result and "middle" not in result and "end" not in result
    assert result.count("\n") == text.count("\n")
    assert "int a;" in result and "int b;" in result


def test_does_not_strip_double_slash_inside_string_literal():
    # A URL in a string must survive -- this is exactly the case the cmake
    # guard's own simpler stripper (check_no_std_atomic_shared_ptr.cmake)
    # documents as accepted collateral; M2 requires this tool do better.
    text = 'const char* url = "https://example.com/path";\n'
    result = cpp_text.strip_comments(text)
    assert result == text


def test_does_not_strip_block_comment_markers_inside_string_literal():
    text = 'const char* s = "/* not a comment */";\n'
    result = cpp_text.strip_comments(text)
    assert result == text


def test_handles_escaped_quote_inside_string_literal():
    text = 'const char* s = "a \\" // still inside the string";\nint after;\n'
    result = cpp_text.strip_comments(text)
    # The line-comment marker is INSIDE the string (after an escaped quote),
    # so it must not truncate the string or eat the next line.
    assert "int after;" in result


def test_char_literal_is_respected():
    text = "char c = '/'; int x;\n"
    result = cpp_text.strip_comments(text)
    assert "int x;" in result
    assert "char c = '/';" in result


def test_doc_comment_naming_a_symbol_becomes_invisible_to_a_reference_search():
    # This is the exact Snapshot.h:230 regression M2 fixes: a doc comment
    # that merely NAMES a component must not read as a "reference" to it
    # once stripped and searched.
    text = (
        "/// list). Lane L6a task W2-B's `SplAlarms` is the one producer of\n"
        "/// this snapshot.\n"
        "struct Unrelated {};\n"
    )
    result = cpp_text.strip_comments(text)
    assert "SplAlarms" not in result


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
