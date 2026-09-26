# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for tools/orphan_symbols.py -- the symbol-declaration heuristic
and the self-definition exclusion (fix round 1, item L1)."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import orphan_symbols as osym  # noqa: E402


# --- extract_symbols ----------------------------------------------------


def test_extracts_struct_name():
    assert osym.extract_symbols("struct SplHistory {\n    int x;\n};\n") == {"SplHistory"}


def test_extracts_class_name():
    assert osym.extract_symbols("class SplAlarms {\npublic:\n};\n") == {"SplAlarms"}


def test_extracts_free_function_declaration():
    text = "std::optional<double> slidingMaxLeqDb(std::span<const Block> b, double fs);\n"
    assert "slidingMaxLeqDb" in osym.extract_symbols(text)


def test_extracts_out_of_line_member_definition_qualified():
    text = "SplAlarmReading SplAlarm::report() const {\n    return out;\n}\n"
    assert "SplAlarm::report" in osym.extract_symbols(text)


def test_ignores_control_flow_lines():
    text = "if (windowFull) {\nfor (int i = 0; i < 3; ++i) {\nwhile (x) {\n"
    assert osym.extract_symbols(text) == set()


def test_ignores_return_statement_calling_a_qualified_function():
    text = (
        "inline std::string formatUtcTimestamp(const std::tm& tm) {\n"
        "    std::ostringstream oss;\n"
        "    return std::put_time(&tm, \"%Y%m%dT%H%M%SZ\");\n"
        "}\n"
    )
    symbols = osym.extract_symbols(text)
    assert "std::put_time" not in symbols
    assert "formatUtcTimestamp" in symbols


def test_multiple_symbols_in_one_header():
    text = "struct SplHistory {\n    int size() const;\n};\n\nvoid writeSplLog(const SplHistory& h);\n"
    symbols = osym.extract_symbols(text)
    assert "SplHistory" in symbols
    assert "writeSplLog" in symbols


# --- is_self_definition_of (fix round 1, item L1) --------------------------


def test_class_is_the_qualifier():
    # SplLogWriter is declared in SplLog.h and defined in SplLogWriter.cpp --
    # its own out-of-line methods must not read as an external caller.
    assert osym.is_self_definition_of("SplLogWriter::write(const Block& b) {", "SplLogWriter")
    assert osym.is_self_definition_of("void SplLogWriter::reconfigure(int x) {", "SplLogWriter")


def test_multiline_constructor_is_recognised_from_its_opening_line_alone():
    # The real bug this was written against: a multi-parameter constructor's
    # signature wraps onto later lines, so a check anchored to a full
    # single-line declaration shape (_FUNC_RE) misses it entirely.
    line = "SplLogWriter::SplLogWriter(std::string basePath, rta::measure::SplConfig config,"
    assert osym.is_self_definition_of(line, "SplLogWriter")


def test_method_name_is_the_qualified_suffix():
    # enableSplLogging is extracted as a bare member name (extract_symbols
    # has no notion of which class a member belongs to); its own definition
    # is `AnalysisThread::enableSplLogging(...)`, where the qualifier is the
    # OTHER identifier, not the symbol itself.
    line = "void AnalysisThread::enableSplLogging(const SplConfig& config, std::span<const int> ch) {"
    assert osym.is_self_definition_of(line, "enableSplLogging")


def test_unrelated_qualified_call_is_not_excluded():
    # A genuinely different symbol's definition/call must not be excluded
    # when checking an unrelated name.
    line = "void Other::helper() {"
    assert not osym.is_self_definition_of(line, "enableSplLogging")
    assert not osym.is_self_definition_of(line, "SplLogWriter")


def test_bare_unqualified_call_is_not_a_self_definition():
    # A plain call/reference with no "::" at all is real evidence of use and
    # must not be excluded by this check (it is not shaped like a
    # definition at all).
    assert not osym.is_self_definition_of("thread.enableSplLogging(config, channels);", "enableSplLogging")
    assert not osym.is_self_definition_of("auto w = SplLogWriter(path, cfg, info, 0);", "SplLogWriter")


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
