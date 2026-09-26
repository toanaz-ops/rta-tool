#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""orphan_symbols.py -- the symbol-declaration heuristic orphan_check.py uses.

Split out of orphan_check.py so the regex heuristic (and its own documented
blind spots) lives in one small, independently testable place.

WHAT IS EXTRACTED. A regex line-scan, not a C++ parser:
  - `class Foo` / `struct Foo` declaration lines -> the type name.
  - A free-function-shaped line: some prefix of type tokens, then a bare
    identifier, then `(args)`, then optionally `const`/`noexcept`, ending in
    `{` or `;`. This also catches a member function defined out-of-line
    (`Foo::bar(...)`), where the extracted "name" is `Foo::bar`; a reference
    check for that qualified name is stricter than for a bare one, which is
    the conservative direction (it undercounts hits rather than overcounts
    them, and an undercount here means "possibly still an orphan", not the
    reverse).
This is deliberately narrow: a symbol this heuristic MISSES (a macro-
generated declaration, a lambda assigned to a named variable, a
`using Alias = ...`) is never flagged either way, rather than risk a false
orphan report on code the heuristic cannot read.

`is_self_definition_of(line, symbol)` answers a different question a
REFERENCE search needs (fix round 1, item L1): is this line the symbol's OWN
out-of-line member definition (`ReturnType SplLogWriter::write(...) { ... }`)
rather than something else calling it? A class's constructor/method
definitions live in a .cpp that is not always paired with its header by
basename (`SplLogWriter` is declared in `SplLog.h`, defined in
`SplLogWriter.cpp` -- different stems, so the existing same-basename pairing
does not exclude that file), so without this a class's own definition reads
as an external "caller" of itself.
"""

from __future__ import annotations

import re

_CLASS_RE = re.compile(r"^\s*(?:class|struct)\s+([A-Za-z_]\w*)\b")
# type-tokens, then NAME(args) [const] [noexcept] , ending in { or ;
# NAME may be qualified (Foo::bar) for an out-of-line member definition.
_FUNC_RE = re.compile(
    r"^\s*[A-Za-z_][\w:<>,\*&\s]*[\s\*&]"
    r"((?:[A-Za-z_]\w*::)*[A-Za-z_]\w*)"
    r"\s*\([^;{}]*\)\s*(const)?\s*(noexcept)?\s*(override)?\s*[;{]\s*$"
)
_CONTROL_KEYWORDS = {"if", "for", "while", "switch", "return", "catch", "sizeof", "static_assert"}
# A line that OPENS with one of these is a statement inside some function's
# body (a call, a return, a throw), never a declaration -- even though its
# tail can otherwise look exactly like one. Without this, `return
# std::put_time(&tm, fmt);` inside an inline header function is misread as a
# declaration of a function named `std::put_time` (a standard-library call,
# not a project symbol), which is a real false positive this check hit
# against SplSessionFolderName.h on its first run against the live repo.
_STATEMENT_PREFIX_RE = re.compile(r"^(return|throw|co_return)\b")


def extract_symbols(text: str) -> set[str]:
    """Best-effort class/struct/free-function names declared in `text`
    (expected to already be comment-stripped -- see cpp_text.strip_comments).
    See the module docstring's "WHAT IS EXTRACTED" section for the exact rule
    and its known blind spots.
    """
    names: set[str] = set()
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if _STATEMENT_PREFIX_RE.match(line):
            continue
        m = _CLASS_RE.match(line)
        if m:
            names.add(m.group(1))
            continue
        m2 = _FUNC_RE.match(line)
        if m2:
            name = m2.group(1)
            # A header can reference std:: (or any other outside namespace)
            # from inside a statement that otherwise matches the declaration
            # shape (`out << std::put_time(...);` is the real example that
            # tripped this against SplSessionFolderName.h) -- it can never
            # DECLARE a symbol there, so this is a call, not a declaration.
            if name.startswith("std::"):
                continue
            if name.split("::")[-1] not in _CONTROL_KEYWORDS:
                names.add(name)
    return names


def _self_def_patterns(symbol: str) -> tuple[re.Pattern[str], re.Pattern[str]]:
    esc = re.escape(symbol)
    return (
        re.compile(r"\b" + esc + r"::[A-Za-z_~]\w*\s*\("),  # symbol is the CLASS
        re.compile(r"\b[A-Za-z_]\w*::" + esc + r"\s*\("),  # symbol is the METHOD
    )


def is_self_definition_of(line: str, symbol: str) -> bool:
    """True if `line` (expected comment-stripped) is the OPENING line of
    `symbol`'s own out-of-line definition, in either of the two shapes a
    symbol extracted by this module can take:
      - `symbol` IS the class: `symbol::method(...)` or the constructor
        `symbol::symbol(...)`.
      - `symbol` IS a bare method name extracted from inside a class body
        (`extract_symbols` has no notion of "this name belongs to that
        class", so a member like `enableSplLogging` is recorded bare): its
        own out-of-line definition is `SomeClass::enableSplLogging(...)`,
        where SOME OTHER identifier is the qualifier. Missing this shape is
        what let `AnalysisThread::enableSplLogging`'s own definition read as
        an external "reference" to `enableSplLogging` the first time this
        check ran against the real ad4046b/8c5d407 history.
    Either way this is excluded from a reference search: defining a symbol
    (by itself, or as somebody else's member) is not evidence that something
    ELSE calls it.

    Deliberately a substring match, NOT anchored to a full single-line
    declaration shape (unlike `_FUNC_RE` above): a real multi-parameter
    constructor routinely wraps its parameter list onto later lines
    (`SplLogWriter::SplLogWriter(std::string basePath,\n    ... ) {`), and
    `_FUNC_RE` only matches a signature that opens AND closes on one line --
    it missed exactly this shape too, the first time this ran against the
    live repo. The trade-off this loosens into: a genuine external qualified
    STATIC call (`Other::helper()`, called from outside `Other`) would also
    be excluded here, which is the safe direction for THIS check's purpose --
    crediting a class's own definition as someone else's call is the failure
    this exists to prevent; excluding an occasional real external call site
    instead makes a symbol look MORE orphaned, prompting a human to look,
    never less.
    """
    class_pattern, method_pattern = _self_def_patterns(symbol)
    return class_pattern.search(line) is not None or method_pattern.search(line) is not None
