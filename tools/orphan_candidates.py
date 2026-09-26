# SPDX-License-Identifier: AGPL-3.0-or-later
"""orphan_candidates.py -- functions/methods/constructors declared on lines
this branch ADDED under app/src/**, for orphan_check.py v2's linker-
reachability check.

The declaration-shape regex heuristic (what counts as a candidate, what is
UNCHECKABLE and why) lives in `orphan_shapes.py` -- split out so both files
stay under this repo's 400-line cap. This module is the ORCHESTRATION loop:
walk each added line, resolve its enclosing scope via `cpp_scopes`, flatten
its declaration, classify it, and produce the two lists `orphan_check.py`
asks for.
"""

from __future__ import annotations

import re

from cpp_scopes import (
    ANONYMOUS,
    declaration_level_by_line,
    in_template_scope_by_line,
    scopes_by_line,
)
from cpp_text import strip_comments
from difflines import AddedLine
from orphan_shapes import (
    BARE_NAME_RE,
    BARE_QUALIFIED_RE,
    CLASS_RE,
    NON_DECLARATION_LINE_RE,
    TEMPLATE_LINE_RE,
    TYPED_DECL_RE,
    Candidate,
    Uncheckable,
    CONTROL_KEYWORDS,
    display_name,
    flatten_declaration,
    is_uncheckable_text,
    resolve_qualified,
    strip_attributes,
    strip_leading_keywords,
    unfragmentable_shape,
)

__all__ = ["Candidate", "Uncheckable", "find_candidates"]


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
    file_in_template: dict[str, list[bool]] = {}
    seen_classes: dict[str, tuple[int, list[str]]] = {}  # class name -> (lineno, enclosing)
    classes_with_a_member: set[str] = set()

    for added in added_lines:
        first = added.text.strip()
        if not first:
            continue
        first_word = re.split(r"[\s(]", first, maxsplit=1)[0]
        if (
            first_word in CONTROL_KEYWORDS
            or first.startswith(("//", "#"))
            or NON_DECLARATION_LINE_RE.match(first)
        ):
            continue

        if added.path not in file_lines:
            text = strip_comments(read_file(added.path))
            file_lines[added.path] = text.splitlines()
            file_scopes[added.path] = scopes_by_line(text)
            file_decl_level[added.path] = declaration_level_by_line(text)
            file_in_template[added.path] = in_template_scope_by_line(text)
        lines = file_lines[added.path]
        scopes = file_scopes[added.path]
        decl_level = file_decl_level[added.path]
        in_template_arr = file_in_template[added.path]
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

        class_m = CLASS_RE.match(lines[idx])
        if class_m:
            seen_classes[class_m.group(1)] = (added.lineno, list(enclosing))
            continue

        flattened = flatten_declaration(lines, idx)
        if flattened is None:
            continue

        is_template = bool(
            (idx > 0 and TEMPLATE_LINE_RE.match(lines[idx - 1]))
            or TEMPLATE_LINE_RE.match(lines[idx])
        )
        in_template_class = added.lineno < len(in_template_arr) and in_template_arr[added.lineno]
        attr_stripped = strip_attributes(flattened)

        # F2-F4: a shape this tool cannot fragment at all -- checked BEFORE
        # name extraction is even attempted, so it is reported as
        # UNCHECKABLE with a best-effort display name rather than either
        # silently vanishing (a symbolic operator's name has no `\w`
        # characters for TYPED_DECL_RE/BARE_NAME_RE to capture) or being
        # handed a wrong fragment (`operator()` giving `?operator@...`,
        # `explicit operator bool` giving `?bool@...`).
        shape_reason = unfragmentable_shape(attr_stripped)
        if shape_reason:
            uncheckable.append(
                Uncheckable(added.path, added.lineno, display_name(attr_stripped), shape_reason)
            )
            continue

        head = strip_leading_keywords(attr_stripped)

        m = TYPED_DECL_RE.match(head)
        is_ctor = False
        name: str | None = None
        full_enclosing = enclosing
        if m:
            raw_name = m.group(1)
            if raw_name.startswith("std::") or raw_name.split("::")[-1] in CONTROL_KEYWORDS:
                raw_name = None
            if raw_name is not None:
                resolved = resolve_qualified(raw_name, enclosing)
                if resolved is None:
                    continue  # a destructor (~Class) -- not in this tool's candidate set
                name, full_enclosing, is_ctor = resolved
        else:
            m2 = BARE_NAME_RE.match(head)
            m3 = BARE_QUALIFIED_RE.match(head)
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
                resolved = resolve_qualified(m3.group(1), enclosing)
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
        elif in_template_class:
            reason = "member of a class template (fragment depends on instantiation)"
        else:
            reason = is_uncheckable_text(flattened)
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
