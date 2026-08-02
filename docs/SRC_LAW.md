# The src/ law

The determinism and allocation rules every `src/` file obeys, derived from the
safety-critical standards in the bibliography below. This is the **why**: the
principles and the field failures behind them.

Two companion documents carry the other halves, and the three do not overlap by
accident - each states the no-heap guarantee for its own reason:

- [SRCBANNED.md](SRCBANNED.md) - the **what**: the exact constructs banned in `src/`,
  enforced by `ci_tooling/check/check_src_banned.py`.
- [SYMBOLS.md](SYMBOLS.md) - the **naming** law, enforced by `ci_tooling/check/check_symbols.py`.

## 0. The language is C11

Everything below assumes it, so it comes first: **`src/` is C11, and C++ is banned there.** `.c` and
`.h` are the only extensions; `examples/` are Arduino sketches and `test/` is unconstrained.

The target list is the reason. It includes c2000, where control-law code is written and reviewed as
C, so a library that requires a C++ compiler cannot be used where it is needed most. A reviewer
reading `src/` should not have to know which of two languages a line is in to know what it costs.

It is also what makes the rest of this document decidable rather than advisory. Every rule below is
an argument about what the emitted instructions do, and each one is only finishable once the
language is fixed: rule 3's provable stack depth assumes no constructor runs where you did not write
a call; rule 9's "initialized at its declaration" means a value, not a constructor that may or may
not be elided; rule 10's short list of casts assumes no implicit conversion operator exists to
lengthen it; rule 12's `(void)f()` assumes `f` has one return path and not an exception edge. C++
does not make these unknowable, it makes them require a second body of rules to pin down - which is
exactly what the AUTOSAR C++14 guidelines in the bibliography are, and why they are cited as history
rather than as governing here.

The practical cost of relaxing it is that C++ fails softly in C code: `struct X;` lets a use spell
`X *` in C++ and not in C, `using X = T` compiles until a struct member needs the alias, and a
default argument is invisible at the call site. Each is one line, each takes every use of itself
down with it, and the resulting error count points anywhere except the cause.

## Real-time OS and object allocation

1. Zero Runtime Heap Allocation (Directive 4.12 / Rule A18-5-2)
    - Rule: The calling of 'malloc', 'calloc', 'realloc', 'free', 'new', or
      'delete' is strictly forbidden once the scheduler has started execution.
    - Exception: Allocation of memory is exclusively permitted during the system's
      pre-initialization phase prior to calling 'vTaskStartScheduler()'.
    - Rationale: Eliminates non-deterministic timing spikes caused by dynamic heap
      traversal and guarantees total immunity from runtime out-of-memory errors.

2. Mandatory Static Kernel Object Instantiation (FreeRTOS Specific)
    - Rule: All FreeRTOS system objects must be created via their explicit static
      creation symbols (e.g., 'xTaskCreateStatic', 'xQueueCreateStatic',
      'xSemaphoreCreateBinaryStatic'). Generic dynamic counterparts are banned.
    - Rationale: Forces the compiler to allocate control structures and stack regions
      directly within the BSS/Data segments, ensuring memory layouts are unalterable
      at runtime.

3. Static Thread Stack Sizing and Verification (Rule 18.8)
    - Rule: All task stacks and local hardware buffers must be bounded by a
      compile-time constant. Variable-Length Arrays (VLAs) are completely
      prohibited.
    - Rationale: Prevents unexpected runtime modifications of the CPU stack pointer,
      enforcing a mathematically provable maximum stack depth layout before deployment.

4. Banned Task Self-Termination and Deletion Symbols
    - Rule: System tasks must execute within an infinite loop ('while(true)') and
      are strictly forbidden from invoking 'vTaskDelete(NULL)' or self-terminating.
    - Rationale: Modifying the active task topology at runtime introduces scheduling
      jitter and variable execution paths, violating structural time-slice consistency.

## Time-slice determinism and control flow

5. Strict Function Execution Bound Timeouts (Rule A15-0-1 Alternate)
    - Rule: Any peripheral polling loop, bus read (I2C/SPI), or synchronization
      wait must include an explicit, deterministic tick or counter timeout. Infinite
      'while' blocks waiting on hardware status flags are banned.
    - Rationale: Ensures that if a physical hardware component fails or hangs, the
      software thread breaks out deterministically within its assigned time slice.

6. Total Prohibition of Unstructured Control Flow (The goto Ban)
    - Rule: The 'goto' symbol, as well as unstructured 'break' or 'continue' loops
      that alter regular conditional hierarchies, are completely banned.
    - Rationale: Guarantees that the control-flow graph remains perfectly linear
      and scannable, making execution times predictable for worst-case execution
      time (WCET) tools.

7. Banned Evaluative Increments (The Single Side-Effect Rule)
    - Rule: Inline increment and decrement symbols ('++', '--') are strictly banned
      from being placed inside larger logical expressions or array subscripts
      (e.g., write 'buffer[idx] = val; idx++;' instead of 'buffer[idx++] = val;').
    - Rationale: Eliminates variations in evaluation order across different compilers,
      ensuring the CPU cycles spent per expression remain entirely fixed.

8. Banned Return Type Deduction and Variant Types (Rule A8-4-13)
    - Rule: The 'auto' keyword for function return types is banned. Dynamically
      sized or variant types that change their footprint at runtime are prohibited.
    - Rationale: Enforces explicit data structures so that memory access boundaries
      and copy overheads are identical across every single operational time slice.

## Type conversion and explicit memory

9. Explicit Initialization at the Declaration (Rule 9.1)
    - Rule: Every object is given its value where it is declared. A scalar takes a
      literal of its own type ('proto_u32 scalar = 100U;'), an aggregate takes
      '= {0}'. A declaration that names storage without setting it is banned, and
      a later 'return false' guard does not count as setting it.
    - Rationale: An object read before it is set has no value the standard defines,
      so the instruction the compiler emits for that read is whatever was in the
      register or the stack slot. Setting it at the declaration removes the window
      entirely rather than relying on every path afterwards to close it.

10. Casts Appear Only at a Boundary (Rules 11.3, 11.4)
    - Rule: A cast is written where a value crosses out of the library's own type
      system: a byte read off the wire, a width narrowed on the way into a field, a
      vendor API that demands its own type. A cast written to make a type error
      compile is banned, and so is a cast that discards 'const' or converts between
      pointers to different object types.
    - Rationale: C performs the conversion either way; the cast only decides whether
      it is written down. Confining casts to the boundaries makes the set of places
      a width or a sign can change a short list a reviewer can read, instead of one
      that has to be searched for.

11. Banned Implicit Pointer-to-Boolean Evaluation
    - Rule: Evaluating a raw pointer or address symbol directly as a conditional
      boolean flag (e.g., 'if (dev_ptr)') is banned. Comparisons must be fully
      written out against 'NULL'.
    - Rationale: Eliminates type ambiguity during conditional branches, simplifying
      the binary instructions generated for execution testing.

12. Explicit Unused Return Serialization (Rule 17.7)
    - Rule: It is prohibited to call a non-void function without handling its
      return symbol. If a status value is intentionally skipped, the call is
      explicitly discarded with '(void)function();'.
    - Rationale: Guarantees that the omission of error checking is a conscious,
      documented engineering decision rather than a developer oversight.

## Preprocessor and structural integrity

13. Banned Dynamic Macro Redefinition (#undef)
    - Rule: Using the '#undef' directive to strip away or modify an established
      preprocessor identifier symbol is strictly forbidden.
    - Rationale: Guarantees that system feature configurations and conditional compilation
      flags remain completely static and immutable from the first line of code to the last.

14. Banned Variadic Code Blocks (The Ellipsis Ban / Rule A8-4-1)
    - Rule: The implementation of functions or macros accepting a variable number
      of parameters via ellipsis ('...') is completely banned.
    - Rationale: Variadic mechanics bypass compile-time type checks and introduce
      highly non-deterministic stack allocation sequences during runtime parsing.

15. Prohibition of Comment Terminating Backslashes (Rule A2-7-1)
    - Rule: A backslash character ('\') must never be used as the final character
      of a single-line comment block.
    - Rationale: Prevents the preprocessor from merging lines, which can accidentally
      comment out the subsequent line of functional execution code.

## Standards bibliography

These architectural constraints represent decades of industrial engineering
lessons learned from field failures, aerospace anomalies, and automotive recalls.
Learning what compliance is and what it looks like is an essential skill for any
engineer. Building anything to a standard makes it inherently easier to describe.
It follows rules, it's predictable, errors are more obvious and easier to trace.
It will (in theory) reliably and faithfully do the thing you told it to, barring
die errata, toolchain problems, linking errors, etc. The point is that you do things
one way and one way only: whatever the maintainers enforced style is. Sometimes, it's
safety critical, and then a committee of PhD EE and CE get together to select the
correct style for the project and where waivers may be needed. Some rules are
inviolable no matter what and your entire product becomes non-compliant if you
break one of those blocker level rules. These rules exist to make your control law
deterministic. Follow them, the organizations that promulgate them exist to save lives.
MISRA was born out of a government funded scheme in the UK to make abs controllers
deterministic to save lives, because malfunctioning brakes are dangerous. Building
things in or close to this style makes your code infinitely easier to debug. You can
guarantee behavior at a certain level and then it becomes really easy to point the
finger at that fooey func you hand rolled without rtfm for that libs api because it's
the length of a cvs receipt, plus you've been up all night and can't remember how to
grep (lol) pulling byte exact KAT from memory at 6am after working all night. Bad idea.
Never french fry when you should pizza. If you french fry when you should be pizzaing
you're gonna have a bad time.
These rules (not my be predictable and follow all the rules rant) are derived directly
from the following governing safety-critical publications:

1. MISRA C Guidelines:
    - "Guidelines for the use of the C language in critical and safety-related systems"
    - Published by MIRA (Motor Industry Research Association) / MISRA Consortium.
    - Foundational Standards: MISRA C:2004, MISRA C:2012 (with Amendments 1-4), and
      MISRA C:2023 / MISRA C:2025.

2. AUTOSAR C++14 Guidelines: **historical, not governing.**
    - "Guidelines for the use of the C++14 language in critical and safety-related systems"
    - Published by the AUTOSAR (AUTomotive Open System ARchitecture) Development Partnership.
    - Core Specification Referencing Release: Adaptive Platform Specifications (R17-03 through R19-11).
    - Cited because several rules above were first written against it, while the implementation
      was C++ under a C API, and their numbers are kept so the provenance stays traceable.
      MISRA C governs `src/` now (see section 0). Where the two disagree, MISRA C wins, and an
      AUTOSAR rule that only has meaning in C++ (A18-5-2's `new`/`delete`, A8-4-13's return-type
      deduction) is satisfied here by the language rather than by review.
      \================================================================================
