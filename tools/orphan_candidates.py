# SPDX-License-Identifier: AGPL-3.0-or-later
"""orphan_candidates.py -- functions/methods/constructors declared on lines
this branch ADDED under app/src/**, for orphan_check.py v2's linker-
reachability check.

WHY NOT REUSE orphan_symbols.py (the v1 grep heuristic). v1's `extract_symbols`
requires a declaration to open AND close on ONE physical line (`_FUNC_RE` is
anchored `^...$`) -- `enableSplLogging`'s own real declaration in
AnalysisThread.h spans three lines (the parameter list wraps), which is
exactly the shape v1's regex cannot see. This module flattens a candidate's
signature across lines FIRST (tracking paren depth, so an embedded `{}`
default-argument initializer -- `std::string logDirectory = {}`, which
v1's tail-anchored regex also could not admit -- never gets mistaken for the
declaration's own terminator), then matches only the HEAD of that flattened
text (return-type-ish prefix, name, opening paren). It never needs to find or
validate the tail at all: `_flatten_declaration` already proved the
declaration terminates in `;` or `{` by construction.

WHAT COUNTS AS A CANDIDATE. The flattened head must be:
  - a return-type-shaped prefix (any type token, INCLUDING `void`) followed
    by a name and `(` -- catches ordinary methods and free functions of any
    return type; or
  - a BARE name (no return-type prefix at all) that exactly equals the
    immediately enclosing class name (from `cpp_scopes`) -- a constructor,
    which never carries a return type.
Requiring a return-type token in the general case (rather than making it
optional, the way v1's `_FUNC_RE` did) is deliberate: an ordinary CALL
statement added on its own line (`doSomething(x);`) has no return-type
prefix either, so an optional-prefix regex reads it exactly like a
constructor-shaped declaration. Making the prefix load-bearing removes that
whole false-positive class; the narrow constructor exception is the one place
a call and a declaration are genuinely ambiguous by spelling, and it is
resolved by cross-checking the name against the ACTUAL enclosing class
`cpp_scopes` found at that line, not just its own shape.

A member-access call (`obj.enableSplLogging(...)`, `obj->enableSplLogging(...)`)
never matches either shape: `.` and `->` are not in the return-type token
charset, so the head regex fails to find a name+`(` at position 0 at all.

UNCHECKABLE CLASSIFICATION (the task's own list): a candidate whose flattened
text contains `= delete` or `= default`, or the keyword `constexpr`/
`consteval`, or whose enclosing chain passes through an anonymous namespace
(`cpp_scopes.ANONYMOUS`), is reported separately and never asked for a map
fragment. A class/struct ADDED in the diff with zero function-shaped
candidates found inside it (a pure-data aggregate) is also UNCHECKABLE, found
by a second, narrower pass over the same added lines.
"""

from __future__ import annotations

import re
from dataclasses import dataclass

from cpp_scopes import ANONYMOUS, declaration_level_by_line, scopes_by_line
from cpp_text import strip_comments
from difflines import AddedLine

_CONTROL_KEYWORDS = {
    "if", "for", "while", "switch", "return", "throw", "catch",
    "sizeof", "static_assert", "co_return", "else", "using", "typedef",
}
# An access-specifier line (`public:`) has NO terminator of its own
# (`_flatten_declaration` never sees `;` or `{` on it) and is not itself a
# declaration -- without this exclusion, flattening walks straight through
# it into whatever comes next (blank lines where a stripped doc comment used
# to be, then the REAL next declaration), fusing the two into one garbled
# "declaration" and mis-shaping the real one's own candidate. Real bug: an
# added `public:` line immediately before SplAlarm's constructor produced a
# spurious extra candidate literally named `SplAlarm` with the wrong shape,
# the first time this ran against the real repo.
_NON_DECLARATION_LINE_RE = re.compile(r"^(?:public|private|protected)\s*:$")
_LEADING_KEYWORDS_RE = re.compile(
    r"^(?:virtual|static|explicit|inline|constexpr|consteval|friend)\s+"
)
_TYPED_DECL_RE = re.compile(
    r"^[A-Za-z_][\w:<>,\*&\s]*[\s\*&]((?:[A-Za-z_]\w*::)*~?[A-Za-z_]\w*)\s*\("
)
_BARE_NAME_RE = re.compile(r"^([A-Za-z_]\w*)\s*\(")
# An out-of-line constructor has no return type AND is qualified
# (`MainComponent::MainComponent(`) -- distinct from both other shapes.
_BARE_QUALIFIED_RE = re.compile(r"^((?:[A-Za-z_]\w*::)+~?[A-Za-z_]\w*)\s*\(")
_CLASS_RE = re.compile(r"^\s*(?:class|struct)\s+([A-Za-z_]\w*)\b")
_TEMPLATE_LINE_RE = re.compile(r"^\s*template\s*<")


@dataclass
class Candidate:
    path: str
    lineno: int
    name: str  # bare, or "Class::method" when found in a .cpp out-of-line
    enclosing: list[str]  # innermost first
    is_constructor: bool


@dataclass
class Uncheckable:
    path: str
    lineno: int
    name: str
    reason: str


def _flatten_declaration(lines: list[str], start_idx: int, max_lines: int = 12) -> str | None:
    """Join lines[start_idx:] until a `;` or `{` at paren-depth 0, truncating
    the winning line right at that character. None if no terminator turns up
    within `max_lines` (an unusually long signature, or not a declaration at
    all) -- callers skip such a candidate rather than guess.
    """
    depth = 0
    pieces: list[str] = []
    end = min(start_idx + max_lines, len(lines))
    for i in range(start_idx, end):
        line = lines[i]
        for pos, ch in enumerate(line):
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth = max(0, depth - 1)
            elif ch in ";{" and depth == 0:
                pieces.append(line[: pos + 1])
                return " ".join(pieces)
        pieces.append(line)
    return None


def _is_uncheckable_text(flattened: str) -> str | None:
    if "= delete" in flattened or "= default" in flattened:
        return "= default / = delete member"
    if re.search(r"\bconstexpr\b", flattened) or re.search(r"\bconsteval\b", flattened):
        return "constexpr/consteval function"
    return None


def _resolve_qualified(name: str, enclosing: list[str]) -> tuple[str, list[str], bool] | None:
    """Split a possibly-qualified out-of-line name (`Class::method`, or
    `Outer::Inner::method` for a nested class) into its bare name and the
    full enclosing chain, innermost first. `cpp_scopes` never sees a
    qualifier that is baked into the NAME itself (a .cpp defining
    `void MainComponent::calibrationStartClicked() {` does not reopen
    `class MainComponent`, so `enclosing` from the .cpp's own braces alone is
    missing the class entirely) -- the qualifier segments, most-deeply-nested
    LAST in the source spelling, are innermost FIRST in MSVC's decoration
    order, so they are reversed before being prepended to whatever namespace
    scope (if any) the .cpp file's own braces did contribute. Returns None
    for a destructor (`~Class`), which is not in this tool's candidate set;
    recognises `Class::Class(...)` as an out-of-line constructor.
    """
    parts = name.split("::")
    bare = parts[-1]
    if bare.startswith("~"):
        return None
    qualifiers = parts[:-1]
    is_ctor = bool(qualifiers) and bare == qualifiers[-1]
    full_enclosing = list(reversed(qualifiers)) + enclosing
    return bare, full_enclosing, is_ctor


def find_candidates(
    added_lines: list[AddedLine], read_file
) -> tuple[list[Candidate], list[Uncheckable]]:
    """`read_file(path) -> str` returns a file's current full text (this
    function comment-strips it itself). Only lines under the paths the caller
    already scoped `added_lines` to (orphan_check.py passes `app/src`) are
    ever considered.
    """
    candidates: list[Candidate] = []
    uncheckable: list[Uncheckable] = []
    file_lines: dict[str, list[str]] = {}
    file_scopes: dict[str, list[list[str]]] = {}
    file_decl_level: dict[str, list[bool]] = {}
    seen_classes: dict[str, tuple[int, list[str]]] = {}  # class name -> (lineno, enclosing)
    classes_with_a_member: set[str] = set()

    for added in added_lines:
        first = added.text.strip()
        if not first:
            continue
        first_word = re.split(r"[\s(]", first, maxsplit=1)[0]
        if (
            first_word in _CONTROL_KEYWORDS
            or first.startswith(("//", "#"))
            or _NON_DECLARATION_LINE_RE.match(first)
        ):
            continue

        if added.path not in file_lines:
            text = strip_comments(read_file(added.path))
            file_lines[added.path] = text.splitlines()
            file_scopes[added.path] = scopes_by_line(text)
            file_decl_level[added.path] = declaration_level_by_line(text)
        lines = file_lines[added.path]
        scopes = file_scopes[added.path]
        decl_level = file_decl_level[added.path]
        idx = added.lineno - 1
        if idx < 0 or idx >= len(lines):
            continue
        enclosing = scopes[added.lineno] if added.lineno < len(scopes) else []
        if added.lineno < len(decl_level) and not decl_level[added.lineno]:
            # Nested inside some unnamed block (a function/method body, an
            # if/for/lambda body) -- a local variable using
            # direct-initialisation syntax (`std::ofstream out(path, ...);`)
            # is byte-for-byte indistinguishable from a declaration by name+
            # paren shape alone, and this is the only place that ambiguity is
            # actually resolvable: a real declaration's own scope is always
            # its innermost NAMED enclosure with nothing unnamed in between.
            continue

        class_m = _CLASS_RE.match(lines[idx])
        if class_m:
            seen_classes[class_m.group(1)] = (added.lineno, list(enclosing))
            continue

        flattened = _flatten_declaration(lines, idx)
        if flattened is None:
            continue

        is_template = idx > 0 and _TEMPLATE_LINE_RE.match(lines[idx - 1])
        head = _LEADING_KEYWORDS_RE.sub("", flattened.lstrip())

        m = _TYPED_DECL_RE.match(head)
        is_ctor = False
        name: str | None = None
        full_enclosing = enclosing
        if m:
            raw_name = m.group(1)
            if raw_name.startswith("std::") or raw_name.split("::")[-1] in _CONTROL_KEYWORDS:
                raw_name = None
            if raw_name is not None:
                resolved = _resolve_qualified(raw_name, enclosing)
                if resolved is None:
                    continue  # a destructor (~Class) -- not in this tool's candidate set
                name, full_enclosing, is_ctor = resolved
        else:
            m2 = _BARE_NAME_RE.match(head)
            m3 = _BARE_QUALIFIED_RE.match(head)
            if m2 and enclosing and m2.group(1) == enclosing[0]:
                # A bare, UNqualified name inside a class body matching that
                # class's own name -- an in-class constructor declaration.
                name = m2.group(1)
                is_ctor = True
            elif m3:
                # A bare, QUALIFIED name with no return type -- only a
                # constructor shape is accepted here (`Class::Class(`);
                # anything else this matches (a qualified call with no
                # return type visible on this line) is not a declaration and
                # is deliberately left uncaught rather than guessed at.
                resolved = _resolve_qualified(m3.group(1), enclosing)
                if resolved is not None:
                    candidate_name, candidate_enclosing, candidate_is_ctor = resolved
                    if candidate_is_ctor:
                        name, full_enclosing, is_ctor = (
                            candidate_name, candidate_enclosing, candidate_is_ctor
                        )
        if name is None:
            continue

        if full_enclosing:
            # record that the innermost enclosing CLASS has at least one
            # function-shaped member (declared in a header OR defined
            # out-of-line in a .cpp -- either way it resolves through
            # `full_enclosing`, never the bare per-file `enclosing`), for the
            # pure-data-aggregate pass below.
            classes_with_a_member.add(full_enclosing[0])

        reason = None
        if ANONYMOUS in enclosing:
            reason = "declared inside an anonymous namespace"
        elif is_template:
            reason = "template (instantiation not tracked)"
        else:
            reason = _is_uncheckable_text(flattened)
        if reason:
            uncheckable.append(Uncheckable(added.path, added.lineno, name, reason))
            continue

        candidates.append(Candidate(added.path, added.lineno, name, full_enclosing, is_ctor))

    for class_name, (lineno, enclosing) in seen_classes.items():
        if class_name not in classes_with_a_member:
            uncheckable.append(
                Uncheckable(
                    "", lineno, class_name, "pure-data aggregate, no function member to check"
                )
            )

    return candidates, uncheckable
