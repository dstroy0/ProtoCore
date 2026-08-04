# Symbols and naming

This is the naming law for ProtoCore. It has two jobs: it states the rules precisely enough that a
tool can check them, and it explains why each rule exists so you can apply it to a case the table
does not cover.

If you only read one section, read [The contract](#the-contract).

**Audience.** Anyone reading or extending the library, including people who have never worked on
embedded code. Where a rule depends on a C detail, that detail is explained rather than
assumed. If a term is new, [docs/learn](learn/) has the primers.

---

## The shape of the library, in one paragraph

**ProtoCore is C. The API and the implementation are both C11.**

That sentence decides almost every rule below.

Flat names, one global namespace, no overloading, no templates, no scoped enums. This is not
nostalgia. The target list is xtensa, riscv, arm, and **c2000**, and control-law code on those parts
is written and reviewed as C. A library that requires a C++ compiler is a library that cannot be used
where it is most needed.

**C11**, which every compiler in that target list ships. Three of its features are load-bearing here:

| C11                                | what it buys                                                                                                |
| ---------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| `_Static_assert`                   | a sizing or layout invariant fails at compile time, naming itself when it trips                             |
| `<stdatomic.h>`, `_Atomic`         | the acquire/release ordering the SPSC rings and slot-state writes are built on (`shared_primitives/ring.h`) |
| anonymous struct and union members | a nested field is reached by its own name                                                                   |

The language is the guarantee rather than a detail above one. Every construct whose cost is decided
at runtime, or whose call target is not in the image, is absent because the language does not offer
it, not because a rule forbids it. What remains is checked at the binary anyway (see
[section 6](#6-guarantees-are-proven-at-the-binary)).

So: **one flat naming law, applied everywhere.** A reader moving between a header and its
implementation does not change mental models halfway.

---

## The contract

| Thing                     | Form                               | Example                                       |
| ------------------------- | ---------------------------------- | --------------------------------------------- |
| Server API function       | `proto_snake_case`, flat           | `proto_begin_ws`, `proto_on_tcp`              |
| Primitive function        | `proto_snake_case`, flat           | `proto_sha256_init`                           |
| Type                      | `proto_snake_case`, flat           | `proto_sha256_ctx`                            |
| Macro / compile-time flag | `PROTO_UPPER_SNAKE`, flat          | `PROTO_ENABLE_SSH`, `PROTO_SHA256_DIGEST_LEN` |
| Sizing / capacity bound   | `PROTO_MAX_*`, flat                | `PROTO_MAX_CONNS`, `PROTO_MAX_HANDLERS`       |
| Enum type                 | `typedef enum`, `proto_snake_case` | `typedef enum { … } proto_ip_family;`         |
| Enum member               | keeps its descriptive prefix       | `PROTO_IP_V4`                                 |
| Include guard             | `PROTOCORE_<FILE>_H`, max 31       | `PROTOCORE_SHA256_H`                          |
| File and directory        | `snake_case`                       | `src/crypto/mac/hmac_sha256.h`                |
| Test env / suite          | `native_<topic>` / `test_<topic>`  | `native_ip`, `test_ip`                        |

No `namespace`. No `using namespace`. Everything below is the reasoning; the table is the rule.

**One prefix, two cases: `proto_` for anything callable or nameable, `PROTO_` for anything the
preprocessor sees.** A two-letter prefix does not own a token. Short prefixes are what a
board-support header, a vendor SDK, and half the embedded projects on the internet also reach for,
and a prefix somebody else is equally likely to pick buys none of the uniqueness that is the entire
reason to have one. `PROTO_` is long enough and specific enough to actually be ours.

It applies to every kind of symbol, so there is no split to remember: a name is `proto_`/`PROTO_`
whether it is API or primitive, function or type or macro.

**Hard ban: a bare `MAX_` name.** `MAX_CONNS`, `MAX_ROUTES`, `MAX_HEADERS` and their kind are among the
most collided identifiers in embedded C. They are exactly the names a vendor SDK, an RTOS port, or a
third-party header reaches for, and the preprocessor has no scope to protect ours from theirs
(section 2). Every capacity bound carries `PROTO_MAX_`, with no exception, so the collision cannot
happen in either direction.

**One exemption: `src/board_drivers/`.** That is where vendor SDKs are spoken to, and their headers
are already full of names this law does not govern - unprefixed macros, their own casing, whatever
the toolchain ships. A driver has to name those to do its job. So a vendor symbol appears verbatim
inside `board_drivers/` and nowhere else, and everything `board_drivers/` _exports_ still obeys the
table above. The exemption is for what a driver must consume, never for what it publishes. That
boundary is the point of the directory: contamination is contained by being confined.

---

## 1. Prefixes, no namespaces

**The rule: every symbol the library exports is prefixed, at global scope. One prefix, in the case
the language calls for:** `proto_` for functions and types, `PROTO_` for macros and enum members.

There is no split by layer. A prefix that varies with what a symbol is for makes the name
unpredictable from the rule alone, because guessing right means already knowing which half the
symbol lives in, and it doubles the token space the library has to defend for no gain.

C has no namespaces. A C caller disambiguates by name alone, so every exported name must be globally
unique on its own. The prefix is what buys that uniqueness, and it has to be on every symbol, because
a prefix with holes in it is not a guarantee.

C offers nothing to wrap internals in, so the prefix is the whole mechanism rather than a convention
layered over one. One prefix means the mechanism has no seam in it: there is no pair of names where a
reader has to decide which law applies.

```c
typedef struct proto_sha256_ctx { /* ... */ } proto_sha256_ctx;

void proto_sha256_init(proto_sha256_ctx *ctx);
void proto_sha256_update(proto_sha256_ctx *ctx, const uint8_t *data, size_t len);
void proto_sha256_final(proto_sha256_ctx *ctx, uint8_t digest[PROTO_SHA256_DIGEST_LEN]);
```

**Pick names that stay under the linker's and the preprocessor's limits.** See section 2 for the
31-character macro rule, which is the tightest of them.

---

## 2. Macros are flat because they have to be

**The rule: macros are `PROTO_UPPER_SNAKE`, at global scope, always.**

The preprocessor runs before the compiler and knows nothing about scope. A macro is textual
replacement across the whole translation unit. There is no construct that can contain one, so "flat"
is not a choice here, it is the only option.

That has a consequence worth internalizing, because it produces genuinely confusing errors: a macro
will rewrite a token that is already scoped.

```c
#define OUTPUT 1                                  // e.g. from a vendor header

typedef enum { OUTPUT } proto_tcp_op;             // error: expands to `{ 1 }`
```

Substitution happens before the compiler sees a declaration at all, so there is nothing a member can
be declared inside that would protect it. This is exactly why enum members keep descriptive prefixes
(section 3), and why `PROTO_` on every macro matters: it keeps our macros out of everyone else's
token space and theirs out of ours.

**Keep macro names under 31 characters.** C89 guarantees only the first 31 characters of a macro name
are significant, and ProtoCore targets toolchains where that limit is real, c2000 included. Two macros
agreeing in their first 31 characters are the same macro there, silently.

`PROTO_` spends six of those 31 characters before the name proper starts, leaving 25. That is the
price of a prefix wide enough to actually be ours, and it is paid out of the budget below rather
than waived: a name that does not fit gets abbreviated by the rules that follow, never truncated and
never granted an exception.

**When a name does not fit, abbreviate whole words. Never cut a word short.** The two produce names
that look similar and read completely differently:

|             |                                                                                         |
| ----------- | --------------------------------------------------------------------------------------- |
| Chopped     | last word cut off partway - reads as a typo; the reader stops to work out the real name |
| Abbreviated | `PROTO_SSH_MSG_CH_WIN_ADJ` - reads as deliberate; every token maps back one-to-one      |

Abbreviation keeps the word boundaries, so the correspondence to the spec survives:
`CH`=CHANNEL, `WIN`=WINDOW, `ADJ`=ADJUST still recovers `SSH_MSG_CHANNEL_WINDOW_ADJUST` from RFC 4254,
which is what a reviewer checks the constant against. That matters most for the names that are least
arbitrary: protocol message names, IANA cipher suites, and datasheet register names are quoted from a
document, and mangling one breaks the only link back to it.

Prefer abbreviations the library already uses, so a reader meets a small vocabulary rather than a new
guess each time: `MAX` (369 uses), `LEN` (136), `BUF` (85), `REG` (72), `CMD` (43), `MSG` (42), `HDR`
(29), `AUTH` (25), `REQ` (18), `ERR`.

**How to shorten a word: keep its consonant skeleton.** Dropping vowels leaves a frame the eye still
resolves to one word, which is why the abbreviations already in the tree read as words rather than
noise - `HDR`, `MSG`, `CFG`, `PKT` are all built this way. It extends to words with no conventional
short form: `ENOUGH` to `ENGH`, `INTERACTIVE` to `IACTV`, `KEYBOARD` to `KBD`.

**An abbreviation that reads two ways is no better than a chop.** `INVALID` has an obvious five-letter
short form that fits the limit, and it reads just as easily as "in-value" as it does "invalid". The
test is not "can this be decoded", it is "does every reader decode it the same way, without stopping".
When a word has no unambiguous short form, use a synonym that does: `..._MSG_TYPE_ERR` says what
`BadTcpMessageTypeInvalid` means and cannot be read any other way.

73 macros exceed the limit, longest 47 (`PROTO_ENABLE_SSH_KEYBOARD_INTERACTIVE_NEEDS_SSH`), tracked
in [ROADMAP.md](ROADMAP.md). The dependency gates are the worst of them, because
`PROTO_ENABLE_<A>_NEEDS_<B>` spends its budget twice over on two feature names plus the connective.
Several are user-facing feature flags that also appear in every example's `build_opt.h` and in the
configurator, so renaming one means regenerating those too. They are renamed anyway, before 1.0.0:
this is the last moment a flag can change without it costing anyone anything, and a name that
violates the law on the day the law ships is a name nobody will ever fix.

---

## 3. Enums are flat; the prefix is what scopes them

**The rule: every enum is a `typedef enum`. Members carry a descriptive prefix, always.**

```c
typedef enum
{
    PROTO_IP_V4,
    PROTO_IP_V6,
} proto_ip_family;

proto_ip_family fam = PROTO_IP_V4;
```

C has no scoped enum. Every member lands in one global namespace the moment it is declared, so the
prefix is not decoration - **it is the only thing keeping two enums from colliding.** A member that drops its prefix is a link-time or compile-time collision
waiting for the second enum that wants the same word, and the words enums want are the common ones:
auditing this library found `FAILED` wanted by four enums, `IDLE` by three, and `STOP`, `START`,
`DONE`, `PENDING`, `MISS`, `HIT` by two each.

The prefix is mandatory rather than a per-enum decision. Three further reasons:

1. **Names are for human recognition.** A member is read far more often at a use site, in a log, a
   packet dump, or a debugger than in its declaration. `PROTO_IP_V4` is self-describing when it appears
   alone. `V4` is not.
2. **De-prefixing collides with the preprocessor.** As section 2 shows, a bare member is still a
   macro-substitutable token. Auditing the library found roughly 80 members that would become common
   macro names (`OUTPUT`, `ERROR`, `DELETE`, `NONE`, `READ`, `TIMEOUT`) and be rewritten out from
   under us on some target.
3. **Some members cannot be de-prefixed at all.** Eleven would become identifiers starting with a
   digit, which is not a legal identifier:

    | Member              | Bare form | Legal? |
    | ------------------- | --------- | ------ |
    | `HTTP_11`           | `11`      | no     |
    | `SMB2_DIALECT_0311` | `0311`    | no     |
    | `DEVICENET_GROUP_1` | `1`       | no     |

A rule that cannot be applied uniformly is not a rule. The prefix stays, on every member of every
enum, with no per-enum exception - there is no scope to fall back on if one is wrong.

---

## 4. Include guards, files, and test targets

**Include guards are `PROTOCORE_<FILE>_H`**, built from the file's own name:
`src/crypto/hash/sha256.h` guards with `PROTOCORE_SHA256_H`.

Guards take the full library name rather than the `PROTO_` prefix, because a guard is the one macro
that has to be unique across _someone else's_ build. Every header a user compiles shares a single
macro namespace, so `PROTO_HTTP_PARSER_H` is a plausible name for another library's guard while
`PROTOCORE_HTTP_PARSER_H` is not.

**Every header file name under `src/` is unique, and `check_symbols.py` enforces it.** That uniqueness is what
makes a filename-derived guard collision-proof, so it is checked rather than assumed: adding a second
`parser.h` anywhere in the tree fails CI.

The guard is derived from the file's own name and not from its path. A path-derived guard has to
carry every directory it sits under, which in a tree five and six levels deep overruns the
31-character limit from section 2 on three quarters of the headers here. The filename form lands at
median 20.

**The filename form is authoritative when it fits. When it does not, the guard must still convey
intent.** Two headers overflow (`PROTOCORE_NTRIP_CASTER_LISTENER_H` at 33,
`PROTOCORE_PROVISIONING_SERVICE_H` at 32). Neither is chopped mid-word: a guard whose last word has
been cut off partway names nothing and teaches nothing. A whole word is elided instead, leaving a name
that still says what the header is:

| Header                                    | Guard                        | Length |
| ----------------------------------------- | ---------------------------- | ------ |
| `services/gnss/ntrip_caster_listener.h`   | `PROTOCORE_NTRIP_LISTENER_H` | 26     |
| `services/system/provisioning_service/…h` | `PROTOCORE_PROVISIONING_H`   | 25     |

Which word carries the meaning is a judgment call, which is why the exceptions are recorded here and
in `check_symbols.py` rather than computed: `caster` is implied by `ntrip`, and a `_service` header is
a service. There are exactly as many exceptions as this table has rows, and the checker rejects any
guard that is neither the filename form nor a listed exception - it raises rather than inventing a
shortening, because inventing one is exactly the mistake this paragraph exists to prevent.

**This is [section 3](#3-enums-are-scoped-members-keep-their-prefixes)'s rule applied to guards:
names are for human recognition.** A mechanically shortened name can be provably unique and still be
a bad name, because uniqueness is a property of the string and recognition is a property of the
reader. A word cut off partway does not read as a new identifier; it reads as the original with a
typo, and the reader stops to work out which. Any rule here that produces names by truncation is
optimizing for the checker over the person, and the person is who the name is for.

The limit itself is not negotiable. 31 is the
C89 limit on significant characters in an external identifier; a longer name is not an error, it is
**unspecified** - a conforming toolchain may consider only the first 31 and silently merge two guards,
which manifests as a header that mysteriously does not get included. Every compiler currently in the
target list keeps the full name, but "the toolchains we happen to have tried do not truncate" is a
measurement, not a guarantee, and this library's whole argument is that its guarantees hold on the
target rather than on the ones already tested. Staying inside the standard's floor is what makes it a
guarantee. Truncation can itself create a collision, so `check_symbols.py` checks uniqueness of the
**final, truncated** guard, not of the filename it came from.

`#pragma once` is not used: it is not standard, and the target list includes toolchains where its
behavior across duplicated or symlinked headers is not guaranteed.

**Source files and directories are `snake_case`**, matching the API they declare and avoiding the
case-sensitivity trap, where a repository developed on case-insensitive Windows breaks on
case-sensitive Linux over a capitalization-only difference.

**Under `src/`, `.c` and `.h` are the only extensions.** The language is part of the name here: a
`.cpp` in `src/` is a file this law does not govern, because half of what the law decides - that a
symbol is flat, that an enum member carries its prefix, that there is no scope to qualify with -
stops being a rule and becomes a preference the moment the file can declare a namespace. The
extension is also the one part a checker can decide without parsing, so it is the cheapest place to
catch a file that was moved into `src/` from somewhere less constrained. `examples/` keeps `.ino`
and `performance_benching/` keeps `.cpp`; neither is governed by this document.

**One exception, and it is the same boundary section 1 already draws.** A `board_drivers/` adapter
whose entire job is to wrap a C++ vendor API keeps `.cpp`, because the extension is what selects the
compiler and the vendor type cannot be named from C at all. Today that is
`board_drivers/hal/esp/esp_mnt_fs.cpp`, which turns an Arduino `fs::FS` into a `pc_mnt_backend`.

The exception is narrow in exactly the way section 1's is. It covers what a driver must _consume_,
never what it publishes: `pc_mnt_fs()` hands back a `pc_mnt_backend *`, so every caller above the
board layer is C speaking to C, and the C++ stops at the file that needs it. A `.cpp` anywhere else
under `src/`, or one that exports a C++ type, is a violation rather than an instance of this.

The reason it is written down here rather than left to judgment: the C11 conversion renamed this
file to `.c` without converting its contents, and because no native env had reached it, nothing
failed until a full-tree compile sweep did. A rule with an unrecorded exception is a rule that gets
"fixed" by the next mechanical pass.

Markdown is the documented exception: docs use `UPPER_SNAKE` (`README.md`, `FEATURES.md`,
`SRCBANNED.md`, and this file), including the per-die register references under
`board_drivers/hal/esp/` such as `P4_MIPI_HELPERS.md`. The case-sensitivity argument is about
`#include` paths resolving differently per platform, which does not apply to prose.

**Test environments and suites carry no house prefix.** They are `native_<topic>` and `test_<topic>`,
not `native_proto_<topic>`. A prefix exists to prevent collisions in a shared global namespace; a test
environment name lives only in `platformio.ini` and has no such namespace to protect. `native_ip` says
everything `native_proto_ip` would.

---

## 5. Designs considered and rejected

Recording the roads not taken, because "why not the obvious thing" is usually the more useful half.

- **`namespace protocore` around the internals.** Rejected: it splits the library into two naming
  conventions with a translation layer between them, and buys scoping the flat prefix already
  provides. Moot now that the implementation is C, which has no namespace to reach for.
- **De-prefix all enum members.** Rejected for the reasons in section 3, and in C the prefix is
  load-bearing rather than stylistic.
- **A C++ implementation under a C API.** This is what the library did until the C conversion, on the
  argument that only the emitted binary matters (section 6) so the source language above it is a
  detail. Reversed: the argument holds for what the compiler emits, but not for what a reviewer can
  establish by reading. A control engineer reviewing `src/` should not have to know which of two
  languages a line is in to know what it costs.
- **`#pragma once`.** Rejected: not standard, and the target list is deliberately wide.

---

## 6. Guarantees are proven at the binary

The naming law is the visible half of a larger rule: **where ProtoCore promises a behavior, the
promise is checked against the emitted instructions, not argued from the source.**

C makes the source readable as cost, but reading is still not proof. The claims that get this
treatment are the ones a caller actually depends on:

- **Constant-time** comparisons and crypto: no branch and no memory access depends on a secret.
- **No heap after `begin()`**: no allocator call reachable in the relevant `.text`.
- **Bounded interrupt and critical-section paths**: a counted worst case, not an estimate.

Each is documented as claim, then disassembly, then why the disassembly establishes the claim. A
guarantee without that chain is a comment, and comments do not survive a compiler upgrade.

---

## 7. Enforcement

The mechanical rules are checked, not trusted to review:

```sh
python ci_tooling/check/check_symbols.py --all     # this document
python ci_tooling/check/check_src_banned.py --all  # docs/SRCBANNED.md hard bans
python ci_tooling/check/check_owned_context.py     # single-owner state rule
```

`check_symbols.py` decides only what is decidable: prefix and casing, macro scope and length,
include-guard form, file naming and extension, and the absence of `namespace` / `using namespace`.
Judgment calls
(is this name descriptive, should this particular enum de-prefix) are review, and this document is
what the review argues from.

CI runs all three. A rule that is not enforced is a suggestion, and suggestions drift.

---

## See also

- [SRCBANNED.md](SRCBANNED.md), constructs banned outright in `src/`
- [SRC_LAW.md](SRC_LAW.md), the determinism and allocation law
- [ARCHITECTURE.md](ARCHITECTURE.md), the OSI layering the names live in
- [learn/](learn/), zero-knowledge primers
