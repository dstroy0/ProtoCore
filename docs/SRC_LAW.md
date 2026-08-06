# The src/ law

The determinism and allocation rules every `src/` file obeys, derived from the safety-critical
standards in the bibliography. Two companion documents carry the other halves:

- [SRCBANNED.md](SRCBANNED.md) - the exact constructs banned in `src/`, enforced by
  `tools/ci_tooling/check/check_src_banned.py`.
- [SYMBOLS.md](SYMBOLS.md) - the naming law, enforced by `tools/ci_tooling/check/check_symbols.py`.

## 0. The language is C11

**`src/` is C11.** `.c` and `.h` are the only extensions, with the three `board_drivers/` vendor
adapters SYMBOLS.md section 4 records. `examples/` are Arduino sketches; `test/` is unconstrained.
The target list and the reasoning are in SYMBOLS.md.

It comes first because it is what makes the rest decidable rather than advisory. Every rule below is
an argument about what the emitted instructions do, and each is only finishable once the language is
fixed: rule 2's provable stack depth assumes no constructor runs where you did not write a call;
rule 7's "initialized at its declaration" means a value, not a constructor that may be elided;
rule 8's short list of casts assumes no implicit conversion operator exists to lengthen it;
rule 10's `(void)f()` assumes `f` has one return path and not an exception edge.

The rules are renumbered 1 to 13 here. Two FreeRTOS-specific rules were dropped: they governed
`xTaskCreateStatic` and `vTaskDelete`, which appear nowhere in `src/`.

## Real-time and object allocation

1. **Zero runtime heap allocation** (MISRA Directive 4.12)
    - Rule: `malloc`, `calloc`, `realloc`, and `free` are never called. There is no
      pre-initialization exception: `check_src_banned.py` bans both the calls and the
      `<stdlib.h>` include outright, so every byte the library owns is BSS reserved at link time.
    - Rationale: removes non-deterministic timing spikes from heap traversal and gives total
      immunity from runtime out-of-memory.

2. **Static stack sizing, no VLAs** (Rule 18.8)
    - Rule: all task stacks and local hardware buffers are bounded by a compile-time constant.
      Variable-length arrays are prohibited. Function-local arrays are separately banned
      ([SRCBANNED.md](SRCBANNED.md) #19).
    - Rationale: makes maximum stack depth provable before deployment.

## Time-slice determinism and control flow

3. **Every hardware wait carries a timeout** (Rule A15-0-1 alternate)
    - Rule: a peripheral polling loop, a bus read, or a synchronization wait has an explicit tick or
      counter timeout. An infinite `while` on a hardware status flag is banned.
    - Rationale: a failed or hung part breaks the thread out inside its assigned time slice.

4. **No unstructured control flow** (the `goto` ban)
    - Rule: `goto` is banned, as is a `break` or `continue` that alters the regular conditional
      hierarchy.
    - Rationale: keeps the control-flow graph scannable, so worst-case execution time is computable.

5. **No evaluative increments** (the single side-effect rule)
    - Rule: `++` and `--` do not appear inside a larger expression or an array subscript. Write
      `buffer[idx] = val; idx++;`, never `buffer[idx++] = val;`.
    - Rationale: removes evaluation-order variation between compilers.

6. **No return-type deduction or variant types** (Rule A8-4-13)
    - Rule: `auto` is banned everywhere, including a local ([SRCBANNED.md](SRCBANNED.md) #3).
      Dynamically sized or variant types that change footprint at runtime are prohibited.
    - Rationale: keeps memory access boundaries and copy overheads identical across every time slice.

## Type conversion and explicit memory

7. **Explicit initialization at the declaration** (Rule 9.1)
    - Rule: every object is given its value where it is declared. A scalar takes a literal of its own
      type (`proto_u32 scalar = 100U;`), an aggregate takes `= {0}`. A declaration that names storage
      without setting it is banned, and a later `return false` guard does not count as setting it.
    - Rationale: an object read before it is set has no value the standard defines, so the read emits
      whatever was in the register or the stack slot. Setting it at the declaration removes the
      window rather than relying on every path afterwards to close it.

8. **Casts appear only at a boundary** (Rules 11.3, 11.4)
    - Rule: a cast is written where a value crosses out of the library's own type system: a byte read
      off the wire, a width narrowed into a field, a vendor API that demands its own type. A cast
      written to make a type error compile is banned, and so is one that discards `const` or converts
      between pointers to different object types.
    - Rationale: C performs the conversion either way; the cast only decides whether it is written
      down. Confining casts to boundaries makes the set of places a width or a sign can change a
      short list a reviewer can read.

9. **No implicit pointer-to-boolean evaluation**
    - Rule: `if (dev_ptr)` is banned. Comparisons are written out against `NULL`.
    - Rationale: removes type ambiguity at the branch.

10. **Explicit unused-return serialization** (Rule 17.7)
    - Rule: a non-void function is never called without handling its return. An intentionally skipped
      status is discarded with `(void)function();`.
    - Rationale: makes an omitted error check a documented decision rather than an oversight.

## Preprocessor and structural integrity

11. **No `#undef`**
    - Rule: `#undef` never strips or modifies an established preprocessor identifier.
    - Rationale: keeps feature configuration static from the first line of code to the last.

12. **No variadics** (Rule A8-4-1)
    - Rule: functions and macros taking `...` are banned.
    - Rationale: ellipsis bypasses compile-time type checks and introduces non-deterministic stack
      sequences during runtime parsing.

13. **No comment-terminating backslash** (Rule A2-7-1)
    - Rule: `\` is never the final character of a single-line comment.
    - Rationale: prevents the preprocessor merging the next line into the comment.

## Guarantees are proven at the binary

Where ProtoCore promises a behavior, the promise is checked against the emitted instructions, not
argued from the source. C makes the source readable as cost, but reading is not proof. The claims
that get this treatment are the ones a caller depends on:

- **Constant-time** comparisons and crypto: no branch and no memory access depends on a secret.
- **No heap after `begin()`**: no allocator call reachable in the relevant `.text`.
- **Bounded interrupt and critical-section paths**: a counted worst case, not an estimate.

Each is documented as claim, then disassembly, then why the disassembly establishes the claim. A
guarantee without that chain is a comment, and comments do not survive a compiler upgrade.

## Standards bibliography

1. **MISRA C** - "Guidelines for the use of the C language in critical and safety-related systems",
   MIRA / MISRA Consortium. MISRA C:2004, MISRA C:2012 (Amendments 1-4), MISRA C:2023 / C:2025.
   **Governing.**

2. **AUTOSAR C++14** - "Guidelines for the use of the C++14 language in critical and safety-related
   systems", AUTOSAR Development Partnership, R17-03 through R19-11. **Historical, not governing.**
   Several rules above were first written against it while the implementation was C++ under a C API,
   and their numbers are kept so the provenance stays traceable. Where the two disagree, MISRA C
   wins, and an AUTOSAR rule with meaning only in C++ is satisfied here by the language.
