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


def test_strip_string_and_char_literals_removes_only_the_interior():
    # fix round 2 (verifier), F-I: a bare mention of a name inside a string
    # literal must not read as a real code reference once stripped.
    text = 'log("about to call enableSplLoggingForTest");\nchar c = \'x\';\n'
    result = cpp_text.strip_string_and_char_literals(text)
    assert "enableSplLoggingForTest" not in result
    assert 'log("")' in result
    assert "char c = '';" in result


def test_strip_string_and_char_literals_respects_an_escaped_quote():
    text = 'const char* s = "a \\"quoted\\" word"; int after;\n'
    result = cpp_text.strip_string_and_char_literals(text)
    assert "quoted" not in result
    assert "int after;" in result


def test_digit_separator_does_not_open_a_char_literal():
    # fix round 3 (verifier), MEDIUM-2. The real fixture:
    # app/tests/test_spl_session_folder_name.cpp:19's
    # `constexpr std::time_t kInstant = 1'700'000'000;` -- THREE `'`
    # (a digit separator, never a char literal), the odd count that used to
    # leave the scan "inside a literal" for the rest of the file and erase a
    # real ForTest call site further down.
    text = (
        "constexpr std::time_t kInstant = 1'700'000'000;\n"
        "void run() { thing.enableSplLoggingForTest(); }\n"
    )
    result = cpp_text.strip_string_and_char_literals(text)
    assert "1'700'000'000" in result
    assert "enableSplLoggingForTest" in result


def test_odd_digit_separator_count_does_not_erase_the_rest_of_the_file():
    # A single separator (`48'000`) is ALSO an odd count -- the bug did not
    # need three quotes, one was already enough to desynchronise the scan.
    text = "int rate = 48'000;\nvoid run() { thing.enableSplLoggingForTest(); }\n"
    result = cpp_text.strip_string_and_char_literals(text)
    assert "enableSplLoggingForTest" in result


def test_raw_string_with_an_odd_number_of_quotes_is_skipped_as_one_unit():
    # fix round 3 (verifier), MEDIUM-2 (raw strings). The real shape:
    # app/src/export/SplReportScript.h's `R"JS(...)JS"` -- embedded content
    # with an ODD number of `"` would otherwise close the naive string scan
    # early and desynchronise everything after it, hiding a real ForTest
    # call further down the file.
    text = (
        'constexpr auto kBlob = R"JS(var x = "one quote here)JS";\n'
        "void run() { thing.enableSplLoggingForTest(); }\n"
    )
    result = cpp_text.strip_string_and_char_literals(text)
    assert "one quote here" not in result
    assert "enableSplLoggingForTest" in result


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
