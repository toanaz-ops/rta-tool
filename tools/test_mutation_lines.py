# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for tools/mutation_lines.py -- the mutation-candidate line
classifier."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import mutation_lines  # noqa: E402


def test_blank_line_is_not_mutable():
    assert not mutation_lines.is_mutable_line("")
    assert not mutation_lines.is_mutable_line("    ")


def test_line_comment_is_not_mutable():
    assert not mutation_lines.is_mutable_line("// a comment explaining the formula")
    assert not mutation_lines.is_mutable_line("    // indented comment")


def test_self_contained_block_comment_is_not_mutable():
    assert not mutation_lines.is_mutable_line("/* single-line block comment */")


def test_unclosed_block_comment_opener_is_not_mutable():
    assert not mutation_lines.is_mutable_line("/* opening a block comment")


def test_brace_shape_lines_are_not_mutable():
    for shape in ["{", "}", "};", "});", ");", "),", "(", ",", ";"]:
        assert not mutation_lines.is_mutable_line(shape), shape


def test_preprocessor_lines_are_not_mutable():
    assert not mutation_lines.is_mutable_line('#include "measure/SplAlarms.h"')
    assert not mutation_lines.is_mutable_line("#pragma once")
    assert not mutation_lines.is_mutable_line("#define FOO 1")


def test_namespace_line_is_not_mutable():
    assert not mutation_lines.is_mutable_line("namespace {")
    assert not mutation_lines.is_mutable_line("namespace rta::measure {")


def test_access_specifiers_are_not_mutable():
    assert not mutation_lines.is_mutable_line("public:")
    assert not mutation_lines.is_mutable_line("private:")
    assert not mutation_lines.is_mutable_line("protected:")


def test_pure_declaration_is_not_mutable():
    assert not mutation_lines.is_mutable_line("void update(const std::vector<Block>& blocks);")
    assert not mutation_lines.is_mutable_line("int foo() const override;")
    assert not mutation_lines.is_mutable_line("virtual void bar() = 0;")


def test_bare_call_statement_is_mutable():
    # No leading return-type token before the name -- a call, not a
    # declaration -- so it IS a mutation candidate.
    assert mutation_lines.is_mutable_line("doThing(x);")
    assert mutation_lines.is_mutable_line("alarm.update(blocks, 48000.0, 1.0, 0.0, i, history);")


def test_assignment_statement_is_mutable():
    assert mutation_lines.is_mutable_line("double x = amplitudeFromDb(cfg.levelDbFsPeak);")


def test_return_statement_is_mutable():
    assert mutation_lines.is_mutable_line("return centre - 0.25 * (left - right) * p;")


def test_check_macro_line_is_mutable():
    assert mutation_lines.is_mutable_line(
        "CHECK(alarm.report().state == rta::measure::SplAlarmState::Fired);"
    )


# --- fix round 1, item L5: a leading "*" or a closed "/* */" is not always a
# comment ----------------------------------------------------------------


def test_pointer_dereference_starting_with_star_is_mutable():
    # The old regex's bare `\*.*` alternative treated ANY line starting with
    # "*" as a block-comment continuation, so a real dereference statement
    # was silently exempted from ever being mutated.
    assert mutation_lines.is_mutable_line("*out = 5;")


def test_block_comment_followed_by_real_code_is_mutable():
    # The old regex's `/\*.*` alternative (no closing-`*/` requirement)
    # swallowed a closing "*/" and everything after it, so a line with real
    # code after an inline block comment was never mutated either.
    assert mutation_lines.is_mutable_line("/* x */ foo();")


def test_block_comment_continuation_line_is_still_not_mutable_via_comment_only():
    # A middle line of a genuine multi-line block comment (no "/*" opener,
    # no "*/" closer on this line) is not caught by is_comment_only_line at
    # all any more (the unsafe bare-"*" rule was removed) -- it is simply
    # mutated like any other line, which is documented as a safe, low-cost
    # trade-off (deleting real comment prose changes nothing, so it is a
    # harmless SURVIVED at worst). Asserted explicitly so a future change
    # cannot silently reintroduce the unsafe short-cut without this test
    # noticing the behaviour it would restore.
    assert mutation_lines.is_mutable_line("* continuation of a block comment")


def test_is_comment_only_line_directly():
    assert mutation_lines.is_comment_only_line("// line comment")
    assert mutation_lines.is_comment_only_line("/* self contained */")
    assert mutation_lines.is_comment_only_line("/* unclosed opener")
    assert not mutation_lines.is_comment_only_line("/* x */ foo();")
    assert not mutation_lines.is_comment_only_line("*out = 5;")


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
