# Contributing

Thanks for your interest in ProtoCore. This is a zero-heap,
OSI-layered, RFC-faithful network server for the ESP32. The bar for changes is
high because the whole point of the library is predictability: fixed buffers, no
runtime allocation, bounded latency, and behavior that matches the relevant
specification. The notes below keep contributions aligned with that.

## Ground rules

- The project is licensed **AGPL-3.0-or-later**. By contributing you agree your
  contribution is licensed under the same terms.
- Every new source file starts with the standard header:

    ```c
    // Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
    // SPDX-License-Identifier: AGPL-3.0-or-later
    ```

- Be excellent to each other. See the [Code of Conduct](CODE_OF_CONDUCT.md).

## Development environment

- [PlatformIO](https://platformio.org/) is the build system (CLI or the VS Code
  extension). Most logic is host-testable, so you do not need an ESP32 to start.
- Node is only needed for the docs/spelling tooling. After `npm install` you get
  `npm run format`, `npm run format:check`, and `npm run spell`.

## Build and test

The architecture is deliberately split so the logic compiles and runs on your
host machine, separate from the hardware wrappers. There are two paths and one
question selects between them: **does this part have the capability**. A gate in
the core reads a `PROTOCORE_HAS_*` capability and takes the hardware path or the
software one. Nothing in the core asks which build this is, and nothing is
spelled `ARDUINO` - naming one vendor's toolchain put every non-Espressif target
on the software path.

A detected vendor answers for its own silicon in
[core_setup/board_profiles/protocore_platform.h](../core_setup/board_profiles/protocore_platform.h);
a build with no vendor answers 0 and turns a capability on with
`-DPROTOCORE_HAS_<X>=1`, which is how a test env drives a hardware path on a machine
that has no hardware. Which vendor a build speaks to is `core_setup/`'s job, the
backends a suite stands up live in `test/mocks/`, and no vendor header appears in
the core.

- **Native tests** (fast, no hardware): every feature has a `native_*` test
  environment. Run one with:

    ```sh
    pio test -e <native_env>      # pick the env for the area you touched
    ```

    The `native_*` env blocks in [platformio.ini](../platformio.ini) are
    **generated** from a single table, [test/test_matrix.json](../test/test_matrix.json).
    To add or change a test env, edit that table (each entry keeps its own flags,
    src filter, and test dirs - per-feature isolation is the point) and regenerate:

    ```sh
    python3 -m test.gen_test_envs            # rewrite the env blocks in platformio.ini
    python3 -m test.gen_test_envs --check    # CI: fail if the ini is out of date
    ```

    Do not hand-edit the generated region of `platformio.ini`. The full suite
    (`test/run_tests.sh`) auto-discovers every native env, so a new entry is run
    and reported automatically.

- **Compile for hardware:**

    ```sh
    pio run -e esp32dev
    ```

- **Build an example.** Examples are gated behind feature flags, and `pio ci`
  only applies a sketch's `#define` to the sketch translation unit, not to the
  separately compiled library. Pass the feature flags as `build_flags` so the
  library compiles with them too:

    ```sh
    pio ci --board=esp32dev \
      --project-option="framework=arduino" \
      --project-option="build_flags=-DPROTOCORE_ENABLE_WEBSOCKET=1" \
      --lib="." examples/L6-Presentation/WebSocket/WebSocket.ino
    ```

    This is the single most common "it builds for me but CI fails" gotcha. See
    also [docs/EXAMPLES.md](../docs/EXAMPLES.md) when present.

New logic must ship with a native test. Make it work, then optimize, then re-run
the tests to confirm no regression.

## What every contribution must ship

The required artifacts depend on _what_ you are adding. Review and CI check for
these; a PR missing them will be sent back.

- **A service** - a feature that lives in its own `src/services/<name>/`
  directory - must ship **all** of:
    - the implementation in `src/services/<name>/`;
    - a native test in `test/test_<name>/` **and** its entry in
      [test/test_matrix.json](../test/test_matrix.json) (then regenerate
      `platformio.ini` as above - do not hand-edit the generated region);
    - a **micro-benchmark** in `test/performance_benching/services/<name>/` (which mirrors `src/`):
      an on-device CCOUNT bench (`main/main.c`) and/or a host bench (`host.c`);
    - a runnable **example** under `examples/<Layer-or-Drivers>/<Name>/` (a
      `.ino` plus its `README.md`, and a `build_opt.h` when it needs flags);
    - an entry in [docs/FEATURES.md](../docs/FEATURES.md) - **the source of truth
      for features**; the README feature table and the docs site both link to it.

- **Core** - a piece of a network layer rather than a service, i.e. code under
  `src/network_drivers/<layer>/`, `src/server/`, or `src/shared_primitives/` -
  lives in the appropriate layer directory and ships a **bench** in the mirrored
  `performance_benching/` path (`performance_benching/network_drivers/<layer>/<name>/`, `performance_benching/server/<name>/`, or
  `performance_benching/core/<name>/`, same `main/main.c` + `host.c` shape as a service), a **native
  test** (+ matrix entry), and an **example**, plus a
  [docs/FEATURES.md](../docs/FEATURES.md) entry **if it is user-facing** (a
  `PROTOCORE_ENABLE_*` flag or an observable behavior); purely internal plumbing may
  skip the FEATURES entry.

`performance_benching/` mirrors `src/` exactly: `performance_benching/services/<name>/`,
`performance_benching/network_drivers/<layer>/<name>/`, `performance_benching/server/<name>/`, `performance_benching/core/<name>/`,
each a uniform dir with an on-device CCOUNT bench (`main/main.c`, a `dbench_run()` using
`#include "device_bench.h"`) and an optional `host.c`. Both are C11 - the benches build with the
ESP-IDF and host toolchains, not Arduino. See
[test/performance_benching/README.md](../test/performance_benching/README.md).

- **An example only** (no new library code) requires just its `README.md`, in
  the style below.

### Example README style (enforced)

Every example's `README.md` follows the house style - see any example under
`examples/`, e.g. [examples/Drivers/Ads1115/README.md](../examples/Drivers/Ads1115/README.md):

1. **Title:** `# Name - one-line "what it is"`.
2. **What it is,** in plain language, up front - a newcomer should get the point
   before seeing any code.
3. **A detailed, full explanation:** why you would use it, the hardware/setup it
   needs, wiring (a pin table when relevant), how to flash it and what the
   expected serial/HTTP output looks like, and where it fits the bigger picture -
   plus a **Troubleshooting** list for the common failure modes.
4. **A "Build and run (PlatformIO)" block** with the exact
   `pio ci ... --project-option="build_flags=-DPROTOCORE_ENABLE_<FEATURE>=1"` command
   (the flag must reach the whole build, not just the sketch - the #1 CI gotcha).
5. **A "How it works (for the curious)" section** that walks the **actual source
   verbatim, heavily annotated** - name the real functions and key lines, and
   cite the test that covers them.

The examples index [docs/EXAMPLES.md](../docs/EXAMPLES.md) is generated from
these READMEs, so the per-example README is where the real documentation lives.

## Code style

- **`src/` is C11. No C++. Hard stop.** `.c` and `.h` are the only extensions there, and a file
  compiles as C11 or it does not ship. Not "C-style C++", not "C plus a couple of conveniences."
  `examples/` are Arduino sketches, so they are C++. `test/performance_benching/` is C11 too - its
  benches build with the ESP-IDF and host toolchains, so `constexpr`, `namespace`, templates,
  lambdas, `nullptr` and `Type::VALUE` have no place there. The rest of `test/` has no style rules.

    The reason is the target list. It includes c2000, where control-law code is written and reviewed
    as C, so a library that needs a C++ compiler cannot be used where it is most needed. It is also
    the rule that costs most to get wrong late, because C++ fails softly: `struct X;` lets you write
    `X *` in C++ and not in C, `using X = T` compiles until a struct member needs it, and a default
    argument is invisible at the call site. Each is one line, and each takes every use of itself down
    with it, so the error count points anywhere except the cause.

    The C spelling of each: `using X = T` is a `typedef`; a default argument is spelled at the call
    site (or `NULL` means the default and the callee resolves it); a reference parameter is a
    pointer; `X::Y` is a prefixed name ([SYMBOLS.md](../docs/SYMBOLS.md) sec 3); an in-class
    initializer moves to the object's definition; `static_assert` is `_Static_assert`; `nullptr` is
    `NULL`; `bool` is `proto_bool`. A forward-declared `struct X;` is fine and stays opaque, but in C
    it declares only the tag, so uses spell `struct X *`.

- **Formatting is enforced.** C/C++/`.ino` follow
  [.clang-format](../.clang-format) (4-space indent, 120 columns). Markdown is
  formatted with Prettier. Run both before opening a PR:

    ```sh
    clang-format -i <files-you-changed>
    npm run format
    ```

- `snake_case` for identifiers; short names where context is obvious.
- **American English** in code, identifiers, comments, and docs.
- **No `<stdlib.h>` / `<cstdlib>` in library code.** Parse by hand; do not pull
  in `malloc`, `atoi`, `strtoll`, `strtod`, and friends. `stdio`/`string` are
  fine.
- No dynamic allocation in the request path. Every buffer is sized at compile
  time and bounds-checked.
- **Follow the relevant RFC.** Implement correct framing, headers, status codes,
  and field syntax, and cite the RFC and section in code and docs (the codebase
  already does this, for example RFC 7230, 6455, 7233, 5424, 7252). If you must
  deviate for a constraint, document it as an explicit deviation.

## Build flags

Features are opt-in via `PROTOCORE_ENABLE_*` flags (default off) in
[src/protocore_config.h](../src/protocore_config.h). Some features depend on
others; illegal combinations fail at compile time with a clear `#error`. If you
add a feature that builds on another, add the matching dependency guard and
update the build-flag tree in the README.

## Commits and pull requests

- **Conventional Commits.** Use `feat:`, `fix:`, `docs:`, `ci:`, `chore:`,
  `refactor:`, `test:`, etc. The changelog is generated from commit history, so
  the prefix matters.
- Keep PRs focused. One logical change per PR.
- Fill out the pull request template. CI runs clang-format, Prettier, cspell,
  CodeQL, the native test suites, and the pentest suite; all must pass.
- New features need the full artifact set from
  [What every contribution must ship](#what-every-contribution-must-ship)
  (implementation, native test + matrix entry, micro-benchmark, example, and a
  `docs/FEATURES.md` entry), plus a compile check. Hardware verification is
  appreciated but the maintainer will confirm on real hardware before release.

## Reporting bugs and security issues

- Functional bugs: open an issue using the bug report form.
- Security vulnerabilities: do **not** open a public issue. See
  [SECURITY.md](SECURITY.md).
