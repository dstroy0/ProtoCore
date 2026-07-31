// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
//   pio run -d performance_benching/device/ikev2 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/security/ikev2/ikev2.h"
#include <Arduino.h>
#include <string.h> // memset

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
    if (!pc_ike_sa_first_proposal(body, body_len, &prop))
    {
        return 0;
    }
    IkeTransformIter it;
    IkeTransformRef t;
    pc_ike_transform_iter_init(&it, &prop);
    size_t acc = prop.num_transforms;
    while (pc_ike_transform_next(&it, &t))
    {
        acc += (size_t)t.id + (size_t)(t.key_length + 1);
    }
    return acc;
}

// Parse the header, then forward-walk the generic payload chain of a whole IKE_SA_INIT message.
static size_t ike_chain_walk(const uint8_t *msg, size_t len)
{
    IkeHeader h;
    if (!pc_ike_hdr_parse(msg, len, &h))
    {
        return 0;
    }
    IkePayloadIter it;
    pc_ike_payload_iter_init(&it, h.next_payload, msg + PC_IKE_HDR_LEN, len - PC_IKE_HDR_LEN);
    IkePayload pl;
    size_t acc = 0;
    while (pc_ike_payload_next(&it, &pl))
    {
        acc += pl.body_len + (size_t)pl.type;
    }
    return acc;
}

static void ikev2_bench_task(void *)
{
    // A header to build (mirrors GV_HDR: init SPI 0x11..0x88, IKE_SA_INIT, Initiator).
    IkeHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    for (int i = 0; i < PC_IKE_SPI_LEN; i++)
    {
        hdr.init_spi[i] = (uint8_t)((i + 1) * 0x11);
    }
    hdr.next_payload = IkePayloadType::IKE_PL_NONE;
    hdr.version = PC_IKE_VERSION;
    hdr.exchange = IkeExchange::IKE_SA_INIT;
    hdr.flags = PC_IKE_FLAG_INITIATOR;
    hdr.message_id = 0;
    hdr.length = PC_IKE_HDR_LEN;

    // One IKE proposal, ENCR AES-CBC (256-bit key) + PRF HMAC-SHA2-256 - the GV_SA_KEYLEN tree.
    static const IkeTransform tr[2] = {{IkeTransformType::IKE_TRANSFORM_ENCR, IKE_ENCR_AES_CBC, 256},
                                       {IkeTransformType::IKE_TRANSFORM_PRF, IKE_PRF_HMAC_SHA2_256, -1}};

    static uint8_t hbuf[64];
    static uint8_t sabuf[64];
    IkeHeader hdr_out;

    for (;;)
    {
        Serial.printf("DB ==== ikev2 device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        DBENCH_OP("pc_ike_hdr_build", 200000, sink += pc_ike_hdr_build(hbuf, sizeof(hbuf), &hdr));
        DBENCH_OP("pc_ike_hdr_parse", 200000, sink += pc_ike_hdr_parse(GV_HDR, sizeof(GV_HDR), &hdr_out));
        DBENCH_OP("pc_ike_sa_build (2 tx, keylen)", 100000,
                  sink += pc_ike_sa_build(sabuf, sizeof(sabuf), IkePayloadType::IKE_PL_KE, 1,
                                          IkeProtocol::IKE_PROTO_IKE, nullptr, 0, tr, 2));
        DBENCH_OP("pc_ike_sa_parse (prop+tx tree)", 100000,
                  sink += ike_sa_parse_tree(GV_SA_KEYLEN + 4, sizeof(GV_SA_KEYLEN) - 4));
        DBENCH_BULK("pc_ike chain walk (SA_INIT)", 50000, sizeof(GV_FULL),
                    sink += ike_chain_walk(GV_FULL, sizeof(GV_FULL)));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: ikev2 device microbench");
    xTaskCreatePinnedToCore(ikev2_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
