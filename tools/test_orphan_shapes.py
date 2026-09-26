# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for the UNCHECKABLE classification `orphan_candidates.py`'s
`find_candidates` applies via `orphan_shapes.py`: deleted/defaulted members,
constexpr/consteval, anonymous namespaces, templates (function and class-
member), operators, friend functions, pure virtuals, and pure-data
aggregates. Core candidate detection (single-line, multi-line, out-of-line,
local-variable/access-specifier exclusions) is tested in
test_orphan_candidates.py -- split the same way the production code is, to
stay under this repo's 400-line cap.

Every case here is a real failure mode found running this tool against the
live repo (fix round 1, verifier, items F1-F4) -- see each test's own
docstring/comment for the specific wrong or silently-dropped result it
replaced.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from difflines import AddedLine  # noqa: E402
from orphan_candidates import find_candidates  # noqa: E402


def _reader(files: dict[str, str]):
    return lambda path: files[path]


def test_deleted_copy_constructor_is_uncheckable():
    text = "class Foo {\npublic:\n    Foo(const Foo&) = delete;\n};\n"
    added = [AddedLine("f.h", 3, "    Foo(const Foo&) = delete;")]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert candidates == []
    assert len(uncheckable) == 1
    assert "delete" in uncheckable[0].reason


def test_defaulted_member_is_uncheckable():
    text = "class Foo {\npublic:\n    Foo() = default;\n};\n"
    added = [AddedLine("f.h", 3, "    Foo() = default;")]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert candidates == []
    assert len(uncheckable) == 1


def test_constexpr_function_is_uncheckable():
    text = "namespace rta {\nconstexpr int square(int x) { return x * x; }\n}\n"
    added = [AddedLine("f.h", 2, "constexpr int square(int x) { return x * x; }")]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert candidates == []
    assert len(uncheckable) == 1
    assert "constexpr" in uncheckable[0].reason


def test_anonymous_namespace_member_is_uncheckable():
    text = "namespace {\nvoid helper() {}\n}\n"
    added = [AddedLine("f.cpp", 2, "void helper() {}")]
    candidates, uncheckable = find_candidates(added, _reader({"f.cpp": text}))
    assert candidates == []
    assert len(uncheckable) == 1
    assert "anonymous" in uncheckable[0].reason


def test_template_function_is_uncheckable():
    text = "namespace rta {\ntemplate <typename T>\nT identity(T x) { return x; }\n}\n"
    added = [AddedLine("f.h", 3, "T identity(T x) { return x; }")]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert candidates == []
    assert len(uncheckable) == 1
    assert "template" in uncheckable[0].reason


# --- fix round 1 (verifier), F1: leading [[attribute]] groups ---------------


def test_attributed_member_is_a_candidate():
    # The real bug: MainComponent.h's `[[nodiscard]] rta::view::PaneView
    # currentPaneView() const noexcept { ... }` has zero callers of any kind
    # and was silently dropped -- neither a candidate nor UNCHECKABLE -- the
    # first time this ran against the real repo, because the flattened head
    # starts with `[`, which matches neither TYPED_DECL_RE nor
    # BARE_NAME_RE/BARE_QUALIFIED_RE (both anchor on [A-Za-z_] at position 0).
    text = (
        "namespace rta {\nclass MainComponent {\npublic:\n"
        "    [[nodiscard]] rta::view::PaneView currentPaneView() const noexcept "
        "{ return currentPaneView_; }\n};\n}\n"
    )
    added = [
        AddedLine(
            "MainComponent.h",
            4,
            "    [[nodiscard]] rta::view::PaneView currentPaneView() const noexcept "
            "{ return currentPaneView_; }",
        )
    ]
    candidates, uncheckable = find_candidates(added, _reader({"MainComponent.h": text}))
    assert uncheckable == []
    assert len(candidates) == 1
    assert candidates[0].name == "currentPaneView"
    assert candidates[0].enclosing == ["MainComponent", "rta"]


def test_attributed_static_member_is_a_candidate():
    text = (
        "namespace rta {\nclass Foo {\npublic:\n"
        "    [[nodiscard]] static constexpr bool usesFallback() noexcept { return true; }\n"
        "};\n}\n"
    )
    added = [
        AddedLine(
            "f.h", 4,
            "    [[nodiscard]] static constexpr bool usesFallback() noexcept { return true; }",
        )
    ]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    # constexpr makes this UNCHECKABLE regardless -- the point under test is
    # that the attribute did not cause it to be silently DROPPED instead.
    assert candidates == []
    assert len(uncheckable) == 1
    assert uncheckable[0].name == "usesFallback"
    assert "constexpr" in uncheckable[0].reason


def test_attributed_out_of_line_cpp_definition_is_a_candidate():
    text = "namespace rta {\n[[nodiscard]] bool Foo::isReady() const noexcept {\n    return true;\n}\n}\n"
    added = [AddedLine("f.cpp", 2, "[[nodiscard]] bool Foo::isReady() const noexcept {")]
    candidates, uncheckable = find_candidates(added, _reader({"f.cpp": text}))
    assert uncheckable == []
    assert len(candidates) == 1
    assert candidates[0].name == "isReady"
    assert candidates[0].enclosing == ["Foo", "rta"]


def test_two_line_attributed_declaration_is_a_candidate():
    # The attribute and return type share a line; the name+params wrap to
    # the next -- flattening must still find the terminator and the
    # attribute-stripping must still apply to the JOINED head.
    text = (
        "namespace rta {\nclass Foo {\npublic:\n"
        "    [[nodiscard]] std::optional<double>\n"
        "    computeSomething() const noexcept;\n};\n}\n"
    )
    added = [AddedLine("f.h", 4, "    [[nodiscard]] std::optional<double>")]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert uncheckable == []
    assert len(candidates) == 1
    assert candidates[0].name == "computeSomething"
    assert candidates[0].enclosing == ["Foo", "rta"]


def test_repeated_attribute_groups_are_all_stripped():
    text = "namespace rta {\n[[nodiscard]] [[deprecated]] int Foo::legacyValue() const {\n    return 1;\n}\n}\n"
    added = [AddedLine("f.cpp", 2, "[[nodiscard]] [[deprecated]] int Foo::legacyValue() const {")]
    candidates, uncheckable = find_candidates(added, _reader({"f.cpp": text}))
    assert uncheckable == []
    assert len(candidates) == 1
    assert candidates[0].name == "legacyValue"


# --- fix round 1 (verifier), F2-F4: shapes that must become UNCHECKABLE ----
# (never silently dropped, never handed a wrong fragment)


def test_call_operator_is_uncheckable_not_wrongly_fragmented():
    # Before this fix: `operator()` matched TYPED_DECL_RE with name literally
    # "operator" (the regex's name group stops at the first non-word char,
    # and the FIRST `(` of `operator()` satisfied `\s*\(`) -- producing the
    # wrong fragment `?operator@...`.
    text = "void Foo::operator()(int x) {\n}\n"
    added = [AddedLine("f.cpp", 1, "void Foo::operator()(int x) {")]
    candidates, uncheckable = find_candidates(added, _reader({"f.cpp": text}))
    assert candidates == []
    assert len(uncheckable) == 1
    assert "operator" in uncheckable[0].reason


def test_conversion_operator_is_uncheckable_not_wrongly_fragmented():
    # Before this fix: `explicit operator bool` -- after stripping
    # "explicit " -- matched with "operator" absorbed into the "return type"
    # prefix and "bool" captured as the name, giving the wrong fragment
    # `?bool@...`.
    text = "class Foo {\npublic:\n    explicit operator bool() const;\n};\n"
    added = [AddedLine("f.h", 3, "    explicit operator bool() const;")]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert candidates == []
    assert len(uncheckable) == 1
    assert "operator" in uncheckable[0].reason


def test_symbolic_operator_is_uncheckable_not_silently_dropped():
    # Before this fix: "==" is not a word character, so neither
    # TYPED_DECL_RE's nor BARE_NAME_RE's name-capturing group could reach
    # past "operator" at all -- the whole match failed and the declaration
    # vanished, neither a candidate nor UNCHECKABLE.
    text = "class Foo {\npublic:\n    bool operator==(const Foo& other) const;\n};\n"
    added = [AddedLine("f.h", 3, "    bool operator==(const Foo& other) const;")]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert candidates == []
    assert len(uncheckable) == 1
    assert uncheckable[0].name == "operator=="


def test_one_line_template_function_is_uncheckable():
    # `template <...>` on the SAME line as the signature -- the `<`/`>`
    # characters are valid members of TYPED_DECL_RE's permissive
    # "type-tokens" charset, so without an explicit check this would have
    # matched with a wrong/misleading name rather than being recognised as
    # a template at all.
    text = "namespace rta {\ntemplate <typename T> void identity(T x) {}\n}\n"
    added = [AddedLine("f.h", 2, "template <typename T> void identity(T x) {}")]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert candidates == []
    assert len(uncheckable) == 1
    assert uncheckable[0].name == "identity"
    assert "template" in uncheckable[0].reason


def test_member_of_a_class_template_is_uncheckable():
    text = (
        "namespace rta {\ntemplate <typename T>\nclass Container {\npublic:\n"
        "    void push(T value);\n};\n}\n"
    )
    added = [AddedLine("f.h", 5, "    void push(T value);")]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert candidates == []
    assert len(uncheckable) == 1
    assert uncheckable[0].name == "push"
    assert "class template" in uncheckable[0].reason


def test_pure_virtual_is_uncheckable():
    text = "class Foo {\npublic:\n    virtual void update() = 0;\n};\n"
    added = [AddedLine("f.h", 3, "    virtual void update() = 0;")]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert candidates == []
    assert len(uncheckable) == 1
    assert "pure virtual" in uncheckable[0].reason


def test_pure_virtual_final_is_uncheckable():
    # fix round 2 (verifier), F-E: `final` sealing a pure virtual against
    # further overriding was missed by the qualifier chain -- the real shape
    # named in the review, `virtual void fin() final = 0;`.
    text = "class Foo {\npublic:\n    virtual void fin() final = 0;\n};\n"
    added = [AddedLine("f.h", 3, "    virtual void fin() final = 0;")]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert candidates == []
    assert len(uncheckable) == 1
    assert "pure virtual" in uncheckable[0].reason


def test_default_argument_equals_zero_is_not_mistaken_for_pure_virtual():
    # `= 0` as a default ARGUMENT (inside the parameter list, before the
    # closing paren) must not trip the pure-virtual check, which only
    # matches `= 0` AFTER the closing paren.
    text = "class Foo {\npublic:\n    void f(int x = 0);\n};\n"
    added = [AddedLine("f.h", 3, "    void f(int x = 0);")]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert uncheckable == []
    assert len(candidates) == 1
    assert candidates[0].name == "f"


def test_friend_function_is_uncheckable():
    # A friend's true linkage scope is not necessarily the class it is
    # declared a friend of -- treating it as a member of the enclosing class
    # (as `_LEADING_KEYWORDS_RE` stripping "friend" and proceeding normally
    # would) risks a wrong fragment.
    text = "class Foo {\n    friend void helper(Foo& f);\n};\n"
    added = [AddedLine("f.h", 2, "    friend void helper(Foo& f);")]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert candidates == []
    assert len(uncheckable) == 1
    assert uncheckable[0].name == "helper"
    assert "friend" in uncheckable[0].reason


def test_pure_data_aggregate_is_uncheckable():
    text = "namespace rta {\nstruct Point {\n    float x;\n    float y;\n};\n}\n"
    added = [AddedLine("f.h", 2, "struct Point {")]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert candidates == []
    assert len(uncheckable) == 1
    assert uncheckable[0].name == "Point"
    assert "aggregate" in uncheckable[0].reason


def test_class_with_a_real_method_is_not_reported_as_a_pure_data_aggregate():
    text = "namespace rta {\nstruct Widget {\n    void tick();\n};\n}\n"
    added = [
        AddedLine("f.h", 2, "struct Widget {"),
        AddedLine("f.h", 3, "    void tick();"),
    ]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    names = [c.name for c in candidates]
    assert "tick" in names
    assert not any(u.name == "Widget" for u in uncheckable)


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
