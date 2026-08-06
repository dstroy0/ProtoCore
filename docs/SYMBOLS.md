# Symbols and naming

The naming law for ProtoCore, stated so a tool can check it and explained so it can be applied to a
case the table does not cover.

## The language

**ProtoCore is C. The API and the implementation are both C11.**

Flat names, one global namespace, no overloading, no templates, no scoped enums. The target list is
xtensa, riscv, arm, and **c2000**, and control-law code on those parts is written and reviewed as C.
A library that requires a C++ compiler is a library that cannot be used where it is most needed.

C11, which every compiler in that list ships. Three of its features are load-bearing:

| C11                                | what it buys                                                                                   |
| ---------------------------------- | ---------------------------------------------------------------------------------------------- |
| `_Static_assert`                   | a sizing or layout invariant fails at compile time, naming itself when it trips                |
| `<stdatomic.h>`, `_Atomic`         | the acquire/release ordering the SPSC rings and slot-state writes are built on (`mmgr/ring.h`) |
| anonymous struct and union members | a nested field is reached by its own name                                                      |

## The contract

| Thing                      | Form                                              | Example                                       |
| -------------------------- | ------------------------------------------------- | --------------------------------------------- |
| Function (API, primitive)  | `proto_snake_case`, flat                          | `proto_begin_ws`, `proto_sha256_init`         |
| Type (struct, enum, union) | `StructName`, flat                                | `HttpReq`, `AuthNs`, `TcpConn`, `ConnState`   |
| Struct member, local       | `snake_case`                                      | `rx_head`, `auth_id`                          |
| Macro / compile-time flag  | `PROTO_UPPER_SNAKE`, flat                         | `PROTO_ENABLE_SSH`, `PROTO_SHA256_DIGEST_LEN` |
| Sizing / capacity bound    | `PROTO_MAX_*`, flat                               | `PROTO_MAX_CONNS`, `PROTO_MAX_HANDLERS`       |
| Enum member                | `PROTO_UPPER_SNAKE`, keeps its descriptive prefix | `PROTO_IP_V4`                                 |
| Include guard              | `PROTOCORE_<FILE>_H`, max 31                      | `PROTOCORE_SHA256_H`                          |
| File and directory         | `snake_case`                                      | `src/crypto/mac/hmac_sha256.h`                |
| Test env / suite           | `native_<topic>` / `test_<topic>`                 | `native_ip`, `test_ip`                        |

No `namespace`. No `using namespace`. The table is the rule; the sections below are the reasoning.

**The tree is mid-rename.** `pc_` / `PC_` is the internal spelling the library carries today;
`proto_` / `PROTO_` is what it publishes. `check_symbols.py` still gates the internal spelling and
`ci_tooling/check/symbols_baseline.json` counts the distance left, which ratchets down only. The
table above is the destination, and the rename lands before 1.0.0.

**One prefix, two cases: `proto_` for anything callable, `PROTO_` for anything the preprocessor
sees.** A two-letter prefix does not own a token. `PROTO_` is long enough and specific enough to
actually be ours. A type carries no prefix because `StructName` casing already separates it from
every C library's token space.

**Hard ban: a bare `MAX_` name.** `MAX_CONNS`, `MAX_ROUTES`, `MAX_HEADERS` and their kind are among
the most collided identifiers in embedded C, and the preprocessor has no scope to protect ours from a
vendor SDK's. Every capacity bound carries `PROTO_MAX_`, with no exception.

**One exemption: `src/board_drivers/`.** That is where vendor SDKs are spoken to, and their headers
are full of names this law does not govern. A vendor symbol appears verbatim inside `board_drivers/`
and nowhere else, and everything `board_drivers/` _exports_ still obeys the table above. The
exemption covers what a driver must consume, never what it publishes.

## 1. Prefixes, no namespaces

C has no namespaces. A C caller disambiguates by name alone, so every exported function must be
globally unique on its own, and the prefix is what buys that. A prefix with holes in it is not a
guarantee, so there is no split by layer: a function is `proto_` whether it is API or primitive.

```c
typedef struct Sha256Ctx Sha256Ctx;

void proto_sha256_init(Sha256Ctx *ctx);
void proto_sha256_update(Sha256Ctx *ctx, const uint8_t *data, size_t len);
void proto_sha256_final(Sha256Ctx *ctx, uint8_t digest[PROTO_SHA256_DIGEST_LEN]);
```

## 2. Macros are flat because they have to be

**Macros are `PROTO_UPPER_SNAKE`, at global scope, always.** The preprocessor runs before the
compiler and knows nothing about scope, so there is no construct that can contain one.

That produces genuinely confusing errors, because a macro rewrites a token that is already scoped:

```c
#define OUTPUT 1                      // e.g. from a vendor header

typedef enum { OUTPUT } TcpOp;        // error: expands to `{ 1 }`
```

Substitution happens before the compiler sees a declaration, so nothing can protect a member. This is
why enum members keep descriptive prefixes (section 3) and why `PROTO_` on every macro matters.

**Keep macro names under 31 characters.** C89 guarantees only the first 31 characters of an external
identifier are significant, and the target list includes toolchains where that is real, c2000
included. Two macros agreeing in their first 31 characters are the same macro there, silently.
`PROTO_` spends six of the 31, leaving 25.

**When a name does not fit, abbreviate whole words. Never cut a word short.** A chopped last word
reads as a typo and the reader stops to work out the real name. An abbreviation keeps the word
boundaries, so the correspondence to the spec survives: in `PROTO_SSH_MSG_CH_WIN_ADJ`, `CH`=CHANNEL,
`WIN`=WINDOW, `ADJ`=ADJUST still recovers `SSH_MSG_CHANNEL_WINDOW_ADJUST` from RFC 4254, which is
what a reviewer checks the constant against. That matters most for names quoted from a document:
protocol messages, IANA cipher suites, datasheet registers.

**To shorten a word, keep its consonant skeleton.** `HDR`, `MSG`, `CFG`, `PKT` are all built this way,
and it extends to words with no conventional short form: `ENOUGH` to `ENGH`, `INTERACTIVE` to `IACTV`,
`KEYBOARD` to `KBD`. Prefer abbreviations the library already uses so a reader meets a small
vocabulary: `MAX`, `LEN`, `BUF`, `REG`, `CMD`, `MSG`, `HDR`, `AUTH`, `REQ`, `ERR`.

**An abbreviation that reads two ways is no better than a chop.** `INVALID` has an obvious five-letter
short form that reads just as easily as "in-value". The test is not "can this be decoded" but "does
every reader decode it the same way, without stopping". When a word has no unambiguous short form,
use a synonym that does: `..._MSG_TYPE_ERR`.

Macros over the limit are tracked in [ROADMAP.md](ROADMAP.md) and renamed before 1.0.0.

## 3. Enums are flat; the prefix is what scopes them

**Every enum is a `typedef enum` with a `StructName` type. Members carry a descriptive
`PROTO_UPPER_SNAKE` prefix, always.**

```c
typedef enum
{
    PROTO_IP_V4,
    PROTO_IP_V6,
} IpFamily;

IpFamily fam = PROTO_IP_V4;
```

C has no scoped enum. Every member lands in one global namespace the moment it is declared, so the
prefix is the only thing keeping two enums from colliding, and the words enums want are the common
ones: `FAILED`, `IDLE`, `STOP`, `START`, `DONE`, `PENDING`, `MISS`, `HIT`.

Two further reasons the prefix is mandatory rather than a per-enum decision:

1. **Names are for human recognition.** A member is read far more often at a use site, in a log, a
   packet dump, or a debugger than in its declaration. `PROTO_IP_V4` is self-describing when it
   appears alone. `V4` is not.
2. **Some members cannot be de-prefixed at all.** These would become identifiers starting with a
   digit, which is not legal:

    | Member              | Bare form | Legal? |
    | ------------------- | --------- | ------ |
    | `HTTP_11`           | `11`      | no     |
    | `SMB2_DIALECT_0311` | `0311`    | no     |
    | `DEVICENET_GROUP_1` | `1`       | no     |

A rule that cannot be applied uniformly is not a rule.

## 4. Guards, files, and test targets

**Include guards are `PROTOCORE_<FILE>_H`**, built from the file's own name:
`src/crypto/hash/sha256.h` guards with `PROTOCORE_SHA256_H`.

A guard takes the full library name rather than the `PROTO_` prefix, because it is the one macro that
has to be unique across _someone else's_ build: `PROTO_HTTP_PARSER_H` is a plausible name for another
library's guard while `PROTOCORE_HTTP_PARSER_H` is not.

**Every header file name under `src/` is unique, and `check_symbols.py` enforces it.** That is what
makes a filename-derived guard collision-proof. The guard is derived from the file's name and not its
path, because a path-derived guard overruns the 31-character limit on three quarters of the headers in
a tree this deep; the filename form lands at median 20.

**When the filename form does not fit, a whole word is elided, never chopped:**

| Header                                                        | Guard                        | Length |
| ------------------------------------------------------------- | ---------------------------- | ------ |
| `services/timing_position/gnss/ntrip_caster_listener.h`       | `PROTOCORE_NTRIP_LISTENER_H` | 26     |
| `services/system/provisioning_service/provisioning_service.h` | `PROTOCORE_PROVISIONING_H`   | 25     |

`caster` is implied by `ntrip`, and a `_service` header is a service. `check_symbols.py` rejects any
guard that is neither the filename form nor a listed exception; it raises rather than inventing a
shortening. It checks uniqueness of the **final** guard, since truncation can itself create a
collision.

`#pragma once` is not used: it is not standard, and the target list is deliberately wide.

**Source files and directories are `snake_case`**, matching the API they declare and avoiding the
case-sensitivity trap where a repository developed on Windows breaks on Linux.

**Under `src/`, `.c` and `.h` are the only extensions.** The language is part of the name here: half
of what this law decides stops being a rule the moment a file can declare a namespace. `examples/`
keeps `.ino` and `performance_benching/` keeps `.cpp`; neither is governed by this document.

**One exception, the same boundary section 1 draws.** A `board_drivers/` adapter whose entire job is
to wrap a C++ vendor API keeps `.cpp`, because the extension selects the compiler and the vendor type
cannot be named from C at all. Today that is three files:

| File                                          | The C++ it wraps                                          |
| --------------------------------------------- | --------------------------------------------------------- |
| `board_drivers/hal/esp/esp_mnt_fs.cpp`        | an Arduino `fs::FS`, turned into a `pc_mnt_backend`       |
| `board_drivers/hal/esp/esp_nvs.cpp`           | an Arduino `Preferences` namespace, behind `nvs.h`        |
| `board_drivers/physical/esp/physical_esp.cpp` | the Arduino `WiFi` and `ETH` objects, behind `physical.h` |

It covers what a driver must consume, never what it publishes: every name `physical_esp.cpp` defines
is declared in `physical.h` between `PROTO_BEGIN_DECLS` and `PROTO_END_DECLS`, so every caller above
the board layer is C speaking to C. A `.cpp` anywhere else under `src/`, or one that exports a C++
type, is a violation rather than an instance of this. The list is written down because a rule with an
unrecorded exception gets "fixed" by the next mechanical pass.

Markdown is the documented exception to `snake_case`: docs use `UPPER_SNAKE`, including the per-die
register references under `board_drivers/hal/esp/`.

**Test environments and suites carry no house prefix.** They are `native_<topic>` and `test_<topic>`.
A prefix prevents collisions in a shared global namespace; a test environment name lives only in
`platformio.ini` and has no such namespace to protect.

## 5. Enforcement

```sh
python -m ci_tooling.check.check_symbols --all     # this document
python -m ci_tooling.check.check_src_banned --all  # docs/SRCBANNED.md hard bans
python -m ci_tooling.check.check_owned_context     # single-owner state rule
```

`check_symbols.py` decides only what is decidable: prefix and casing, macro scope and length,
include-guard form, file naming and extension, and the absence of `namespace` / `using namespace`.
Judgment calls are review, and this document is what the review argues from. CI runs all three, and
the baselines ratchet down only.

## See also

- [SRCBANNED.md](SRCBANNED.md), constructs banned outright in `src/`
- [SRC_LAW.md](SRC_LAW.md), the determinism and allocation law
- [ARCHITECTURE.md](ARCHITECTURE.md), the OSI layering the names live in
- [learn/](learn/), zero-knowledge primers
