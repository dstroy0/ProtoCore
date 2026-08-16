// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file h2_defaults.h
 * @brief ESP32-H2 die profile - single RISC-V, 320 KB SRAM, BLE 5.0 + 802.15.4, NO Wi-Fi.
 *
 * A Thread/Zigbee/Matter radio part: 802.15.4 + BLE 5.0, no Wi-Fi. 320 KB SRAM, single core, so it
 * stays at the conservative floor. Full crypto HW: AES, SHA, RSA/MPI, ECC, ECDSA, HMAC, DS. No
 * PSRAM. classic_defaults.h is the sizing floor; every macro is `#ifndef`-guarded.
 */

#ifndef PROTOCORE_H2_DEFAULTS_H
#define PROTOCORE_H2_DEFAULTS_H

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

// --- SHA accelerator register map ---
// The die is identified here, so the values are stated here and crypto/ reads only these names. Clock
// and reset share one PCR register and nothing else holds SHA, so the hold-clear mask is empty.
#ifndef PROTOCORE_SHA_BASE
#define PROTOCORE_SHA_BASE 0x60089000u
#endif
#ifndef PROTOCORE_SHA_CLK_REG
#define PROTOCORE_SHA_CLK_REG 0x600960C8u // PCR_SHA_CONF_REG
#endif
#ifndef PROTOCORE_SHA_CLK_BIT
#define PROTOCORE_SHA_CLK_BIT (1u << 0) // PCR_SHA_CLK_EN
#endif
#ifndef PROTOCORE_SHA_RST_REG
#define PROTOCORE_SHA_RST_REG 0x600960C8u // PCR_SHA_CONF_REG (same reg)
#endif
#ifndef PROTOCORE_SHA_RST_BIT
#define PROTOCORE_SHA_RST_BIT (1u << 1) // PCR_SHA_RST_EN
#endif

// AES accelerator: the block register window and the clock/reset bits that bring it up.
// Drives crypto/cipher, crypto/aead and crypto/mac through core_setup/hal/esp/esp_aes_hal.h.
#ifndef PROTOCORE_AES_BASE
#define PROTOCORE_AES_BASE 0x60088000u
#endif
#ifndef PROTOCORE_AES_CLK_REG
#define PROTOCORE_AES_CLK_REG 0x600960C4u // PCR_AES_CONF_REG
#endif
#ifndef PROTOCORE_AES_CLK_BIT
#define PROTOCORE_AES_CLK_BIT (1u << 0) // PCR_AES_CLK_EN
#endif
#ifndef PROTOCORE_AES_RST_REG
#define PROTOCORE_AES_RST_REG 0x600960C4u // PCR_AES_CONF_REG (same reg)
#endif
#ifndef PROTOCORE_AES_RST_BIT
#define PROTOCORE_AES_RST_BIT (1u << 1) // PCR_AES_RST_EN
#endif
#ifndef PROTOCORE_SHA_HOLD_REG
#define PROTOCORE_SHA_HOLD_REG 0x600960C8u
#endif
#ifndef PROTOCORE_SHA_HOLD_CLEAR
#define PROTOCORE_SHA_HOLD_CLEAR 0u
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
#endif // PROTOCORE_H2_DEFAULTS_H
