# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for tools/cpp_scopes.py -- the namespace/class chain enclosing a
given line, used to build an MSVC decorated-name fragment."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cpp_scopes  # noqa: E402


def test_separately_nested_namespaces():
    text = (
        "namespace rta {\n"
        "namespace measure {\n"
        "class AnalysisThread {\n"
        "public:\n"
        "    void enableSplLogging(int x);\n"
        "};\n"
        "}\n"
        "}\n"
    )
    # line 5 is "    void enableSplLogging(int x);"
    assert cpp_scopes.enclosing_scope_at_line(text, 5) == [
        "AnalysisThread",
        "measure",
        "rta",
    ]


def test_single_line_nested_namespace_syntax():
    text = "namespace rta::measure {\nvoid sessionFolderName();\n}\n"
    assert cpp_scopes.enclosing_scope_at_line(text, 2) == ["measure", "rta"]


def test_global_scope_has_an_empty_chain():
    text = "void main();\n"
    assert cpp_scopes.enclosing_scope_at_line(text, 1) == []


def test_class_with_no_enclosing_namespace():
    text = "class MainComponent {\npublic:\n    void calibrationStartClicked();\n};\n"
    assert cpp_scopes.enclosing_scope_at_line(text, 3) == ["MainComponent"]


def test_anonymous_namespace_is_flagged():
    text = "namespace {\nvoid helper();\n}\n"
    assert cpp_scopes.enclosing_scope_at_line(text, 2) == [cpp_scopes.ANONYMOUS]


def test_function_body_does_not_pollute_the_chain():
    # A candidate line sitting INSIDE some unrelated function's body (an if,
    # a for, a lambda) must not pick up that block as a bogus enclosing
    # "class" or "namespace" -- only actual namespace/class scopes count.
    text = (
        "namespace rta {\n"
        "void outer() {\n"
        "    if (x) {\n"
        "        int y = 0;\n"
        "    }\n"
        "}\n"
        "}\n"
    )
    assert cpp_scopes.enclosing_scope_at_line(text, 4) == ["rta"]


def test_brace_inside_a_comment_does_not_affect_depth():
    text = "namespace rta {\n// a comment with a { brace in it\nvoid foo();\n}\n"
    assert cpp_scopes.enclosing_scope_at_line(text, 3) == ["rta"]


def test_brace_inside_a_string_literal_does_not_affect_depth():
    text = 'namespace rta {\nconst char* s = "{";\nvoid foo();\n}\n'
    assert cpp_scopes.enclosing_scope_at_line(text, 3) == ["rta"]


def test_base_class_list_is_not_mistaken_for_the_type_being_defined():
    text = "namespace rta {\nclass Derived : public Base {\n    void method();\n};\n}\n"
    assert cpp_scopes.enclosing_scope_at_line(text, 3) == ["Derived", "rta"]


def test_scopes_by_line_covers_every_line_in_one_pass():
    text = "namespace rta {\nvoid a();\n}\nvoid b();\n"
    scopes = cpp_scopes.scopes_by_line(text)
    assert scopes[2] == ["rta"]
    assert scopes[4] == []


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
