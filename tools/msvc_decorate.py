# SPDX-License-Identifier: AGPL-3.0-or-later
"""msvc_decorate.py -- build an MSVC decorated-name FRAGMENT for a candidate
symbol, given the namespace/class scope enclosing its declaration.

WHY A FRAGMENT, NOT THE FULL DECORATED NAME. MSVC's C++ name mangling scheme
encodes a name as:

    ?<name-or-special-code>@<enclosing-scope-innermost-first>@@<encoding>

where <encoding> covers calling convention, access/storage qualifiers, return
type and parameter types -- reconstructing that from a bare declaration line
would mean re-implementing MSVC's own type-mangling rules, including every
standard-library specialisation (`std::span<const float>` does not mangle the
way it prints). The qualified-NAME portion, up to and including the `@@` that
closes it, does NOT depend on the signature at all -- only on the symbol's own
name and the chain of namespaces/classes enclosing its declaration, which
`cpp_scopes.py` already recovers from the source. That portion is exactly
this module's job, and it is a legitimate MSVC decorated name in its own
right up to the point it stops -- `map_symbols.is_fragment_live` checks it as
a PREFIX of the real, fully-encoded names a .map file lists.

EXAMPLES.
  - Free function `rta::measure::sessionFolderName(...)`:
    enclosing=["measure", "rta"] (innermost first) -> `?sessionFolderName@measure@rta@@`
  - Free function at global scope: enclosing=[] -> `?main@@`
  - Member function `rta::measure::AnalysisThread::enableSplLogging(...)`:
    enclosing=["AnalysisThread", "measure", "rta"] -> `?enableSplLogging@AnalysisThread@measure@rta@@`
  - Constructor of the same class: is_constructor=True, enclosing=["AnalysisThread", "measure", "rta"]
    -> `??0AnalysisThread@measure@rta@@` (MSVC's special-member code `0`
    replaces the repeated class name after the leading `?`; constructors
    never carry their own name in the mangled form).

WHAT THIS DOES NOT HANDLE (documented, not silently wrong):
  - Overload sets: a fragment matches ANY overload of a name in that scope,
    since the encoding that would distinguish them is exactly what a fragment
    omits. This is the deliberately safe direction -- one live overload is
    enough to call the whole candidate live, never a false orphan report over
    an overload the map-reader could not resolve.
  - Templates, operator overloads (`operator==`), and destructors are out of
    this module's scope -- orphan_check.py classifies those UNCHECKABLE
    before ever asking this module for a fragment.
"""

from __future__ import annotations


def decorated_fragment(name: str, enclosing: list[str], *, is_constructor: bool = False) -> str:
    """`enclosing` is innermost-first (immediate class, then its enclosing
    namespaces, outermost last) -- the same order `cpp_scopes.py` returns.
    For a free function, `enclosing` holds only namespaces (or is empty at
    global scope); for a member function or constructor, `enclosing[0]` is
    its own class.
    """
    if is_constructor:
        if not enclosing:
            raise ValueError("a constructor's enclosing scope must include its own class")
        return "??0" + "@".join(enclosing) + "@@"
    chain = [name] + enclosing
    return "?" + "@".join(chain) + "@@"
