# NOTES

Written 2026-09-16 by the tree-wide objectives pass. Read this before working in ProtoCore. Paths
are relative to the repository root unless stated otherwise.

ProtoCore has the largest tool surface of the six public repositories and the weakest build
documentation. Both are addressed below.

## 1. Where the tools are, and where they go

`repotools.toml:26` already states `tools = "tools"`, so this repository satisfies objective 5's
top-level name. 256 files live under `tools/`:

| path | what it is |
| --- | --- |
| `tools/TOOLS.md` | the inventory page |
| `tools/ci_tooling/` | `assets/ build/ check/ coverage/ generate/ lib/ sonar/` plus its own README |
| `tools/crypto/` | nine vector and key generators |
| `tools/dev_env/` | `codemask.py`, `move_code.py`, `nsmap.py`, `nodeset.py`, `uatree.py`, `opcua_conform.py`, `pimpl_test.py`, plus `pimpl_bench/` and `listener_queue/` |
| `tools/git-hooks/` | `pre-commit`, `post-commit`, `add_cspell_words.py`, `merge_dependabot.sh` |
| `tools/psram/` | `rebuild_arduino_core_psram.sh` and a README |
| `tools/repotools/` | the fetched toolkit tree: `code/`, `lib/`, `media_tools/` |
| loose at `tools/` | `harness.py`, `findroot.py`, `include_footprint.py`, `pid_tune.py` |

What objective 5 changes here:

- The four loose scripts at the root of `tools/` get a named subdirectory each, so every tool
  belongs to a set and the promotion path upstream is a rename. `harness.py` is the most important
  of the four — it generates `test/CMakeLists.txt` and every `src/**/CMakeLists.txt` — and burying
  it is the wrong answer; give it a directory whose name says it is the build driver.
- `tools/repotools/` is the **fetched** tree. It is not edited in place. That is the gatekeeper
  rule: a repository fetches and does not edit a fetched file. The `[fetch]` table at
  `repotools.toml:90-104` and `repotools.lock` (31 lines) are what record it.
- `tools/ci_tooling/check/` and `tools/ci_tooling/generate/` are the largest set of tools in this
  tree that have no upstream equivalent. They are the primary promotion candidates for objective 3.
  `docs/TOOLKIT.md` already records why `code/code_maint` is deliberately not fetched: three of its
  eight files collide with a ProtoCore tool that is load-bearing or better, and a set is fetched
  whole.

## 2. The dependency standard, and what ProtoCore must change

The decided standard is **submodule-first**. A directory taken from another repository is taken as
a git submodule, narrowed with `--no-cone` sparse patterns recorded in `.gitmodules` as
`sparsePaths`, and the bootstrap asserts the post-narrow shape.

### `include/MMgr` is not a submodule today, whatever `.gitmodules` says

`.gitmodules` declares one entry:

```
[submodule "include/MMgr"]
	path = include/MMgr
	url = https://github.com/dstroy0/MMgr.git
```

Measured state: `git submodule status` prints a clean space prefix over
`ebc417397e6f64797369dd1515f1da50aa0e1122`, and `git ls-tree HEAD include/MMgr` records
`160000 commit ebc4173...`. But `include/MMgr/.git` is a **directory**, not a gitfile, and
`.git/modules` does not exist in this repository at all. It is a nested clone wearing a submodule's
name, and a fresh clone of ProtoCore will not reproduce what is on this disk.

### Blocked by MMgr's two histories — this is step 0 for the tree

`ebc4173` is on MMgr's branch `opt`, not `main`. MMgr `main` is `69d7cb8`, and
`git merge-base ebc4173 main` returns nothing: unrelated histories. idemIP pins `37c65c4`, also on
`opt`, committed 37 minutes after ProtoCore's. Re-pointing this submodule is a **port, not a SHA
swap**: the `opt`-era tree has no `include/` directory and a different `src/` module set.

### ProtoCore does not build against MMgr, and needs exactly one file

Measured: nothing in the build consumes `include/MMgr`. The only substantive reference in the tree
is `src/config/platform/ns_contract.h:11`, which cites
`include/MMgr/src/mmgr_compiler_directives.h` as the shape `src/mmgr/` was taken from, and names
the three pieces it took: `PROTOCORE_NS`, `PROTOCORE_NS_LAYOUT`, `PROTOCORE_CALL`.

So the roughly 400-file phantom checkout becomes one file. Non-cone sparse patterns are full
gitignore syntax and match per path rather than per directory, so the narrow is:

```
git config -f .gitmodules submodule.include/MMgr.sparsePaths /src/mmgr_compiler_directives.h
```

That makes the `ns_contract.h:11` citation resolve and be checkable instead of aspirational.

Two measured hazards the bootstrap must handle, both of which fail silently at exit 0:

- Without `MSYS2_ARG_CONV_EXCL='*'`, git on this machine records the pattern as
  `C:/Program Files/Git/src/...` and the tree comes out **completely empty**, exit 0, no error.
- A stock `git clone --recurse-submodules` of a sparse superproject applies no narrowing and gets
  the **superset**.

After applying sparse-checkout, count the entries under the mount, compare against the
`sparsePaths` count, and refuse naming both numbers on any mismatch.

### The cross-repository pin check

ProtoCore and idemIP are the two consumers of the same MMgr URL and they have already diverged by
37 minutes on a branch no remote serves. One tree-level script reads every `.gitmodules` and
compares gitlink SHAs across consumers of the same URL. It makes the disagreement visible; it does
not prevent it.

### repotools

`repotools.toml:90-104` fetches `code/code_verify`, `media_tools` and `lib/numerics` into
`tools/repotools`, recorded in `repotools.lock`. Under the standard the fetch mechanism, the lock
and the stamp subsystem retire. The toolkit edge itself carries the documented exception, because
repo_tools is a private repository and a public `.gitmodules` naming it is strictly more disclosure
than today. Until that exception is written, leave `tools/repotools/` as it stands and do not edit
a fetched file in place.

Note that `lib/numerics` is named explicitly at line 103 because a fetch of `media_tools` alone did
not pull it: `build_sound_view.py` and `build_sweep_view.py` both open with
`from numerics import dsp` and died on `ModuleNotFoundError`. Whatever replaces the fetch has to
carry that dependency the same way.

## 3. Backgrounded agents may commit

Backgrounded agents are permitted to commit in this repository. The message is **terse and names
category, subject and type only** — for example `docs build bugfix`. No body, no attribution
trailer, no prose.

Precondition, specific to ProtoCore: this repository ships `tools/git-hooks/pre-commit` and
`post-commit` and documents "Install once per clone: `git config core.hooksPath tools/git-hooks`".
`core.hooksPath` is **unset** in this clone. That hook is the only thing running clang-format on
staged C, black, prettier, shfmt, cspell capture, test-matrix regeneration, the `src/` banned
construct hard gate, per-file version stamping and the `.bumpversion.cfg` registration gate. A
backgrounded commit today runs none of them. Install the hook before relying on commit authority.

Stage explicitly with `git add <named paths>`. Do not use `git commit -a` or bare `git add .`.

## 4. The build script this repository needs

Objectives 8 and 18: one script that builds all of `src/` and `examples/` and walks a user through
it, with a worked invocation in the README.

**ProtoCore is the worst of the ten repositories on this and should be fixed first.** Measured:
`README.md` is 286 lines and contains **zero build commands**. Grepping it for `cmake`, `pio run`,
`idf.py`, `arduino-cli` or `harness.py` returns nothing. The "Quick start" section at line 113 is a
verbatim copy of `examples/Foundation/Basic/Basic.ino` — a source listing, not a command.

The real entry points, none of which the README names:

- **Host suite.** `python tools/harness.py build cmake` generates `test/CMakeLists.txt`, and
  `python tools/harness.py build modules` generates every `src/**/CMakeLists.txt`. Then
  `cmake -S test -B build/native`, `cmake --build build/native -j`,
  `ctest --test-dir build/native`. The configure command appears nowhere in the repository except
  inside the generated file's own header comment at `test/CMakeLists.txt:12-14`. **A fresh clone
  must run the generator before it can configure anything, and nothing outside that comment says
  so.**
- **PlatformIO.** `pio run`; `platformio.ini` is at the root.
- **ESP-IDF.** `idf.py build`, with the `-DPROTOCORE_WITH_ARDUINO=OFF` variant already documented
  at `CMakeLists.txt:24-26`. The root `CMakeLists.txt` is ESP-IDF component registration only — it
  contains `idf_component_register` and no `project()` call, and it says so in its own first line.

`examples/` has nine families: `Foundation`, `Drivers`, `L4-Transport`, `L5-Session`,
`L6-Presentation`, `L7-Application`, `Peripherals`, `esp-idf`. `repotools.toml:24` declares
`examples = ["examples"]`. The only thing that compiles them is
`tools/ci_tooling/check/compile_examples.sh`, which the README never names.

**Security item, separate from the buildability gap.** That script is unrunnable by a new user and
carries a real credential as a default. Lines 23-31:

```
REMOTE_HOST="${PROTOCORE_RPI_HOST:-192.168.1.223}"
REMOTE_USER="${PROTOCORE_RPI_USER:-dstroy0}"
SSID="${PROTOCORE_TEST_SSID:-q_6}"
PASS="${PROTOCORE_TEST_PASS:-12345678!}"
```

This is a public repository. Split the script: a local-only default mode needing nothing but
`arduino-cli`, and `--remote` as the opt-in. Remove the SSID and password entirely and fail with a
named message when they are unset rather than falling back to a live credential. The script's own
header records 130 of 152 sketches unbuildable against 5756 passing host tests, so it is the only
thing standing between `examples/` and rot.

## 5. Where the skills live

`D:/git_project/repos/owned/private/repo_tools/skills`

Five skills are there now: `code-python`, `code-shell`, `code-verify`, `docs-readme`,
`repotools-workflow`. None is installed anywhere a harness discovers skills, and four of five
declare a frontmatter `name` differing from their directory name, so cross-references between them
cite identifiers nobody can type. Objective 6 rebuckets and rewrites them; an install mechanism
lands first.

ProtoCore is C11 throughout `src/` (`repotools.toml:32` selects `.c` and `.h` as a parser and not a
scope). The two skills that bind that work, `code-c11` and `code-comments`, live only at
`C:/Users/Douglas/.claude/skills/` today and are moving into the repository path above.

`code-shell` names `ci-workflows` at `~/.claude/skills/code-ci/SKILL.md` as a dependency. That
skill does not exist anywhere. It matters here more than in the other five repositories, because
`tools/ci_tooling/` is the largest CI surface in the tree and nothing governs how it is written.

## 6. The prose gates are becoming pre-commit gates

Objective 12 adds an AI-word detector and objective 13 adds a British-English ban, and both become
pre-commit gates in every repository. British spellings are banned in comments, docstrings and
description blocks unless the subject itself is British.

ProtoCore is the one repository where these must **not** be added by naming a gate in
`repotools.toml`. Lines 60-66 leave `[hooks] gates = []` empty on purpose, with the reason stated:
`tools/git-hooks/pre-commit` already does nine things the toolkit's `gates.py` does not, and naming
a gate would run a second, thinner hook beside it. Add the two prose gates **into that hook**.

Scope when they land is `[prose] roots` at line 58: `README.md` and `docs`. That is narrower than
this repository's real prose surface — `tools/TOOLS.md`, `tools/ci_tooling/README.md`,
`tools/psram/README.md`, `tools/dev_env/pimpl_bench/README.md` and `docs/TOOLKIT.md` all make the
kind of claim a page makes. Widen the root list in the same commit that installs the gates, and
compare the file count at the foot of the run before and after, since that count is the only thing
distinguishing a root that moved from a root that emptied.

The reference implementations are in anchor_sift at `maint/prose/ai_detect.py` and
`maint/prose/english_gate.py`. They are promoted upstream and reach ProtoCore from there.

## 7. The theory layout

Every repository, public and private, gets exactly two directories for written work:

- `workbook/` — locally authored, top level, a sibling of `src/`, `test/` and `tools/`. The book
  about **this** repository: its engine, its results, its reproduction instructions.
- `theory/` — wholly a git dependency of the `theory_bucket` repository. Nothing is authored here.
  Everything inside arrived from the theory_bucket remote, and the whole directory can be deleted
  and re-fetched without losing work. Each upstream book is pulled individually, a consumer
  takes only the books it asks for.

All theory, from every public and private repository, is authored upstream in theory_bucket.

**What ProtoCore has today: neither.** There is no `theory/`, no `theory_bucket/` and no
`workbook/`. `docs/` holds reference documentation about the library — the features pages, the
hardware reference, `TOOLKIT.md`, the interop matrix — much of it generated by
`tools/ci_tooling/generate/`. That is repository documentation, not a book, and it stays in `docs/`.

What that means going forward:

- Do not create `theory/` until there is a book to mount. An empty mount point is a liability under
  the refusal rule, which enumerates the mount and refuses on any file it cannot account for.
- ProtoCore has real workbook material and nowhere to put it: the PIMPL bench under
  `tools/dev_env/pimpl_bench/`, the PSRAM rebuild work, the include-footprint and feature-budget
  measurements. Those are results about this tree and belong in `workbook/` at the root, not in
  `docs/` beside generated reference pages and not upstream.
- Anything general enough to be a book — the layering argument, the namespace contract that
  `ns_contract.h` states — is authored upstream in theory_bucket and returns through a `theory/`
  submodule if ProtoCore ever wants it in its own tree.
