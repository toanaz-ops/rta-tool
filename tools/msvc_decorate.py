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
  - Operator overloads (`operator==`, `operator()`, a conversion operator like
    `operator bool`) and templates (a one-line function template, or a member
    of a class template) are out of this module's scope -- fix round 1 (F2-F4)
    corrected a docstring claim here that these were "already" handled: at
    that point orphan_candidates.py silently dropped or mis-fragmented them
    instead. It now classifies them UNCHECKABLE via
    `orphan_shapes.unfragmentable_shape` and the class-template scope check in
    `cpp_scopes.in_template_scope_by_line`, BEFORE this module is ever asked
    for a fragment -- the fragment this module builds either cannot be formed
    at all (an operator's real decorated name uses a special code this module
    does not implement, e.g. `??8` for `operator==`) or depends on
    instantiation arguments no source-level read can know.
  - Destructors are excluded differently again: orphan_candidates.py drops
    them BEFORE they ever become a candidate (see
    `resolve_qualified`/`test_out_of_line_destructor_is_not_a_candidate` in
    the test suite) -- they are never offered to this module at all, as
    UNCHECKABLE or otherwise, because a destructor's reachability is not a
    meaningful question to ask: every destructible object's destructor is
    implicitly used by the language, not by anything this tool could see as a
    "caller".
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
