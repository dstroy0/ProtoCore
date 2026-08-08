// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file s3_defaults.h
 * @brief ESP32-S3 die profile - dual Xtensa LX7, 512 KB SRAM, Wi-Fi 4 + BLE 5.0, Octal PSRAM.
 *
 * Roomier usable DRAM than the classic ESP32 plus AI/vector DSP instructions, so the RAM-backed
 * pools get a modest bump even with no PSRAM fitted (a PSRAM profile, included first, scales them
 * further). Crypto HW: AES, SHA, RSA/MPI, HMAC, DS (no ECC/ECDSA). classic_defaults.h is pulled
 * in last as the sizing floor; every macro is `#ifndef`-guarded so a -D override wins.
 */

#ifndef PROTOCORE_S3_DEFAULTS_H
#define PROTOCORE_S3_DEFAULTS_H

// --- HW crypto accelerators ---
#ifndef PC_HW_AES
#define PC_HW_AES 1
#endif
#ifndef PC_HW_SHA
#define PC_HW_SHA 1
#endif
#ifndef PC_HW_RSA
#define PC_HW_RSA 1
#endif
#ifndef PC_HW_ECC
#define PC_HW_ECC 0
#endif
#ifndef PC_HW_ECDSA
#define PC_HW_ECDSA 0
#endif
#ifndef PC_HW_HMAC
#define PC_HW_HMAC 1
#endif
#ifndef PC_HW_DS
#define PC_HW_DS 1
#endif

// --- Vector unit ---
// The LX7's PIE (Processor Instruction Extensions): 128-bit vector registers with their own
// load/store path and byte-lane compares, which is a real unit rather than the wider GPR the classic
// die offers. Declared here because which silicon is present is this file's subject; what any of it
// is worth belongs to whatever consumes the flag and to a cycle counter, not to this header. A PIE
// load wants its pointer 16-byte aligned, so a consumer states that at its own boundary.
#ifndef PC_HW_SIMD
#define PC_HW_SIMD 1
#endif
#ifndef PC_HW_SIMD_BYTES
#define PC_HW_SIMD_BYTES 16
#endif

// --- Sizing (bumped over the classic floor to use the S3's ~400 KB usable DRAM) ---
// These are internal-SRAM-budget values (no PSRAM assumed); a PSRAM-size profile, included first,
// scales the RAM-backed buffers further and moves the big TLS / HTTP-2 pools off-chip.

// Connection pools + per-connection buffers.
#ifndef MAX_CONNS
#define MAX_CONNS 12
#endif
#ifndef RX_BUF_SIZE
#define RX_BUF_SIZE 2048
#endif
#ifndef PC_PLAINTEXT_ARENA_SIZE
#define PC_PLAINTEXT_ARENA_SIZE 12288
#endif
#ifndef PC_CLIENT_RX_BUF
#define PC_CLIENT_RX_BUF 8192
#endif

// HTTP surface (roomier route/header/body budgets).
#ifndef MAX_ROUTES
#define MAX_ROUTES 32
#endif
#ifndef MAX_HEADERS
#define MAX_HEADERS 16
#endif
#ifndef BODY_BUF_SIZE
#define BODY_BUF_SIZE 1024
#endif

// WebSocket / SSE fan-out.
#ifndef MAX_WS_CONNS
#define MAX_WS_CONNS 4
#endif
#ifndef MAX_SSE_CONNS
#define MAX_SSE_CONNS 4
#endif

// TLS: one handshake on the internal-DRAM arena (the ~48 KB arena + ~32 KB/extra conn would overflow
// internal DRAM); a PSRAM profile raises this with the arena moved off-chip and auto-sized.
#ifndef MAX_TLS_CONNS
#define MAX_TLS_CONNS 1
#endif

// SSH server + reverse-SSH client.
#ifndef MAX_SSH_CONNS
#define MAX_SSH_CONNS 2
#endif
#ifndef PC_SSH_MAX_CHANNELS
#define PC_SSH_MAX_CHANNELS 4
#endif
#ifndef PC_SSH_CLIENT_MAX_CHANNELS
#define PC_SSH_CLIENT_MAX_CHANNELS 6
#endif

// Edge cache + mesh (RAM-backed L1).
#ifndef PC_EDGE_CACHE_SLOTS
#define PC_EDGE_CACHE_SLOTS 8
#endif
#ifndef PC_EDGE_BODY_MAX
#define PC_EDGE_BODY_MAX 4096
#endif
#ifndef PC_EDGE_FETCH_SLOTS
#define PC_EDGE_FETCH_SLOTS 3
#endif
#ifndef PC_MESH_MAX_PEERS
#define PC_MESH_MAX_PEERS 6
#endif
#ifndef PC_MESH_MAX_CONNS
#define PC_MESH_MAX_CONNS 2
#endif

#include "../classic_defaults.h" // sizing floor for anything not set above
#endif                           // PROTOCORE_S3_DEFAULTS_H
