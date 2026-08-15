// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file s31_defaults.h
 * @brief ESP32-S31 die profile (PREVIEW) - dual RISC-V up to 320 MHz, 512 KB SRAM, multiprotocol.
 *
 * PREVIEW: the target exists in ESP-IDF `master` but not a stable release yet - re-verify specs at
 * release. Brings Bluetooth Classic back (BT 5.4 LE+Classic) alongside Wi-Fi 6, 802.15.4, and a
 * Gigabit-Ethernet MAC; an HMI/multiprotocol part. 512 KB SRAM, dual core, so a modest bump like
 * the S3. Full crypto HW: AES, SHA, RSA/MPI (4096-bit), ECC, ECDSA, HMAC, DS. Supports PSRAM.
 * classic_defaults.h is the sizing floor; every macro is `#ifndef`-guarded.
 */

#ifndef PROTOCORE_S31_DEFAULTS_H
#define PROTOCORE_S31_DEFAULTS_H

// --- HW crypto accelerators (full suite) ---
#ifndef PROTOCORE_HW_AES
#define PROTOCORE_HW_AES 1
#endif
#ifndef PROTOCORE_HW_SHA
#define PROTOCORE_HW_SHA 1
#endif
#ifndef PROTOCORE_HW_RSA
#define PROTOCORE_HW_RSA 1
#endif
#ifndef PROTOCORE_HW_ECC
#define PROTOCORE_HW_ECC 1
#endif
#ifndef PROTOCORE_HW_ECDSA
#define PROTOCORE_HW_ECDSA 1
#endif
#ifndef PROTOCORE_HW_HMAC
#define PROTOCORE_HW_HMAC 1
#endif
#ifndef PROTOCORE_HW_DS
#define PROTOCORE_HW_DS 1
#endif

// --- Sizing (modest bump like the S3; dual core, 512 KB SRAM) ---
// Internal-SRAM-budget values (no PSRAM assumed); a PSRAM-size profile, included first, scales the
// RAM-backed buffers further and moves the big TLS / HTTP-2 pools off-chip.

// Connection pools + per-connection buffers.
#ifndef MAX_CONNS
#define MAX_CONNS 12
#endif
#ifndef RX_BUF_SIZE
#define RX_BUF_SIZE 2048
#endif
#ifndef PROTOCORE_PLAINTEXT_SCRATCH
#define PROTOCORE_PLAINTEXT_SCRATCH 12288
#endif
#ifndef PROTOCORE_CLIENT_RX_BUF
#define PROTOCORE_CLIENT_RX_BUF 8192
#endif

// HTTP surface.
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

// TLS: one handshake on the internal-DRAM arena; a PSRAM profile raises this with the arena in PSRAM.
#ifndef MAX_TLS_CONNS
#define MAX_TLS_CONNS 1
#endif

// SSH server + reverse-SSH client.
#ifndef MAX_SSH_CONNS
#define MAX_SSH_CONNS 2
#endif
#ifndef PROTOCORE_SSH_MAX_CHANNELS
#define PROTOCORE_SSH_MAX_CHANNELS 4
#endif
#ifndef PROTOCORE_SSH_CLIENT_MAX_CHANNELS
#define PROTOCORE_SSH_CLIENT_MAX_CHANNELS 6
#endif

// Edge cache + mesh (RAM-backed L1).
#ifndef PROTOCORE_EDGE_CACHE_SLOTS
#define PROTOCORE_EDGE_CACHE_SLOTS 8
#endif
#ifndef PROTOCORE_EDGE_BODY_MAX
#define PROTOCORE_EDGE_BODY_MAX 4096
#endif
#ifndef PROTOCORE_EDGE_FETCH_SLOTS
#define PROTOCORE_EDGE_FETCH_SLOTS 3
#endif
#ifndef PROTOCORE_MESH_MAX_PEERS
#define PROTOCORE_MESH_MAX_PEERS 6
#endif
#ifndef PROTOCORE_MESH_MAX_CONNS
#define PROTOCORE_MESH_MAX_CONNS 2
#endif

#include "../classic_defaults.h"
#endif // PROTOCORE_S31_DEFAULTS_H
