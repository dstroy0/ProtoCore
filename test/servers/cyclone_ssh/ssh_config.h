// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
// Test tooling only, NOT part of the library; builds against Oryx CycloneSSH/CycloneCRYPTO
// (GPLv2), fetched at run time by run_interop.sh - never vendored here.
#ifndef _SSH_CONFIG_H
#define _SSH_CONFIG_H

// ssh.h includes ssh_config.h first, before core/net.h pulls in os_port.h. Pull os_port.h in here
// so ENABLED/DISABLED (and the TRACE_LEVEL_* constants via debug conventions) are defined for the
// macros below.
#include "os_port.h"

#ifndef GPL_LICENSE_TERMS_ACCEPTED
#define GPL_LICENSE_TERMS_ACCEPTED
#endif

// --- role: client only ---
#define SSH_CLIENT_SUPPORT ENABLED
#define SSH_SERVER_SUPPORT DISABLED

// --- trace: library progress (KEX/auth) on stderr; command output stays on stdout ---
#define SSH_TRACE_LEVEL TRACE_LEVEL_INFO
#define SHELL_TRACE_LEVEL TRACE_LEVEL_INFO

// --- reuse CycloneSSH's own shell client as the client FSM ---
#define SHELL_CLIENT_SUPPORT ENABLED
#define SHELL_SERVER_SUPPORT DISABLED

// --- one connection, one channel ---
#define SSH_MAX_CONNECTIONS 1

// --- authentication: password + public key ---
#define SSH_PASSWORD_AUTH_SUPPORT ENABLED
#define SSH_PUBLIC_KEY_AUTH_SUPPORT ENABLED
#define SSH_CERT_SUPPORT DISABLED
#define SSH_ENCRYPTED_KEY_SUPPORT DISABLED

// --- protocol extensions ---
#define SSH_EXT_INFO_SUPPORT ENABLED
#define SSH_KEX_STRICT_SUPPORT ENABLED

// --- key exchange: curve25519-sha256 (primary) + ecdh-sha2-nistp256 (secondary) ---
#define SSH_ECDH_KEX_SUPPORT ENABLED
#define SSH_RSA_KEX_SUPPORT DISABLED
#define SSH_DH_KEX_SUPPORT DISABLED
#define SSH_DH_GEX_KEX_SUPPORT DISABLED
#define SSH_KEM_KEX_SUPPORT DISABLED
#define SSH_HYBRID_KEX_SUPPORT DISABLED

// --- named curves for ECDH KEX ---
#define SSH_CURVE25519_SUPPORT ENABLED
#define SSH_NISTP256_SUPPORT ENABLED
#define SSH_NISTP384_SUPPORT DISABLED
#define SSH_NISTP521_SUPPORT DISABLED
#define SSH_CURVE448_SUPPORT DISABLED

// --- host-key / signature algorithms: ssh-ed25519 (primary), plus ecdsa/rsa for breadth ---
#define SSH_ED25519_SIGN_SUPPORT ENABLED
#define SSH_ED448_SIGN_SUPPORT DISABLED
#define SSH_ECDSA_SIGN_SUPPORT ENABLED
#define SSH_RSA_SIGN_SUPPORT ENABLED
#define SSH_DSA_SIGN_SUPPORT DISABLED

// --- ciphers: chacha20-poly1305 + aes256-gcm/aes128-gcm (AEAD) + aes*-ctr (with HMAC) ---
#define SSH_GCM_CIPHER_SUPPORT ENABLED
#define SSH_CHACHA20_POLY1305_SUPPORT ENABLED
#define SSH_CTR_CIPHER_SUPPORT ENABLED
#define SSH_CBC_CIPHER_SUPPORT DISABLED
#define SSH_STREAM_CIPHER_SUPPORT DISABLED
#define SSH_AES_128_SUPPORT ENABLED
#define SSH_AES_192_SUPPORT DISABLED
#define SSH_AES_256_SUPPORT ENABLED

// --- hashes / MAC: hmac-sha2-256 (primary) + hmac-sha2-512 ---
#define SSH_SHA1_SUPPORT ENABLED
#define SSH_SHA224_SUPPORT DISABLED
#define SSH_SHA256_SUPPORT ENABLED
#define SSH_SHA384_SUPPORT DISABLED
#define SSH_SHA512_SUPPORT ENABLED

#endif
