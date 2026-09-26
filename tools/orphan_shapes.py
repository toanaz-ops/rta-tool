# SPDX-License-Identifier: AGPL-3.0-or-later
"""orphan_shapes.py -- the declaration-shape regex heuristic
orphan_candidates.py's `find_candidates` orchestrates. Split out of that
module (fix round 1, verifier) to keep both files under this repo's 400-line
cap once F1-F4's fixes grew the shape logic past it.

WHY NOT REUSE orphan_symbols.py (the v1 grep heuristic). v1's `extract_symbols`
requires a declaration to open AND close on ONE physical line (`_FUNC_RE` is
anchored `^...$`) -- `enableSplLogging`'s own real declaration in
AnalysisThread.h spans three lines (the parameter list wraps), which is
exactly the shape v1's regex cannot see. `_flatten_declaration` joins a
candidate's signature across lines FIRST (tracking paren depth, so an
embedded `{}` default-argument initializer -- `std::string logDirectory =
{}`, which v1's tail-anchored regex also could not admit -- never gets
mistaken for the declaration's own terminator); only the HEAD of that
flattened text (return-type-ish prefix, name, opening paren) is ever matched.
It never needs to find or validate the tail at all: `_flatten_declaration`
already proved the declaration terminates in `;` or `{` by construction.

WHAT COUNTS AS A CANDIDATE. The flattened head, after `_strip_attributes` and
`_LEADING_KEYWORDS_RE` remove `[[...]]` groups and keywords, must be:
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

UNCHECKABLE CLASSIFICATION. Two kinds:
  - Content-based (`_is_uncheckable_text`, checked AFTER a name is found):
    `= delete`/`= default`, `constexpr`/`consteval`, a pure virtual's
    trailing `= 0`.
  - Shape-based (`_unfragmentable_shape`, checked BEFORE attempting to
    extract a name at all -- fix round 1, verifier, F2-F4): `friend`
    functions and every `operator` shape. These are checked first because
    the normal name-extraction regexes either silently find NOTHING for them
    (a symbolic operator like `operator==` has no word-character name for the regex to
    capture) or find something WRONG (`operator()` reads as a function
    literally named "operator"; `explicit operator bool` reads "bool" as the
    function name with "operator" absorbed into the fake "return type") --
    both are worse than UNCHECKABLE, which is why this check runs first and
    unconditionally short-circuits.
Also UNCHECKABLE, decided by the caller using `cpp_scopes` results this
module does not itself compute: an anonymous-namespace symbol, a template
(same-line or previous-line `template <...>`), a member of a class template
(fragment depends on instantiation arguments this tool cannot know), and a
class/struct with zero function-shaped members (a pure-data aggregate).
"""

from __future__ import annotations

import re
from dataclasses import dataclass

CONTROL_KEYWORDS = {
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
NON_DECLARATION_LINE_RE = re.compile(r"^(?:public|private|protected)\s*:$")
_LEADING_KEYWORDS_RE = re.compile(
    r"^(?:virtual|static|explicit|inline|constexpr|consteval|friend)\s+"
)
# `[[nodiscard]]`, `[[deprecated]]`, etc. -- fix round 1 (verifier), F1. These
# precede the return type (`[[nodiscard]] bool isReady();`) or a leading
# keyword (`[[nodiscard]] static constexpr bool f();`), and can repeat
# (`[[nodiscard]] [[deprecated]] int g();`). Left unstripped, the flattened
# head starts with `[`, which matches neither _TYPED_DECL_RE nor
# _BARE_NAME_RE/_BARE_QUALIFIED_RE (both anchor on `[A-Za-z_]` at position 0)
# -- the whole declaration silently vanished, neither a candidate nor
# UNCHECKABLE. 396 lines in app/src carry a leading attribute; 114 of those
# were added in the L6a range this tool was built to audit, including
# `MainComponent.h`'s `[[nodiscard]] rta::view::PaneView currentPaneView()
# const noexcept { ... }`, which has zero callers of any kind and was the
# concrete miss that found this.
_ATTRIBUTE_GROUP_RE = re.compile(r"^\[\[[^\]]*\]\]\s*")
TYPED_DECL_RE = re.compile(
    r"^[A-Za-z_][\w:<>,\*&\s]*[\s\*&]((?:[A-Za-z_]\w*::)*~?[A-Za-z_]\w*)\s*\("
)
BARE_NAME_RE = re.compile(r"^([A-Za-z_]\w*)\s*\(")
# An out-of-line constructor has no return type AND is qualified
# (`MainComponent::MainComponent(`) -- distinct from both other shapes.
BARE_QUALIFIED_RE = re.compile(r"^((?:[A-Za-z_]\w*::)+~?[A-Za-z_]\w*)\s*\(")
CLASS_RE = re.compile(r"^\s*(?:class|struct)\s+([A-Za-z_]\w*)\b")
TEMPLATE_LINE_RE = re.compile(r"^\s*template\s*<")
# fix round 1 (verifier), F2-F4: shapes this tool cannot give a correct
# decorated-name fragment to. Each MUST become UNCHECKABLE with its own
# reason -- never silently dropped (the F1 failure mode) and never handed a
# WRONG fragment that would either falsely clear a real orphan or falsely
# flag a live one.
_FRIEND_RE = re.compile(r"^friend\s+")
# `\boperator\b` catches every shape: `operator()` (call), `operator==`
# (symbolic, previously silently dropped -- `==` is not a \w character, so
# neither _TYPED_DECL_RE nor _BARE_NAME_RE's name group could ever reach past
# it), and `explicit operator bool` (a conversion operator, previously
# mis-parsed as a function literally named `bool`, taking "operator" as part
# of the "return type" prefix).
_OPERATOR_RE = re.compile(r"\boperator\b")
# A pure virtual's `= 0` sits AFTER the closing paren and any trailing
# const/noexcept/override qualifiers, immediately before the terminating
# `;` -- anchored this way so an ordinary default ARGUMENT `= 0` inside the
# parameter list (`void f(int x = 0);`) is never mistaken for one (that `= 0`
# sits BEFORE the closing paren, which this pattern never looks past).
_PURE_VIRTUAL_RE = re.compile(
    r"\)\s*(?:const\s*)?(?:noexcept(?:\([^)]*\))?\s*)?(?:override\s*)?=\s*0\s*;\s*$"
)
# Best-effort name for an UNCHECKABLE report only -- never used to build a
# fragment. Falls back to whatever token precedes the first `(`.
_DISPLAY_NAME_RE = re.compile(r"([A-Za-z_][\w:]*|operator\S*)\s*\(")


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


def flatten_declaration(lines: list[str], start_idx: int, max_lines: int = 12) -> str | None:
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


def strip_attributes(text: str) -> str:
    """Strip every leading `[[...]]` group, in order, along with the
    whitespace after each -- repeatable, so `[[nodiscard]] [[deprecated]]`
    both go. Applied BEFORE `_LEADING_KEYWORDS_RE`, matching this codebase's
    own convention of an attribute preceding a keyword
    (`[[nodiscard]] static constexpr bool f();`), never the reverse.
    """
    text = text.lstrip()
    while True:
        m = _ATTRIBUTE_GROUP_RE.match(text)
        if not m:
            return text
        text = text[m.end():]


def strip_leading_keywords(text: str) -> str:
    return _LEADING_KEYWORDS_RE.sub("", text)


def is_uncheckable_text(flattened: str) -> str | None:
    if "= delete" in flattened or "= default" in flattened:
        return "= default / = delete member"
    if re.search(r"\bconstexpr\b", flattened) or re.search(r"\bconsteval\b", flattened):
        return "constexpr/consteval function"
    if _PURE_VIRTUAL_RE.search(flattened):
        return "pure virtual (no definition of its own to check)"
    return None


def unfragmentable_shape(attr_stripped: str) -> str | None:
    """A shape this tool's decorated-name construction cannot handle at all
    -- checked BEFORE attempting to extract a name, so it can never be
    silently dropped (F1's failure mode) or handed a wrong fragment (F2-F4).
    `attr_stripped` has had `[[...]]` groups removed but NOT yet the leading
    keywords (`friend`, `virtual`, ...), since `friend` itself is one of the
    things being tested for here.
    """
    if _FRIEND_RE.match(attr_stripped):
        # A friend's true linkage scope is not necessarily the class it is
        # declared a friend of (often the surrounding namespace instead, or
        # another class entirely) -- cpp_scopes has no way to know which, so
        # any fragment built from "the enclosing class" would be a guess.
        return "friend function (fragment scope ambiguous)"
    if _OPERATOR_RE.search(attr_stripped):
        # Covers the call operator (`operator()`), every symbolic operator
        # (`operator==`, `operator+`, ...) and conversion operators
        # (`operator bool`) in one check: MSVC's decoration of an operator
        # uses a fixed per-operator special code (`??4` for operator=, `??8`
        # for operator==, ...) that this tool does not model, and a
        # conversion operator's decorated name embeds the TARGET type's own
        # encoding, not a plain identifier.
        return "operator overload / conversion operator (fragment not modeled)"
    return None


def display_name(text: str) -> str:
    """Best-effort name for an UNCHECKABLE report ONLY -- never fed to
    msvc_decorate.
    """
    m = _DISPLAY_NAME_RE.search(text)
    return m.group(1) if m else "?"


def resolve_qualified(name: str, enclosing: list[str]) -> tuple[str, list[str], bool] | None:
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
