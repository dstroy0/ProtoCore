// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
// Test tooling only, NOT part of the library; builds against Oryx CycloneSSH/CycloneCRYPTO
// (GPLv2), fetched at run time by run_interop.sh - never vendored here.
#ifndef _CRYPTO_CONFIG_H
#define _CRYPTO_CONFIG_H

#ifndef GPL_LICENSE_TERMS_ACCEPTED
#define GPL_LICENSE_TERMS_ACCEPTED
#endif

// Elliptic-curve + EdDSA primitives.
//   - X25519 : curve25519-sha256 key exchange
//   - Ed25519: ssh-ed25519 host-key verify
//   - EC/ECDH/ECDSA: ecdh-sha2-nistp256 KEX + ecdsa-sha2-nistp256 host key (secondary)
#define EC_SUPPORT ENABLED
#define ECDH_SUPPORT ENABLED
#define ECDSA_SUPPORT ENABLED
#define CURVE25519_SUPPORT ENABLED
#define X25519_SUPPORT ENABLED
#define EDDSA_SUPPORT ENABLED
#define ED25519_SUPPORT ENABLED

// Symmetric ciphers / AEAD.
//   - AES + GCM  : aes256-gcm@openssh.com / aes128-gcm@openssh.com and aes*-ctr
//   - ChaCha20 + Poly1305: chacha20-poly1305@openssh.com
#define AES_SUPPORT ENABLED
#define GCM_SUPPORT ENABLED
#define CTR_SUPPORT ENABLED
#define CHACHA_SUPPORT ENABLED
#define POLY1305_SUPPORT ENABLED
#define CHACHA20_POLY1305_SUPPORT ENABLED

// Hashes. SHA-256 for curve25519-sha256 + hmac-sha2-256; SHA-512 required by Ed25519.
#define SHA1_SUPPORT ENABLED
#define SHA224_SUPPORT ENABLED
#define SHA256_SUPPORT ENABLED
#define SHA384_SUPPORT ENABLED
#define SHA512_SUPPORT ENABLED

// MAC / KDF / PRNG. HMAC for hmac-sha2-256/512; HMAC_DRBG is the CSPRNG (seeded from urandom).
#define HMAC_SUPPORT ENABLED
#define HKDF_SUPPORT ENABLED
#define HMAC_DRBG_SUPPORT ENABLED

// RSA host keys (ssh-rsa / rsa-sha2-256 / rsa-sha2-512) for interop breadth.
#define RSA_SUPPORT ENABLED

// X.509 not used by the SSH client, but PEM/key import paths reference these.
#define X509_SUPPORT ENABLED
#define PEM_SUPPORT ENABLED

#endif
