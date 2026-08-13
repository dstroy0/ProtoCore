// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file h21_defaults.h
 * @brief ESP32-H21 die profile (PREVIEW) - single RISC-V, 320 KB SRAM, BLE 5 + 802.15.4, no Wi-Fi.
 *
 * PREVIEW: the target exists in ESP-IDF `master` but not a stable release yet - re-verify at
 * release. An ultra-low-power BLE / 802.15.4 part with an integrated DC-DC; no Wi-Fi. 320 KB SRAM,
 * single core, so it stays at the conservative floor. Full crypto HW: AES, SHA, RSA/MPI, ECC,
 * ECDSA, HMAC, DS. No PSRAM. classic_defaults.h is the sizing floor; every macro is `#ifndef`-guarded.
 */

#ifndef PROTOCORE_H21_DEFAULTS_H
#define PROTOCORE_H21_DEFAULTS_H

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

// --- Sizing (conservative: single core, 320 KB SRAM, no Wi-Fi) ---
// Internal-SRAM-budget values (no PSRAM assumed); a PSRAM-size profile, included first, scales the
// RAM-backed buffers further and moves the big TLS / HTTP-2 pools off-chip.

// Connection pools + per-connection buffers.
#ifndef MAX_CONNS
#define MAX_CONNS 8
#endif
#ifndef RX_BUF_SIZE
#define RX_BUF_SIZE 1024
#endif
#ifndef PROTOCORE_PLAINTEXT_SCRATCH
#define PROTOCORE_PLAINTEXT_SCRATCH 8192
#endif
#ifndef PROTOCORE_CLIENT_RX_BUF
#define PROTOCORE_CLIENT_RX_BUF 4096
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
#ifndef PROTOCORE_SSH_MAX_CHANNELS
#define PROTOCORE_SSH_MAX_CHANNELS 2
#endif
#ifndef PROTOCORE_SSH_CLIENT_MAX_CHANNELS
#define PROTOCORE_SSH_CLIENT_MAX_CHANNELS 2
#endif

// Edge cache + mesh (RAM-backed L1).
#ifndef PROTOCORE_EDGE_CACHE_SLOTS
#define PROTOCORE_EDGE_CACHE_SLOTS 4
#endif
#ifndef PROTOCORE_EDGE_BODY_MAX
#define PROTOCORE_EDGE_BODY_MAX 2048
#endif
#ifndef PROTOCORE_EDGE_FETCH_SLOTS
#define PROTOCORE_EDGE_FETCH_SLOTS 2
#endif
#ifndef PROTOCORE_MESH_MAX_PEERS
#define PROTOCORE_MESH_MAX_PEERS 4
#endif
#ifndef PROTOCORE_MESH_MAX_CONNS
#define PROTOCORE_MESH_MAX_CONNS 1
#endif

#include "../classic_defaults.h"
#endif // PROTOCORE_H21_DEFAULTS_H
