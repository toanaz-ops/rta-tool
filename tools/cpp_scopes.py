# SPDX-License-Identifier: AGPL-3.0-or-later
"""cpp_scopes.py -- the namespace/class chain enclosing a given line, for
building an MSVC decorated-name fragment (msvc_decorate.py).

WHAT IT DOES. A single forward scan over the WHOLE file (after
`cpp_text.strip_comments`, so a `{`/`}` inside a comment is never counted),
tracking brace depth with a stack of scope entries. Each `{` pushes exactly
one entry (an opening brace always has exactly one matching close, even a
nested-namespace one -- `namespace a::b::c { ... }` is ONE open/close pair
whose entry carries THREE names); each `}` pops one. A scope entry is
classified by the text since the last statement boundary (`;`, `{`, or `}`):
  - `namespace NAME` (optionally `a::b::c`) -> a namespace entry, names split
    on `::`.
  - `namespace` with no name at all -> an ANONYMOUS entry (its own sentinel;
    orphan_check.py treats anything declared inside one as UNCHECKABLE,
    per the task's own classification -- an anonymous-namespace symbol has
    internal linkage and is a different question than "does the app call
    this").
  - `class NAME` / `struct NAME` (the type BEING DEFINED, i.e. the first
    identifier after the keyword -- a base-class list after `:` is never
    mistaken for it, since it always appears later in the same header text)
    -> a class entry, one name.
  - anything else (a function body, an if/for/while/lambda block, a brace
    initialiser) -> an EMPTY entry: it still needs its own pop, but
    contributes no name to any chain.
A single `\\n` also advances an internal line counter; `scopes_by_line(text)`
returns, for every line, the chain of names enclosing it (innermost first),
built by walking the stack from the top down and flattening each entry's own
names in reverse (so `namespace a::b::c` contributes `["c", "b", "a"]`).

A character inside a string or char literal is never treated as a brace or a
statement boundary -- brace-in-a-string-literal is the one case
`cpp_text.strip_comments` alone would not protect against (it only strips
comments, not strings), so this module tracks quotes itself over the
comment-stripped text.

WHAT THIS DOES NOT HANDLE (documented, matching orphan_symbols.py's own
"regex heuristic, not a parser" standard): a macro that itself opens or closes
a brace, `#if`-guarded scope declarations, and raw string literals (`R"(...)"`)
-- none of which occur in this codebase outside external/, the same
verified-absent claim cpp_text.py's own docstring already makes and this
module leans on.
"""

from __future__ import annotations

import re

from cpp_text import strip_comments

ANONYMOUS = "<anonymous>"

_NAMESPACE_RE = re.compile(r"\bnamespace\s+([A-Za-z_]\w*(?:\s*::\s*[A-Za-z_]\w*)*)\s*$")
_ANON_NAMESPACE_RE = re.compile(r"\bnamespace\s*$")
_CLASS_RE = re.compile(r"\b(?:class|struct)\s+([A-Za-z_]\w*)")


class _ScopeEntry:
    __slots__ = ("names",)

    def __init__(self, names: list[str]) -> None:
        self.names = names  # outer-to-inner textual order; [] if unnamed


def _classify(header: str) -> list[str]:
    """The text since the last statement boundary, up to (not including) the
    `{` it opens -- returns the name(s) that scope contributes, outer-to-inner
    textual order, or [] for an unnamed block, or [ANONYMOUS] for `namespace {`.
    """
    stripped = header.strip()
    m = _NAMESPACE_RE.search(stripped)
    if m:
        return [part.strip() for part in m.group(1).split("::")]
    if _ANON_NAMESPACE_RE.search(stripped):
        return [ANONYMOUS]
    m2 = _CLASS_RE.search(stripped)
    if m2:
        return [m2.group(1)]
    return []


def _scan(text: str) -> tuple[list[list[str]], list[bool]]:
    """One forward pass producing both per-line results:
      - the enclosing namespace/class chain (innermost first);
      - whether that line sits at DECLARATION level -- the stack is empty
        (global scope) or its TOP entry is a NAMED one (a namespace or
        class/struct, including an anonymous namespace) -- as opposed to
        being nested inside some UNNAMED block (a function body, an
        if/for/while/lambda body, a brace initialiser).
    The second question is what separates a real declaration
    (`void bar();` directly inside a class body, or `void Foo::bar() {`
    directly inside a namespace/at global scope) from a local variable using
    direct-initialisation syntax one function body deeper
    (`std::ofstream out(path, ...);` inside `void bar() { ... }`) -- which is
    syntactically identical to a declaration by name+paren shape alone, and
    is exactly the false-positive class this second array exists to rule
    out. Only the IMMEDIATE top-of-stack entry needs checking: a declaration
    line's own scope is always its innermost NAMED enclosure, with nothing
    unnamed between it and that enclosure, by construction of the language.
    """
    code = strip_comments(text)
    line_count = code.count("\n") + 1
    chains: list[list[str]] = [[] for _ in range(line_count + 2)]
    at_decl_level: list[bool] = [True for _ in range(line_count + 2)]

    stack: list[_ScopeEntry] = []
    stmt_start = 0
    line_no = 1
    in_string = False
    in_char = False
    i = 0
    n = len(code)
    while i < n:
        ch = code[i]
        if ch == "\n":
            line_no += 1
            chains[line_no] = _flatten(stack)
            at_decl_level[line_no] = not stack or bool(stack[-1].names)
            i += 1
            continue
        if in_string or in_char:
            if ch == "\\":
                i += 2
                continue
            if (in_string and ch == '"') or (in_char and ch == "'"):
                in_string = in_char = False
            i += 1
            continue
        if ch == '"':
            in_string = True
            i += 1
            continue
        if ch == "'":
            in_char = True
            i += 1
            continue
        if ch == "{":
            header = code[stmt_start:i]
            stack.append(_ScopeEntry(_classify(header)))
            stmt_start = i + 1
        elif ch == "}":
            if stack:
                stack.pop()
            stmt_start = i + 1
        elif ch == ";":
            stmt_start = i + 1
        i += 1
    return chains, at_decl_level


def scopes_by_line(text: str) -> list[list[str]]:
    """Index 0 is unused (line numbers are 1-based, matching
    difflines.AddedLine.lineno); index i holds the chain enclosing line i,
    innermost first.
    """
    chains, _ = _scan(text)
    return chains


def declaration_level_by_line(text: str) -> list[bool]:
    """Index i is True iff line i sits at declaration level (see `_scan`'s
    own docstring) -- False for a local variable, statement, or nested
    expression inside some function/block body.
    """
    _, at_decl_level = _scan(text)
    return at_decl_level


def _flatten(stack: list[_ScopeEntry]) -> list[str]:
    chain: list[str] = []
    for entry in reversed(stack):
        chain.extend(reversed(entry.names))
    return chain


def enclosing_scope_at_line(text: str, lineno: int) -> list[str]:
    """Convenience for a single lookup; scopes_by_line(text)[lineno] directly
    if checking many lines in the same file (this recomputes the whole scan
    every call).
    """
    lines = scopes_by_line(text)
    if lineno < 0 or lineno >= len(lines):
        return []
    return lines[lineno]
