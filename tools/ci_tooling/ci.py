#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""One entry point for every CI operation, so the whole surface is visible in one place.

  ci.py help [command]        every command's help in one call, or one command's
  ci.py list                  every operation this runs, grouped
  ci.py gen [name...]         run generators (default: the workflow's set, in order)
  ci.py gen --check [name...] assert the tracked files already match (what CI gates on)
  ci.py check [name...]       run guards (default: the workflow's set)
  ci.py baseline [name...]    record a ratcheted guard's violations as its new floor
  ci.py cov <sub>             coverage tooling: run / base / map / plan / dedupe
  ci.py fmt [--check]         clang-format, Prettier, black and shfmt over the whole tree
  ci.py sonar <sub>           compile database: merge / accept-style

Each name is dispatched to the module that already implements it, with argv set the way the
workflow sets it, so behavior is identical to invoking the module directly. Adding a module here is
what makes it discoverable; a tool nobody can find gets written a second time.

Exit code is the first non-zero any step returned, and every step runs even after one fails, so a
single invocation reports the whole picture rather than the first thing to break.
"""

import argparse
import os
import runpy
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

# The generators the Feature Tables workflow runs, in its order: later ones read what earlier ones
# wrote, so the sequence is part of the contract.
GEN_DEFAULT = [
    "feature_tables",
    "readme_sections",
    "configurator",
    "flag_deps",
    "api_flow",
    "build_opt",
    "examples",
    "nav_groups",
    "tools_inventory",
]

GEN = {
    "feature_tables": "generate.gen_feature_tables",
    "readme_sections": "generate.gen_readme_sections",
    "readme_intro": "generate.gen_readme_intro",
    "configurator": "generate.gen_configurator",
    "flag_deps": "generate.gen_flag_deps",
    "api_flow": "generate.gen_api_flow",
    "build_opt": "generate.gen_build_opt",
    "examples": "generate.gen_examples",
    "nav_groups": "generate.gen_nav_groups",
    "tools_inventory": "generate.gen_tools_inventory",
    "features_page": "generate.gen_features_page",
    "features_tree": "generate.gen_features_tree",
    "hardware_ref": "generate.gen_hardware_ref",
    "interop_matrix": "generate.gen_interop_matrix",
    "dep_graph": "generate.gen_dep_graph",
    "changelog": "generate.decorate_changelog",
    "footprints": "generate.example_footprints",
    "feature_budget": "generate.feature_budget",
}

# The guards the Format Code workflow runs. src_banned takes --all there.
CHECK_DEFAULT = [
    "owned_context",
    "test_coverage",
    "src_banned",
    "duplicate_symbols",
    "frame_specs",
    "coverage_xml",
    "version_sites",
    "version_stamps",
    "test_matrix",
    "module_graph",
]

CHECK = {
    "owned_context": "check.check_owned_context",
    "test_coverage": "check.check_test_coverage",
    "src_banned": "check.check_src_banned",
    "duplicate_symbols": "check.check_duplicate_symbols",
    "frame_specs": "check.check_frame_specs",
    "coverage_xml": "check.check_coverage_xml",
    "version_sites": "check.check_version_sites",
    "version_stamps": "check.stamp_version",
    "symbols": "check.check_symbols",
    "null_ctx": "check.check_null_ctx",
    "docs": "check.check_docs",
    "comments": "check.check_comments",
    "examples": "check.check_examples",
    "layering": "check.check_layering",
    "module_graph": "check.check_module_graph",
    "test_matrix": "check.check_test_matrix",
}

# Flags a guard needs to match how the workflow invokes it.
CHECK_ARGS = {"src_banned": ["--all"], "version_stamps": ["--check"]}

# The ratcheted guards: each records its current violations as a floor and then fails only on new
# ones. Raising a floor is a deliberate act - it is how a mid-flight conversion keeps its gate
# useful instead of red on every run - so it is its own command rather than a flag on `check`.
BASELINE_ARGS = {
    "owned_context": ["--baseline"],
    "src_banned": ["--all", "--baseline"],
    "symbols": ["--baseline"],
    "null_ctx": ["--baseline"],
    "comments": ["--save"],
    "test_coverage": ["--save"],
    "test_matrix": ["--baseline"],
}
BASELINE_DEFAULT = ["owned_context", "src_banned"]

COV = {
    "run": "coverage.covrun",
    "base": "coverage.covbase",
    "map": "coverage.covmap",
    "plan": "coverage.covplan",
    "dedupe": "coverage.dedupe_sonar_cov",
}

SONAR = {
    "merge": "sonar.merge_compiledb",
    "accept-style": "sonar.accept_style_conflicts",
}

# Everything Prettier owns. The exclusions live in .prettierignore, which Prettier applies itself,
# so this glob states the file types and nothing else.
PRETTIER_GLOB = "**/*.{md,json,yml,yaml,css,html,js,cjs,mjs}"

# The git hooks are shell scripts with no extension, so no glob finds them.
HOOKS = ["tools/git-hooks/pre-commit", "tools/git-hooks/post-commit"]

FMT_HELP = """Every formatter the tree has, over every file type it owns.

  clang-format  .c .cpp .h .ino
  Prettier      .md .json .yml .yaml .css .html .js .cjs .mjs, minus .prettierignore
  black         .py
  shfmt         .sh and the git hooks, which are shell with no extension

.prettierignore carries the exclusions and states why each one is there: the two matrices are
spliced rather than re-rendered, the ratchet baselines are written by json.dump, and the web assets
are embedded into a C blob byte for byte, so reformatting one changes the blob and its size.

shfmt is not in any package the repo installs. `winget install mvdan.shfmt` on Windows,
`go install mvdan.cc/sh/v3/cmd/shfmt@latest` anywhere; without it that step reports skip and the
shell scripts go unformatted."""


def run_module(suffix, argv):
    """Run one ci_tooling module as if it were invoked directly. Returns its exit code."""
    mod = "tools.ci_tooling." + suffix
    saved = sys.argv
    sys.argv = [mod.split(".")[-1]] + list(argv)
    try:
        runpy.run_module(mod, run_name="__main__")
        return 0
    except SystemExit as e:
        if e.code is None:
            return 0
        if isinstance(e.code, int):
            return e.code
        print(e.code, file=sys.stderr)
        return 1
    finally:
        sys.argv = saved


def dispatch(table, names, extra=(), per_name_args=None):
    """Run each named module, reporting as it goes. Returns the first non-zero code."""
    rc_all = 0
    for name in names:
        if name not in table:
            print("unknown: %s (try `ci.py list`)" % name, file=sys.stderr)
            rc_all = rc_all or 2
            continue
        argv = list(per_name_args.get(name, [])) if per_name_args else []
        rc = run_module(table[name], argv + list(extra))
        print("%s %s" % ("ok  " if rc == 0 else "FAIL", name))
        rc_all = rc_all or rc
    return rc_all


def cmd_list(a):
    for title, table, default in (
        ("generators (ci.py gen)", GEN, GEN_DEFAULT),
        ("guards (ci.py check)", CHECK, CHECK_DEFAULT),
        ("coverage (ci.py cov)", COV, None),
        ("sonar (ci.py sonar)", SONAR, None),
    ):
        print("\n%s" % title)
        for name in sorted(table):
            star = " *" if default and name in default else ""
            print("  %-18s %s%s" % (name, table[name], star))
    print("\n  * in the default set, run when no name is given")
    return 0


def _subcommands(parser):
    """{name: subparser} for a parser's one subparser group."""
    for action in parser._actions:
        if isinstance(action, argparse._SubParsersAction):
            return action.choices
    return {}


def cmd_help(a):
    """Every command's own help in one call, or one command's.

    `-h` prints usage one level at a time, so reading the whole surface means invoking it once per
    subcommand. This prints all of it, then the module tables `list` prints, so a single call is
    the whole tool.
    """
    parser = build_parser()
    subs = _subcommands(parser)
    if a.command:
        if a.command not in subs:
            print("no such command: %s (try `ci.py help`)" % a.command, file=sys.stderr)
            return 2
        subs[a.command].print_help()
        return 0
    print(__doc__.strip())
    for name in sorted(subs):
        print("\n" + "-" * 78)
        print("### ci.py %s" % name)
        print(subs[name].format_help().strip())
    print("\n" + "-" * 78)
    return cmd_list(a)


def cmd_gen(a):
    return dispatch(GEN, a.names or GEN_DEFAULT, ["--check"] if a.check else [])


def cmd_check(a):
    return dispatch(CHECK, a.names or CHECK_DEFAULT, per_name_args=CHECK_ARGS)


def cmd_baseline(a):
    names = a.names or BASELINE_DEFAULT
    unknown = [n for n in names if n not in BASELINE_ARGS]
    if unknown:
        print(
            "not a ratcheted guard: %s (ratcheted: %s)" % (" ".join(unknown), " ".join(sorted(BASELINE_ARGS))),
            file=sys.stderr,
        )
        return 2
    return dispatch(CHECK, names, per_name_args=BASELINE_ARGS)


def cmd_cov(a):
    return dispatch(COV, [a.sub], a.rest)


def cmd_sonar(a):
    return dispatch(SONAR, [a.sub], a.rest)


def source_files():
    """The tree's own C sources: what the Format Code workflow's find selects."""
    out = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if not d.startswith(".") and d != "managed_components"]
        for fn in filenames:
            if fn == "build_opt.h" or not fn.endswith((".c", ".cpp", ".h", ".ino")):
                continue
            out.append(os.path.join(dirpath, fn))
    return out


def tracked(*globs):
    """Files git tracks matching any glob, repo-relative."""
    out = subprocess.run(["git", "ls-files"] + list(globs), cwd=ROOT, capture_output=True, text=True).stdout
    return [os.path.relpath(f, ROOT) if os.path.isabs(f) else f for f in out.split()]


def cmd_fmt(a):
    """Every formatter the tree has, over every file type it owns, in check or write mode.

    Four languages, four tools, one call:

      clang-format  .c .cpp .h .ino
      Prettier      .md .json .yml .yaml .css .html .js .cjs .mjs, minus .prettierignore
      black         .py
      shfmt         .sh and the git hooks, which are shell with no extension

    .prettierignore carries the exclusions and states why each one is there: the two matrices are
    spliced rather than re-rendered, the ratchet baselines are written by json.dump, and the web
    assets are embedded into a C blob byte for byte.
    """
    rc_all = 0
    cf = shutil.which("clang-format")
    if not cf:
        print("FAIL clang-format not on PATH")
        rc_all = 1
    else:
        files = source_files()
        args = ["--style=file"] + (["--dry-run", "--Werror"] if a.check else ["-i"])
        # One process per batch: the whole list overflows a command line on Windows.
        rc = 0
        for i in range(0, len(files), 200):
            p = subprocess.run([cf] + args + files[i : i + 200], cwd=ROOT)
            rc = rc or p.returncode
        print("%s clang-format (%d files)" % ("ok  " if rc == 0 else "FAIL", len(files)))
        rc_all = rc_all or rc

    npx = shutil.which("npx")
    if not npx:
        print("skip Prettier (npx not on PATH)")
    else:
        p = subprocess.run(
            [npx, "prettier@3.9.6", "--check" if a.check else "--write", PRETTIER_GLOB],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        print("%s Prettier" % ("ok  " if p.returncode == 0 else "FAIL"))
        if p.returncode != 0:
            print(p.stdout.strip()[-2000:])
        rc_all = rc_all or p.returncode

    py = tracked("*.py")
    if py:
        p = subprocess.run(
            [sys.executable, "-m", "black"] + (["--check", "--diff"] if a.check else []) + py,
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        print("%s black (%d files)" % ("ok  " if p.returncode == 0 else "FAIL", len(py)))
        if p.returncode != 0:
            print((p.stdout + p.stderr).strip()[-2000:])
        rc_all = rc_all or p.returncode

    sh = tracked("*.sh") + [h for h in HOOKS if os.path.exists(os.path.join(ROOT, h))]
    fmt = shutil.which("shfmt")
    if not fmt:
        print("skip shfmt (not on PATH: winget install mvdan.shfmt, or go install mvdan.cc/sh/v3/cmd/shfmt@latest)")
    elif sh:
        # -i 4 is what 17 of the 24 scripts already use; -ci indents case arms, which every case
        # block in the tree already does. shfmt has no line limit, so nothing rewraps.
        args = ["-i", "4", "-ci"] + (["-d"] if a.check else ["-w"])
        p = subprocess.run([fmt] + args + sh, cwd=ROOT, capture_output=True, text=True)
        # -d prints the diff and exits 1 on any difference, so the code is the verdict.
        print("%s shfmt (%d files)" % ("ok  " if p.returncode == 0 else "FAIL", len(sh)))
        if p.returncode != 0:
            print((p.stdout + p.stderr).strip()[-2000:])
        rc_all = rc_all or p.returncode
    return rc_all


def build_parser():
    ap = argparse.ArgumentParser(
        prog="ci.py", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = ap.add_subparsers(dest="group", required=True)

    sub.add_parser("list", help="every operation, grouped").set_defaults(fn=cmd_list)

    p = sub.add_parser("help", help="every command's help in one call, or one command's")
    p.add_argument("command", nargs="?")
    p.set_defaults(fn=cmd_help)

    p = sub.add_parser("gen", help="run generators")
    p.add_argument("names", nargs="*")
    p.add_argument("--check", action="store_true", help="assert the tracked files already match")
    p.set_defaults(fn=cmd_gen)

    p = sub.add_parser("check", help="run guards")
    p.add_argument("names", nargs="*")
    p.set_defaults(fn=cmd_check)

    p = sub.add_parser("baseline", help="record a ratcheted guard's current violations as its floor")
    p.add_argument("names", nargs="*")
    p.set_defaults(fn=cmd_baseline)

    p = sub.add_parser("cov", help="coverage tooling")
    p.add_argument("sub", choices=sorted(COV))
    p.add_argument("rest", nargs=argparse.REMAINDER)
    p.set_defaults(fn=cmd_cov)

    p = sub.add_parser("sonar", help="compile database tooling")
    p.add_argument("sub", choices=sorted(SONAR))
    p.add_argument("rest", nargs=argparse.REMAINDER)
    p.set_defaults(fn=cmd_sonar)

    p = sub.add_parser(
        "fmt",
        help="clang-format, Prettier, black and shfmt over the whole tree",
        description=FMT_HELP,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--check", action="store_true", help="report violations instead of rewriting")
    p.set_defaults(fn=cmd_fmt)
    return ap


def main():
    a = build_parser().parse_args()
    return a.fn(a)


if __name__ == "__main__":
    if ROOT not in sys.path:
        sys.path.insert(0, ROOT)
    sys.exit(main())
