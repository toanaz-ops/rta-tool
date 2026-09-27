# orphan_check.py -- known limits

Linked from `tools/orphan_check.py`'s own module docstring (moved out from
there, lane-end LOW batch round 1, LOW V3: the docstring's own growth pushed
the file to 427 lines, over the project's 400-line cap). Read that
docstring first for what the tool checks and how (UNCHECKABLE categories,
NOT IN TARGET, TEST HOOK) -- this file only adds the two entries below,
which are prose, not code, and do not need to live in the file the linter
itself reads.

## TEST HOOK reached through a wrapper (lane-end LOW batch, 2026-09-27)

The TEST HOOK check searches `app/tests*` for the CANDIDATE's own literal
name. A private `*ForTest` method reached only through a same-class friend
(the `MainComponentTestAccess` pattern: `component.paneComponentForTest()`
is called from nowhere but `MainComponentTestAccess::pane(component)`,
itself declared in `app/src/MainComponentTestAccess.h`, not under
`app/tests*`) fails this search on BOTH names -- the `*ForTest` method's own
name never appears verbatim under `app/tests*` (only the wrapper's does, and
the wrapper's name does not end in `ForTest`), and the wrapper's name does
not match `TEST_HOOK_NAME_RE` at all. Both halves of the pair report as
ordinary failing orphans (`analysisThreadForTest`/`paneComponentForTest`/
`channelRoleTableForTest`/`routingMatrixForTest` and their
`MainComponentTestAccess.h` wrappers `analysisThread`/`channelRoleTable`/
`pane`/`routingMatrix`, all current, 2026-09-27 lane-end LOW batch triage).

Renaming either half does not fix this: the indirection itself, not either
name's spelling, is what a literal-text search cannot see through. A
CLASS-level (not method-level) test-only-access declaration -- resolving the
`friend struct MainComponentTestAccess` line itself, rather than searching
for uses of what it grants access to -- would be a structurally different
check, not a fix to this one; left as a known limitation rather than
attempted here. A HUMAN judgement call, not this tool's: is the accessor a
legitimate test seam (keep) or genuinely unwired (delete)? See
`memory/a-component-with-no-production-caller-is-not-shipped.md` for the
cost of guessing wrong on that question.

## Five UNCHECKABLE shapes never written down (PR #39 round 2 verifier, F-B/F-C/F-D/F-F/F-G)

Graded LOW and left as known limits without ever being written down in the
tool that has them -- lane-end LOW batch, 2026-09-27, traced each one
against the CURRENT `orphan_shapes.py`/`orphan_candidates.py` by reading the
matching order, not by re-running the round-2 fixture; re-derive from a real
construction before relying on the specific mechanism named below.

- **F-B**: a template with a default argument or a variadic pack, declared
  and defined on the same line (`template <class T = int> void f() {}`) --
  reads as UNCHECKABLE either way, since `template <...>` on the same or
  previous line is what `orphan_candidates.py` checks BEFORE looking at the
  rest of the shape at all; the default argument/pack never gets a chance to
  matter.
- **F-C**: a class template's member defined OUT OF LINE in a `.cpp`
  (`template <class T> void Foo<T>::bar() {}`) -- `in_template_class`
  (`orphan_candidates.py`) is decided from `cpp_scopes`' header-declaration
  tracking of the ENCLOSING class, which does not care whether this
  particular member's OWN definition is inline or out-of-line -- reads as
  UNCHECKABLE (member of a class template) either way.
- **F-D**: a `decltype(auto)` or trailing-`->`-return-type declaration --
  `TYPED_DECL_RE`'s prefix class matches `decltype` as an ordinary
  return-type token, so this reads as an ORDINARY candidate, not as
  UNCHECKABLE or silently dropped; round 2 graded it LOW because the one
  real instance checked did not produce a WRONG verdict, not because the
  tool exempts the shape.
- **F-F**: `friend class X;` (a friend CLASS declaration, not a friend
  FUNCTION) -- `_LEADING_KEYWORDS_RE` strips a leading `friend` keyword
  before either name-extraction regex runs, leaving `class X` as the
  flattened head; neither `_BARE_NAME_RE` nor `_TYPED_DECL_RE` matches a
  bare `class` keyword followed by a name and `;` with no `(` at all, so
  this produces NEITHER a candidate NOR an UNCHECKABLE entry -- silently
  invisible to both lists, the same non-failing outcome as UNCHECKABLE, just
  with no line printed for it.
- **F-G**: a 2-line out-of-line `.cpp` definition where only the NAME line
  was added by this branch (`Foo::bar(` on line N, unchanged; `) { ... }` on
  line N+1, added) -- `_flatten_declaration` joins forward from wherever a
  candidate's own start line is, and `git_diff_added_lines` is what decides
  a line is a candidate start at all; a declaration whose START line
  predates this branch's diff was never ADDED by it, so it correctly never
  becomes a candidate for this run, not a false UNCHECKABLE.

None of these five needed a code change to stay correct on the evidence
read; they are written down here so a future reader does not re-read the
same three files from scratch to re-derive the same five shapes, the exact
cost this note exists to avoid (project CLAUDE.md, "read narrow" -- search
`memory/` and this note before re-deriving).
