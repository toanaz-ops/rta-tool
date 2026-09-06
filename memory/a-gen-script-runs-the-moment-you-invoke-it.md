# A gen script runs the moment you invoke it

2026-08-30, EP06 review round. The reviewer ran `gen_bands.py --help` to check
usage. The script has **no argparse** — it ignored the flag, ran its whole
pipeline, and overwrote `core/tests/golden/bands.txt` in the working tree of the
main checkout.

The accident was harmless for exactly one reason: the generator is
deterministic, so the regenerated file came out **byte-identical** —
`git status` clean, `git diff` empty. The mistake thereby produced something
useful by accident: an end-to-end determinism check of the bands generator
against its committed golden, on numpy 2.5.2 / scipy 1.18.1.

Both halves are worth keeping:

- **None of the `tools/gen_*.py` scripts should be assumed to parse arguments.**
  They are run-on-invoke: the moment Python executes the file, it writes into
  `core/tests/golden/`. Read the script's argument handling before passing any
  flag, and treat every invocation as a write.
- **Deterministic golden generation is what turned a mutation into a no-op.**
  If a future generator picks up hidden state (timestamps, unseeded randomness,
  platform-dependent float paths), the same accident becomes a silent golden
  corruption that a later diff attributes to the wrong session. The
  byte-identical regen property is load-bearing; test for it deliberately, not
  accidentally.

## The rule

Treat `tools/gen_*.py` as a write to the repository, not as a program with a
CLI. If you only want to know what it does, read it.

## Correction, 2026-09-06 (L6b station 1)

`tools/gen_mtw.py` (L3) already follows this rule: real `argparse`, `--out`,
`--help` exits before any write — the only one of the eight `gen_*.py` that
does, verified by `grep -l argparse tools/*.py`. The seven older scripts still
run on import. A new generator copies `gen_mtw.py`, not the older seven.
