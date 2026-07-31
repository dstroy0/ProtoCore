// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file CoapObserve.ino
 * @brief CoAP resource observation (RFC 7641): the server pushes updates.
 *
 * Serves an observable "/count" resource over CoAP/UDP. A client that sends a GET
 * with the Observe option is registered as an observer; every second the sketch
 * increments the counter and calls pc_coap_notify("/count"), pushing the new value to
 * all observers (a CoAP notification from the server port with an increasing
 * Observe sequence). Try it with:
 *     coap-client -m get -s 30 coap://<ip>/count      # libcoap, -s = observe
 *     aiocoap-client --observe coap://<ip>/count
 *
 * NOTE: optional services are gated by a compile flag the *library* sources must
 * also see; for PlatformIO enable it for the whole build, e.g.:
 *     build_flags = -DPC_ENABLE_COAP=1 -DPC_ENABLE_COAP_OBSERVE=1
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_COAP 1
#define PC_ENABLE_COAP_OBSERVE 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/iot/coap/coap.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static uint32_t g_count = 0;

void h_count(const CoapRequest *req, CoapResponse *resp)
{
    (void)req;
    int n = snprintf((char *)resp->payload, resp->payload_cap, "%lu", (unsigned long)g_count);
    resp->payload_len = (n > 0) ? (size_t)n : 0;
    resp->content_format = CoapContentFormat::COAP_CF_TEXT;
    resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CONTENT;
}

void setup()
{
    Serial.begin(115200);

    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!wifi_ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    pc_coap_server_reset();
    pc_coap_server_add_resource("/count", CoapMethodMask::COAP_ALLOW_GET, h_count);
    pc_coap_server_begin(5683);
    Serial.println("CoAP server on :5683, observe coap://<ip>/count");
}

void loop()
{
    static uint32_t last = 0;
    if (millis() - last >= 1000)
    {
        last = millis();
        g_count++;
        pc_coap_notify("/count"); // push the new value to every observer
    }
}
