# Symbols and naming

This is the naming law for ProtoCore. It has two jobs: it states the rules precisely enough that a
tool can check them, and it explains why each rule exists so you can apply it to a case the table
does not cover.

If you only read one section, read [The contract](#the-contract).

**Audience.** Anyone reading or extending the library, including people who have never worked on
embedded code. Where a rule depends on a C or C++ detail, that detail is explained rather than
assumed. If a term is new, [docs/learn](learn/) has the primers.

---

## The shape of the library, in one paragraph

**ProtoCore presents a C API. The implementation is C++.**

That sentence decides almost every rule below, so it is worth being precise about what each half
means and why they differ.

The **API** is C-shaped: flat names, one global namespace, no overloading, no templates in the
signatures a caller depends on. This is not nostalgia. The target list is xtensa, riscv, arm, and
**c2000**, and control-law code on those parts is written and reviewed as C. An API that requires a
C++ compiler to call is an API that cannot be used where the library most needs to be usable.

The **implementation** is C++, and that is deliberately not a contradiction. What a control engineer
needs to guarantee is the behavior of the emitted binary at the points where timing and memory
matter, not the source language that produced it. ProtoCore guarantees those points directly, by
verifying the generated instructions (see [section 6](#6-guarantees-are-proven-at-the-binary)). Once
behavior is established at the binary, the source language above it is an implementation detail.

So: **one flat naming law, applied everywhere.** There is no second, namespaced convention for
internals, because a reader moving between a header and its implementation should not have to change
mental models halfway.

---

## The contract

| Thing                     | Form                              | Example                                 |
| ------------------------- | --------------------------------- | --------------------------------------- |
| Function                  | `pc_snake_case`, flat             | `pc_sha256_init`                        |
| Type                      | `pc_snake_case`, flat             | `pc_sha256_ctx`                         |
| Macro / compile-time flag | `PC_UPPER_SNAKE`, flat            | `PC_ENABLE_SSH`, `PC_SHA256_DIGEST_LEN` |
| Enum type                 | `enum class`, `pc_snake_case`     | `enum class pc_ip_family : uint8_t`     |
| Enum member               | keeps its descriptive prefix      | `pc_ip_family::PC_IP_V4`                |
| Include guard             | `PROTOCORE_<FILE>_H`, max 31      | `PROTOCORE_SHA256_H`                    |
| File and directory        | `snake_case`                      | `src/crypto/mac/hmac_sha256.cpp`        |
| Test env / suite          | `native_<topic>` / `test_<topic>` | `native_ip`, `test_ip`                  |

No `namespace`. No `using namespace`. Everything below is the reasoning; the table is the rule.

---

## 1. One prefix, no namespaces

**The rule: every symbol the library exports is prefixed `pc_` or `PC_`, at global scope.**

C has no namespaces. A C caller disambiguates by name alone, so every exported name must be globally
unique on its own. The prefix is what buys that uniqueness, and it has to be on every symbol, because
a prefix with holes in it is not a guarantee.

This is why ProtoCore does **not** wrap its C++ internals in `namespace protocore` even though it
could. Two conventions in one library means every reader has to know which half of the codebase they
are in before they can predict a name, and every symbol that crosses the boundary needs a translation.
One flat law is duller and better.

```cpp
typedef struct pc_sha256_ctx { /* ... */ } pc_sha256_ctx;

void pc_sha256_init(pc_sha256_ctx *ctx);
void pc_sha256_update(pc_sha256_ctx *ctx, const uint8_t *data, size_t len);
void pc_sha256_final(pc_sha256_ctx *ctx, uint8_t digest[PC_SHA256_DIGEST_LEN]);
```

**Pick names that stay under the linker's and the preprocessor's limits.** See section 2 for the
31-character macro rule, which is the tightest of them.

---

## 2. Macros are flat because they have to be

**The rule: macros are `PC_UPPER_SNAKE`, at global scope, always.**

The preprocessor runs before the compiler and knows nothing about scope. A macro is textual
replacement across the whole translation unit. There is no construct that can contain one, so "flat"
is not a choice here, it is the only option.

That has a consequence worth internalizing, because it produces genuinely confusing errors: a macro
will rewrite a token that is already scoped.

```cpp
#define OUTPUT 1              // e.g. from Arduino.h

enum class pc_tcp_op { OUTPUT };  // error: expands to `enum class pc_tcp_op { 1 };`
```

The member is scoped and it does not help, because substitution happens before the compiler sees the
scope. This is exactly why enum members keep descriptive prefixes (section 3), and why `PC_` on every
macro matters: it keeps our macros out of everyone else's token space and theirs out of ours.

**Keep macro names under 31 characters.** C89 guarantees only the first 31 characters of a macro name
are significant, and ProtoCore targets toolchains where that limit is real, c2000 included. Two macros
agreeing in their first 31 characters are the same macro there, silently.

**When a name does not fit, abbreviate whole words. Never cut a word short.** The two produce names
that look similar and read completely differently:

|             |                                                                                         |
| ----------- | --------------------------------------------------------------------------------------- |
| Chopped     | last word cut off partway - reads as a typo; the reader stops to work out the real name |
| Abbreviated | `PC_SSH_MSG_CH_WIN_ADJ` - reads as deliberate; every token maps back one-to-one         |

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

Nine macros currently exceed the limit, longest 37 (`PC_OPCUA_BAD_TCP_NOT_ENOUGH_RESOURCES`), tracked
in [ROADMAP.md](ROADMAP.md). One of them, `PC_ENABLE_SSH_KEYBOARD_INTERACTIVE`, is a user-facing
feature flag that also appears in every example's `build_opt.h` and in the configurator, so renaming it
means regenerating those too. It is renamed anyway, before 1.0.0: this is the last moment a flag can
change without it costing anyone anything, and a name that violates the law on the day the law ships
is a name nobody will ever fix.

---

## 3. Enums are scoped; members keep their prefixes

**The rule: every enum is an `enum class`. Members keep a descriptive prefix. Dropping a prefix is a
per-enum decision, never a blanket rewrite.**

`enum class` is required because a plain enum leaks its members into the surrounding scope and
converts to `int` implicitly, silencing exactly the type check that makes the enum worth having.

Keeping member prefixes is the part people argue with, since `pc_ip_family::PC_IP_V4` looks redundant
beside `pc_ip_family::V4`. Three reasons it stays:

1. **Names are for human recognition.** A member is read far more often at a use site, in a log, a
   packet dump, or a debugger than in its declaration. `PC_IP_V4` is self-describing when it appears
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

A rule that cannot be applied uniformly is not a rule. The prefix stays by default; any individual
enum that genuinely reads better without one is changed deliberately, after checking that its bare
members are neither macro-prone nor digit-leading.

---

## 4. Include guards, files, and test targets

**Include guards are `PROTOCORE_<FILE>_H`**, built from the file's own name:
`src/crypto/hash/sha256.h` guards with `PROTOCORE_SHA256_H`.

Guards take the full library name rather than the `PC_` prefix, because a guard is the one macro that
has to be unique across _someone else's_ build. Every header a user compiles shares a single macro
namespace, so `PC_HTTP_PARSER_H` is a plausible name for another library's guard while
`PROTOCORE_HTTP_PARSER_H` is not. A guard is also not part of the API surface that `PC_` exists to
keep short and typeable.

**Every header file name under `src/` is unique, and `check_symbols.py` enforces it.** That uniqueness is what
makes a filename-derived guard collision-proof, so it is checked rather than assumed: adding a second
`parser.h` anywhere in the tree fails CI.

This replaces an earlier `PC_<PATH>_H` rule that derived the guard from the full path. That rule was
unsatisfiable in this tree: it also demanded the 31-character limit from section 2, and **283 of 373
headers (75%) blew it** - median 38, worst 62
(`PC_SERVICES_SYSTEM_PROVISIONING_SERVICE_PROVISIONING_SERVICE_H`). The path form was reasoned about
against a three-level tree; the tree is now five and six levels deep. A rule that cannot be satisfied
is not a strict rule, it is a rule that gets ignored, and an ignored rule stops protecting the case it
was written for. `PROTOCORE_<FILE>_H` lands at median 20.

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

Markdown is the documented exception: docs use `UPPER_SNAKE` (`README.md`, `FEATURES.md`,
`SRCBANNED.md`, and this file), including the per-die register references under
`board_drivers/hal/esp/` such as `P4_MIPI_HELPERS.md`. The case-sensitivity argument is about
`#include` paths resolving differently per platform, which does not apply to prose.

**Test environments and suites carry no house prefix.** They are `native_<topic>` and `test_<topic>`,
not `native_pc_<topic>`. A prefix exists to prevent collisions in a shared global namespace; a test
environment name lives only in `platformio.ini` and has no such namespace to protect. `native_ip` says
everything `native_pc_ip` would.

---

## 5. Designs considered and rejected

Recording the roads not taken, because "why not the obvious thing" is usually the more useful half.

- **`namespace protocore` around the C++ internals.** Rejected: it splits the library into two naming
  conventions with a translation layer between them, and buys scoping the flat prefix already
  provides. It would also put a C++-only construct in the path of a C-shaped API.
- **De-prefix all enum members.** Rejected for the three reasons in section 3.
- **Rewrite the implementation in C.** Rejected as unnecessary. The control-law objection to C++ is
  about the behavior of the emitted code, and that is established directly at the binary (section 6)
  rather than inferred from the source language.
- **`#pragma once`.** Rejected: not standard, and the target list is deliberately wide.

---

## 6. Guarantees are proven at the binary

The naming law is the visible half of a larger rule: **where ProtoCore promises a behavior, the
promise is checked against the emitted instructions, not argued from the source.**

This is what makes a C++ implementation acceptable under a C API for control-law use. The claims that
get this treatment are the ones a caller actually depends on:

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
include-guard form, file naming, and the absence of `namespace` / `using namespace`. Judgment calls
(is this name descriptive, should this particular enum de-prefix) are review, and this document is
what the review argues from.

CI runs all three. A rule that is not enforced is a suggestion, and suggestions drift.

---

## See also

- [SRCBANNED.md](SRCBANNED.md), constructs banned outright in `src/`
- [SRC_LAW.md](SRC_LAW.md), the determinism and allocation law
- [ARCHITECTURE.md](ARCHITECTURE.md), the OSI layering the names live in
- [learn/](learn/), zero-knowledge primers
