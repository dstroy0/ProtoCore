// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
// Lean CycloneCRYPTO build config for the DTLS 1.3 interop client (test tooling; see run_interop.sh).
#ifndef _CRYPTO_CONFIG_H
#define _CRYPTO_CONFIG_H

#ifndef GPL_LICENSE_TERMS_ACCEPTED
#define GPL_LICENSE_TERMS_ACCEPTED
#endif

// Elliptic-curve + EdDSA primitives (X25519 key exchange, Ed25519 verify).
// ec.h defaults X25519_SUPPORT / ED25519_SUPPORT to DISABLED, so enable here.
#define EC_SUPPORT ENABLED
#define ECDH_SUPPORT ENABLED
#define ECDSA_SUPPORT ENABLED
#define CURVE25519_SUPPORT ENABLED
#define X25519_SUPPORT ENABLED
#define EDDSA_SUPPORT ENABLED
#define ED25519_SUPPORT ENABLED

// AEAD + hash for TLS_AES_128_GCM_SHA256; SHA-512 is needed by Ed25519.
#define AES_SUPPORT ENABLED
#define GCM_SUPPORT ENABLED
#define SHA1_SUPPORT ENABLED
#define SHA224_SUPPORT ENABLED
#define SHA256_SUPPORT ENABLED
#define SHA384_SUPPORT ENABLED
#define SHA512_SUPPORT ENABLED

// MAC / KDF / PRNG (HKDF for TLS 1.3 key schedule, HMAC_DRBG as the PRNG).
#define HMAC_SUPPORT ENABLED
#define HKDF_SUPPORT ENABLED
#define HMAC_DRBG_SUPPORT ENABLED

// X.509 parsing of the server certificate (verification is bypassed at runtime).
#define X509_SUPPORT ENABLED
#define PEM_SUPPORT ENABLED
#define RSA_SUPPORT ENABLED

#endif
