# The tools under tools/repotools

**Purpose:** State what the tools under `tools/repotools/` do, and how each one finds its settings.
**Scope:** `repotools.toml` and everything under `tools/repotools/`.

ProtoCore owns these tools. They follow the same gates as every other tool in `tools/`: `black`,
`prettier`, the version stamp, the prose ratchet and the `SonarCloud` analysis.

```
repotools sets      the tool sets under tools/repotools, and how many files each holds
repotools list      every tool, by set, with the first line of its docstring
repotools config    what repotools.toml resolves to, after the defaults merge under it
```

## The sets

| set                | what it holds                                                                                                   |
| ------------------ | --------------------------------------------------------------------------------------------------------------- |
| `lib/repotools`    | the shared layer: `boot`, `root`, `config`, `findings`, `shape` and `cli`                                       |
| `lib/numerics`     | `dsp.py` and `precision.py`, transforms with the standard library only                                          |
| `code/code_verify` | `verify_asm.py`: it compiles a case, disassembles the object, and requires the instructions the source must use |
| `media_tools`      | six tools that show data as a page, `settings.py`, two page templates and `src2png.py`                          |

The page tools in `media_tools` import `lib/numerics`.

## How a tool finds its settings

Each runnable tool opens with the lines that `tools/repotools/lib/repotools/boot.py` shows. These
lines walk up to the `lib/` directory and put it on the import path. The tool then reads
`repotools.toml` at the root of the repository. Each setting has a default in
`tools/repotools/lib/repotools/defaults.toml`, and `repotools.toml` states only where ProtoCore differs.

## Checking this stays true

```
python tools/repotools/lib/repotools/cli.py config    the settings that the tools read
python tools/harness.py ci check                      ProtoCore's own guards
```
