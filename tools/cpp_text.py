#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""cpp_text.py -- strip C++ comments from a whole file's text, respecting
string and character literals.

Fix round 1, item M2. orphan_check.py's reference-search haystack was not
comment-stripped, so a doc comment that merely NAMES a component (`SplAlarms`
is the one producer of this...`) counted as a "reference" to it -- the exact
reason `SplAlarms`, `SplHistory` and `SplLogWriter` were invisible as orphans
at commit 8c5d407 even before M1's added-hunk fix. `//` and `/* ... */` are
stripped; a `//` or `/*` INSIDE a string or char literal (a URL, an escaped
quote) must not be mistaken for the start of a comment, so this is a small
character-by-character scanner with three states (normal code, string
literal, char literal) rather than a single regex -- a regex cannot express
"unless we are inside a string" without look-behind that would still not
handle escaped quotes correctly.

WHAT THIS DOES NOT HANDLE: raw string literals (`R"(...)"`), trigraphs, and a
line-comment whose `//` is itself escaped across a line continuation
backslash. None of those appear in this codebase's own sources (a scan run
against the whole tree during this fix confirmed zero raw string literals
outside `external/`), so the extra complexity to parse them was not added;
if one is ever introduced, the affected line degrades to being treated as
unstripped code rather than silently corrupting the scan, which is the safe
direction for a haystack (worst case: a would-be reference site is missed
inside a real comment and a real orphan is FALSE-negatived away by that one
line, which is exactly the same class of imprecision this whole tool already
accepts elsewhere and documents in orphan_check.py's own docstring).
"""

from __future__ import annotations


def strip_comments(text: str) -> str:
    """Return `text` with every `//...` and `/* ... */` comment removed,
    except where either would start inside a string or char literal. Newlines
    are preserved (including those inside a stripped block comment) so line
    numbers and surrounding-line adjacency are not corrupted; comment BODIES
    are dropped entirely rather than replaced with spaces, since nothing here
    depends on column positions.
    """
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        two = text[i : i + 2]
        if two == "//":
            j = text.find("\n", i)
            if j == -1:
                break
            out.append("\n")
            i = j + 1
            continue
        if two == "/*":
            j = text.find("*/", i + 2)
            if j == -1:
                break
            out.append("\n" * text.count("\n", i, j + 2))
            i = j + 2
            continue
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == c:
                    j += 1
                    break
                j += 1
            out.append(text[i:j])
            i = j
            continue
        out.append(c)
        i += 1
    return "".join(out)


def strip_string_and_char_literals(text: str) -> str:
    """Return `text` with the CONTENTS of every string/char literal removed
    (the delimiting quotes stay, so what remains still reads as `""`/`''`)
    -- fix round 2 (verifier), F-I, for a caller that searches for a real
    CODE reference to a name, not a name that merely appears inside a string
    (a log message, a display label). `orphan_targets.test_hook_is_referenced`
    is the caller: without this, a log line like
    `"calling enableSplLoggingForTest now"` counted as a reference to the
    hook even though nothing actually calls it.

    Deliberately does not preserve a newline swallowed by a backslash
    line-continuation inside a literal -- multi-line literals of that shape
    do not appear in this codebase (same scope note as `strip_comments`
    above), and the caller here only tests for a name's PRESENCE, never
    reads a line number back out of this result.
    """
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            out.append(c)
            j = i + 1
            while j < n and text[j] != c:
                if text[j] == "\\":
                    j += 2
                    continue
                j += 1
            if j < n:
                out.append(c)
                i = j + 1
            else:
                i = j
            continue
        out.append(c)
        i += 1
    return "".join(out)
