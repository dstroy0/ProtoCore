// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the CoAP server codec (services/iot/coap):
// pc_coap_server_process() takes a complete request datagram, parses the header/token/options,
// reconstructs the Uri-Path, dispatches against the resource table (or synthesizes the
// /.well-known/core RFC 6690 discovery listing), and encodes the piggybacked response - pure (no
// sockets, no heap). Benched here: a plain GET, a PUT carrying a payload + Content-Format, the
// .well-known/core discovery listing build (walks the resource table), and RFC 7959 block-wise
// transfer (Block2 response paging of a 150-byte resource, Block1 request-upload reassembly) -
// enabled the same way test_matrix.json proved services/iot/coap/ compiles (PC_ENABLE_COAP_BLOCK=1,
// PC_COAP_BLOCK_SZX_MAX=2, PC_COAP_BLOCK1_MAX=128, matching test/test_coap/test_coap.cpp's own
// env). Worked example for performance_benching/device/<service>/: a pure protocol codec with no hardware
// involved, so every call here exercises the real production code path (contrast with
// performance_benching/device/ads1115, a peripheral driver where the bus transaction itself is stubbed). Request
// datagrams below are hand-encoded to the exact byte sequences test/test_coap/test_coap.cpp's own
// encoder produces for the same calls (test_get_content, test_put_with_payload,
// test_well_known_core_discovery, test_block2_explicit_paging, test_block1_upload_two_blocks); out
// of scope: the UDP transport (pc_coap_server_begin), CoAP-over-DTLS (coaps.cpp/coaps_server.cpp,
// which need PC_ENABLE_DTLS), and Observe (PC_ENABLE_COAP_OBSERVE) - none of that is a pure CPU
// codec path.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/coap -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/iot/coap/coap.h"
#include <Arduino.h>
#include <string.h>

// A resource handler mirroring test_coap.cpp's h_resource: GET returns a 2-byte text body, the
// other allowed methods return an empty success body.
static void h_temp(const CoapRequest *req, CoapResponse *resp)
{
    switch (req->method)
    {
    case CoapMethod::COAP_GET:
        resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CONTENT;
        memcpy(resp->payload, "hi", 2);
        resp->payload_len = 2;
        resp->content_format = CoapContentFormat::COAP_CF_TEXT;
        break;
    case CoapMethod::COAP_POST:
        resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CREATED;
        resp->payload_len = 0;
        break;
    case CoapMethod::COAP_PUT:
        resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CHANGED;
        resp->payload_len = 0;
        break;
    case CoapMethod::COAP_DELETE:
        resp->code = (uint8_t)CoapResponseCode::COAP_RSP_DELETED;
        resp->payload_len = 0;
        break;
    }
}

// A 150-byte representation (bigger than the 64-byte max block, SZX_MAX=2), mirroring
// test_coap.cpp's h_big: big[i] = 'A' + (i % 26). Exercises Block2 response paging.
static const size_t BIG_LEN = 150;
static void h_big(const CoapRequest *req, CoapResponse *resp)
{
    (void)req;
    resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CONTENT;
    resp->content_format = CoapContentFormat::COAP_CF_TEXT;
    resp->payload_len = BIG_LEN;
    for (size_t i = 0; i < BIG_LEN; i++)
        resp->payload[i] = (uint8_t)('A' + (int)(i % 26));
}

static void coap_bench_task(void *)
{
    pc_coap_server_reset();
    pc_coap_server_add_resource("/temp",
                                CoapMethodMask::COAP_ALLOW_GET | CoapMethodMask::COAP_ALLOW_POST |
                                    CoapMethodMask::COAP_ALLOW_PUT | CoapMethodMask::COAP_ALLOW_DELETE,
                                h_temp);
    pc_coap_server_add_resource(
        "/big", CoapMethodMask::COAP_ALLOW_GET | CoapMethodMask::COAP_ALLOW_POST | CoapMethodMask::COAP_ALLOW_PUT,
        h_big);

    // GET /temp, token AA BB CC DD, MID 0x1234 (test_get_content): hdr(Ver1,CON,TKL4) + code GET +
    // MID + token + Uri-Path "temp".
    static const uint8_t get_temp[] = {0x44, 0x01, 0x12, 0x34, 0xAA, 0xBB, 0xCC, 0xDD, 0xB4, 0x74, 0x65, 0x6D, 0x70};

    // PUT /temp, payload "25", Content-Format text/plain (0), MID 0x0004 (test_put_with_payload):
    // hdr(Ver1,CON,TKL0) + code PUT + MID + Uri-Path "temp" + Content-Format(0, 0 value bytes) +
    // payload marker + "25".
    static const uint8_t put_temp[] = {0x40, 0x03, 0x00, 0x04, 0xB4, 0x74, 0x65, 0x6D, 0x70, 0x10, 0xFF, 0x32, 0x35};

    // GET /.well-known/core, MID 0x0CDE (test_well_known_core_discovery): hdr + code GET + MID +
    // Uri-Path ".well-known" + Uri-Path "core" (two segments of the same option number).
    static const uint8_t get_discovery[] = {0x40, 0x01, 0x0C, 0xDE, 0xBB, 0x2E, 0x77, 0x65, 0x6C, 0x6C, 0x2D,
                                            0x6B, 0x6E, 0x6F, 0x77, 0x6E, 0x04, 0x63, 0x6F, 0x72, 0x65};

    // GET /big with Block2 (NUM=0, M=0, SZX=2 -> 64-byte blocks), MID 0x3000
    // (test_block2_explicit_paging): hdr + code GET + MID + Uri-Path "big" + Block2 option.
    static const uint8_t get_block2[] = {0x40, 0x01, 0x30, 0x00, 0xB3, 0x62, 0x69, 0x67, 0xC1, 0x02};

    // POST /temp, Block1 (NUM=0, M=1, SZX=2), a 64-byte chunk (0x00..0x3F), MID 0x3600
    // (test_block1_upload_two_blocks, first block): hdr + code POST + MID + Uri-Path "temp" +
    // Block1 option (extended delta: 27-11=16 -> 13 + ext byte 3) + payload marker + chunk.
    static uint8_t post_block1[13 + 64];
    {
        static const uint8_t hdr[] = {0x40, 0x02, 0x36, 0x00, 0xB4, 0x74, 0x65, 0x6D, 0x70, 0xD1, 0x03, 0x0A, 0xFF};
        memcpy(post_block1, hdr, sizeof(hdr));
        for (int i = 0; i < 64; i++)
            post_block1[sizeof(hdr) + i] = (uint8_t)i;
    }

    static uint8_t resp[300];

    for (;;)
    {
        Serial.printf("DB ==== coap device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;

        DBENCH_OP("pc_coap_server_process GET", 20000,
                  sink += pc_coap_server_process(get_temp, sizeof(get_temp), resp, sizeof(resp)));

        DBENCH_OP("pc_coap_server_process PUT", 20000,
                  sink += pc_coap_server_process(put_temp, sizeof(put_temp), resp, sizeof(resp)));

        DBENCH_OP("pc_coap_server_process discovery", 20000,
                  sink += pc_coap_server_process(get_discovery, sizeof(get_discovery), resp, sizeof(resp)));

        DBENCH_BULK("pc_coap_server_process block2", 20000, 64,
                    sink += pc_coap_server_process(get_block2, sizeof(get_block2), resp, sizeof(resp)));

        DBENCH_BULK("pc_coap_server_process block1", 20000, 64,
                    sink += pc_coap_server_process(post_block1, sizeof(post_block1), resp, sizeof(resp)));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: coap device microbench");
    xTaskCreatePinnedToCore(coap_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
