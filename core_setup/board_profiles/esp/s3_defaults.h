// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
#define PROTOCORE_HW_ECC 0
#endif
#ifndef PROTOCORE_HW_ECDSA
#define PROTOCORE_HW_ECDSA 0
#endif
#ifndef PROTOCORE_HW_HMAC
#define PROTOCORE_HW_HMAC 1
#endif
#ifndef PROTOCORE_HW_DS
#define PROTOCORE_HW_DS 1
#endif

// --- SHA accelerator register map ---
// The die is identified here, so the values are stated here and crypto/ reads only these names. The
// block register offsets are shared across every die that carries the unified SHA peripheral and are
// stated once beside the driver; what differs per die is the peripheral base and the clock, reset and
// hold-clear bits below. Reset holds SHA while the sibling DS and HMAC resets are set, so both clear.
#ifndef PROTOCORE_SHA_BASE
#define PROTOCORE_SHA_BASE 0x6003B000u
#endif
#ifndef PROTOCORE_SHA_CLK_REG
#define PROTOCORE_SHA_CLK_REG 0x600C001Cu // SYSTEM_PERIP_CLK_EN1_REG
#endif
#ifndef PROTOCORE_SHA_CLK_BIT
#define PROTOCORE_SHA_CLK_BIT (1u << 2) // SYSTEM_CRYPTO_SHA_CLK_EN
#endif
#ifndef PROTOCORE_SHA_RST_REG
#define PROTOCORE_SHA_RST_REG 0x600C0024u // SYSTEM_PERIP_RST_EN1_REG
#endif
#ifndef PROTOCORE_SHA_RST_BIT
#define PROTOCORE_SHA_RST_BIT (1u << 2) // SYSTEM_CRYPTO_SHA_RST
#endif

// AES accelerator: the block register window and the clock/reset bits that bring it up.
// Drives crypto/cipher, crypto/aead and crypto/mac through core_setup/hal/esp/esp_aes_hal.h.
#ifndef PROTOCORE_AES_BASE
#define PROTOCORE_AES_BASE 0x6003A000u
#endif
#ifndef PROTOCORE_AES_CLK_REG
#define PROTOCORE_AES_CLK_REG 0x600C001Cu // SYSTEM_PERIP_CLK_EN1_REG
#endif
#ifndef PROTOCORE_AES_CLK_BIT
#define PROTOCORE_AES_CLK_BIT (1u << 1) // SYSTEM_CRYPTO_AES_CLK_EN
#endif
#ifndef PROTOCORE_AES_RST_REG
#define PROTOCORE_AES_RST_REG 0x600C0024u // SYSTEM_PERIP_RST_EN1_REG
#endif
#ifndef PROTOCORE_AES_RST_BIT
#define PROTOCORE_AES_RST_BIT (1u << 1) // SYSTEM_CRYPTO_AES_RST
#endif
#ifndef PROTOCORE_SHA_HOLD_REG
#define PROTOCORE_SHA_HOLD_REG 0x600C0024u // SYSTEM_PERIP_RST_EN1_REG (same reg)
#endif
#ifndef PROTOCORE_SHA_HOLD_CLEAR
#define PROTOCORE_SHA_HOLD_CLEAR ((1u << 4) | (1u << 5)) // SYSTEM_CRYPTO_DS_RST | SYSTEM_CRYPTO_HMAC_RST
#endif

// --- Vector unit ---
// The LX7's PIE (Processor Instruction Extensions): 128-bit vector registers with their own
// load/store path and byte-lane compares, which is a real unit rather than the wider GPR the classic
// die offers. Declared here because which silicon is present is this file's subject; what any of it
// is worth belongs to whatever consumes the flag and to a cycle counter, not to this header. A PIE
// load wants its pointer 16-byte aligned, so a consumer states that at its own boundary.
#ifndef PROTOCORE_HW_SIMD
#define PROTOCORE_HW_SIMD 1
#endif
#ifndef PROTOCORE_HW_SIMD_BYTES
#define PROTOCORE_HW_SIMD_BYTES 16
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
#ifndef PROTOCORE_PLAINTEXT_SCRATCH
#define PROTOCORE_PLAINTEXT_SCRATCH 12288
#endif
#ifndef PROTOCORE_CLIENT_RX_BUF
#define PROTOCORE_CLIENT_RX_BUF 8192
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

#include "../classic_defaults.h" // sizing floor for anything not set above
#endif                           // PROTOCORE_S3_DEFAULTS_H
