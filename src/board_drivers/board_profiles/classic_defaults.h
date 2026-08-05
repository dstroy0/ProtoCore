// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file classic_defaults.h
 * @brief Classic ESP32 die profile + the universal conservative sizing floor.
 *
 * Dual Xtensa LX6, 520 KB internal SRAM (~122 KB usable `dram0_0_seg` after IRAM/reserved),
 * no PSRAM by default. This is the smallest usable-DRAM budget of the supported targets, so its
 * sizing doubles as the floor every other chip profile includes last (they override upward).
 * Sizing values match the library's historical flat defaults, so classic-ESP32 and host builds
 * are unchanged.
 *
 * The crypto-HW flags below are the classic ESP32's accelerator set (AES, SHA, RSA/MPI - no
 * ECC/ECDSA/HMAC/DS). Every chip profile that includes this file as the floor first defines its
 * OWN `PC_HW_*` flags, so these apply only to the classic-ESP32 (and, harmlessly, host) path.
 * All macros are `#ifndef`-guarded, so a -D override or a richer variant profile always wins.
 */

#ifndef PROTOCORE_CLASSIC_DEFAULTS_H
#define PROTOCORE_CLASSIC_DEFAULTS_H

// --- HW crypto accelerators (classic ESP32: AES + SHA + RSA/MPI only) ---
#ifndef PC_HW_AES
#define PC_HW_AES 1
#endif
#ifndef PC_HW_SHA
#define PC_HW_SHA 1
#endif
#ifndef PC_HW_RSA
#define PC_HW_RSA 1 // the MPI/bignum accelerator
#endif
#ifndef PC_HW_ECC
#define PC_HW_ECC 0
#endif
#ifndef PC_HW_ECDSA
#define PC_HW_ECDSA 0
#endif
#ifndef PC_HW_HMAC
#define PC_HW_HMAC 0
#endif
#ifndef PC_HW_DS
#define PC_HW_DS 0 // Digital Signature peripheral
#endif

// --- Vector unit ---
// A register file wider than a GPR that the byte-lane primitives could be issued on. The classic
// die's LX6 has none, and neither does a host build, so the floor is off and mmgr/swar.h
// keeps doing its lane math in a general-purpose register. A profile raising this is asserting a
// real unit out of its own die's TRM, never inferring one from the core name.
#ifndef PC_HW_SIMD
#define PC_HW_SIMD 0
#endif
#ifndef PC_HW_SIMD_BYTES
#define PC_HW_SIMD_BYTES 0 // vector width in bytes; 0 when PC_HW_SIMD is 0
#endif

// --- Byte order ---
// Which end of a register the lowest-addressed byte lands on. One bit of information, named once
// here, so every direction in the library is written in terms of it: a lane count starts from the
// matching end, a funnel shift goes the matching way, a mask anchors at the matching side.
//
// Defaulted from the toolchain because the compiler must already agree with us about byte order - a
// build whose loads disagree is broken whatever we declare. A -D override exercises the other arm.
#ifndef PC_HW_BIG_ENDIAN
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define PC_HW_BIG_ENDIAN 1
#else
#define PC_HW_BIG_ENDIAN 0
#endif
#endif

// --- Register width ---
// Bits in this die's general-purpose register: the widest rung anything word-at-a-time may step in
// ONE instruction. Every part in the target list is 32-bit (xtensa LX6/LX7, riscv32, cortex-M,
// c2000), so that is the floor, and a 64-bit part raises it in its own profile.
//
// Declared rather than inferred from the toolchain, because the host toolchain is 64-bit and a host
// build would then take a shape that ships nowhere: 8-byte lane math, an 8-byte move ladder and
// 64-bit index arithmetic, none of which the target executes.
//
// Anything wider is not synthesized into one operation, it is done as n+1 of them: the move ladder
// in mmgr/protomem.h enters at this width and steps down, and a lane carrier above it
// is refused in protocore_config.h rather than compiled into half-registers the caller cannot see.
#ifndef PC_HW_WORD_BITS
#define PC_HW_WORD_BITS 32
#endif

// --- Unaligned load ---
// Whether this die's load instruction accepts an address that is not a multiple of the access
// width. Xtensa does not, and the compiler then synthesizes each unaligned word from byte loads plus
// shifts and ors, which is why shared_primitives/ aligns first and steps whole words after.
#ifndef PC_HW_UNALIGNED_LOAD
#define PC_HW_UNALIGNED_LOAD 0
#endif

// --- Edge cache (RAM-backed L1: each slot holds one cached object, ~2.6 KB) ---
#ifndef PC_EDGE_CACHE_SLOTS
#define PC_EDGE_CACHE_SLOTS 4 // L1 RAM entries
#endif
#ifndef PC_EDGE_BODY_MAX
#define PC_EDGE_BODY_MAX 2048 // largest cacheable body in bytes (per L1 entry)
#endif
#ifndef PC_EDGE_FETCH_SLOTS
#define PC_EDGE_FETCH_SLOTS 2 // concurrent in-flight origin fetches (<= PC_CLIENT_CONNS)
#endif

// --- Edge mesh (sibling-cache distribution) ---
#ifndef PC_MESH_MAX_PEERS
#define PC_MESH_MAX_PEERS 4 // sibling peers queried on a local miss (in series, first hit wins)
#endif
#ifndef PC_MESH_MAX_CONNS
#define PC_MESH_MAX_CONNS 1 // concurrent inbound peer-serve connections
#endif

#endif // PROTOCORE_CLASSIC_DEFAULTS_H
