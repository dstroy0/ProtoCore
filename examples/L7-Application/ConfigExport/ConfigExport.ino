// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ConfigExport.ino
 * @brief Schema-driven config export / restore (PROTOCORE_ENABLE_CONFIG_IO).
 *
 * Declares a schema of persisted fields and serves them as a portable text blob:
 *   GET  /config            -> dumps `key=value` lines (backup / migrate)
 *   POST /config (that body)-> restores them into NVS (bulk provisioning)
 * Schema-driven over the typed NVS config store - deterministic, zero-heap.
 *
 * NOTE: enable both flags for the whole build. In platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_CONFIG_STORE=1 -DPROTOCORE_ENABLE_CONFIG_IO=1
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PROTOCORE_ENABLE_CONFIG_STORE 1
#define PROTOCORE_ENABLE_CONFIG_IO 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "server/storage/config_io/config_io.h"
#include "server/storage/config_store/config_store.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// The persisted fields to back up / restore.
static const protocore_cfg_field SCHEMA[] = {
    {"hostname", protocore_cfg_type::PROTOCORE_CFG_STR},
    {"http_port", protocore_cfg_type::PROTOCORE_CFG_U32},
    {"location", protocore_cfg_type::PROTOCORE_CFG_STR},
};
static const size_t SCHEMA_N = sizeof(SCHEMA) / sizeof(SCHEMA[0]);
static uint8_t cfg_io_work[16]; // the borrow a ConfigIo entry takes; the module holds nothing, so it never reads it

void setup()
{
    Serial.begin(115200);
    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Seed a couple of values (normally set at provisioning).
    ConfigStoreV.begin_args.ns = "app";
    ConfigStore.begin(protocore_config_store_span());
    ConfigStoreV.set_str_args.key = "hostname";
    ConfigStoreV.set_str_args.val = "sensor-01";
    ConfigStore.set_str(protocore_config_store_span());
    ConfigStoreV.set_u32_args.key = "http_port";
    ConfigStoreV.set_u32_args.val = 80;
    ConfigStore.set_u32(protocore_config_store_span());
    ConfigStoreV.set_str_args.key = "location";
    ConfigStoreV.set_str_args.val = "lab";
    ConfigStore.set_str(protocore_config_store_span());

    on_http("/config", HTTP_GET, [](uint8_t id, HttpReq *) {
        char buf[512];
        ConfigIoV.export_args.ns = "app";
        ConfigIoV.export_args.fields = SCHEMA;
        ConfigIoV.export_args.n = SCHEMA_N;
        ConfigIoV.export_args.out = buf;
        ConfigIoV.export_args.cap = sizeof(buf);
        ConfigIo.dump(cfg_io_work);
        send_text(id, 200, "text/plain", buf);
    });
    on_http("/config", HTTP_POST, [](uint8_t id, HttpReq *req) {
        ConfigIoV.import_args.ns = "app";
        ConfigIoV.import_args.fields = SCHEMA;
        ConfigIoV.import_args.n = SCHEMA_N;
        ConfigIoV.import_args.text = (const char *)req->body;
        ConfigIoV.import_args.len = req->body_len;
        protocore_config_io_import(cfg_io_work); // ConfigIo.import, called by name beside export above
        int n = ConfigIoV.n;
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
