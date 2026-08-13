# pimpl_bench - what an opaque cross-TU accessor costs, and what makes it free

The transport keeps its slot state behind an incomplete type, so the ring accessors are defined in
one translation unit and called from others. That is only free if the compiler can still inline
them. This harness answers when it can.

Run it:

    bash tools/dev_env/pimpl_bench/sweep.sh

## What it builds

| file                            | role                                                                                                |
| ------------------------------- | --------------------------------------------------------------------------------------------------- |
| `pub.h` / `use_pub.c`           | CONTROL: the fully-public struct with `static inline` accessors, the shape the transport had before |
| `opq.h` / `opq.c` / `use_opq.c` | TREATMENT: incomplete type, slot-index handle, accessor bodies in another TU                        |
| `main.c`                        | drives both so neither is dead-code eliminated, and checks they agree                               |
| `pad.c`                         | whether `sizeof(Internal)` being a power of two changes the `&s_internal[slot]` index math          |

`hot_opq` / `hot_pub` are `__attribute__((noinline))` on purpose: without it LTO folds them into
`main`, the disassembly is empty, and an empty function reads as "zero calls" whether the accessors
were inlined or not. `sweep.sh` matches symbols by PREFIX for the same class of reason - GCC's LTO
privatizes locals and renames them to `hot_opq.lto_priv.0`.

## Results, gcc 13.2 / gcc 16.1 / clang 22, x86-64

Calls to the accessors remaining inside the caller's hot loop:

| build                                        | opaque                                    | verdict       |
| -------------------------------------------- | ----------------------------------------- | ------------- |
| `-O0` `-O1` `-O2` `-O3` `-Os` `-Oz`, no LTO  | **2** at every level, all three compilers | never inlined |
| `-flto -O1`+ (gcc)                           | 0                                         | inlined       |
| `-flto -O2`+ (clang; still 2 at `-O1 -flto`) | 0                                         | inlined       |

Optimization level alone never does it: without LTO the accessor bodies are already machine code by
link time, so there is nothing left to inline from. This is why `-flto` is in `native_base` rather
than being a preference, and why `crypto_opt.h`'s `#pragma GCC optimize` does NOT substitute - that
raises the level for functions defined in a TU, which cannot make another TU's call site inline.

Instruction counts in the hot loop once LTO is on - the abstraction is not merely free:

| build              | opaque | public+inline |
| ------------------ | ------ | ------------- |
| gcc 13 `-Os -flto` | 22     | 22            |
| clang `-Os -flto`  | 24     | 40            |

## The index math

`&s_internal[slot]` is a multiply by `sizeof(Internal)` unless the size is a power of two.

| `sizeof`                      | `-Os`      | `-O2`       |
| ----------------------------- | ---------- | ----------- |
| 32 (natural, no explicit pad) | `shl`      | `shl`       |
| 40                            | **`imul`** | `lea`+`shl` |
| 64 (explicitly padded)        | `shl`      | `shl`       |

Two things follow. A realistic field set (`id`, two `_Atomic size_t`, a `Storage *`) already lands
on 32 through alignment padding alone, so an explicit pad is usually unnecessary - but assert
`sizeof` rather than counting by hand. And the multiply only appears at `-Os`, which is exactly the
level the Arduino framework appends after any env `build_flags`.

## Unity and LTO

`native_base` also carries `lib_archive = no`. Unity is meant to be compiled from source with the
project's own flags; a `.a` built without LTO IR cannot satisfy an LTO link, and the plugin drops
`UnityAssertEqualNumber` / `UnityFail` so every suite fails to link. Linking the objects directly
fixes it - and incidentally resolved a mingw `.weak.<name>.<anchor>` COMDAT collision that had been
failing `native_transport`, `native_tcp` and `native_workers_stack`.
