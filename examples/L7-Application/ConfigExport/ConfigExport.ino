// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ConfigExport.ino
 * @brief Schema-driven config export / restore (PC_ENABLE_CONFIG_IO).
 *
 * Declares a schema of persisted fields and serves them as a portable text blob:
 *   GET  /config            -> dumps `key=value` lines (backup / migrate)
 *   POST /config (that body)-> restores them into NVS (bulk provisioning)
 * Schema-driven over the typed NVS config store - deterministic, zero-heap.
 *
 * NOTE: enable both flags for the whole build. In platformio.ini:
 *     build_flags = -DPC_ENABLE_CONFIG_STORE=1 -DPC_ENABLE_CONFIG_IO=1
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_CONFIG_STORE 1
#define PC_ENABLE_CONFIG_IO 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/storage/config_io/config_io.h"
#include "services/storage/config_store/config_store.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// The persisted fields to back up / restore.
static const pc_cfg_field SCHEMA[] = {
    {"hostname", pc_cfg_type::PC_CFG_STR},
    {"http_port", pc_cfg_type::PC_CFG_U32},
    {"location", pc_cfg_type::PC_CFG_STR},
};
static const size_t SCHEMA_N = sizeof(SCHEMA) / sizeof(SCHEMA[0]);

void setup()
{
    Serial.begin(115200);
    Physical.wifi->init(SSID, PASSWORD);
    while (!Physical.wifi->ready())
    {
        delay(250);
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Seed a couple of values (normally set at provisioning).
    pc_config_begin("app");
    pc_config_set_str("hostname", "sensor-01");
    pc_config_set_u32("http_port", 80);
    pc_config_set_str("location", "lab");

    on_http("/config", HTTP_GET, [](uint8_t id, HttpReq *) {
        char buf[512];
        pc_config_export("app", SCHEMA, SCHEMA_N, buf, sizeof(buf));
        send_text(id, 200, "text/plain", buf);
    });
    on_http("/config", HTTP_POST, [](uint8_t id, HttpReq *req) {
        int n = pc_config_import("app", SCHEMA, SCHEMA_N, (const char *)req->body, req->body_len);
        char msg[48];
        snprintf(msg, sizeof(msg), "imported %d field(s)\n", n);
        send_text(id, 200, "text/plain", msg);
    });
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
