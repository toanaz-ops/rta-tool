# A plan that names both the check and the mutation has still not run them against each other

Lane L7-ALIGN, Wave 3b, 2026-09-16. The station-3 plan's task I is unusually
careful. Row I4 writes out the exact grep that proves no optimiser reached the
crossover surface —

    grep -rniE "maximi[sz]e|optimi[sz]e|minimi[sz]e ?(ripple|sum)|argmax.*sum" \
        app/src/view/CrossoverSurface.* app/src/measure/AlignmentWizard.*

— and, two bullets later, names the mutation that must make it fail: *add a
`bestDelayForLoudestSum()` helper to the model.* Both halves were written by
the same planner in the same pass, against a file that did not exist yet.

Applied to the shipped file, the mutation leaves the check **green**. The
identifier contains none of those words. The only line that does is the
header's own comment explaining why the move is forbidden — so a raw grep flags
the argument against the objective and misses the objective.

That is the third time this shape has been found in one lane, and the first two
were also acceptance rows rather than code:

- **D6** (Wave 3a): the word-grep for `linkwitz|butterworth|topolog` over
  `core/` was already false on the untouched tree — eight matches, all prose
  and includes.
- **I3**: the "no pass/fail declaration" regex carried a leading `[A-Za-z_]*`,
  so the word matched *inside* a longer identifier and
  `az::ui::AzLookAndFeel lookAndFeel;` hit.
- **I4**: this one.

## The rule

**Run the prescribed mutation against the prescribed check before you believe
either.** A plan can state a red step and a green step that have never met; the
cost of finding out is one build, and the cost of not finding out is a guard
that has quietly stopped guarding while its row stays ticked.

And when the answer is "the word list cannot do this", the fix is usually to
stop enumerating what is FORBIDDEN and start enumerating what is ALLOWED. The
shipped I4 lists the callables and requires the set to match exactly, so
`bestDelayForLoudestSum()` fails at
`REQUIRE( declared.size() == allowed.size() )` whatever it is called — the
property no word list ever had. A whitelist is only affordable where the
surface is small and deliberate, which is exactly where the strongest claims
are made.

## The coda, and it is the same lesson again: a whitelist has a SCOPE

The first version of that whitelist scanned only between `class CrossoverSurface`
and the first column-0 `};`. PR #9's verifier declared the identical forbidden
move as a **free function** in the same header, after the class:

```cpp
[[nodiscard]] double bestDelayForLoudestSum(const CrossoverSurface& surface) noexcept;
```

Green. Exported from the header, reachable by every includer, and missed by the
original word-grep too — so that shape was caught by *nothing*, while the PR body
and this file both said "it cannot be evaded by naming".

So the rule has a second half. **State the scope of a structural check in the
same breath as its claim, and make the claim no wider.** "Any objective declared
as a member of `CrossoverSurface` goes red" was true; "any objective goes red"
was not, and the gap between them is where the next one lives. The fix is the
boring one: widen the scan to the whole header and to the `.cpp`'s non-member
definitions, then re-run the mutation in its new form and watch `22 == 21`.

Notice the shape of this coda. A check written to catch an overreaching claim
overreached in exactly the same way, one level down, and was caught by exactly
the same method — somebody applying a mutation the author had not thought of.
That is not an argument against the method. It is the argument for it.

**And it happened a third time, one level down again.** The widened scan skipped
any line whose `=` precedes its `(` — a deliberate rule, so that
`const double k = 20.0 * std::log10(2.0);` is not read as declaring `log10`. A
namespace-scope callable bound to an *object* is exactly that shape:

```cpp
inline constexpr auto bestDelayForLoudestSum =
    [](const CrossoverSurface&, double step) noexcept { return step * 2.0; };
```

Exported from the header, callable by every includer, and the whole 649-test OFF
suite stayed green. The round-2 verifier also recorded the near-miss that makes
the point sharpest: their first attempt gave the lambda a body containing a call,
and the scan DID go red — but the printed enumeration showed `binwidthhz`, not
`bestdelayforloudestsum`. It went red for the wrong reason, by accident. Take the
parenthesis out of the body and the red disappears.

So the final rule, after three turns: **write the scope INTO the artefact, not
into the prose around it** — the shipped scan's comment now lists the two forms
it reads and names what it does not cover (macros, another TU), so the next
reader does not have to infer the boundary from a claim. And when a mutation goes
red, read WHY: a red for the wrong reason is a green with extra steps.

## The smaller trap that came with it

A structural scan for "this member is assigned only in these functions"
searched for `member_ + " ="`. Every **read** spelled `member_ == Source::Main`
begins with that string, so the first run reported five offenders that were the
code doing precisely what the rule requires. An assignment scan has to exclude
`==` explicitly — and its own counts (writes seen, functions seen) have to be
asserted, or a scan that stops matching passes vacuously instead of going red.

## What to do with the finding

Amend the plan **in place**, at the row, the way D6 was amended: strike the
mechanism, say what was measured, name what shipped instead, and state whether
the DECISION RECORD changed. In all three cases here the record was right and
only the acceptance mechanism was wrong — which is worth writing down, because
a future reader who finds a row contradicted will otherwise wonder whether the
whole ruling moved.

## Related

- `memory/a-fixture-can-be-too-well-behaved-to-fail.md` — the same question
  (*what wrong implementation would make this red?*) asked of a fixture instead
  of a guard.
- `memory/a-threshold-read-off-a-grid-is-that-grids-floor.md` — the other
  recurring shape: a number that is a property of the thing that measured it.
- `docs/plans/2026-09-15-L7-align-impl-plan.md` rows D6 and I4 carry both
  amendments and the measurements behind them.
