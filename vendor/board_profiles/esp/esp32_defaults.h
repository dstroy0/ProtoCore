// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp32_defaults.h
 * @brief Classic ESP32 die profile - dual Xtensa LX6, 520 KB SRAM, Wi-Fi 4 + BT 4.2, no PSRAM assumed.
 *
 * The static data of the classic die has the smallest ceiling of the ESP family. This profile sets
 * the sizes of the pools that sit in that static data. A PSRAM profile comes first and sets its own
 * sizes. classic_defaults.h comes last as the floor. Each macro has an `#ifndef` guard, so a -D
 * override wins.
 */

#ifndef PROTOCORE_ESP32_DEFAULTS_H
#define PROTOCORE_ESP32_DEFAULTS_H

// --- Connection pools ---
// stack_profile.h gives the classic die 2 TCBs, so the server pool keeps 2 connections.
#ifndef MAX_CONNS
#define MAX_CONNS 2
#endif

// --- idemIP stack ---
// The counts that idemIP reads are in vendor/board_profiles/stack_profile.h.
// The transmit segments of each TCP connection of the stack. Each segment holds one write.
#ifndef PROTOCORE_STACK_TCP_SEGMENTS
#define PROTOCORE_STACK_TCP_SEGMENTS 2u
#endif
// The receive buffer of each TCP connection of the stack. The receive window is this count less one.
#ifndef PROTOCORE_STACK_NET_RX_BYTES
#define PROTOCORE_STACK_NET_RX_BYTES 1024u
#endif
// The packet buffers that carry received text to the transport.
#ifndef PROTOCORE_STACK_NET_PBUFS
#define PROTOCORE_STACK_NET_PBUFS 2u
#endif
// The TCP control blocks that listen, and the UDP control blocks.
#ifndef PROTOCORE_STACK_NET_LISTENERS
#define PROTOCORE_STACK_NET_LISTENERS 2u
#endif
#ifndef PROTOCORE_STACK_NET_UDP_PCBS
#define PROTOCORE_STACK_NET_UDP_PCBS 4u
#endif

// --- Scratch ---
// The default scratch holds the deepest nest of the SSH receive path. A build with no SSH and no TLS has a
// shallower nest, and 4096 octets hold it. A build that turns SSH or TLS on gets the default.
#if !PROTOCORE_ENABLE_SSH && !PROTOCORE_ENABLE_TLS
#ifndef PROTOCORE_PLAINTEXT_SCRATCH
#define PROTOCORE_PLAINTEXT_SCRATCH 4096
#endif
#endif

// --- Features ---
// Each feature that protocore_config.h turns on is off here, except the HTTP parser, which the core feeds on
// every build. A build turns on what it needs with a compile definition or a build_opt.h, as each demo does.
#ifndef PROTOCORE_ENABLE_SHA1
#define PROTOCORE_ENABLE_SHA1 0
#endif
#ifndef PROTOCORE_ENABLE_SHA256
#define PROTOCORE_ENABLE_SHA256 0
#endif
#ifndef PROTOCORE_ENABLE_SHA512
#define PROTOCORE_ENABLE_SHA512 0
#endif
#ifndef PROTOCORE_ENABLE_MD
#define PROTOCORE_ENABLE_MD 0
#endif
#ifndef PROTOCORE_ENABLE_HMAC_SHA256
#define PROTOCORE_ENABLE_HMAC_SHA256 0
#endif
#ifndef PROTOCORE_ENABLE_HMAC_SHA512
#define PROTOCORE_ENABLE_HMAC_SHA512 0
#endif
#ifndef PROTOCORE_ENABLE_POLY1305
#define PROTOCORE_ENABLE_POLY1305 0
#endif
#ifndef PROTOCORE_ENABLE_AES_CMAC
#define PROTOCORE_ENABLE_AES_CMAC 0
#endif
#ifndef PROTOCORE_ENABLE_GHASH
#define PROTOCORE_ENABLE_GHASH 0
#endif
#ifndef PROTOCORE_ENABLE_CT_EQ
#define PROTOCORE_ENABLE_CT_EQ 0
#endif
#ifndef PROTOCORE_ENABLE_RAWMEMCPY
#define PROTOCORE_ENABLE_RAWMEMCPY 0
#endif
#ifndef PROTOCORE_ENABLE_SSH_RSA
#define PROTOCORE_ENABLE_SSH_RSA 0
#endif
#ifndef PROTOCORE_ENABLE_BASE64
#define PROTOCORE_ENABLE_BASE64 0
#endif
#ifndef PROTOCORE_ENABLE_KDF
#define PROTOCORE_ENABLE_KDF 0
#endif
#ifndef PROTOCORE_ENABLE_AESGCM
#define PROTOCORE_ENABLE_AESGCM 0
#endif
#ifndef PROTOCORE_ENABLE_CHACHAPOLY
#define PROTOCORE_ENABLE_CHACHAPOLY 0
#endif
#ifndef PROTOCORE_ENABLE_CHACHA20
#define PROTOCORE_ENABLE_CHACHA20 0
#endif
#ifndef PROTOCORE_ENABLE_AES256CTR
#define PROTOCORE_ENABLE_AES256CTR 0
#endif
#ifndef PROTOCORE_ENABLE_AES_BLOCK
#define PROTOCORE_ENABLE_AES_BLOCK 0
#endif
#ifndef PROTOCORE_ENABLE_AES_SBOX
#define PROTOCORE_ENABLE_AES_SBOX 0
#endif
#ifndef PROTOCORE_ENABLE_BIGNUM
#define PROTOCORE_ENABLE_BIGNUM 0
#endif
#ifndef PROTOCORE_ENABLE_CURVE25519
#define PROTOCORE_ENABLE_CURVE25519 0
#endif
#ifndef PROTOCORE_ENABLE_FE25519
#define PROTOCORE_ENABLE_FE25519 0
#endif
#ifndef PROTOCORE_ENABLE_ECDSA
#define PROTOCORE_ENABLE_ECDSA 0
#endif
#ifndef PROTOCORE_ENABLE_ED25519
#define PROTOCORE_ENABLE_ED25519 0
#endif
#ifndef PROTOCORE_ENABLE_RSA
#define PROTOCORE_ENABLE_RSA 0
#endif
#ifndef PROTOCORE_ENABLE_RNG
#define PROTOCORE_ENABLE_RNG 0
#endif

#endif // PROTOCORE_ESP32_DEFAULTS_H
