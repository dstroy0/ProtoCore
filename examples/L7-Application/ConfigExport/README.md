# ConfigExport - schema-driven config export / restore

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_CONFIG_STORE`, `PC_ENABLE_CONFIG_IO`

## What this example teaches

Backing up or bulk-provisioning a fleet needs the device's persisted settings in a
portable form. This declares a schema of persisted fields and exposes them as a
text blob: `GET /config` dumps `key=value` lines for backup or migration, and
`POST /config` with that body restores them into NVS. It is schema-driven over the
typed NVS config store - deterministic and zero-heap.

**Declare the schema once, with field types:**

```cpp
static const pc_cfg_field SCHEMA[] = {
    {"hostname",  pc_cfg_type::PC_CFG_STR},
    {"http_port", pc_cfg_type::PC_CFG_U32},
    {"location",  pc_cfg_type::PC_CFG_STR},
};
```

**Export and import drive off that schema.** The config store is opened under a
namespace ("app") and seeded; export serializes exactly the schema fields, import
parses them back:

```cpp
pc_config_export("app", SCHEMA, SCHEMA_N, buf, sizeof(buf));            // GET -> key=value lines
int n = pc_config_import("app", SCHEMA, SCHEMA_N, req->body, req->body_len); // POST -> n fields restored
```

Because both sides share the schema, an unknown or mistyped key in the import body
is rejected rather than silently written.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_CONFIG_STORE=1 -DPC_ENABLE_CONFIG_IO=1" \
  --lib="." examples/L7-Application/ConfigExport/ConfigExport.ino
```

```sh
curl http://<ip>/config                       # back up: hostname=sensor-01 ...
curl -X POST http://<ip>/config --data-binary @config.txt   # restore into NVS
```

## Annotated source

The complete sketch ([ConfigExport.ino](ConfigExport.ino)), reproduced
verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_CONFIG_STORE 1
#define PC_ENABLE_CONFIG_IO 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/storage/config_io/config_io.h"
#include "services/storage/config_store/config_store.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// The persisted fields to back up / restore (shared by export and import).
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
        delay(250);
    Serial.print("IP: ");
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Seed a couple of values (normally set at provisioning).
    pc_config_begin("app");
    pc_config_set_str("hostname", "sensor-01");
    pc_config_set_u32("http_port", 80);
    pc_config_set_str("location", "lab");

    server.on("/config", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        char buf[512];
        pc_config_export("app", SCHEMA, SCHEMA_N, buf, sizeof(buf));
        server.send(id, 200, "text/plain", buf);
    });
    server.on("/config", HttpMethod::HTTP_POST, [](uint8_t id, HttpReq *req) {
        int n = pc_config_import("app", SCHEMA, SCHEMA_N, (const char *)req->body, req->body_len);
        char msg[48];
        snprintf(msg, sizeof(msg), "imported %d field(s)\n", n);
        server.send(id, 200, "text/plain", msg);
    });
    server.begin(80);
}

void loop()
{
    server.handle();
}
```
