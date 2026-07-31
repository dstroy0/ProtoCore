// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file c3_defaults.h
 * @brief ESP32-C3 die profile - single RISC-V, 400 KB SRAM, Wi-Fi 4 + BLE 5.0, no PSRAM.
 *
 * The mainstream single-core RISC-V drop-in for the classic ESP32. 400 KB SRAM with a roomier
 * usable-DRAM map than the classic die, so a small bump over the floor; single core keeps the
 * concurrency-bound pools modest. Crypto HW: AES, SHA, RSA/MPI, HMAC, DS (no ECC/ECDSA). No PSRAM.
 * classic_defaults.h is the sizing floor; every macro is `#ifndef`-guarded.
 */

#ifndef PROTOCORE_C3_DEFAULTS_H
#define PROTOCORE_C3_DEFAULTS_H

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

// --- Sizing (bump over the floor; single core, 400 KB SRAM) ---
// Internal-SRAM-budget values (no PSRAM assumed); a PSRAM-size profile, included first, scales the
// RAM-backed buffers further and moves the big TLS / HTTP-2 pools off-chip.

// Connection pools + per-connection buffers.
#ifndef MAX_CONNS
#define MAX_CONNS 12
#endif
#ifndef RX_BUF_SIZE
#define RX_BUF_SIZE 1536
#endif
#ifndef PC_SCRATCH_ARENA_SIZE
#define PC_SCRATCH_ARENA_SIZE 10240
#endif
#ifndef PC_CLIENT_RX_BUF
#define PC_CLIENT_RX_BUF 8192
#endif

// HTTP surface.
#ifndef MAX_ROUTES
#define MAX_ROUTES 24
#endif
#ifndef MAX_HEADERS
#define MAX_HEADERS 12
#endif
#ifndef BODY_BUF_SIZE
#define BODY_BUF_SIZE 512
#endif

// WebSocket / SSE fan-out.
#ifndef MAX_WS_CONNS
#define MAX_WS_CONNS 4
#endif
#ifndef MAX_SSE_CONNS
#define MAX_SSE_CONNS 4
#endif

// TLS: one handshake on the internal-DRAM arena; a PSRAM profile raises this with the arena in PSRAM.
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
#define PC_SSH_CLIENT_MAX_CHANNELS 4
#endif

// Edge cache + mesh (RAM-backed L1).
#ifndef PC_EDGE_CACHE_SLOTS
#define PC_EDGE_CACHE_SLOTS 6
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
#define PC_MESH_MAX_CONNS 1
#endif

#include "../classic_defaults.h"
#endif // PROTOCORE_C3_DEFAULTS_H
