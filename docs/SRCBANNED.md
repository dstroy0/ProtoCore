# Banned in `src/`

Numbers are stable and cited from source comments; #16 is folded into #12 and keeps its number.

| #   | Banned                                                                                                                                                                             |
| --- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | `strlen`                                                                                                                                                                           |
| 2   | `<stdlib.h>` `<cstdlib>` `malloc` `calloc` `realloc` `free` `aligned_alloc` `atoi` `atol` `atoll` `strtol` `strtoll` `strtoul` `strtoull` `strtod` `strtof` `qsort` `rand` `srand` |
| 3   | `auto`                                                                                                                                                                             |
| 4   | `delay`                                                                                                                                                                            |
| 5   | `millis`                                                                                                                                                                           |
| 6   | `udp_*` `tcp_*` `pbuf` outside `transport/` and `tls/`, and any outside networking library                                                                                         |
| 7   | U+2014                                                                                                                                                                             |
| 8   | `gmtime` `localtime` `ctime` `asctime`                                                                                                                                             |
| 9   | `(int)` `(byte)` `(uint8_t)` on an enum member                                                                                                                                     |
| 10  | signed `+` `*` `<<` that can overflow                                                                                                                                              |
| 11  | a declaration with no initializer                                                                                                                                                  |
| 12  | a file-scope mutable outside one `<Name>Ctx`                                                                                                                                       |
| 13  | an alias, a wrapper, a second spelling                                                                                                                                             |
| 14  | British spelling                                                                                                                                                                   |
| 15  | a repeated string literal                                                                                                                                                          |
| 16  | (see #12)                                                                                                                                                                          |
| 17  | `#include` after code                                                                                                                                                              |
| 18  | `constexpr`                                                                                                                                                                        |
| 19  | `T name[N]` at function scope                                                                                                                                                      |
| 20  | `snprintf` `vsnprintf`                                                                                                                                                             |
| 21  | an unbraced `if` `else` `for` `while` body                                                                                                                                         |
| 22  | `virtual` `: public` `: protected` `: private` `dynamic_cast` `typeid` `std::function`                                                                                             |
| 23  | `?:`                                                                                                                                                                               |
| 24  | `memcpy` `memmove` `memcmp` `memchr` `memset`                                                                                                                                      |
| 25  | `<stdio.h>` `<cstdio>` `<string.h>` `<cstring>`                                                                                                                                    |

## Exceptions

- `// PC_ALLOW_LATE_INCLUDE: <reason>` (#17)
- `// PC_ALLOW_STACK_ARRAY: <reason>` (#19)
- `// PC_ALLOW_SNPRINTF: <reason>` (#20)
