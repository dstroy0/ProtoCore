// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file crypto_opt.h
 * @brief Per-translation-unit optimization override for hot, pure-integer crypto.
 *
 * The library ships at the arduino framework's `-Os` (the framework appends it AFTER any PlatformIO env
 * `build_flags`, so a plain `-O2` there is overridden). `-Os` roughly halves the throughput of the software
 * ciphers/MACs. Put @ref PC_CRYPTO_HOT at the top of such a `.cpp` (after its includes) to force a higher
 * level for that one translation unit, regardless of the consumer's size-optimized build:
 *
 * @code
 *   #include "crypto/cipher/chacha20.h"
 *   #include "crypto/crypto_opt.h"
 *   PC_CRYPTO_HOT   // this TU builds at -O2 (or the configured level)
 * @endcode
 *
 * The level is `PC_CRYPTO_OPT_LEVEL`: `2` or `3`, or `0` to inherit the framework `-Os`. It is NOT a
 * ServerConfig / user knob - it is internal crypto tuning with a measured per-die default (see below), which
 * a hot TU can override for its own build with `#define PC_CRYPTO_OPT_LEVEL 3` before this include. Only GCC
 * honors `#pragma GCC optimize`; clang/other compilers get a no-op and inherit their normal level.
 *
 * When a TU's `-O3` win bisects (on-device) to a SINGLE named transform, prefer the deliberate
 * `PC_CRYPTO_HOT_PEEL` / `PC_CRYPTO_HOT_UNSWITCH` pin (the `-O2` floor + just that transform) over full
 * `-O3`, scoped inside the TU's own die guard - same speed, none of `-O3`'s extra code-size / miscompile risk:
 * @code
 *   #include "crypto/crypto_opt.h"
 *   #if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
 *   PC_CRYPTO_HOT_UNSWITCH   // S3: the -O3 win here is entirely -funswitch-loops (bisected)
 *   #else
 *   PC_CRYPTO_HOT            // every other die: the per-die default
 *   #endif
 * @endcode
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * CAVEATS - read before applying this to a new file
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * 1. CONSTANT TIME. Apply this ONLY to code that is constant-time by STRUCTURE - no secret-dependent
 *    branches and no secret-dependent memory indexing: stream ciphers (ChaCha20), MACs (Poly1305), hashes.
 *    Do NOT put it on scalar-multiplication / bignum / point-arithmetic code that relies on branchless
 *    mask-selects to stay constant-time: an aggressive optimizer can (rarely, but it is documented) turn a
 *    mask-select into a data-dependent branch and reintroduce a timing side channel. Those paths are also
 *    HW-accelerator-dominated (RSA/MPI MODMULT, HW AES/SHA), so the `-O` level buys them almost nothing -
 *    all risk, no reward. When in doubt, leave it off.
 *
 * 2. SIZE. `-O2`/`-O3` inline and unroll aggressively -> larger flash and IRAM; `-O3` more so. On the
 *    classic ESP32 (tight internal DRAM/IRAM, where TLS example builds already run close to the limit)
 *    prefer level `2` or `0`.
 *
 * 3. `-O3` IS NOT UNIVERSALLY FASTER than `-O2` here - it is per-die and per-algo. The bigger unrolled code
 *    can thrash the instruction / flash cache and regress (e.g. the S3's Ed25519 sign is ~1.2% SLOWER at
 *    `-O3`), and `-O3` widens the miscompile / latent-UB surface. So full `-O3` is taken only where it was
 *    MEASURED to help wholesale (the P4, whose win is `-O3`'s parameter budget - see below); on the S3 the
 *    win of a given TU is one transform, taken via a `PC_CRYPTO_HOT_*` pin, and `-O2` is the floor elsewhere.
 */

#ifndef PROTOCORE_CRYPTO_OPT_H
#define PROTOCORE_CRYPTO_OPT_H

#include "board_drivers/board_profiles/pc_platform.h" // PROTOCORE_HOT, tested below

#if defined(__GNUC__) && !defined(__clang__)
#if PROTOCORE_HOT
#include "sdkconfig.h" // CONFIG_IDF_TARGET_* - the per-die default below + the single-transform die guards in the TUs
#endif
#ifndef PC_CRYPTO_OPT_LEVEL
// Per-variant default, measured on the crypto bench (main_cryptobench). The ESP32-P4 (RISC-V) is faster or flat
// at -O3 across the whole software crypto suite (chacha -22.9%, poly -15.6%, x25519 -6.8%, ed25519 -4.5%; the HW
// ops flat) - and its win is -O3's larger inline / unroll PARAMETER budget, not any one -f transform (verified:
// -O3 with all 14 O2->O3 delta flags disabled still hits the full -O3 numbers), so the P4 takes -O3 wholesale.
// Every other die defaults to -O2 (the big -Os -> -O2 win); the S3's few -O3 wins are each a SINGLE transform,
// pinned per-TU with the PC_CRYPTO_HOT_* macros below (see ecdsa/chacha20/hmac_sha256).
#if defined(CONFIG_IDF_TARGET_ESP32P4) && CONFIG_IDF_TARGET_ESP32P4
#define PC_CRYPTO_OPT_LEVEL 3
#else
#define PC_CRYPTO_OPT_LEVEL 2
#endif
#endif
#if PC_CRYPTO_OPT_LEVEL == 3
#define PC_CRYPTO_HOT _Pragma("GCC optimize(\"O3\")")
#elif PC_CRYPTO_OPT_LEVEL == 2
#define PC_CRYPTO_HOT _Pragma("GCC optimize(\"O2\")")
#else
#define PC_CRYPTO_HOT // 0 / other: inherit the framework -Os
#endif
// Deliberate single-transform pins: the -O2 floor + exactly ONE -O3 transform that (alone, not full -O3) was
// MEASURED to carry a TU's win - capturing it without -O3's code-size / miscompile / cache-thrash baggage. A TU
// selects one inside its own die guard (e.g. #if CONFIG_IDF_TARGET_ESP32S3) and uses PC_CRYPTO_HOT otherwise.
// This is a closed set (add one only after bisecting the win to a named flag on-device); it is NOT a user knob.
#define PC_CRYPTO_HOT_PEEL _Pragma("GCC optimize(\"O2\",\"peel-loops\")")
#define PC_CRYPTO_HOT_UNSWITCH _Pragma("GCC optimize(\"O2\",\"unswitch-loops\")")
#else
#define PC_CRYPTO_HOT          // non-GCC: no per-TU pragma; inherit the toolchain default
#define PC_CRYPTO_HOT_PEEL     // "
#define PC_CRYPTO_HOT_UNSWITCH // "
#endif

#endif // PROTOCORE_CRYPTO_OPT_H
