# Banned in `src/` (and what to use instead)

Hard rules for library code. If a change violates one of these, it is wrong: no exceptions, no
"it was simpler," no "just this once."

**Scope**

- **`src/`** - fully constrained. Every rule below applies.
- **`examples/`** - Arduino sketches, so Arduino APIs (`Serial`, `WiFi.begin`, `delay()`, `millis()`,
  `IPAddress`, `String`) are fine **except networking**: the socket rules (#6) apply here too. Use the
  library's own transport, never `WiFiClient` / `WiFiUDP`.
- **`test/`** - anything goes (any C++ / STL / stdlib). Do **not** apply these rules to tests.

**Enforcement.** `ci_tooling/check/check_src_banned.py` gates the machine-detectable bans; the
pre-commit hook runs it on the staged `src/` sources and refuses the commit on any hit. Comments and
string literals are exempt. The rest are review items, and each block below says which it is under
**Catch**. Command list: [SYMBOLS.md](SYMBOLS.md) section 5.

Numbers are stable and cited from source comments. #16 is folded into #12 and keeps its number.

## Index

| #   | Banned                                           | Use instead                              |
| --- | ------------------------------------------------ | ---------------------------------------- |
| 1   | `strlen`                                         | `strnlen(p, cap)`                        |
| 2   | `<stdlib.h>` and everything in it                | fixed BSS, hand-rolled parse             |
| 3   | `auto`                                           | the explicit type                        |
| 4   | `delay()`                                        | `pcdelay(ms)`                            |
| 5   | bare `millis()` for new timing                   | `pc_millis()`                            |
| 6   | outside networking libs, raw lwIP                | `Tcp.client->`, `Udp.listener->`         |
| 7   | em-dash (U+2014)                                 | comma, parentheses, a linking word       |
| 8   | `gmtime` / `localtime` / `ctime`                 | the `_r` forms                           |
| 9   | casting an enum to an integer to compile         | propagate the enum type                  |
| 10  | signed-overflow UB in a parser                   | accumulate unsigned, then apply sign     |
| 11  | uninitialized variables and out-params           | initialize at the declaration            |
| 12  | file-scope mutable outside one `<Name>Ctx`       | one owned context struct                 |
| 13  | back-compat shims, compat aliases                | make the breaking change, bump major     |
| 14  | British spelling                                 | American spelling                        |
| 15  | duplicated or inline config strings              | a named `const`, or `protocore_config.h` |
| 16  | (see #12)                                        |                                          |
| 17  | a mid-file `#include`                            | hoist every include to the top           |
| 18  | `constexpr` for a size other code uses           | `#define` in `protocore_config.h`        |
| 19  | a function-local array, any size                 | borrow a `pc_span` from a pool           |
| 20  | `snprintf` / `vsnprintf`                         | a `pc_field` frame spec, or `pc_sb`      |
| 21  | a braceless `if` / `else` / `for` / `while` body | always brace the body                    |
| 22  | `virtual`, `: public`, RTTI, `std::function`     | the C11 object                           |
| 23  | the conditional expression `?:` in a body        | `if` / `else if` / `else`                |

## The bans

### 1. `strlen`

**Banned.** `strlen`.
**Use.** `strnlen(p, cap)`, or carry an explicit length.
**Why.** Unbounded read on a buffer that may not be terminated.
**Catch.** `check_src_banned.py`

### 2. `<stdlib.h>` and everything in it

**Banned.** `<stdlib.h>` / `<cstdlib>`, and `malloc` / `free` / `calloc` / `realloc`, `atoi` / `atol`
/ `strtol` / `strtoll` / `strtod`, `qsort`, `rand`, `abs`. `<stdio.h>` with them: its formatting is
#20 and its file I/O is the filesystem accessor's.
**Use.** Fixed BSS buffers; hand-rolled integer and float parse. `<string.h>` (`memcpy`, `memcmp`,
`memset`, `strnlen`) is allowed.
**Why.** No heap after `begin()`. Hidden allocation and locale-dependent parsing.
**Catch.** `check_src_banned.py`

### 3. `auto`

**Banned.** The `auto` keyword, anywhere, including a local.
**Use.** Spell the type. Suppress Sonar S5827, do not apply it.
**Why.** Hides the type and the conversions at the point they happen.
**Catch.** `check_src_banned.py`

### 4. `delay(...)`

**Banned.** `delay(...)`.
**Use.** `pcdelay(ms)` from `server/clock/clock.h`.
**Why.** Blocks the worker and bypasses the pluggable clock.
**Catch.** `check_src_banned.py`

### 5. Bare `millis()` for new timing

**Banned.** `millis()` in new timing code.
**Use.** `pc_millis()` from `server/clock/clock.h`.
**Why.** Bypasses the pluggable clock, so the timing cannot be driven in a test.
**Catch.** review, `rg -n '\bmillis\s*\(' src/`

### 6. Outside networking libraries and raw lwIP

**Banned.** `WiFiClient`, `WiFiUDP`, `AsyncUDP`, `ETH.*`, any outside networking library, and raw
lwIP (`udp_*` / `tcp_*` / `pbuf`) outside `transport/` and `tls/`. **Applies to `examples/` too.**
**Use.** TCP: `Tcp.client->open` / `->send` / `->read` / `->available` / `->is_closed` / `->close`
(`network_drivers/transport/tcp.h`). UDP: `Udp.listener->listen` / `->reply` / `->sendto`, and
`Udp.client->sendto` (`network_drivers/transport/udp.h`).
**Why.** Breaks the OSI layering and ties code to one platform stack.
**Catch.** review, `rg -n 'WiFiClient|WiFiUDP|AsyncUDP' src/ examples/`

### 7. Em-dashes

**Banned.** U+2014, the em-dash.
**Use.** A comma, parentheses, or a linking word.
**Why.** House style.
**Catch.** `check_src_banned.py`. An `rg` recipe must exclude this file, which names the codepoint.

### 8. `gmtime` / `localtime` / `ctime` / `asctime`

**Banned.** All four.
**Use.** The `_r` forms: `gmtime_r`, `localtime_r`. Never put a time bug in a test seam either.
**Why.** Non-reentrant: they return a pointer to one shared static.
**Catch.** `check_src_banned.py`

### 9. Casting an enum to an integer to make it compile

**Banned.** `(int)` / `(byte)` / `(uint8_t)` on an enum member to silence a type error.
**Use.** Propagate the enum type through the call chain. Cast only at the literal wire byte read or
written.
**Why.** Silences the type check at the point it was doing its job.
**Catch.** review any `(int)` / `(uint8_t)` on an enum

### 10. Signed-overflow UB in hand-rolled parsers

**Banned.** `v = v * 10 + d` on a signed int, `neg << 8`, and their kind.
**Use.** Accumulate in an unsigned type, guard against overflow, then apply the sign.
**Why.** Undefined behavior. `-fno-sanitize-recover=all` traps it.
**Catch.** `-fsanitize=undefined` in the pentest build

### 11. Uninitialized variables and out-params

**Banned.** A declaration that names storage without setting it. A `return false` guard does not
count as setting an out-param.
**Use.** Initialize at the declaration; write every out-param on **every** path, the false-return
path included. See [SRC_LAW.md](SRC_LAW.md) rule 7 for the two initializer forms.
**Why.** An object read before it is set has no value the standard defines.
**Catch.** Sonar, `-Wmaybe-uninitialized`

### 12. File-scope mutable state outside one owned `<Name>Ctx`

**Banned.** A mutable file-scope variable that is not a member of the file's single owned context
struct. A new `src/` file is designed with that context from the start (this is #16).
**Use.** One `<Name>Ctx` per file, declared in the header as an opaque tag and defined only in the
owning `.c`, so the layout never leaves the translation unit that carries its `static_assert` and
its `PC_WORK_*` budget. A wire or message struct is the opposite case and keeps its layout
published, because there the layout is the contract.
**Why.** Scattered ownership produces interlayer bugs.
**Catch.** `python ci_tooling/check/check_owned_context.py`

### 13. Back-compat shims and compat aliases

**Banned.** An alias, wrapper, or second spelling kept so old callers still compile. A module that
publishes both a namespace struct and its flat functions is a shim and this covers it.
**Use.** Make the breaking change and bump major.
**Why.** This library prefers industry best practice over internal back-compat.
**Catch.** review

### 14. British spelling

**Banned.** British spelling in identifiers and comments.
**Use.** American spelling everywhere.
**Why.** House style.
**Catch.** `cspell`

### 15. Duplicated string literals, inline config strings

**Banned.** The same literal written twice; a configurable or default string written at its use site.
**Use.** Dedup to a named `const`. Put config and default strings in `protocore_config.h` under a
`PC_ENABLE_*` guard.
**Why.** They drift, and an inline one cannot be configured.
**Catch.** review

### 17. A mid-file `#include`

**Banned.** An `#include` after any code. A vendor header behind a conditional in the core.
**Use.** Hoist every include to the top of the file. A vendor header belongs in `board_drivers/`.
**Why.** Makes the dependency graph read-order-dependent and hides the layering.
**Escape.** `// PC_ALLOW_LATE_INCLUDE: <reason>` on the include line, for an include that genuinely
derives from an earlier macro.
**Catch.** `check_src_banned.py`

### 18. `constexpr` for a value other code sizes itself against

**Banned.** `constexpr` for a capacity, size, or knob, unless the standard being implemented dictates
it.
**Use.** `#define` it in `protocore_config.h` (or the module header) under the usual `#ifndef` guard.
**Why.** Symbol-kind mismatch. A `#define` is an untyped preprocessor token; a `constexpr` is a typed
object the preprocessor cannot see, so it cannot appear in an `#if`, cannot set another knob's
default, and cannot fail at config time. Separately, a `constexpr` **function** is a permission
rather than a mandate: it may emit runtime evaluation with no diagnostic unless the call site forces
constant evaluation.
**Catch.** `check_src_banned.py`

### 19. A function-local array, any size

**Banned.** A function-local array (a stack buffer) of any size. `static` locals are BSS and belong
to #12.
**Use.** Borrow it. `pc_plaintext_mark()` / `pc_plaintext_span()` / `pc_plaintext_release()`
(`mmgr/plaintext.h`) for handler and I/O buffers; `pc_secure_mark()` / `pc_secure_span()` /
`pc_secure_release()` (`mmgr/secure.h`) for crypto leaf math, where the pool zeroes the region on
release so key material never outlives the call. Both hand back a `pc_span`, so the run length
travels with the pointer. Both fail closed with an empty span that `pc_span_ok()` catches.
**Why.** Stack is the one allocation the footprint cannot see. Every other byte is a fixed BSS pool
sized at config time, so peak DRAM is computable before flashing; a local array puts worst-case stack
depth outside that accounting, which is why the size of the array is irrelevant to the ban.
**Escape.** `// PC_ALLOW_STACK_ARRAY: <reason>` on the declaration line. Setup-time code is not the
target: a stack array inside `begin()` or a one-shot init path is exempt with
`// PC_ALLOW_STACK_ARRAY: begin()-time only`.
**Catch.** `check_src_banned.py` (ratcheted baseline)

### 20. `snprintf` / `vsnprintf`

**Banned.** Runtime format-string formatting.
**Use.** Declare the frame: a `static const pc_field[]` spec in `mmgr/frame.h` plus one
`pc_frame_build` / `pc_frame_append` call. The spec is pre-decoded data in rodata, so nothing is
parsed at runtime. For a short frame built by hand, the appenders it is built on are `pc_sb` in
`mmgr/membuild.h`: `pc_sb_put` / `pc_sb_u32` / `pc_sb_json` / `pc_sb_xml` bump-append into
a caller-owned buffer and latch `ok = false` the first time something would not fit, so overflow is
one flag test at `pc_sb_finish` instead of a truncation nobody notices.
**Why.** A format string makes the CPU re-parse at runtime what the code knew at compile time, and it
drags the libc float formatter into the image whether or not `%f` is ever used. Measurements:
[FEATURE_PERFORMANCE.md](FEATURE_PERFORMANCE.md) 2b. One case goes the other way: a **pure-literal**
frame is faster through `snprintf`, so do not wrap one in a spec.
**Escape.** `// PC_ALLOW_SNPRINTF: <reason>`
**Catch.** `check_src_banned.py` (ratcheted baseline)

### 21. A braceless body

**Banned.** A body after `if` / `else` / `for` / `while` with no braces, whether on the same line or
the next.
**Use.** Always brace the body. `InsertBraces: true` in `.clang-format` does it without changing
semantics, so the fix is a reformat rather than an edit. What it cannot reach is a condition split
across `#if` / `#else`, where the brace and its `if` land in different preprocessor branches.
**Why.** One statement silently becomes two: a line added to an unbraced body lands outside the
condition, still compiles, still looks right, and is wrong at runtime. It also breaks every
mechanical rewrite.
**Catch.** `check_src_banned.py` and `clang-format`

### 22. `virtual`, class hierarchies, RTTI, `std::function`

**Banned.** `virtual` functions, `: public`, `dynamic_cast`, `typeid`, `std::function`.
**Use.** **The C11 object, and it is the endorsed shape rather than an exception to this row.** An
opaque context plus a `static const` table of that concern's entry points, with the context carried
as a member of the table. `ProtoHandler` is the model: one table of
`{on_accept, on_data, on_close, on_poll}` per protocol. Modules join into the layer objects a caller
uses, so a call site reads `Network.auth.login(user, pass)`. Or walk a spec table (`pc_field`).
**Why.** The call target is not in the binary: a virtual call jumps through a value that does not
exist until runtime, so the worst-case path is unknown, the call cannot be devirtualized, and a
corrupted object turns every later call into an arbitrary jump. RTTI adds an unbounded runtime type
walk; `std::function` type-erases through an allocation. A `static const` table is written at compile
time, lives in rodata, and cannot be reassigned, so the reachable target set is fixed in the image
and the linker sees the whole closed list. That is the property this row protects.
**Catch.** `check_src_banned.py`

### 23. The conditional expression `?:` in a code body

**Banned.** `x = c ? a : b`, `return c ? a : b`, a chain `c1 ? a : c2 ? b : d`, or one nested inside
another expression.
**Use.** `if` / `else if` / `else`, one conditional per statement. Declare the variable with the value
the fall-through case wants, then assign inside the branch that owns it:

```c
size_t w = 1u;
if (n >= PC_SWAR_BYTES) { w = PC_SWAR_BYTES; }
else if (n >= 4u)       { w = 4u; }
```

For a selector between two implementations, brace both arms:
`if (ci) { return eq_ci(a, b, cap); } return eq_cs(a, b, cap);`
**Why.** MISRA bounds how many conditionals a line may carry. A ternary hides a branch inside an
expression, so the decision stops being visible where the value is set; a chain is an `if` / `else if`
ladder whose arms cannot be lined up against their conditions.
**Catch.** review, `rg -n '\?[^)]*:' src/`

## See also

- [SRC_LAW.md](SRC_LAW.md), the determinism and allocation law
- [SYMBOLS.md](SYMBOLS.md), the naming law and the checker commands
- [ARCHITECTURE.md](ARCHITECTURE.md), the module and layer shape the namespace struct builds
