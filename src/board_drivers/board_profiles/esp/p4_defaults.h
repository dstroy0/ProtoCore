// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file p4_defaults.h
 * @brief ESP32-P4 die profile - dual RISC-V + FPU up to 400 MHz, 768 KB L2MEM, no radio.
 *
 * The high-performance host MCU: dual RISC-V with FPU/AI, MIPI-CSI/DSI, USB-HS, and the largest
 * internal SRAM (768 KB L2MEM). It has NO built-in radio (pairs with a companion Wi-Fi/BT chip),
 * and is commonly fitted with high-bandwidth PSRAM up to 32 MB (a PSRAM profile, included first,
 * scales further). Full crypto HW: AES, SHA, RSA/MPI (4096-bit), ECC, ECDSA, HMAC, DS.
 * classic_defaults.h is the sizing floor; every macro is `#ifndef`-guarded.
 */

#ifndef PROTOCORE_P4_DEFAULTS_H
#define PROTOCORE_P4_DEFAULTS_H

// --- HW crypto accelerators (full suite, RSA up to 4096-bit) ---
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
#define PC_HW_ECC 1
#endif
#ifndef PC_HW_ECDSA
#define PC_HW_ECDSA 1
#endif
#ifndef PC_HW_HMAC
#define PC_HW_HMAC 1
#endif
#ifndef PC_HW_DS
#define PC_HW_DS 1
#endif

// --- Sizing (largest no-PSRAM bump: 768 KB L2MEM + fast dual core) ---
// Internal-SRAM-budget values; a PSRAM-size profile (included first) scales the RAM-backed buffers
// further and moves the big TLS / HTTP-2 pools to the (up to 32 MB) high-bandwidth PSRAM.

// Connection pools + per-connection buffers.
#ifndef MAX_CONNS
#define MAX_CONNS 16
#endif
#ifndef RX_BUF_SIZE
#define RX_BUF_SIZE 2048
#endif
#ifndef PC_SCRATCH_ARENA_SIZE
#define PC_SCRATCH_ARENA_SIZE 16384
#endif
#ifndef PC_CLIENT_RX_BUF
#define PC_CLIENT_RX_BUF 8192
#endif

// HTTP surface.
#ifndef MAX_ROUTES
#define MAX_ROUTES 48
#endif
#ifndef MAX_HEADERS
#define MAX_HEADERS 24
#endif
#ifndef BODY_BUF_SIZE
#define BODY_BUF_SIZE 2048
#endif

// WebSocket / SSE fan-out.
#ifndef MAX_WS_CONNS
#define MAX_WS_CONNS 8
#endif
#ifndef MAX_SSE_CONNS
#define MAX_SSE_CONNS 8
#endif

// TLS: one handshake on the internal-DRAM arena; a PSRAM profile raises this with the arena in PSRAM.
#ifndef MAX_TLS_CONNS
#define MAX_TLS_CONNS 1
#endif

// SSH server + reverse-SSH client.
#ifndef MAX_SSH_CONNS
#define MAX_SSH_CONNS 3
#endif
#ifndef PC_SSH_MAX_CHANNELS
#define PC_SSH_MAX_CHANNELS 6
#endif
#ifndef PC_SSH_CLIENT_MAX_CHANNELS
#define PC_SSH_CLIENT_MAX_CHANNELS 8
#endif

// Edge cache + mesh (RAM-backed L1).
#ifndef PC_EDGE_CACHE_SLOTS
#define PC_EDGE_CACHE_SLOTS 12
#endif
#ifndef PC_EDGE_BODY_MAX
#define PC_EDGE_BODY_MAX 8192
#endif
#ifndef PC_EDGE_FETCH_SLOTS
#define PC_EDGE_FETCH_SLOTS 4
#endif
#ifndef PC_MESH_MAX_PEERS
#define PC_MESH_MAX_PEERS 8
#endif
#ifndef PC_MESH_MAX_CONNS
#define PC_MESH_MAX_CONNS 2
#endif

#include "../classic_defaults.h"
#endif // PROTOCORE_P4_DEFAULTS_H
