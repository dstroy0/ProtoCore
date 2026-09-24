// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Multipart.ino
 * @brief Parse a multipart/form-data POST body (RFC 7578) in place.
 *
 * POST /upload with a multipart body; Multipart.parse() splits it into parts and
 * Multipart.get_field() returns a named text field. The whole body must fit in
 * BODY_BUF_SIZE (no streaming), so this suits small form fields / tiny uploads.
 * A test form is served at /.
 *
 * Flash, open Serial @ 115200 for the IP, then browse to http://<ip>/.
 */


#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static const char FORM[] = "<!doctype html><meta charset=utf-8><title>upload</title>"
                           "<form method=POST action=/upload enctype=multipart/form-data>"
                           "<input name=name placeholder=name> "
                           "<input type=file name=file> <button>upload</button></form>";

// The borrow every Multipart entry takes. The parser carries nothing between calls and never
// reads it, so a small static buffer serves.
static uint8_t multipart_work[16];

void handle_upload(uint8_t id, HttpReq *req)
{
    MultipartBody mp;
    if (!Multipart.parse(multipart_work, req, &mp))
    {
        send_text(id, 400, "text/plain", "expected multipart/form-data (and within BODY_BUF_SIZE)");
        return;
    }
    const char *name = Multipart.get_field(multipart_work, &mp, "name");
    char out[160];
    snprintf(out, sizeof(out), "parsed %d part(s); field 'name' = %s", mp.part_count, name ? name : "(absent)");
    send_text(id, 200, "text/plain", out);
}

void setup()
{
    Serial.begin(115200);
    // Every Physical entry reads its operands from PhysicalV and writes its outcome back there.
    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
        Serial.print('.');
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    on_http("/", HTTP_GET, [](uint8_t id, HttpReq *) { send_text(id, 200, "text/html", FORM); });
    on_http("/upload", HTTP_POST, handle_upload);
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
