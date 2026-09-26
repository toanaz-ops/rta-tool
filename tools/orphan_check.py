#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""orphan_check.py -- no component without a production caller, decided by
the LINKER, not by grep.

WHY V2. v1 (see git history: process/tooling PR #38) answered "is this
symbol referenced" with a text-grep reference search, and after two rounds of
patching it still had four MEDIUM defects, all from one root: a regex has no
notion of REACHABILITY, only of a spelling appearing somewhere in a file.
It flagged calls it should have credited (a qualified static call, a
same-file private helper called from a sibling method) and credited
references it should have discarded (a doc comment merely naming a symbol, a
mutual reference cycle between two components neither one anything else
calls). Patching each hole traded it for another; see that PR's fix-round-2
body for the specific v1 false positives and negatives this replaces.

v2 asks a stronger, structurally different question: is this function
present in the FINAL LINKED IMAGE, once the linker has discarded everything
transitively unreachable from the app's own entry point? `app/CMakeLists.txt`
builds `rtatool` under `RTA_ORPHAN_LINKMAP=ON` with:
  /Ob0          no inlining -- a candidate must survive as its own
                out-of-line, separately addressable symbol.
  /Gy           function-level linking (COMDATs) -- what /OPT:REF discards.
  /OPT:REF      discard COMDATs nothing references, transitively from main.
  /OPT:NOICF    keep two byte-identical functions as two SEPARATE symbols in
                the map (ICF's default folding would erase one's name).
  /MAP          emit the map this tool reads.
This makes a cycle of two unwired components discard TOGETHER (neither is
"referenced" by anything the entry point reaches, so neither survives, unlike
v1's reference-counting which kept each alive via the other); a qualified
static call resolves exactly like any other reachable call (the linker does
not care how a caller spelled the callee); and a same-file private helper is
live the moment its caller is.

WHAT IS CHECKED. Every function, method and constructor DECLARED on a line
this branch ADDED under app/src/** (headers and .cpp -- see
orphan_candidates.py for exactly how "declared" is read, including a
signature that wraps across lines). A candidate whose own decorated-name
FRAGMENT (msvc_decorate.py) is not a prefix of any name in the map is an
orphan; a class is live iff at least one of its own members is. Some
candidates cannot be asked this question at all -- `= default`/`= delete`
members, `constexpr`/`consteval` functions, anonymous-namespace symbols
(internal linkage answers a different question), templates (instantiation
is not tracked), and a pure-data aggregate with no function to check
reachability through -- these are UNCHECKABLE, reported separately, and
never cause a non-zero exit by themselves.

EXIT CODES. 0 nothing orphaned. 1 orphan(s) found (UNCHECKABLE entries alone
never trigger this). 2 a tool or build error (bad --base, configure/build
failure, no .map file produced).
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from difflines import git_diff_added_lines
from map_symbols import extract_decorated_names, is_fragment_live
from msvc_decorate import decorated_fragment
from orphan_candidates import find_candidates

REPO_ROOT = Path(__file__).resolve().parent.parent


def _configure_and_build(
    source_dir: Path,
    build_dir: Path,
    config: str,
    generator: str | None,
    arch: str | None,
    juce_path: str | None,
) -> int:
    configure_cmd = [
        "cmake", "-S", str(source_dir), "-B", str(build_dir),
        "-DRTA_BUILD_APP=ON", "-DRTA_ORPHAN_LINKMAP=ON",
    ]
    if generator:
        configure_cmd += ["-G", generator]
    if arch:
        configure_cmd += ["-A", arch]
    if juce_path:
        configure_cmd.append(f"-DRTA_JUCE_PATH={juce_path}")
    result = subprocess.run(configure_cmd)
    if result.returncode != 0:
        return result.returncode
    build_cmd = [
        "cmake", "--build", str(build_dir), "--config", config,
        "--target", "rtatool", "--parallel",
    ]
    return subprocess.run(build_cmd).returncode


def _find_map_file(build_dir: Path, config: str) -> Path | None:
    matches = sorted(build_dir.glob(f"app/*_artefacts/{config}/*.map"))
    return matches[0] if matches else None


def _read_file_factory(source_dir: Path):
    def read(path: str) -> str:
        return (source_dir / path).read_text(encoding="utf-8")

    return read


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Flag app/src functions/methods/constructors, declared on lines this "
            "branch added, that the MSVC linker discards as unreachable from "
            "rtatool's entry point."
        )
    )
    parser.add_argument("--base", required=True, help="diff base")
    parser.add_argument(
        "--build-dir", required=True,
        help="CMake build directory for the RTA_ORPHAN_LINKMAP=ON build of rtatool",
    )
    parser.add_argument(
        "--source-dir", default=None,
        help="checked-out tree to diff/build from (default: this repo's own checkout). "
        "For a historical commit, `git worktree add <path> <sha>` it yourself and "
        "pass that path here; remove the worktree afterward.",
    )
    parser.add_argument("--config", default="Release")
    parser.add_argument(
        "--cmake-generator", default=None,
        help='e.g. "Visual Studio 18 2026" -- omit to let CMake auto-detect the way '
        "CI does; pass this explicitly on a machine where a non-MSVC generator "
        "(Ninja+MinGW) would otherwise be picked by default (see "
        "memory/a-misconfigured-build-goes-99-percent-of-the-way.md).",
    )
    parser.add_argument(
        "--cmake-arch", default=None, help="e.g. x64, passed as -A alongside --cmake-generator"
    )
    parser.add_argument(
        "--juce-path", default=None,
        help="existing JUCE 9.0.1 checkout to reuse (RTA_JUCE_PATH); omit to fetch",
    )
    parser.add_argument(
        "--skip-build", action="store_true",
        help="reuse an already-built .map in --build-dir instead of configuring/building "
        "(for testing this tool's own orchestration; never for a real acceptance run)",
    )
    args = parser.parse_args(argv)

    source_dir = Path(args.source_dir).resolve() if args.source_dir else REPO_ROOT
    build_dir = Path(args.build_dir)

    if not args.skip_build:
        rc = _configure_and_build(
            source_dir, build_dir, args.config, args.cmake_generator, args.cmake_arch, args.juce_path
        )
        if rc != 0:
            print(f"orphan_check: configure/build FAILED (exit {rc})", file=sys.stderr)
            return 2

    map_path = _find_map_file(build_dir, args.config)
    if map_path is None:
        print(
            f"orphan_check: no .map file found under {build_dir} -- "
            "did RTA_ORPHAN_LINKMAP=ON actually take effect for this build?",
            file=sys.stderr,
        )
        return 2

    try:
        added_lines = git_diff_added_lines(source_dir, args.base, "HEAD", paths=["app/src"])
    except subprocess.CalledProcessError as exc:
        print(f"orphan_check: git diff failed against base {args.base!r}: {exc}", file=sys.stderr)
        return 2

    decorated_names = extract_decorated_names(map_path.read_text(encoding="utf-8", errors="replace"))
    candidates, uncheckable = find_candidates(added_lines, _read_file_factory(source_dir))

    orphans: dict[str, list[str]] = {}
    for c in candidates:
        fragment = decorated_fragment(c.name, c.enclosing, is_constructor=c.is_constructor)
        if not is_fragment_live(fragment, decorated_names):
            orphans.setdefault(c.path, []).append(c.name)

    if uncheckable:
        print("orphan_check: UNCHECKABLE (never fails; a human call, not the linker's):")
        for u in sorted(uncheckable, key=lambda u: (u.path, u.lineno)):
            where = f"{u.path}:{u.lineno}" if u.path else f"line {u.lineno}"
            print(f"  {where}  {u.name}  ({u.reason})")

    if not orphans:
        print(
            "orphan_check: OK -- every new/changed function, method and constructor "
            "is reachable from rtatool's entry point"
        )
        return 0

    print("orphan_check: found component(s) the linker discarded as unreachable:")
    for path in sorted(orphans):
        print(f"  {path}")
        for name in sorted(set(orphans[path])):
            print(f"    - {name}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
