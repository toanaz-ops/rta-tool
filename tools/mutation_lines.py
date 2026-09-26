#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""mutation_lines.py -- is a source line a mutation candidate?

Split out of diffmut.py (process-tooling PR, fix round 1, item L3) along the
seam between "classify a line" and "run a mutation campaign".

WHAT IS NOT MUTATED. Deleting these either cannot change behaviour or almost
certainly breaks the build in a way unrelated to the statement's logic, which
would make every such mutant NO-BUILD noise rather than a signal:
  - blank lines
  - comment-only lines: `//...`, a self-contained one-line `/* ... */` block
    with nothing else on the line, or the OPENING line of a multi-line `/*`
    block (no closing `*/` anywhere on that line, so the comment continues
    past it). A line that closes its block comment and then has REAL CODE
    after it (`/* x */ foo();`) is NOT comment-only and IS mutated -- fix
    round 1 item L5 found the previous regex treated any line starting with
    `/*` as comment-only regardless of what followed a closing `*/`, which
    silently exempted code from ever being tested this way. A bare leading
    `*` (the usual continuation style for a block comment's middle lines,
    " * more text") is deliberately NOT treated as a comment marker at all
    any more, for the same round: `*out = 5;` is a pointer-dereference
    STATEMENT, not a comment, and the old regex could not tell them apart by
    text alone. The asymmetry is intentional: mutating an actual mid-comment
    line at worst wastes one SURVIVED row (deleting inert prose changes
    nothing); skipping a real statement because it merely started with `*`
    hides a line nobody is testing, which is the defect this tool exists to
    find.
  - brace/paren/semicolon/comma "shape" lines with no other content
    (`{`, `}`, `};`, `);`, `});`, ...)
  - preprocessor directives (`#include`, `#pragma`, `#define`, ...)
  - `namespace ...` opening lines
  - access specifiers (`public:` / `private:` / `protected:`)
  - PURE DECLARATIONS: a line that is *only* `<return-type> name(args) [const]
    [override] [= 0];` with no `=` assignment and no `return` keyword. This is
    a heuristic, not a parser -- it is meant to catch a bare prototype (a
    declaration has no body to mutate), and it is deliberately narrow: a bare
    call statement like `doThing(x);` has no leading type token before the
    name, so it does NOT match and IS mutated (that call is a statement with
    an effect, and deleting it should be observable).
"""

from __future__ import annotations

import re

MUTABLE_EXTENSIONS = {".h", ".hpp", ".cpp"}

_BRACE_SHAPE_RE = re.compile(r"^[\{\}\(\)\;\,\s]*$")
_ACCESS_SPECIFIER_RE = re.compile(r"^(public|private|protected)\s*:\s*$")
_NAMESPACE_RE = re.compile(r"^namespace\b")
_PREPROCESSOR_RE = re.compile(r"^#")
# See the module docstring's "PURE DECLARATIONS" section for what this is and
# is not meant to catch.
_PURE_DECLARATION_RE = re.compile(
    r"^[A-Za-z_][\w:<>,\*&\s]*[\s\*&][A-Za-z_~][\w]*\s*\([^;{}]*\)\s*"
    r"(const)?\s*(override)?\s*(noexcept)?\s*(=\s*0)?\s*;$"
)


def is_comment_only_line(stripped: str) -> bool:
    """True for a `//` line-comment, a self-contained one-line `/* ... */`
    block, or the OPENING line of a multi-line `/*` block (no closing `*/`
    anywhere on it). False for a line that closes its block comment and then
    has real code after it -- see the module docstring (fix round 1 item L5)
    for the bug this replaced and why a bare leading `*` is no longer treated
    as a comment marker at all.
    """
    if stripped.startswith("//"):
        return True
    if stripped.startswith("/*"):
        close = stripped.rfind("*/")
        if close == -1:
            return True  # unclosed opener -- comment continues past this line
        return stripped[close + 2 :].strip() == ""
    return False


def is_mutable_line(line: str) -> bool:
    """True if `line` (no trailing newline) is a candidate for deletion.

    See the module docstring for the full rationale of each exclusion.
    """
    stripped = line.strip()
    if not stripped:
        return False
    if is_comment_only_line(stripped):
        return False
    if _BRACE_SHAPE_RE.match(stripped):
        return False
    if _PREPROCESSOR_RE.match(stripped):
        return False
    if _NAMESPACE_RE.match(stripped):
        return False
    if _ACCESS_SPECIFIER_RE.match(stripped):
        return False
    if _PURE_DECLARATION_RE.match(stripped) and not re.search(r"\breturn\b", stripped):
        return False
    return True
