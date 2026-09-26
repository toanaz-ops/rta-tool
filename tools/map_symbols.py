# SPDX-License-Identifier: AGPL-3.0-or-later
"""map_symbols.py -- parse an MSVC linker /MAP file's symbol table.

orphan_check.py v2 (process/orphan-check-linkmap) asks the LINKER whether a
function is reachable from the app's entry point, rather than grepping source
text for its spelling. `/OPT:REF` discards a COMDAT nothing references,
transitively from the entry point; what survives into the final image is
listed in the "Publics by Value" and "Static symbols" tables of the .map file
`/MAP` produces. A name absent from either table was discarded -- provably
unreachable, not merely "no text match found".

WHAT IS PARSED. Both tables share one line shape:

    0001:00001234       ?enableSplLogging@AnalysisThread@measure@rta@@QEAAXV?$span@$$CBM@std@@@Z 0000000140002234 rtatool_analysis.obj

    <seg>:<offset>      <decorated-or-plain name>          <rva>            <lib:object>

distinguished from the earlier "Start Length Name Class" section table, whose
second column is a hex LENGTH ending in the literal `H` (e.g. `0002ac30H`),
never a symbol name -- a symbol name starts with `?` (C++ decoration) or a
letter/underscore (a plain C-linkage name), never a bare hex digit. That
single constraint is enough to separate the two without caring about section
headers or blank-line boundaries, which vary slightly between MSVC versions.

WHAT THIS DOES NOT DO. It does not decode a name's signature/return-type
encoding, only recognises where a name TOKEN sits in a line and returns it
whole. Matching a candidate against these names is `msvc_decorate.py`'s job
(a decorated-name FRAGMENT, checked as a prefix -- see its own docstring for
why a fragment and not the exact full name).
"""

from __future__ import annotations

import re

# group(1): the symbol name itself. Anchored so the name must START with `?`
# (decorated) or a letter/underscore (plain) -- this is what excludes the
# segment-table's `NNNNNNNNH` length column, which starts with a hex digit.
_SYMBOL_LINE_RE = re.compile(
    r"^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+(\?\S+|[A-Za-z_]\S*)\s+[0-9A-Fa-f]{8,16}\s+\S"
)


def extract_decorated_names(map_text: str) -> set[str]:
    """Every symbol name in the "Publics by Value" and "Static symbols"
    tables of an MSVC .map file's text. Order and section are not preserved
    -- orphan_check.py only ever asks "is this fragment a prefix of any name
    here", a set membership question.
    """
    names: set[str] = set()
    for line in map_text.splitlines():
        m = _SYMBOL_LINE_RE.match(line)
        if m:
            names.add(m.group(1))
    return names


def is_fragment_live(fragment: str, decorated_names: set[str]) -> bool:
    """True if `fragment` (from msvc_decorate.py) is a PREFIX of some name in
    `decorated_names`. A fragment ends right where the qualified-name portion
    of a real decorated name ends (`...Class@ns@@`) and the real name
    continues immediately with calling-convention/parameter encoding this
    module never reconstructs -- so prefix, not equality, is the correct
    test; equality would never match anything.
    """
    return any(name.startswith(fragment) for name in decorated_names)
