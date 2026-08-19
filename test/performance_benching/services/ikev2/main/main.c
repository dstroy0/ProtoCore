// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the IKEv2 (RFC 7296) message + payload codec (services/security/ikev2):
// the 28-octet IKE header build/parse, the SA -> proposal -> transform tree (incl. the key-length
// attribute) build and parse, and a forward walk of a full IKE_SA_INIT payload chain (SA + KE +
// Nonce). Every call here exercises the real production wire codec - it is pure (build/parse into
// caller buffers, no sockets and no crypto), so this is a "pure protocol codec" bench like
// performance_benching/device/modbus, not a peripheral driver. The Diffie-Hellman math, SKEYSEED / SK_* derivation,
// the AEAD encrypt/decrypt of the SK payload, and the IKE_SA_INIT -> IKE_AUTH state machine are later
// tiers of the stack and are deliberately out of scope here (this rig has no network/crypto to drive).
// The sample byte vectors are the scapy-generated golden vectors copied straight out of
// test/test_ikev2/test_ikev2.cpp (already known-good, spec-conformant).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/ikev2 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/security/ikev2/ikev2/ikev2.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// memset

// ── scapy-generated golden vectors (from test/test_ikev2/test_ikev2.cpp) ────────────────────────
// Bare IKE header: init SPI 11..88, resp SPI 0, next=None, ver 2.0, IKE_SA_INIT, Initiator, msgid 0.
static const uint8_t GV_HDR[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x20, 0x22, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1c};
// SA with the first transform carrying a 256-bit key-length attribute (80 0e 01 00).
static const uint8_t GV_SA_KEYLEN[] = {0x22, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x1c, 0x01, 0x01, 0x00,
                                       0x02, 0x03, 0x00, 0x00, 0x0c, 0x01, 0x00, 0x00, 0x0c, 0x80, 0x0e,
                                       0x01, 0x00, 0x00, 0x00, 0x00, 0x08, 0x02, 0x00, 0x00, 0x05};
// Full IKE_SA_INIT: header (next=SA, len 92) + SA + KE + Nonce.
static const uint8_t GV_FULL[] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x20, 0x22,
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5c, 0x22, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x18, 0x01, 0x01,
    0x00, 0x02, 0x03, 0x00, 0x00, 0x08, 0x01, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x08, 0x02, 0x00, 0x00, 0x05, 0x28,
    0x00, 0x00, 0x10, 0x00, 0x0e, 0x00, 0x00, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x14,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

// Parse an SA payload body into its proposal, then iterate every transform (decoding the key-length
// attribute) - the SA -> proposal -> transform tree, the most involved parse the codec does.
static size_t ike_sa_parse_tree(const uint8_t *body, size_t body_len)
{
    IkeProposalRef prop;
    if (!protocore_ike_sa_first_proposal(body, body_len, &prop))
    {
        return 0;
    }
    IkeTransformIter it;
    IkeTransformRef t;
    protocore_ike_transform_iter_init(&it, &prop);
    size_t acc = prop.num_transforms;
    while (protocore_ike_transform_next(&it, &t))
    {
        acc += (size_t)t.id + (size_t)(t.key_length + 1);
    }
    return acc;
}

// Parse the header, then forward-walk the generic payload chain of a whole IKE_SA_INIT message.
static size_t ike_chain_walk(const uint8_t *msg, size_t len)
{
    IkeHeader h;
    if (!protocore_ike_hdr_parse(msg, len, &h))
    {
        return 0;
    }
    IkePayloadIter it;
    protocore_ike_payload_iter_init(&it, h.next_payload, msg + PROTOCORE_IKE_HDR_LEN, len - PROTOCORE_IKE_HDR_LEN);
    IkePayload pl;
    size_t acc = 0;
    while (protocore_ike_payload_next(&it, &pl))
    {
        acc += pl.body_len + (size_t)pl.type;
    }
    return acc;
}

void dbench_run(void)
{
    // A header to build (mirrors GV_HDR: init SPI 0x11..0x88, IKE_SA_INIT, Initiator).
    IkeHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    for (int i = 0; i < PROTOCORE_IKE_SPI_LEN; i++)
    {
        hdr.init_spi[i] = (uint8_t)((i + 1) * 0x11);
    }
    hdr.next_payload = IKE_PL_NONE;
    hdr.version = PROTOCORE_IKE_VERSION;
    hdr.exchange = IKE_SA_INIT;
    hdr.flags = PROTOCORE_IKE_FLAG_INITIATOR;
    hdr.message_id = 0;
    hdr.length = PROTOCORE_IKE_HDR_LEN;

    // One IKE proposal, ENCR AES-CBC (256-bit key) + PRF HMAC-SHA2-256 - the GV_SA_KEYLEN tree.
    static const IkeTransform tr[2] = {{IKE_TRANSFORM_ENCR, IKE_ENCR_AES_CBC, 256},
                                       {IKE_TRANSFORM_PRF, IKE_PRF_HMAC_SHA2_256, -1}};

    static uint8_t hbuf[64];
    static uint8_t sabuf[64];
    IkeHeader hdr_out;

    for (;;)
    {
        DBENCH_BANNER("ikev2");
        volatile size_t sink = 0;
        DBENCH_OP("protocore_ike_hdr_build", 200000, sink += protocore_ike_hdr_build(hbuf, sizeof(hbuf), &hdr));
        DBENCH_OP("protocore_ike_hdr_parse", 200000, sink += protocore_ike_hdr_parse(GV_HDR, sizeof(GV_HDR), &hdr_out));
        DBENCH_OP("protocore_ike_sa_build (2 tx, keylen)", 100000,
                  sink += protocore_ike_sa_build(sabuf, sizeof(sabuf), IKE_PL_KE, 1, IKE_PROTO_IKE, NULL, 0, tr, 2));
        DBENCH_OP("protocore_ike_sa_parse (prop+tx tree)", 100000,
                  sink += ike_sa_parse_tree(GV_SA_KEYLEN + 4, sizeof(GV_SA_KEYLEN) - 4));
        DBENCH_BULK("protocore_ike chain walk (SA_INIT)", 50000, sizeof(GV_FULL),
                    sink += ike_chain_walk(GV_FULL, sizeof(GV_FULL)));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("ikev2")
