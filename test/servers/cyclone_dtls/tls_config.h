// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
// Lean CycloneSSL build config for the DTLS 1.3 interop client (test tooling; see run_interop.sh).
#ifndef _TLS_CONFIG_H
#define _TLS_CONFIG_H

#ifndef GPL_LICENSE_TERMS_ACCEPTED
#define GPL_LICENSE_TERMS_ACCEPTED
#endif

// Client only.
#define TLS_CLIENT_SUPPORT ENABLED
#define TLS_SERVER_SUPPORT DISABLED

// Allow DTLS 1.2..1.3 to be built; runtime pins DTLS 1.3.
#define TLS_MIN_VERSION TLS_VERSION_1_2
#define TLS_MAX_VERSION TLS_VERSION_1_3

// Datagram transport (DTLS).
#define DTLS_SUPPORT ENABLED

// Key exchange / signatures the server negotiates. Only ECDHE is needed; disable the finite-field
// DH key exchanges so tls.h does not reference DhContext (crypto DH_SUPPORT is off).
#define TLS_ECDHE_ECDSA_KE_SUPPORT ENABLED
#define TLS_ECDHE_RSA_KE_SUPPORT ENABLED
#define TLS_RSA_KE_SUPPORT DISABLED
#define TLS_DHE_RSA_KE_SUPPORT DISABLED
#define TLS_ED25519_SIGN_SUPPORT ENABLED
#define TLS_ECDSA_SIGN_SUPPORT ENABLED
#define TLS_RSA_PSS_SIGN_SUPPORT ENABLED

// Named group + cipher: X25519 / TLS_AES_128_GCM_SHA256.
#define TLS_X25519_SUPPORT ENABLED
#define TLS_SECP256R1_SUPPORT ENABLED
#define TLS_GCM_CIPHER_SUPPORT ENABLED
#define TLS_AES_128_SUPPORT ENABLED
#define TLS_AES_256_SUPPORT ENABLED
#define TLS_SHA256_SUPPORT ENABLED
#define TLS_SHA384_SUPPORT ENABLED

// TLS 1.3 key-exchange modes. Disable only the finite-field DHE modes (ENABLED by default), which
// would pull in DhContext while crypto DH_SUPPORT is off. Leave ECDHE + PSK-ECDHE at their
// defaults so the TLS 1.3 key schedule (which references context->psk) stays consistent.
#define TLS13_DHE_KE_SUPPORT DISABLED
#define TLS13_PSK_DHE_KE_SUPPORT DISABLED

// RFC 7250 raw public keys (used by the --rpk variant).
#define TLS_RAW_PUBLIC_KEY_SUPPORT ENABLED

#endif
