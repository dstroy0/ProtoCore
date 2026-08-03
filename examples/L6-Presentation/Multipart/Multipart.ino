// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Multipart.ino
 * @brief Parse a multipart/form-data POST body (RFC 7578) in place.
 *
 * POST /upload with a multipart body; pc_multipart_parse() splits it into parts and
 * pc_multipart_get_field() returns a named text field. The whole body must fit in
 * BODY_BUF_SIZE (no streaming), so this suits small form fields / tiny uploads.
 * A test form is served at /.
 *
 * Flash, open Serial @ 115200 for the IP, then browse to http://<ip>/.
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static const char FORM[] = "<!doctype html><meta charset=utf-8><title>upload</title>"
                           "<form method=POST action=/upload enctype=multipart/form-data>"
                           "<input name=name placeholder=name> "
                           "<input type=file name=file> <button>upload</button></form>";

void handle_upload(uint8_t id, HttpReq *req)
{
    Multipart mp;
    if (!pc_multipart_parse(req, &mp))
    {
        send_text(id, 400, "text/plain", "expected multipart/form-data (and within BODY_BUF_SIZE)");
        return;
    }
    const char *name = pc_multipart_get_field(&mp, "name");
    char out[160];
    snprintf(out, sizeof(out), "parsed %d part(s); field 'name' = %s", mp.part_count, name ? name : "(absent)");
    send_text(id, 200, "text/plain", out);
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

    on_http("/", HTTP_GET, [](uint8_t id, HttpReq *) { send_text(id, 200, "text/html", FORM); });
    on_http("/upload", HTTP_POST, handle_upload);
    begin_http(80);
}

void loop()
{
    handle();
}
