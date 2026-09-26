# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for tools/diffmut.py -- the line classifier and the
restore-byte-identity logic, both runnable with no CMake build at all.

Run: python -m pytest tools/test_diffmut.py -v
(or: python -m unittest tools.test_diffmut -v)
"""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import diffmut  # noqa: E402


# --- is_mutable_line ---------------------------------------------------------


def test_blank_line_is_not_mutable():
    assert not diffmut.is_mutable_line("")
    assert not diffmut.is_mutable_line("    ")


def test_line_comment_is_not_mutable():
    assert not diffmut.is_mutable_line("// a comment explaining the formula")
    assert not diffmut.is_mutable_line("    // indented comment")


def test_block_comment_line_is_not_mutable():
    assert not diffmut.is_mutable_line("/* single-line block comment */")
    assert not diffmut.is_mutable_line("/* opening a block comment")
    assert not diffmut.is_mutable_line("* continuation of a block comment")


def test_brace_shape_lines_are_not_mutable():
    for shape in ["{", "}", "};", "});", ");", "),", "(", ",", ";"]:
        assert not diffmut.is_mutable_line(shape), shape


def test_preprocessor_lines_are_not_mutable():
    assert not diffmut.is_mutable_line('#include "measure/SplAlarms.h"')
    assert not diffmut.is_mutable_line("#pragma once")
    assert not diffmut.is_mutable_line("#define FOO 1")


def test_namespace_line_is_not_mutable():
    assert not diffmut.is_mutable_line("namespace {")
    assert not diffmut.is_mutable_line("namespace rta::measure {")


def test_access_specifiers_are_not_mutable():
    assert not diffmut.is_mutable_line("public:")
    assert not diffmut.is_mutable_line("private:")
    assert not diffmut.is_mutable_line("protected:")


def test_pure_declaration_is_not_mutable():
    assert not diffmut.is_mutable_line("void update(const std::vector<Block>& blocks);")
    assert not diffmut.is_mutable_line("int foo() const override;")
    assert not diffmut.is_mutable_line("virtual void bar() = 0;")


def test_bare_call_statement_is_mutable():
    # No leading return-type token before the name -- a call, not a
    # declaration -- so it IS a mutation candidate. See the module
    # docstring's "PURE DECLARATIONS" section for why this distinction
    # matters: a call is a statement with an effect.
    assert diffmut.is_mutable_line("doThing(x);")
    assert diffmut.is_mutable_line("alarm.update(blocks, 48000.0, 1.0, 0.0, i, history);")


def test_assignment_statement_is_mutable():
    assert diffmut.is_mutable_line("double x = amplitudeFromDb(cfg.levelDbFsPeak);")


def test_return_statement_is_mutable():
    assert diffmut.is_mutable_line("return centre - 0.25 * (left - right) * p;")


def test_check_macro_line_is_mutable():
    assert diffmut.is_mutable_line("CHECK(alarm.report().state == rta::measure::SplAlarmState::Fired);")


# --- parse_added_lines --------------------------------------------------------


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
    added = diffmut.parse_added_lines(diff_text)
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
    added = diffmut.parse_added_lines(diff_text)
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
    added = diffmut.parse_added_lines(diff_text)
    assert len(added) == 2
    assert added[0].path == "core/src/New.cpp"


# --- restore-byte-identity, on a temp file, no build involved ---------------


def test_run_mutant_restores_original_bytes_exactly(tmp_path):
    target_file = tmp_path / "Sample.cpp"
    original = (
        b"// SPDX-License-Identifier: AGPL-3.0-or-later\n"
        b"int add(int a, int b) {\n"
        b"    int result = a + b;\n"
        b"    return result;\n"
        b"}\n"
    )
    target_file.write_bytes(original)
    original_sha = hashlib.sha256(original).hexdigest()

    # No real build system for this fake file: force a build target that
    # cannot resolve to a real cmake target, and expect run_mutant to still
    # restore the file before propagating that outcome (a missing exe is
    # reported as NO-BUILD, not an exception, and never skips the restore).
    build_dir = tmp_path / "build-does-not-exist"
    build_dir.mkdir()
    log_path = tmp_path / "mutant.log"

    result = diffmut.run_mutant(
        target_file,
        lineno=3,  # "int result = a + b;"
        targets=["nonexistent_target"],
        build_dir=build_dir,
        config="Release",
        log_path=log_path,
    )

    # The mutant attempted a build against a target that will never link
    # (cmake itself will fail fast in this fake tree), which is NO-BUILD --
    # but the file on disk must be back to its ORIGINAL bytes regardless.
    assert result.verdict == "NO-BUILD"
    restored = target_file.read_bytes()
    assert restored == original
    assert hashlib.sha256(restored).hexdigest() == original_sha


def test_run_mutant_restores_bytes_even_when_line_out_of_range(tmp_path):
    target_file = tmp_path / "Sample.h"
    original = b"#pragma once\nint x = 1;\n"
    target_file.write_bytes(original)

    build_dir = tmp_path / "build"
    build_dir.mkdir()
    result = diffmut.run_mutant(
        target_file,
        lineno=999,
        targets=["whatever"],
        build_dir=build_dir,
        config="Release",
        log_path=tmp_path / "mutant.log",
    )
    assert result.verdict == "UNRESOLVED"
    assert target_file.read_bytes() == original


# --- test_targets_for ---------------------------------------------------------


def test_core_path_maps_to_core_tests():
    assert diffmut.test_targets_for("core/src/dsp/RealFft.cpp", {}) == ["rta_core_tests"]


def test_platform_types_maps_before_platform():
    assert diffmut.test_targets_for("platform/types/src/ChannelConfig.cpp", {}) == [
        "rta_platform_tests"
    ]
    assert diffmut.test_targets_for("platform/src/AudioIo.cpp", {}) == ["rta_platform_juce_tests"]


def test_ui_path_maps_to_az_ui_tests():
    assert diffmut.test_targets_for("ui/src/GridPanel.cpp", {}) == ["az_ui_tests"]


def test_app_src_uses_parsed_map_first():
    app_map = {"SplAlarms.cpp": "rtatool_analysis_tests"}
    assert diffmut.test_targets_for("app/src/measure/SplAlarms.cpp", app_map) == [
        "rtatool_analysis_tests"
    ]


def test_app_src_falls_back_when_unmapped(capsys):
    result = diffmut.test_targets_for("app/src/measure/BrandNewFile.cpp", {})
    assert result == ["rtatool_analysis_tests"]
    captured = capsys.readouterr()
    assert "WARNING" in captured.err


def test_unknown_path_returns_empty():
    assert diffmut.test_targets_for("docs/dsp/notes.md", {}) == []


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
