# ci_tooling - the repo's own tooling

Everything that **gates** or **generates** this repository lives here. If CI can fail
because of it, or if it writes into a tracked file, it belongs in this tree.

This document exists because its absence caused a bug. Three generators already
solved the prettier collision (below) with `prettier-ignore` fences; a fourth was
written later, did not know, and invented a second mechanism that silently disabled
the very drift detection it was meant to provide. Nothing was written down, so the
next author could only guess. **Read this before adding tooling.**

## Layout

| Directory   | Holds                                                |
| ----------- | ---------------------------------------------------- |
| `generate/` | Writes into tracked files. See `--check` note below. |
| `check/`    | Fails CI on a violation. Writes nothing.             |
|             | `check_examples.py` reads sketches AND README code.  |
| `lib/`      | Shared primitives. Imported, never run.              |
| `coverage/` | Coverage planning, running, and report merging.      |
| `sonar/`    | SonarCloud compile-database and scan plumbing.       |
| `assets/`   | Renders images and packs web assets.                 |
| `build/`    | Compiler wrappers (ccache).                          |

### What does NOT live here

Domain tooling stays with its domain, because splitting a cohesive family across two
roots is worse than either root alone:

- `test/gen_test_envs.py`, `gen_test_readme.py`, `run_tests.sh` - test infrastructure
- `tools/gen_crypto_vectors.py` and the other vector generators - test **data**
- `web_assets/wizard/*` - the web asset build
- `tools/git-hooks/` - git hooks
- `penetration_testing/`, `performance_benching/`, `reverse_engineering/` - separate domains

## The contract for a generator

Use [`lib/doc_region.py`](lib/doc_region.py). Do not hand-roll these four steps.

```python
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import doc_region as dr  # noqa: E402  (path set above)

ROOT = dr.repo_root(__file__)
DOC = os.path.join(ROOT, "docs/MY_DOC.md")
REGION = dr.Region(DOC, "MY BLOCK", dr.tool_id(__file__))


def main():
    body = render()  # no markers, no prettier fences - dr.apply adds both
    return dr.apply(DOC, {REGION: body}, check="--check" in sys.argv)
```

Then add the region to the target document once, by hand:

```markdown
<!-- BEGIN GENERATED MY BLOCK (ci_tooling/generate/gen_my_thing.py) -->
<!-- END GENERATED MY BLOCK -->
```

and wire `--check` into a workflow so staleness fails the build.

### `--check` is one flag, everywhere, and it never writes

Every generator that writes a tracked file supports `--check`, spelled exactly that
way, and every one of them is wired into a workflow. Two took a positional `check`
instead, so the documented spelling silently exited 0 - a gate that cannot fail is
worse than no gate, because the build turns green and the doc still lies.

`--check` is a **dry run**: it computes what it would write, compares, reports, and
touches nothing. Never prune, remove, or write a side artifact on the check path.

The only exception is `gen_dep_graph.py`. Rebuilding `test/dep_graph.json` requires a
full compile pass, so checking it on every pull request would cost more than the drift
it would catch; its argparse rejects `--check` loudly rather than pretending. The two
subcommand tools (`feature_budget`, `example_footprints`) are driven by build logs and
print usage rather than accepting a bare flag.

### Four rules, each paid for

**1. Never compute the repo root by counting `dirname` levels.** Use
`dr.repo_root(__file__)`, which searches upward for a marker. Counting breaks the
moment a script moves: it once pointed an asset generator **above the repo root**,
and it silently broke six scripts during the move into this directory.

**2. Generated regions are wrapped in `prettier-ignore`, and `--check` compares
bytes.** prettier runs _after_ generators and re-pads markdown tables, so a naive
byte comparison reports STALE forever. There are two ways out and only one is
correct: fence the region so prettier leaves it alone (exact compare still catches
real drift), or collapse whitespace before comparing (blind to the drift the check
exists to find). `dr.apply` fences. Do not add a `norm()`.

**3. A generator that cannot produce trustworthy output must fail, not emit.** If a
source file no longer contains what the generator scrapes, exit non-zero. Emitting a
plausible-but-wrong block is worse than emitting nothing, because it looks reviewed.

**4. Assert that a text substitution fired.** `str.replace` with no match returns the
input unchanged, so the generator "succeeds" while shipping the stale text. Use
`dr.sub_once(text, old, new, what)`.

## The contract for a checker

Exit non-zero with the offending file and line on stdout/stderr. Write nothing.

Scope must match the documented rule. [`docs/SRCBANNED.md`](../docs/SRCBANNED.md)
rule 6 says "Applies to `examples/` too" and even prints the command, but
`check_src_banned.py` scanned only `src/` - so 86 example READMEs drifted onto the
banned API while every sketch stayed clean. **If a rule names its scope in prose, the
checker reads that whole scope, or the prose is wrong.**

Do not silence an alarm to make a checker pass. If output is a false positive, prove
it and add a justified in-source suppression; if it is real, fix it or log it in
[`docs/BUGS.md`](../docs/BUGS.md).

### Gating a rule the tree does not satisfy yet

`check_symbols.py` enforces [`docs/SYMBOLS.md`](../docs/SYMBOLS.md), which the tree
does not fully comply with: the prefix sweep is still open, so a bare run reports
~1300 violations. A checker in that position has two bad options - stay out of CI until
the sweep lands (so nothing stops the count growing), or fail the build immediately
(so it gets disabled). It takes a third: `--baseline` records the known set, and
`--check` fails only on violations **not** in it.

The count can then ratchet down and never up. When a sweep lands, re-run `--baseline`
so the new floor is recorded. Do not add to a baseline to make a build pass - that is
silencing an alarm with extra steps.

Writing this checker is also what disproved the rule it enforces: implementing
`PC_<PATH>_H` showed 283 of 373 headers could not satisfy it alongside the
31-character limit, and SYMBOLS.md s4 was amended to `PROTOCORE_<FILE>_H` as a result.
A law worth enforcing is worth testing against the tree before trusting it.

## Running everything locally

```sh
for g in ci_tooling/generate/gen_*.py; do python3 "$g" --check || echo "STALE: $g"; done
python3 ci_tooling/check/check_docs.py
python3 ci_tooling/check/check_owned_context.py
python3 ci_tooling/check/check_src_banned.py --all
python3 ci_tooling/check/check_test_coverage.py
python3 ci_tooling/check/check_examples.py
```

`check_test_coverage.py` asserts that every `src/` translation unit outside
`board_drivers/` is named by some env's `build_src_filter` in `test/test_matrix.json`.
Being compiled does not count: 24 envs carry no filter and build the whole tree, so
compilation alone would mark every file covered while testing none of it deliberately.
It ratchets like the other guards - `--list` prints the current gaps, `--save` re-records
the floor after a suite lands.

## Status

Every region-writing generator goes through `lib/doc_region.py`; no `apply_to`, no
hand-rolled marker search, and no second answer to the prettier collision remains in
this tree. Verify with:

```sh
grep -rl "def apply_to\|prettier-ignore-start" ci_tooling/generate/   # expect no matches
```
