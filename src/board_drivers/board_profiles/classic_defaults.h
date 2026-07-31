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
