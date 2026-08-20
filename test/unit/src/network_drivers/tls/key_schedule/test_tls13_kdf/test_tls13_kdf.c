// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the TLS 1.3 key schedule (network_drivers/tls/key_schedule/key_schedule.h).
//
// RFC 8448 sec 3 "Simple 1-RTT Handshake" prints every intermediate value of the RFC 8446 sec 7.1
// schedule for one real connection: the early secret, each "derived" step, the handshake and master
// secrets, the four traffic secrets, the finished key, and the resulting Finished verify_data.
// test_rfc8448_secret_chain replays that whole chain from the trace's own (EC)DHE input and
// transcript hashes and compares each secret to the octets the RFC prints, so a wrong label, a
// wrong prefix, or a swapped Extract salt/IKM cannot pass. The transcript hashes are recomputed
// from the trace's own handshake messages and checked against the hashes the RFC publishes for the
// same derivations, so the inputs are the RFC's too.

#include "crypto/hash/sha256/sha256.h"
#include "network_drivers/tls/key_schedule/key_schedule.h"
#include <string.h>

#include <unity.h>

// The schedule's terms plus the bytes its HKDF works out of. It must arrive zeroed: TLS13_KS_ZEROS
// is the first Extract's IKM and nothing ever writes it.
static uint8_t g_ks_bytes[PROTOCORE_TLS13_KS_BORROW] __attribute__((aligned(8)));
static uint8_t g_hkdf_work[PROTOCORE_HKDF_BORROW] __attribute__((aligned(8)));
static uint8_t g_hash_work[PROTOCORE_SHA256_BORROW] __attribute__((aligned(4)));
static Tls13KeySchedule g_ks;

void setUp(void)
{
    memset(g_ks_bytes, 0, sizeof(g_ks_bytes));
    memset(&g_ks, 0, sizeof(g_ks));
}
void tearDown(void)
{
}

// RFC 8448 sec 3, the server's handshake flight in transcript order. Copied verbatim from the trace.
static const uint8_t RFC8448_FLIGHT[943] = {
    // ClientHello (196 octets)
    0x01,
    0x00,
    0x00,
    0xc0,
    0x03,
    0x03,
    0xcb,
    0x34,
    0xec,
    0xb1,
    0xe7,
    0x81,
    0x63,
    0xba,
    0x1c,
    0x38,
    0xc6,
    0xda,
    0xcb,
    0x19,
    0x6a,
    0x6d,
    0xff,
    0xa2,
    0x1a,
    0x8d,
    0x99,
    0x12,
    0xec,
    0x18,
    0xa2,
    0xef,
    0x62,
    0x83,
    0x02,
    0x4d,
    0xec,
    0xe7,
    0x00,
    0x00,
    0x06,
    0x13,
    0x01,
    0x13,
    0x03,
    0x13,
    0x02,
    0x01,
    0x00,
    0x00,
    0x91,
    0x00,
    0x00,
    0x00,
    0x0b,
    0x00,
    0x09,
    0x00,
    0x00,
    0x06,
    0x73,
    0x65,
    0x72,
    0x76,
    0x65,
    0x72,
    0xff,
    0x01,
    0x00,
    0x01,
    0x00,
    0x00,
    0x0a,
    0x00,
    0x14,
    0x00,
    0x12,
    0x00,
    0x1d,
    0x00,
    0x17,
    0x00,
    0x18,
    0x00,
    0x19,
    0x01,
    0x00,
    0x01,
    0x01,
    0x01,
    0x02,
    0x01,
    0x03,
    0x01,
    0x04,
    0x00,
    0x23,
    0x00,
    0x00,
    0x00,
    0x33,
    0x00,
    0x26,
    0x00,
    0x24,
    0x00,
    0x1d,
    0x00,
    0x20,
    0x99,
    0x38,
    0x1d,
    0xe5,
    0x60,
    0xe4,
    0xbd,
    0x43,
    0xd2,
    0x3d,
    0x8e,
    0x43,
    0x5a,
    0x7d,
    0xba,
    0xfe,
    0xb3,
    0xc0,
    0x6e,
    0x51,
    0xc1,
    0x3c,
    0xae,
    0x4d,
    0x54,
    0x13,
    0x69,
    0x1e,
    0x52,
    0x9a,
    0xaf,
    0x2c,
    0x00,
    0x2b,
    0x00,
    0x03,
    0x02,
    0x03,
    0x04,
    0x00,
    0x0d,
    0x00,
    0x20,
    0x00,
    0x1e,
    0x04,
    0x03,
    0x05,
    0x03,
    0x06,
    0x03,
    0x02,
    0x03,
    0x08,
    0x04,
    0x08,
    0x05,
    0x08,
    0x06,
    0x04,
    0x01,
    0x05,
    0x01,
    0x06,
    0x01,
    0x02,
    0x01,
    0x04,
    0x02,
    0x05,
    0x02,
    0x06,
    0x02,
    0x02,
    0x02,
    0x00,
    0x2d,
    0x00,
    0x02,
    0x01,
    0x01,
    0x00,
    0x1c,
    0x00,
    0x02,
    0x40,
    0x01,
    // ServerHello (90 octets)
    0x02,
    0x00,
    0x00,
    0x56,
    0x03,
    0x03,
    0xa6,
    0xaf,
    0x06,
    0xa4,
    0x12,
    0x18,
    0x60,
    0xdc,
    0x5e,
    0x6e,
    0x60,
    0x24,
    0x9c,
    0xd3,
    0x4c,
    0x95,
    0x93,
    0x0c,
    0x8a,
    0xc5,
    0xcb,
    0x14,
    0x34,
    0xda,
    0xc1,
    0x55,
    0x77,
    0x2e,
    0xd3,
    0xe2,
    0x69,
    0x28,
    0x00,
    0x13,
    0x01,
    0x00,
    0x00,
    0x2e,
    0x00,
    0x33,
    0x00,
    0x24,
    0x00,
    0x1d,
    0x00,
    0x20,
    0xc9,
    0x82,
    0x88,
    0x76,
    0x11,
    0x20,
    0x95,
    0xfe,
    0x66,
    0x76,
    0x2b,
    0xdb,
    0xf7,
    0xc6,
    0x72,
    0xe1,
    0x56,
    0xd6,
    0xcc,
    0x25,
    0x3b,
    0x83,
    0x3d,
    0xf1,
    0xdd,
    0x69,
    0xb1,
    0xb0,
    0x4e,
    0x75,
    0x1f,
    0x0f,
    0x00,
    0x2b,
    0x00,
    0x02,
    0x03,
    0x04,
    // EncryptedExtensions (40 octets)
    0x08,
    0x00,
    0x00,
    0x24,
    0x00,
    0x22,
    0x00,
    0x0a,
    0x00,
    0x14,
    0x00,
    0x12,
    0x00,
    0x1d,
    0x00,
    0x17,
    0x00,
    0x18,
    0x00,
    0x19,
    0x01,
    0x00,
    0x01,
    0x01,
    0x01,
    0x02,
    0x01,
    0x03,
    0x01,
    0x04,
    0x00,
    0x1c,
    0x00,
    0x02,
    0x40,
    0x01,
    0x00,
    0x00,
    0x00,
    0x00,
    // Certificate (445 octets)
    0x0b,
    0x00,
    0x01,
    0xb9,
    0x00,
    0x00,
    0x01,
    0xb5,
    0x00,
    0x01,
    0xb0,
    0x30,
    0x82,
    0x01,
    0xac,
    0x30,
    0x82,
    0x01,
    0x15,
    0xa0,
    0x03,
    0x02,
    0x01,
    0x02,
    0x02,
    0x01,
    0x02,
    0x30,
    0x0d,
    0x06,
    0x09,
    0x2a,
    0x86,
    0x48,
    0x86,
    0xf7,
    0x0d,
    0x01,
    0x01,
    0x0b,
    0x05,
    0x00,
    0x30,
    0x0e,
    0x31,
    0x0c,
    0x30,
    0x0a,
    0x06,
    0x03,
    0x55,
    0x04,
    0x03,
    0x13,
    0x03,
    0x72,
    0x73,
    0x61,
    0x30,
    0x1e,
    0x17,
    0x0d,
    0x31,
    0x36,
    0x30,
    0x37,
    0x33,
    0x30,
    0x30,
    0x31,
    0x32,
    0x33,
    0x35,
    0x39,
    0x5a,
    0x17,
    0x0d,
    0x32,
    0x36,
    0x30,
    0x37,
    0x33,
    0x30,
    0x30,
    0x31,
    0x32,
    0x33,
    0x35,
    0x39,
    0x5a,
    0x30,
    0x0e,
    0x31,
    0x0c,
    0x30,
    0x0a,
    0x06,
    0x03,
    0x55,
    0x04,
    0x03,
    0x13,
    0x03,
    0x72,
    0x73,
    0x61,
    0x30,
    0x81,
    0x9f,
    0x30,
    0x0d,
    0x06,
    0x09,
    0x2a,
    0x86,
    0x48,
    0x86,
    0xf7,
    0x0d,
    0x01,
    0x01,
    0x01,
    0x05,
    0x00,
    0x03,
    0x81,
    0x8d,
    0x00,
    0x30,
    0x81,
    0x89,
    0x02,
    0x81,
    0x81,
    0x00,
    0xb4,
    0xbb,
    0x49,
    0x8f,
    0x82,
    0x79,
    0x30,
    0x3d,
    0x98,
    0x08,
    0x36,
    0x39,
    0x9b,
    0x36,
    0xc6,
    0x98,
    0x8c,
    0x0c,
    0x68,
    0xde,
    0x55,
    0xe1,
    0xbd,
    0xb8,
    0x26,
    0xd3,
    0x90,
    0x1a,
    0x24,
    0x61,
    0xea,
    0xfd,
    0x2d,
    0xe4,
    0x9a,
    0x91,
    0xd0,
    0x15,
    0xab,
    0xbc,
    0x9a,
    0x95,
    0x13,
    0x7a,
    0xce,
    0x6c,
    0x1a,
    0xf1,
    0x9e,
    0xaa,
    0x6a,
    0xf9,
    0x8c,
    0x7c,
    0xed,
    0x43,
    0x12,
    0x09,
    0x98,
    0xe1,
    0x87,
    0xa8,
    0x0e,
    0xe0,
    0xcc,
    0xb0,
    0x52,
    0x4b,
    0x1b,
    0x01,
    0x8c,
    0x3e,
    0x0b,
    0x63,
    0x26,
    0x4d,
    0x44,
    0x9a,
    0x6d,
    0x38,
    0xe2,
    0x2a,
    0x5f,
    0xda,
    0x43,
    0x08,
    0x46,
    0x74,
    0x80,
    0x30,
    0x53,
    0x0e,
    0xf0,
    0x46,
    0x1c,
    0x8c,
    0xa9,
    0xd9,
    0xef,
    0xbf,
    0xae,
    0x8e,
    0xa6,
    0xd1,
    0xd0,
    0x3e,
    0x2b,
    0xd1,
    0x93,
    0xef,
    0xf0,
    0xab,
    0x9a,
    0x80,
    0x02,
    0xc4,
    0x74,
    0x28,
    0xa6,
    0xd3,
    0x5a,
    0x8d,
    0x88,
    0xd7,
    0x9f,
    0x7f,
    0x1e,
    0x3f,
    0x02,
    0x03,
    0x01,
    0x00,
    0x01,
    0xa3,
    0x1a,
    0x30,
    0x18,
    0x30,
    0x09,
    0x06,
    0x03,
    0x55,
    0x1d,
    0x13,
    0x04,
    0x02,
    0x30,
    0x00,
    0x30,
    0x0b,
    0x06,
    0x03,
    0x55,
    0x1d,
    0x0f,
    0x04,
    0x04,
    0x03,
    0x02,
    0x05,
    0xa0,
    0x30,
    0x0d,
    0x06,
    0x09,
    0x2a,
    0x86,
    0x48,
    0x86,
    0xf7,
    0x0d,
    0x01,
    0x01,
    0x0b,
    0x05,
    0x00,
    0x03,
    0x81,
    0x81,
    0x00,
    0x85,
    0xaa,
    0xd2,
    0xa0,
    0xe5,
    0xb9,
    0x27,
    0x6b,
    0x90,
    0x8c,
    0x65,
    0xf7,
    0x3a,
    0x72,
    0x67,
    0x17,
    0x06,
    0x18,
    0xa5,
    0x4c,
    0x5f,
    0x8a,
    0x7b,
    0x33,
    0x7d,
    0x2d,
    0xf7,
    0xa5,
    0x94,
    0x36,
    0x54,
    0x17,
    0xf2,
    0xea,
    0xe8,
    0xf8,
    0xa5,
    0x8c,
    0x8f,
    0x81,
    0x72,
    0xf9,
    0x31,
    0x9c,
    0xf3,
    0x6b,
    0x7f,
    0xd6,
    0xc5,
    0x5b,
    0x80,
    0xf2,
    0x1a,
    0x03,
    0x01,
    0x51,
    0x56,
    0x72,
    0x60,
    0x96,
    0xfd,
    0x33,
    0x5e,
    0x5e,
    0x67,
    0xf2,
    0xdb,
    0xf1,
    0x02,
    0x70,
    0x2e,
    0x60,
    0x8c,
    0xca,
    0xe6,
    0xbe,
    0xc1,
    0xfc,
    0x63,
    0xa4,
    0x2a,
    0x99,
    0xbe,
    0x5c,
    0x3e,
    0xb7,
    0x10,
    0x7c,
    0x3c,
    0x54,
    0xe9,
    0xb9,
    0xeb,
    0x2b,
    0xd5,
    0x20,
    0x3b,
    0x1c,
    0x3b,
    0x84,
    0xe0,
    0xa8,
    0xb2,
    0xf7,
    0x59,
    0x40,
    0x9b,
    0xa3,
    0xea,
    0xc9,
    0xd9,
    0x1d,
    0x40,
    0x2d,
    0xcc,
    0x0c,
    0xc8,
    0xf8,
    0x96,
    0x12,
    0x29,
    0xac,
    0x91,
    0x87,
    0xb4,
    0x2b,
    0x4d,
    0xe1,
    0x00,
    0x00,
    // CertificateVerify (136 octets)
    0x0f,
    0x00,
    0x00,
    0x84,
    0x08,
    0x04,
    0x00,
    0x80,
    0x5a,
    0x74,
    0x7c,
    0x5d,
    0x88,
    0xfa,
    0x9b,
    0xd2,
    0xe5,
    0x5a,
    0xb0,
    0x85,
    0xa6,
    0x10,
    0x15,
    0xb7,
    0x21,
    0x1f,
    0x82,
    0x4c,
    0xd4,
    0x84,
    0x14,
    0x5a,
    0xb3,
    0xff,
    0x52,
    0xf1,
    0xfd,
    0xa8,
    0x47,
    0x7b,
    0x0b,
    0x7a,
    0xbc,
    0x90,
    0xdb,
    0x78,
    0xe2,
    0xd3,
    0x3a,
    0x5c,
    0x14,
    0x1a,
    0x07,
    0x86,
    0x53,
    0xfa,
    0x6b,
    0xef,
    0x78,
    0x0c,
    0x5e,
    0xa2,
    0x48,
    0xee,
    0xaa,
    0xa7,
    0x85,
    0xc4,
    0xf3,
    0x94,
    0xca,
    0xb6,
    0xd3,
    0x0b,
    0xbe,
    0x8d,
    0x48,
    0x59,
    0xee,
    0x51,
    0x1f,
    0x60,
    0x29,
    0x57,
    0xb1,
    0x54,
    0x11,
    0xac,
    0x02,
    0x76,
    0x71,
    0x45,
    0x9e,
    0x46,
    0x44,
    0x5c,
    0x9e,
    0xa5,
    0x8c,
    0x18,
    0x1e,
    0x81,
    0x8e,
    0x95,
    0xb8,
    0xc3,
    0xfb,
    0x0b,
    0xf3,
    0x27,
    0x84,
    0x09,
    0xd3,
    0xbe,
    0x15,
    0x2a,
    0x3d,
    0xa5,
    0x04,
    0x3e,
    0x06,
    0x3d,
    0xda,
    0x65,
    0xcd,
    0xf5,
    0xae,
    0xa2,
    0x0d,
    0x53,
    0xdf,
    0xac,
    0xd4,
    0x2f,
    0x74,
    0xf3,
    // Finished (36 octets)
    0x14,
    0x00,
    0x00,
    0x20,
    0x9b,
    0x9b,
    0x14,
    0x1d,
    0x90,
    0x63,
    0x37,
    0xfb,
    0xd2,
    0xcb,
    0xdc,
    0xe7,
    0x1d,
    0xf4,
    0xde,
    0xda,
    0x4a,
    0xb4,
    0x2c,
    0x30,
    0x95,
    0x72,
    0xcb,
    0x7f,
    0xff,
    0xee,
    0x54,
    0x54,
    0xb7,
    0x8f,
    0x07,
    0x18,
};

// Cumulative message boundaries in RFC8448_FLIGHT.
#define CH_END 196
#define SH_END 286
#define CV_END 907
#define SFIN_END 943

// The trace's ephemeral X25519 shared secret, the IKM of the "handshake" Extract.
static const uint8_t RFC8448_ECDHE[32] = {0x8b, 0xd4, 0x05, 0x4f, 0xb5, 0x5b, 0x9d, 0x63, 0xfd, 0xfb, 0xac,
                                          0xf9, 0xf0, 0x4b, 0x9f, 0x0d, 0x35, 0xe6, 0xd6, 0x3f, 0x53, 0x75,
                                          0x63, 0xef, 0xd4, 0x62, 0x72, 0x90, 0x0f, 0x89, 0x49, 0x2d};

// Every secret the trace prints, in schedule order.
static const uint8_t RFC8448_EARLY[32] = {0x33, 0xad, 0x0a, 0x1c, 0x60, 0x7e, 0xc0, 0x3b, 0x09, 0xe6, 0xcd,
                                          0x98, 0x93, 0x68, 0x0c, 0xe2, 0x10, 0xad, 0xf3, 0x00, 0xaa, 0x1f,
                                          0x26, 0x60, 0xe1, 0xb2, 0x2e, 0x10, 0xf1, 0x70, 0xf9, 0x2a};
static const uint8_t RFC8448_DERIVED_EARLY[32] = {0x6f, 0x26, 0x15, 0xa1, 0x08, 0xc7, 0x02, 0xc5, 0x67, 0x8f, 0x54,
                                                  0xfc, 0x9d, 0xba, 0xb6, 0x97, 0x16, 0xc0, 0x76, 0x18, 0x9c, 0x48,
                                                  0x25, 0x0c, 0xeb, 0xea, 0xc3, 0x57, 0x6c, 0x36, 0x11, 0xba};
static const uint8_t RFC8448_HANDSHAKE[32] = {0x1d, 0xc8, 0x26, 0xe9, 0x36, 0x06, 0xaa, 0x6f, 0xdc, 0x0a, 0xad,
                                              0xc1, 0x2f, 0x74, 0x1b, 0x01, 0x04, 0x6a, 0xa6, 0xb9, 0x9f, 0x69,
                                              0x1e, 0xd2, 0x21, 0xa9, 0xf0, 0xca, 0x04, 0x3f, 0xbe, 0xac};
static const uint8_t RFC8448_CH_SH_HASH[32] = {0x86, 0x0c, 0x06, 0xed, 0xc0, 0x78, 0x58, 0xee, 0x8e, 0x78, 0xf0,
                                               0xe7, 0x42, 0x8c, 0x58, 0xed, 0xd6, 0xb4, 0x3f, 0x2c, 0xa3, 0xe6,
                                               0xe9, 0x5f, 0x02, 0xed, 0x06, 0x3c, 0xf0, 0xe1, 0xca, 0xd8};
static const uint8_t RFC8448_C_HS[32] = {0xb3, 0xed, 0xdb, 0x12, 0x6e, 0x06, 0x7f, 0x35, 0xa7, 0x80, 0xb3,
                                         0xab, 0xf4, 0x5e, 0x2d, 0x8f, 0x3b, 0x1a, 0x95, 0x07, 0x38, 0xf5,
                                         0x2e, 0x96, 0x00, 0x74, 0x6a, 0x0e, 0x27, 0xa5, 0x5a, 0x21};
static const uint8_t RFC8448_S_HS[32] = {0xb6, 0x7b, 0x7d, 0x69, 0x0c, 0xc1, 0x6c, 0x4e, 0x75, 0xe5, 0x42,
                                         0x13, 0xcb, 0x2d, 0x37, 0xb4, 0xe9, 0xc9, 0x12, 0xbc, 0xde, 0xd9,
                                         0x10, 0x5d, 0x42, 0xbe, 0xfd, 0x59, 0xd3, 0x91, 0xad, 0x38};
static const uint8_t RFC8448_DERIVED_HS[32] = {0x43, 0xde, 0x77, 0xe0, 0xc7, 0x77, 0x13, 0x85, 0x9a, 0x94, 0x4d,
                                               0xb9, 0xdb, 0x25, 0x90, 0xb5, 0x31, 0x90, 0xa6, 0x5b, 0x3e, 0xe2,
                                               0xe4, 0xf1, 0x2d, 0xd7, 0xa0, 0xbb, 0x7c, 0xe2, 0x54, 0xb4};
static const uint8_t RFC8448_MASTER[32] = {0x18, 0xdf, 0x06, 0x84, 0x3d, 0x13, 0xa0, 0x8b, 0xf2, 0xa4, 0x49,
                                           0x84, 0x4c, 0x5f, 0x8a, 0x47, 0x80, 0x01, 0xbc, 0x4d, 0x4c, 0x62,
                                           0x79, 0x84, 0xd5, 0xa4, 0x1d, 0xa8, 0xd0, 0x40, 0x29, 0x19};
static const uint8_t RFC8448_CH_SFIN_HASH[32] = {0x96, 0x08, 0x10, 0x2a, 0x0f, 0x1c, 0xcc, 0x6d, 0xb6, 0x25, 0x0b,
                                                 0x7b, 0x7e, 0x41, 0x7b, 0x1a, 0x00, 0x0e, 0xaa, 0xda, 0x3d, 0xaa,
                                                 0xe4, 0x77, 0x7a, 0x76, 0x86, 0xc9, 0xff, 0x83, 0xdf, 0x13};
static const uint8_t RFC8448_C_AP[32] = {0x9e, 0x40, 0x64, 0x6c, 0xe7, 0x9a, 0x7f, 0x9d, 0xc0, 0x5a, 0xf8,
                                         0x88, 0x9b, 0xce, 0x65, 0x52, 0x87, 0x5a, 0xfa, 0x0b, 0x06, 0xdf,
                                         0x00, 0x87, 0xf7, 0x92, 0xeb, 0xb7, 0xc1, 0x75, 0x04, 0xa5};
static const uint8_t RFC8448_S_AP[32] = {0xa1, 0x1a, 0xf9, 0xf0, 0x55, 0x31, 0xf8, 0x56, 0xad, 0x47, 0x11,
                                         0x6b, 0x45, 0xa9, 0x50, 0x32, 0x82, 0x04, 0xb4, 0xf4, 0x4b, 0xfb,
                                         0x6b, 0x3a, 0x4b, 0x4f, 0x1f, 0x3f, 0xcb, 0x63, 0x16, 0x43};
static const uint8_t RFC8448_FINISHED_KEY[32] = {0x00, 0x8d, 0x3b, 0x66, 0xf8, 0x16, 0xea, 0x55, 0x9f, 0x96, 0xb5,
                                                 0x37, 0xe8, 0x85, 0xc3, 0x1f, 0xc0, 0x68, 0xbf, 0x49, 0x2c, 0x65,
                                                 0x2f, 0x01, 0xf2, 0x88, 0xa1, 0xd8, 0xcd, 0xc1, 0x9f, 0xc8};
static const uint8_t RFC8448_SERVER_VERIFY[32] = {0x9b, 0x9b, 0x14, 0x1d, 0x90, 0x63, 0x37, 0xfb, 0xd2, 0xcb, 0xdc,
                                                  0xe7, 0x1d, 0xf4, 0xde, 0xda, 0x4a, 0xb4, 0x2c, 0x30, 0x95, 0x72,
                                                  0xcb, 0x7f, 0xff, 0xee, 0x54, 0x54, 0xb7, 0x8f, 0x07, 0x18};
// The server's handshake write key and iv, HKDF-Expand-Label(s hs traffic, "key"/"iv", "", L).
static const uint8_t RFC8448_S_HS_KEY[16] = {0x3f, 0xce, 0x51, 0x60, 0x09, 0xc2, 0x17, 0x27,
                                             0xd0, 0xf2, 0xe4, 0xe8, 0x6e, 0xe4, 0x03, 0xbc};
static const uint8_t RFC8448_S_HS_IV[12] = {0x5d, 0x31, 0x3e, 0xb2, 0x67, 0x12, 0x76, 0xee, 0x13, 0x00, 0x0b, 0x30};

// Transcript-Hash of the first @p n octets of the flight.
static void transcript(size_t n, uint8_t out[32])
{
    Sha256.hash_args.data = RFC8448_FLIGHT;
    Sha256.hash_args.len = n;
    Sha256.hash_args.out = out;
    Sha256.hash(g_hash_work);
}

static void bind_early(const Tls13Kdf *kdf)
{
    Tls13Ks.bind.kdf = kdf;
    Tls13Ks.bind.ks = &g_ks;
    Tls13Ks.bind.s = g_ks_bytes;
    Tls13Ks.bind.is384 = PROTO_FALSE;
    Tls13Ks.early(NULL);
}

// The same, bound to the SHA-384 suite's hash. The borrow is the one region either arm runs out of,
// so a schedule that ran under the other hash is wiped first: TLS13_KS_ZEROS is the first extract's
// IKM and nothing ever writes it, which only holds if the bytes start zeroed.
static void bind_early384(const Tls13Kdf *kdf)
{
    memset(g_ks_bytes, 0, sizeof(g_ks_bytes));
    Tls13Ks.bind.kdf = kdf;
    Tls13Ks.bind.ks = &g_ks;
    Tls13Ks.bind.s = g_ks_bytes;
    Tls13Ks.bind.is384 = PROTO_TRUE;
    Tls13Ks.early(NULL);
}

// The transcript hashes the trace feeds each derivation are recomputed here from the trace's own
// messages, and must equal the "hash" values the RFC prints beside those derivations. Without this
// the chain below would be keyed off whatever this test happened to hash.
void test_transcript_hashes_match_the_trace(void)
{
    uint8_t h[32];
    transcript(SH_END, h);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_CH_SH_HASH, h, 32);
    transcript(SFIN_END, h);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_CH_SFIN_HASH, h, 32);

    // Hashing only the ClientHello is a different transcript, so the anchors above are not vacuous.
    uint8_t ch[32];
    transcript(CH_END, ch);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(ch, RFC8448_CH_SH_HASH, 32));
}

// RFC 8446 sec 7.1's schedule, replayed against RFC 8448 sec 3's printed values:
//   Early Secret     = HKDF-Extract(0, 0^32)
//   Derive(early)    = Derive-Secret(Early, "derived", "")
//   Handshake Secret = HKDF-Extract(Derive(early), (EC)DHE)
//   c/s hs traffic   = Derive-Secret(Handshake, "c hs traffic"/"s hs traffic", CH..SH)
//   Derive(hs)       = Derive-Secret(Handshake, "derived", "")
//   Master Secret    = HKDF-Extract(Derive(hs), 0^32)
//   c/s ap traffic   = Derive-Secret(Master, "c ap traffic"/"s ap traffic", CH..server Finished)
void test_rfc8448_secret_chain(void)
{
    uint8_t ch_sh[32];
    uint8_t ch_sfin[32];
    transcript(SH_END, ch_sh);
    transcript(SFIN_END, ch_sfin);

    bind_early(&TLS13_KDF);
    TEST_ASSERT_TRUE(Tls13Ks.ok);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_EARLY, g_ks.s + TLS13_KS_EARLY, 32);

    Tls13Ks.bind.ks = &g_ks;
    Tls13Ks.step.ecdhe = RFC8448_ECDHE;
    Tls13Ks.step.ecdhe_len = sizeof(RFC8448_ECDHE);
    Tls13Ks.step.ch_sh_hash = ch_sh;
    Tls13Ks.handshake(NULL);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_DERIVED_EARLY, g_ks.s + TLS13_KS_DERIVED, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_HANDSHAKE, g_ks.s + TLS13_KS_HANDSHAKE, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_C_HS, g_ks.s + TLS13_KS_CLIENT_HS, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_S_HS, g_ks.s + TLS13_KS_SERVER_HS, 32);

    // The empty-transcript hash the "derived" steps take is SHA-256("").
    static const uint8_t EMPTY[32] = {0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4,
                                      0xc8, 0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b,
                                      0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(EMPTY, g_ks.s + TLS13_KS_EMPTY_HASH, 32);

    Tls13Ks.bind.ks = &g_ks;
    Tls13Ks.step.ch_sfin_hash = ch_sfin;
    Tls13Ks.master(NULL);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_DERIVED_HS, g_ks.s + TLS13_KS_DERIVED, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_MASTER, g_ks.s + TLS13_KS_MASTER, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_C_AP, g_ks.s + TLS13_KS_CLIENT_AP, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_S_AP, g_ks.s + TLS13_KS_SERVER_AP, 32);
}

// RFC 8446 sec 4.4.4: "finished_key = HKDF-Expand-Label(BaseKey, 'finished', '', Hash.length)" and
// "verify_data = HMAC(finished_key, Transcript-Hash(Handshake Context, Certificate*,
// CertificateVerify*))". The trace prints both, so the whole composition is pinned: the transcript
// here runs ClientHello..CertificateVerify, and the MAC must be the server Finished the RFC sends.
void test_rfc8446_4_4_4_finished_mac(void)
{
    uint8_t ch_cv[32];
    transcript(CV_END, ch_cv);

    uint8_t verify[32];
    bind_early(&TLS13_KDF);
    Tls13Ks.bind.ks = &g_ks;
    Tls13Ks.finished_args.base_secret = RFC8448_S_HS;
    Tls13Ks.finished_args.transcript_hash = ch_cv;
    Tls13Ks.finished_args.out = verify;
    Tls13Ks.finished_mac(NULL);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_FINISHED_KEY, g_ks.s + TLS13_KS_FINISHED_KEY, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_SERVER_VERIFY, verify, 32);

    // The trace's own Finished message carries exactly that verify_data behind its 4-byte header.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_SERVER_VERIFY, RFC8448_FLIGHT + CV_END + 4, 32);

    // A transcript one message longer is a different handshake, so the MAC must change.
    uint8_t other_hash[32];
    uint8_t other[32];
    transcript(SFIN_END, other_hash);
    Tls13Ks.bind.ks = &g_ks;
    Tls13Ks.finished_args.base_secret = RFC8448_S_HS;
    Tls13Ks.finished_args.transcript_hash = other_hash;
    Tls13Ks.finished_args.out = other;
    Tls13Ks.finished_mac(NULL);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(other, RFC8448_SERVER_VERIFY, 32));
}

// RFC 8446 sec 7.3: the record keys are HKDF-Expand-Label(Secret, "key", "", key_length) and
// ("iv", "", iv_length). The trace prints both for the server's handshake traffic secret.
void test_rfc8446_7_3_traffic_key_expansion(void)
{
    uint8_t key[16];
    uint8_t iv[12];

    bind_early(&TLS13_KDF);
    Tls13Ks.derive_args.work = g_hkdf_work;
    Tls13Ks.derive_args.secret = RFC8448_S_HS;
    Tls13Ks.derive_args.label = "key";
    Tls13Ks.derive_args.out = key;
    Tls13Ks.derive_args.out_len = sizeof(key);
    Tls13Ks.expand_label(NULL);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_S_HS_KEY, key, sizeof(key));

    Tls13Ks.derive_args.label = "iv";
    Tls13Ks.derive_args.out = iv;
    Tls13Ks.derive_args.out_len = sizeof(iv);
    Tls13Ks.expand_label(NULL);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_S_HS_IV, iv, sizeof(iv));
}

// The same three published inputs run through Derive-Secret directly, which is what the steps
// compose: HKDF-Expand-Label(secret, label, Transcript-Hash(Messages), Hash.length).
void test_derive_secret_matches_the_trace(void)
{
    uint8_t ch_sh[32];
    uint8_t out[32];
    transcript(SH_END, ch_sh);

    bind_early(&TLS13_KDF);
    Tls13Ks.derive_args.work = g_hkdf_work;
    Tls13Ks.derive_args.secret = RFC8448_HANDSHAKE;
    Tls13Ks.derive_args.label = "c hs traffic";
    Tls13Ks.derive_args.transcript_hash = ch_sh;
    Tls13Ks.derive_args.out = out;
    Tls13Ks.derive_secret(NULL);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_C_HS, out, 32);

    Tls13Ks.derive_args.label = "s hs traffic";
    Tls13Ks.derive_secret(NULL);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_S_HS, out, 32);

    // A label differing in one octet gives an unrelated secret: that is what the label is for.
    Tls13Ks.derive_args.label = "s hs traffiC";
    Tls13Ks.derive_secret(NULL);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(out, RFC8448_S_HS, 32));
}

// RFC 8446 sec 7.1 spells the HkdfLabel prefix "tls13 "; RFC 9147 sec 5.9 replaces it with "dtls13"
// for DTLS 1.3. Same secret, same label, different prefix, so the two schedules must never agree.
void test_dtls_prefix_separates_the_schedules(void)
{
    uint8_t tls_out[32];
    uint8_t dtls_out[32];
    uint8_t ch_sh[32];
    transcript(SH_END, ch_sh);

    TEST_ASSERT_EQUAL_STRING("tls13 ", TLS13_KDF.label_prefix);
    TEST_ASSERT_EQUAL_STRING("dtls13", DTLS13_KDF.label_prefix);

    bind_early(&TLS13_KDF);
    Tls13Ks.derive_args.work = g_hkdf_work;
    Tls13Ks.derive_args.secret = RFC8448_HANDSHAKE;
    Tls13Ks.derive_args.label = "s hs traffic";
    Tls13Ks.derive_args.transcript_hash = ch_sh;
    Tls13Ks.derive_args.out = tls_out;
    Tls13Ks.derive_secret(NULL);

    memset(g_ks_bytes, 0, sizeof(g_ks_bytes));
    bind_early(&DTLS13_KDF);
    Tls13Ks.derive_args.work = g_hkdf_work;
    Tls13Ks.derive_args.secret = RFC8448_HANDSHAKE;
    Tls13Ks.derive_args.label = "s hs traffic";
    Tls13Ks.derive_args.transcript_hash = ch_sh;
    Tls13Ks.derive_args.out = dtls_out;
    Tls13Ks.derive_secret(NULL);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_S_HS, tls_out, 32);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(dtls_out, tls_out, 32));

    // The early secret is an Extract with no label at all, so the two variants share it.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_EARLY, g_ks.s + TLS13_KS_EARLY, 32);
}

// The (EC)DHE input is the only handshake-secret operand that varies per connection, so a different
// shared secret must give a different handshake secret and different traffic secrets.
void test_a_different_ecdhe_gives_a_different_handshake_secret(void)
{
    uint8_t ch_sh[32];
    uint8_t ecdhe[32];
    transcript(SH_END, ch_sh);
    memcpy(ecdhe, RFC8448_ECDHE, sizeof(ecdhe));
    ecdhe[31] ^= 0x01;

    bind_early(&TLS13_KDF);
    Tls13Ks.bind.ks = &g_ks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = sizeof(ecdhe);
    Tls13Ks.step.ch_sh_hash = ch_sh;
    Tls13Ks.handshake(NULL);

    TEST_ASSERT_NOT_EQUAL(0, memcmp(g_ks.s + TLS13_KS_HANDSHAKE, RFC8448_HANDSHAKE, 32));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(g_ks.s + TLS13_KS_CLIENT_HS, RFC8448_C_HS, 32));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(g_ks.s + TLS13_KS_SERVER_HS, RFC8448_S_HS, 32));
    // The early secret is upstream of the (EC)DHE, so it is unchanged.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_EARLY, g_ks.s + TLS13_KS_EARLY, 32);
}

// The client and server halves of one level are separate secrets: mixing them up would let a peer
// decrypt its own traffic and nothing else, so they must never coincide.
void test_client_and_server_secrets_differ_at_every_level(void)
{
    uint8_t ch_sh[32];
    uint8_t ch_sfin[32];
    transcript(SH_END, ch_sh);
    transcript(SFIN_END, ch_sfin);

    bind_early(&TLS13_KDF);
    Tls13Ks.bind.ks = &g_ks;
    Tls13Ks.step.ecdhe = RFC8448_ECDHE;
    Tls13Ks.step.ecdhe_len = sizeof(RFC8448_ECDHE);
    Tls13Ks.step.ch_sh_hash = ch_sh;
    Tls13Ks.handshake(NULL);
    Tls13Ks.bind.ks = &g_ks;
    Tls13Ks.step.ch_sfin_hash = ch_sfin;
    Tls13Ks.master(NULL);

    TEST_ASSERT_NOT_EQUAL(0, memcmp(g_ks.s + TLS13_KS_CLIENT_HS, g_ks.s + TLS13_KS_SERVER_HS, 32));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(g_ks.s + TLS13_KS_CLIENT_AP, g_ks.s + TLS13_KS_SERVER_AP, 32));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(g_ks.s + TLS13_KS_CLIENT_HS, g_ks.s + TLS13_KS_CLIENT_AP, 32));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(g_ks.s + TLS13_KS_HANDSHAKE, g_ks.s + TLS13_KS_MASTER, 32));
}

// A schedule with no storage cannot derive anything: the early step reports it, and the later steps
// are no-ops rather than writes through a null pointer.
void test_a_null_borrow_is_refused(void)
{
    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.bind.ks = &g_ks;
    Tls13Ks.bind.s = NULL;
    Tls13Ks.early(NULL);
    TEST_ASSERT_FALSE(Tls13Ks.ok);
    TEST_ASSERT_NULL(g_ks.s);

    uint8_t ch_sh[32];
    transcript(SH_END, ch_sh);
    Tls13Ks.bind.ks = &g_ks;
    Tls13Ks.step.ecdhe = RFC8448_ECDHE;
    Tls13Ks.step.ecdhe_len = sizeof(RFC8448_ECDHE);
    Tls13Ks.step.ch_sh_hash = ch_sh;
    Tls13Ks.handshake(NULL);
    Tls13Ks.step.ch_sfin_hash = ch_sh;
    Tls13Ks.master(NULL);

    uint8_t out[32];
    memset(out, 0x5a, sizeof(out));
    Tls13Ks.finished_args.base_secret = RFC8448_S_HS;
    Tls13Ks.finished_args.transcript_hash = ch_sh;
    Tls13Ks.finished_args.out = out;
    Tls13Ks.finished_mac(NULL);
    for (size_t i = 0; i < sizeof(out); i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x5a, out[i]); // nothing was written
    }
}

// ---- the SHA-384 arm ------------------------------------------------------
//
// RFC 8446 sec 7.1 keys the schedule off the cipher suite's hash, and every case above runs the
// SHA-256 arm against the RFC 8448 trace. There is no published SHA-384 trace, so this one is
// openssl's: tools/harness.py crypto hkdf384 walks the same chain term by term with the openssl CLI
// and refuses to emit anything unless it first reproduces RFC 5869 A.1, the RFC 8448 sec 3 "derived"
// secret, RFC 6234 sec 8.5's SHA-384("abc") and RFC 4231 sec 4.2's HMAC-SHA-384.

typedef struct
{
    int tc;
    const char *ecdhe;
    const char *ch_sh;
    const char *ch_cv;
    const char *ch_sfin;
    const char *empty_hash;
    const char *early;
    const char *derived_early;
    const char *handshake;
    const char *c_hs;
    const char *s_hs;
    const char *derived_hs;
    const char *master;
    const char *c_ap;
    const char *s_ap;
    const char *finished_key;
    const char *verify;
} KatTls13Sha384;

#include "sha384_schedule.inc"

static uint8_t nib384(char c)
{
    return (uint8_t)(c <= '9' ? c - '0' : ((c | 0x20) - 'a' + 10));
}

static size_t unhex384(const char *h, uint8_t *out)
{
    size_t n = 0;
    for (; h[0] && h[1]; h += 2)
    {
        out[n++] = (uint8_t)((nib384(h[0]) << 4) | nib384(h[1]));
    }
    return n;
}

// One term of the trace, compared against the slot the schedule wrote it into.
static void expect_term(const char *want_hex, const uint8_t *got, const char *what)
{
    uint8_t want[TLS13_SECRET_MAX];
    size_t n = unhex384(want_hex, want);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(48u, (unsigned)n, what);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(want, got, 48, what);
}

// The same chain as test_rfc8448_secret_chain, one hash up. Every term is 48 octets and lands in the
// slot the TLS13_KS_* offsets name, which are strides of TLS13_SECRET_MAX so they do not move.
void test_sha384_secret_chain(void)
{
    const KatTls13Sha384 *v = &KAT_TLS13_SHA384[0];
    uint8_t ecdhe[32], ch_sh[48], ch_sfin[48];
    size_t ecdhe_len = unhex384(v->ecdhe, ecdhe);
    unhex384(v->ch_sh, ch_sh);
    unhex384(v->ch_sfin, ch_sfin);

    bind_early384(&TLS13_KDF);
    TEST_ASSERT_TRUE(Tls13Ks.ok);
    TEST_ASSERT_EQUAL_UINT(48u, (unsigned)Tls13Ks.len);
    expect_term(v->early, g_ks.s + TLS13_KS_EARLY, "early");

    Tls13Ks.bind.ks = &g_ks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = ecdhe_len;
    Tls13Ks.step.ch_sh_hash = ch_sh;
    Tls13Ks.handshake(NULL);

    expect_term(v->empty_hash, g_ks.s + TLS13_KS_EMPTY_HASH, "SHA-384(\"\")");
    expect_term(v->derived_early, g_ks.s + TLS13_KS_DERIVED, "Derive-Secret(early, \"derived\", \"\")");
    expect_term(v->handshake, g_ks.s + TLS13_KS_HANDSHAKE, "handshake secret");
    expect_term(v->c_hs, g_ks.s + TLS13_KS_CLIENT_HS, "c hs traffic");
    expect_term(v->s_hs, g_ks.s + TLS13_KS_SERVER_HS, "s hs traffic");

    Tls13Ks.bind.ks = &g_ks;
    Tls13Ks.step.ch_sfin_hash = ch_sfin;
    Tls13Ks.master(NULL);

    expect_term(v->derived_hs, g_ks.s + TLS13_KS_DERIVED, "Derive-Secret(handshake, \"derived\", \"\")");
    expect_term(v->master, g_ks.s + TLS13_KS_MASTER, "master secret");
    expect_term(v->c_ap, g_ks.s + TLS13_KS_CLIENT_AP, "c ap traffic");
    expect_term(v->s_ap, g_ks.s + TLS13_KS_SERVER_AP, "s ap traffic");
}

// RFC 8446 sec 4.4.4 at SHA-384: a 48-octet finished_key and a 48-octet verify_data.
void test_sha384_finished_mac(void)
{
    const KatTls13Sha384 *v = &KAT_TLS13_SHA384[0];
    uint8_t s_hs[48], ch_cv[48], verify[48];
    unhex384(v->s_hs, s_hs);
    unhex384(v->ch_cv, ch_cv);

    bind_early384(&TLS13_KDF);
    Tls13Ks.bind.ks = &g_ks;
    Tls13Ks.finished_args.base_secret = s_hs;
    Tls13Ks.finished_args.transcript_hash = ch_cv;
    Tls13Ks.finished_args.out = verify;
    Tls13Ks.finished_mac(NULL);

    expect_term(v->finished_key, g_ks.s + TLS13_KS_FINISHED_KEY, "finished_key");
    expect_term(v->verify, verify, "verify_data");
}

// The bound hash is what sets the secret length, and a caller reads it back rather than assuming it.
void test_the_bound_hash_sets_the_secret_length(void)
{
    bind_early(&TLS13_KDF);
    TEST_ASSERT_EQUAL_UINT(32u, (unsigned)Tls13Ks.len);
    TEST_ASSERT_EQUAL_UINT(32u, (unsigned)g_ks.len);

    bind_early384(&TLS13_KDF);
    TEST_ASSERT_EQUAL_UINT(48u, (unsigned)Tls13Ks.len);
    TEST_ASSERT_EQUAL_UINT(48u, (unsigned)g_ks.len);

    // The layout does not move with the hash: a term is one TLS13_SECRET_MAX slot either way.
    TEST_ASSERT_EQUAL_UINT(48u, (unsigned)TLS13_SECRET_MAX);
    TEST_ASSERT_EQUAL_UINT(TLS13_SECRET_MAX, (unsigned)TLS13_KS_HANDSHAKE);
}

// Two schedules over the same inputs under different hashes are different schedules. Without this a
// build that ignored the flag and ran SHA-256 throughout would still pass every SHA-256 case.
void test_the_two_hashes_give_different_schedules(void)
{
    uint8_t early256[32];

    bind_early(&TLS13_KDF);
    memcpy(early256, g_ks.s + TLS13_KS_EARLY, sizeof(early256));

    bind_early384(&TLS13_KDF);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(early256, g_ks.s + TLS13_KS_EARLY, sizeof(early256)));
}
