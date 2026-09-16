# The shared toolkit, and what ProtoCore takes from it

**Purpose:** State which maintenance tools ProtoCore fetches from the shared toolkit, which it keeps as its own, and why each one falls where it does.
**Scope:** `repotools.toml`, `repotools.lock`, everything under `tools/repotools/`, and the ProtoCore tools those would otherwise shadow.

ProtoCore is a consumer of the `repo_tools` toolkit. One copy of each general maintenance tool lives
there; ProtoCore states its own settings in `repotools.toml` at the root, and that file being present
is the marker every fetched tool walks up to find.

```
repotools config     what repotools.toml resolves to, after the toolkit defaults merge under it
repotools fetch      copy the named sets into tools/repotools, and write repotools.lock
repotools check      report a fetched tool edited here, or one the toolkit has moved past
```

## The toolkit is upstream, so tools/repotools is not ours to edit

Every file under `tools/repotools/` carries a `repotools-stamp:` line naming its source and the digest
of what was installed, and `repotools.lock` holds the same digest. Editing one here produces a change
that exists in this tree alone and is discarded by the next fetch, and `repotools check` reports it as
breaking. A change worth keeping is promoted back with `repotools adopt` and every other repository
then fetches it.

That has one consequence worth stating, because it looks like an oversight otherwise. Fetched code is
exempt from ProtoCore's own formatting gates: `tools/repotools/` is listed in `.prettierignore` and in
`force-exclude` under `[tool.black]` in `pyproject.toml`. The two contracts refuse each other
otherwise: the format gate would rewrite a fetched file and the lock would then report all of them as
edited, on every commit. This is the same reasoning `.prettierignore` already gives for `include/MMgr/`
and that `.github/workflows/format-code.yml` gives for `managed_components`: code from another
repository, with its own standards, that a commit here cannot reach.

`force-exclude` rather than `extend-exclude` because CI runs `git ls-files '*.py' | xargs black
--check` and the pre-commit hook passes staged paths. Black applies `extend-exclude` only to files it
discovers by walking a directory; a path handed to it explicitly is skipped only by `force-exclude`.

## What ProtoCore fetches

| set                | what it brings                                                                                                         | why it is not ours                                                                          |
| ------------------ | ---------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| `lib/repotools`    | the spine: `boot`, `root`, `config`, `findings`, `fetch`, `shape`, `cli`                                               | travels with every fetch whether or not it is named                                         |
| `code/code_verify` | `verify_asm.py`: compiles a case, disassembles the object, and requires the instructions the source was written to use | nothing in it knows what the project is; ProtoCore gates instruction selection nowhere else |
| `media_tools`      | six viewers, `settings.py`, two page templates, `src2png.py`                                                           | they read bytes, tables and audio; none knows what the data is about                        |
| `lib/numerics`     | `dsp.py`, `precision.py`                                                                                               | standard-library transforms; `media_tools` imports them                                     |

`lib/numerics` is named explicitly in `[fetch] sets`. The toolkit declares it as a dependency of
`media_tools`, but a fetch of `media_tools` alone did not pull it: `build_sound_view.py` and
`build_sweep_view.py` both open with `from numerics import dsp` and died on `ModuleNotFoundError`.
Naming it is what makes the viewers run. Reported upstream.

## What ProtoCore keeps, and why

These answer questions only ProtoCore asks. Promoting any of them would version-lock the toolkit to
this tree.

| ours                                                                                                                            | why it is specific                                                                                                                                                                         |
| ------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `tools/ci_tooling/`                                                                                                             | the twelve guards, the generators, coverage and Sonar. Tied to `src/`'s layout, `docs/SYMBOLS.md`, `docs/SRCBANNED.md`, the module graph, `.bumpversion.cfg` and the PlatformIO env matrix |
| `tools/dev_env/goldenize.py`, `shapeaudit.py`, `nsmap.py`, `pimpl.py`, `funnel.py`                                              | the conversion toolchain for one shape: an entry taking `uint8_t *restrict work`, operands under `<X>V`, state carved out of `PROTOCORE_<X>_BORROW` at asserted offsets                    |
| `tools/dev_env/readclean.py`                                                                                                    | see below: same filename as a toolkit tool, different tool                                                                                                                                 |
| `tools/dev_env/nodeset.py`, `uatree.py`, `uaspace.py`, `opcua_conform.py`                                                       | OPC UA NodeSet readers                                                                                                                                                                     |
| `tools/crypto/`                                                                                                                 | this project's test vectors and keys                                                                                                                                                       |
| `tools/harness.py`, `tools/findroot.py`                                                                                         | ProtoCore's entry point. A tool stays runnable by path; the harness is what makes it findable                                                                                              |
| `tools/git-hooks/`                                                                                                              | nine steps the toolkit's driver does not do, listed under `[hooks]` in `repotools.toml`                                                                                                    |
| `tools/include_footprint.py`, `pid_tune.py`, `dev_env/capsweep.py`, `dev_env/listener_queue/`, `dev_env/pimpl_bench/`, `psram/` | measurements of this firmware                                                                                                                                                              |

## Why `code/code_maint` is not fetched

It is the set with the most overlap and it cannot be taken yet, because a set is fetched whole and
three of its eight files collide.

**`readclean.py` is two different tools sharing a filename.** ProtoCore's blinds a module while
keeping the shape's grammar: `Sha256Ns Sha256Vars Sha256V Sha256Ctx` become `X1Ns X1Vars X1V X1Ctx`,
so the four spellings of one module stay visibly one module, and `PROTOCORE_SHA256_BORROW` becomes
`PROTOCORE_X1_BORROW` while `work`, `proto_bool` and `static_assert` are left alone. The toolkit's
renames to flat `fn1` / `var2` across four languages and knows nothing of the shape. Fetching would
put the generic one beside the specific one under the same name. ProtoCore's is also the copy the
toolkit's deleted `readclean_test.py` was written against.

**`nsconv.py` and `codemask.py` are imported, not run.** `goldenize.py` imports them as siblings from
`tools/dev_env/`. Fetching the set installs a second copy at `tools/repotools/code/code_maint/` that
nothing imports, which is duplication rather than the end of it.

The other five are wanted. `dedup.py` has no ProtoCore equivalent, and the toolkit's
`yank_includes.py` is better than ours: it reads its keep list, its never-yank list and its manifest
path from a Config, where ProtoCore's holds them in its body. Those values are already stated under
`[yank]` in `repotools.toml`, so that half of the migration is done and waiting.

Taking the set needs one of: per-file fetch in the toolkit, a split of `code_maint` into smaller sets,
or ProtoCore renaming its `readclean.py` and repointing `goldenize.py` at fetched modules. That is a
decision for the toolkit owner and it is recorded here rather than settled.

## Checking this stayed true

```
repotools check                     every fetched file still matches the lock
python tools/harness.py ci check    ProtoCore's own guards, which the fetch does not touch
```

**Author:** dstroy0 (Douglas Quigg) <dquigg123@gmail.com>
**Date:** 2026-09-09
