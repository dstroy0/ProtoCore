// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file c2_defaults.h
 * @brief ESP32-C2 (ESP8684) die profile - single RISC-V, 272 KB SRAM, Wi-Fi 4 + BLE 5.0, no PSRAM.
 *
 * The cheapest/smallest die: 272 KB SRAM is tighter than the classic ESP32's usable DRAM, so it
 * stays at the conservative floor (no sizing bump). Reduced crypto: SHA + ECC only - there is NO
 * general-purpose AES peripheral (AES exists only as the XTS flash-encryption engine) and NO
 * RSA/MPI, HMAC or DS, so secure boot is ECC-based. Do not assume "any ESP32 has AES/RSA". No PSRAM.
 * classic_defaults.h is pulled in last as the sizing floor; every macro is `#ifndef`-guarded.
 */

#ifndef PROTOCORE_C2_DEFAULTS_H
#define PROTOCORE_C2_DEFAULTS_H

// --- HW crypto accelerators (reduced: SHA + ECC only; no general AES, no RSA/HMAC/DS) ---
#ifndef PC_HW_AES
#define PC_HW_AES 0
#endif
#ifndef PC_HW_SHA
#define PC_HW_SHA 1
#endif
#ifndef PC_HW_RSA
#define PC_HW_RSA 0
#endif
#ifndef PC_HW_ECC
#define PC_HW_ECC 1
#endif
#ifndef PC_HW_ECDSA
#define PC_HW_ECDSA 0
#endif
#ifndef PC_HW_HMAC
#define PC_HW_HMAC 0
#endif
#ifndef PC_HW_DS
#define PC_HW_DS 0
#endif

// --- Sizing (conservative: single core, 272 KB SRAM - the tightest die) ---
// Internal-SRAM-budget values (no PSRAM assumed); a PSRAM-size profile, included first, scales the
// RAM-backed buffers further and moves the big TLS / HTTP-2 pools off-chip.

// Connection pools + per-connection buffers.
#ifndef MAX_CONNS
#define MAX_CONNS 8
#endif
#ifndef RX_BUF_SIZE
#define RX_BUF_SIZE 1024
#endif
#ifndef PC_SCRATCH_ARENA_SIZE
#define PC_SCRATCH_ARENA_SIZE 8192
#endif
#ifndef PC_CLIENT_RX_BUF
#define PC_CLIENT_RX_BUF 4096
#endif

// HTTP surface.
#ifndef MAX_ROUTES
#define MAX_ROUTES 16
#endif
#ifndef MAX_HEADERS
#define MAX_HEADERS 8
#endif
#ifndef BODY_BUF_SIZE
#define BODY_BUF_SIZE 256
#endif

// WebSocket / SSE fan-out.
#ifndef MAX_WS_CONNS
#define MAX_WS_CONNS 2
#endif
#ifndef MAX_SSE_CONNS
#define MAX_SSE_CONNS 2
#endif

// TLS: a single handshake fits the tight internal SRAM; a PSRAM profile raises this and moves the arena.
#ifndef MAX_TLS_CONNS
#define MAX_TLS_CONNS 1
#endif

// SSH server + reverse-SSH client.
#ifndef MAX_SSH_CONNS
#define MAX_SSH_CONNS 1
#endif
#ifndef PC_SSH_MAX_CHANNELS
#define PC_SSH_MAX_CHANNELS 2
#endif
#ifndef PC_SSH_CLIENT_MAX_CHANNELS
#define PC_SSH_CLIENT_MAX_CHANNELS 2
#endif

// Edge cache + mesh (RAM-backed L1).
#ifndef PC_EDGE_CACHE_SLOTS
#define PC_EDGE_CACHE_SLOTS 4
#endif
#ifndef PC_EDGE_BODY_MAX
#define PC_EDGE_BODY_MAX 2048
#endif
#ifndef PC_EDGE_FETCH_SLOTS
#define PC_EDGE_FETCH_SLOTS 2
#endif
#ifndef PC_MESH_MAX_PEERS
#define PC_MESH_MAX_PEERS 4
#endif
#ifndef PC_MESH_MAX_CONNS
#define PC_MESH_MAX_CONNS 1
#endif

#include "../classic_defaults.h"
#endif // PROTOCORE_C2_DEFAULTS_H
