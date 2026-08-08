// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file h4_defaults.h
 * @brief ESP32-H4 die profile (PREVIEW) - dual RISC-V, 384 KB SRAM, BLE 5.4 + 802.15.4, no Wi-Fi.
 *
 * PREVIEW (target present since ESP-IDF v6.0, not a stable release - re-verify at release). The
 * dual-core H2 successor with more RAM and PSRAM support, for wireless-audio / Matter. No Wi-Fi;
 * BLE 5.4 + 802.15.4. Crypto HW is the odd one among the H-parts: AES, SHA, ECC, ECDSA, HMAC -
 * but NO RSA/MPI and NO DS. Stays at the floor (no Wi-Fi, 384 KB). classic_defaults.h is the sizing
 * floor; every macro is `#ifndef`-guarded.
 */

#ifndef PROTOCORE_H4_DEFAULTS_H
#define PROTOCORE_H4_DEFAULTS_H

// --- HW crypto accelerators (no RSA/MPI, no DS) ---
#ifndef PC_HW_AES
#define PC_HW_AES 1
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
#define PC_HW_ECDSA 1
#endif
#ifndef PC_HW_HMAC
#define PC_HW_HMAC 1
#endif
#ifndef PC_HW_DS
#define PC_HW_DS 0
#endif

// --- Sizing (bump over the floor; dual core, 384 KB SRAM, no Wi-Fi) ---
// Internal-SRAM-budget values (no PSRAM assumed); a PSRAM-size profile, included first, scales the
// RAM-backed buffers further and moves the big TLS / HTTP-2 pools off-chip.

// Connection pools + per-connection buffers.
#ifndef MAX_CONNS
#define MAX_CONNS 12
#endif
#ifndef RX_BUF_SIZE
#define RX_BUF_SIZE 2048
#endif
#ifndef PC_PLAINTEXT_ARENA_SIZE
#define PC_PLAINTEXT_ARENA_SIZE 10240
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
#endif // PROTOCORE_H4_DEFAULTS_H
