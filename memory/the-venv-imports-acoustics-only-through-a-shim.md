# The venv imports python-acoustics only through a shim

Found 2026-08-30, EP06 review round, while verifying literature provenance for
lane L4b against the local source instead of GitHub.

`import acoustics` **dies** in this repo's venv: scipy 1.18.1 removed
`scipy.special.sph_harm`, and the package `__init__` of acoustics 0.2.6 imports
a module that uses it. The only reason `tools/gen_bands.py` works is the shim at
`tools/gen_bands.py:32-39`, installed *before* the import:

```python
if not hasattr(scipy.special, "sph_harm"):
    scipy.special.sph_harm = getattr(scipy.special, "sph_harm_y", None)
```

Two consequences, one obvious and one quiet:

1. **Every future gen script that imports acoustics must carry the shim** (or
   import through a helper that does). A naive import fails with
   `ImportError: cannot import name 'sph_harm'`.
2. **The shim makes imports succeed but makes actual `sph_harm` calls silently
   wrong** — `sph_harm_y` changed the argument convention, not just the name.
   Harmless for the paths this repo uses (IEC 61260 band edges, `t60_impulse`:
   bandpass + reversed cumsum, no spherical harmonics anywhere). Poison for
   anything that genuinely evaluates spherical harmonics. The shim is a door
   propped open, not a repair.

Related: **pyrato is NOT installed** in the venv, although `CLAUDE.md:30` names
it as one of the golden-generator dependencies. Only `acoustics 0.2.6` is
present. Any decision record that makes pyrato a reference implementation must
first install and version-pin it — as of 2026-08-30 a record citing "our pyrato"
cites nothing.

## The rule

Before a record names a Python package as a reference implementation, run one
import of it in the actual venv and paste the result. A package that is
installed, importable, and version-pinned is a source; anything less is a name.
