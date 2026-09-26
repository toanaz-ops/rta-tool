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

WHAT THIS DOES NOT HANDLE (`strip_comments` only -- `strip_string_and_char_
literals` below handles raw strings and digit separators, added fix round 3):
trigraphs, and a line-comment whose `//` is itself escaped across a line
continuation backslash. Neither appears in this codebase's own sources, so
the extra complexity to parse them was not added; if one is ever introduced,
the affected line degrades to being treated as unstripped code rather than
silently corrupting the scan, which is the safe direction for a haystack
(worst case: a would-be reference site is missed inside a real comment and a
real orphan is FALSE-negatived away by that one line, which is exactly the
same class of imprecision this whole tool already accepts elsewhere and
documents in orphan_check.py's own docstring).

CORRECTION (fix round 3, LOW-2): this docstring used to also claim "zero raw
string literals outside external/" -- false. `R"JS(` and `R"CSS(` are real,
committed literals in `app/src/export/SplReportScript.h` and
`SplReportStyle.h`, and two more raw strings appear under `app/tests/`. That
claim was never load-bearing for `strip_comments` itself (which still does
not specially parse a raw string -- an embedded `"` inside one can still
confuse ITS naive string-skip, same as before), but it WAS the premise fix
round 2 built `strip_string_and_char_literals` on, which is why that
function gained real raw-string handling this round rather than repeating
the false premise.
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


_RAW_STRING_PREFIXES = ("u8R", "uR", "UR", "LR", "R")


def _raw_string_delimiter(text: str, quote_index: int) -> str | None:
    """If the `"` at `quote_index` opens a raw string (`R"delim(...)delim"`,
    optionally prefixed `u8R`/`uR`/`UR`/`LR`), return `delim` (possibly
    empty); otherwise None. The prefix letter(s) must be their own token --
    preceded by a non-identifier character or start of text -- so an
    ordinary identifier that happens to end in `R` right before an unrelated
    `"` is never mistaken for one.
    """
    for prefix in _RAW_STRING_PREFIXES:
        start = quote_index - len(prefix)
        if start < 0 or text[start:quote_index] != prefix:
            continue
        before = text[start - 1] if start > 0 else ""
        if before.isalnum() or before == "_":
            continue
        paren = text.find("(", quote_index + 1)
        newline = text.find("\n", quote_index + 1)
        if paren == -1 or (newline != -1 and newline < paren):
            continue  # no `(` before end of line -- not really a raw string
        delimiter = text[quote_index + 1 : paren]
        return delimiter
    return None


def strip_string_and_char_literals(text: str) -> str:
    """Return `text` with the CONTENTS of every string/char literal removed
    (the delimiting quotes stay, so what remains still reads as `""`/`''`)
    -- fix round 2 (verifier), F-I, for a caller that searches for a real
    CODE reference to a name, not a name that merely appears inside a string
    (a log message, a display label). `orphan_targets.test_hook_is_referenced`
    is the caller: without this, a log line like
    `"calling enableSplLoggingForTest now"` counted as a reference to the
    hook even though nothing actually calls it.

    Two fixes from fix round 3 (verifier), MEDIUM-2:
    - A C++14 DIGIT SEPARATOR (`1'700'000'000`) is not a char literal --
      `'` only opens one when the PRECEDING character is not alphanumeric
      and not itself `'` (a real char literal is never preceded by a digit
      or letter; `1'000` and `0xFF'FF'FF` are). The original version opened
      a "char literal" at the first `'`, closed it at the second (silently
      dropping the digits between as if they were a literal's contents), and
      an ODD number of separators in the file left the scan still "inside a
      literal" for everything after -- erasing the rest of the file,
      including any real `*ForTest` call site further down (the concrete
      case: `app/tests/test_spl_session_folder_name.cpp:19`'s
      `1'700'000'000`).
    - A RAW STRING (`R"(...)"`, `R"JS(...)JS"`) is now recognised via
      `_raw_string_delimiter` and its whole span is skipped as one unit
      (its `)delim"` terminator located directly, not by scanning for the
      next bare `"`), so an embedded `"` inside the raw string's own body
      never prematurely closes it and desynchronises everything after.

    Deliberately does not preserve a newline swallowed by a backslash
    line-continuation inside an ordinary (non-raw) literal -- that shape
    does not appear in this codebase, and the caller here only tests for a
    name's PRESENCE, never reads a line number back out of this result.
    """
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"':
            delimiter = _raw_string_delimiter(text, i)
            if delimiter is not None:
                end_marker = ")" + delimiter + '"'
                paren = text.index("(", i + 1)
                end = text.find(end_marker, paren + 1)
                out.append('"')
                if end == -1:
                    i = n
                else:
                    i = end + len(end_marker)
                    out.append('"')
                continue
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
        if c == "'":
            prev = text[i - 1] if i > 0 else ""
            if prev.isalnum() or prev == "'":
                # A C++14 digit separator (`1'700'000'000`, `0xFF'FF'FF`),
                # never a char literal's OWN opening quote -- pass it
                # through unchanged rather than starting a literal scan.
                out.append(c)
                i += 1
                continue
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
