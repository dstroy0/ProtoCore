// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file CoapBlock.ino
 * @brief CoAP block-wise transfer (RFC 7959): large responses and uploads.
 *
 * Two resources show both directions of block-wise transfer:
 *
 *  - GET /big   returns a representation larger than one datagram. A constrained
 *               client pages it with the Block2 option, one block at a time:
 *                   coap-client -m get -b 64 coap://<ip>/big   # libcoap, -b = block
 *                   aiocoap-client coap://<ip>/big             # negotiates blocks
 *
 *  - PUT /upload accepts a payload uploaded with the Block1 option. Each non-final
 *               block is acknowledged 2.31 Continue; the final block delivers the
 *               whole reassembled payload to the handler:
 *                   coap-client -m put -b 64 -f firmware.bin coap://<ip>/upload
 *
 * NOTE: optional services are gated by a compile flag the *library* sources must
 * also see; for PlatformIO enable it for the whole build, e.g.:
 *     build_flags = -DPC_ENABLE_COAP=1 -DPC_ENABLE_COAP_BLOCK=1
 *                   -DPC_COAP_MAX_PAYLOAD=1024
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_COAP 1
#define PC_ENABLE_COAP_BLOCK 1
#define PC_COAP_MAX_PAYLOAD 1024 // room for a multi-block representation

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/iot/coap/coap.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// GET /big: render a 512-byte representation. The library slices it into blocks
// for any client that asks (Block2); a whole-datagram client still gets it inline.
void h_big(const CoapRequest *req, CoapResponse *resp)
{
    (void)req;
    const size_t n = 512;
    for (size_t i = 0; i < n && i < resp->payload_cap; i++)
    {
        resp->payload[i] = (uint8_t)('0' + (int)(i % 10)); // "0123456789012..."
    }
    resp->payload_len = (n < resp->payload_cap) ? n : resp->payload_cap;
    resp->content_format = COAP_CF_TEXT;
    resp->code = (uint8_t)COAP_RSP_CONTENT;
}

// PUT /upload: the library reassembles a block-wise upload and calls this once,
// with the complete payload. Summarize it (length + byte sum) over Serial.
void h_upload(const CoapRequest *req, CoapResponse *resp)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < req->payload_len; i++)
    {
        sum += req->payload[i];
    }
    Serial.printf("upload: %u bytes, checksum=%lu\n", (unsigned)req->payload_len, (unsigned long)sum);
    resp->payload_len = 0;
    resp->code = (uint8_t)COAP_RSP_CHANGED;
}

void setup()
{
    Serial.begin(115200);

    Physical.wifi->init(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!Physical.wifi->ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    pc_coap_server_reset();
    pc_coap_server_add_resource("/big", COAP_ALLOW_GET, h_big);
    pc_coap_server_add_resource("/upload", COAP_ALLOW_PUT, h_upload);
    pc_coap_server_begin(5683);
    Serial.println("CoAP server on :5683");
    Serial.println("  GET coap://<ip>/big      (block-wise responses)");
    Serial.println("  PUT coap://<ip>/upload   (block-wise uploads)");
}

void loop()
{
}
