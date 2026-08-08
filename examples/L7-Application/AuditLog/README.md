# AuditLog - a tamper-evident, hash-chained audit log

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_AUDIT_LOG`

## What this example teaches

Security-relevant events (logins, config changes) need a log you can trust after
the fact. This records them in an append-only log where each record chains
`SHA-256(prev_hash || fields)` - so altering or removing any retained record breaks
the chain, and `/audit` reports the break via `"intact":false` plus the first broken
sequence number. A sink forwards every record to durable storage at the moment it is
created, before the RAM ring can evict it.

**Reset the chain, register a sink, append events.**

```cpp
pc_audit_reset();
pc_audit_set_sink(audit_sink);                 // durable forwarding, runs per record
pc_audit_append(pc_audit_cat::PC_AUDIT_SYSTEM, "boot");
```

Events are appended with a category and a message; the library computes the chain
hash:

```cpp
bool ok = pass && strcmp(pass, "secret") == 0;
pc_audit_append(ok ? pc_audit_cat::PC_AUDIT_AUTH : pc_audit_cat::PC_AUDIT_AUTH_FAIL, msg);
```

**The sink is what makes it durable.** It runs once per record at append time and
receives the full record including its chain hash, so the external copy keeps the
same tamper-evident chain even after the in-RAM ring rolls over:

```cpp
static void audit_sink(const pc_audit_entry *e) {
    char line[256];
    if (pc_audit_format(e, line, sizeof(line)) > 0) {
        Serial.print("[AUDIT] "); Serial.println(line);
        // SD card:  File f = SD.open("/audit.log", FILE_APPEND); f.println(line); f.close();
        // Log svc:  pc_webhook_post("http://logs.example/ingest", line);
    }
}
```

`pc_audit_dump_json()` serializes the chain plus the integrity status for the
`/audit` endpoint.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_AUDIT_LOG=1" \
  --lib="." examples/L7-Application/AuditLog/AuditLog.ino
```

```sh
curl "http://<ip>/login?user=alice&pass=secret"   # logs auth
curl "http://<ip>/config?http_port=8080"          # logs a config change
curl http://<ip>/audit                            # chain dump + {"intact":true}
```

## Annotated source

The complete sketch ([AuditLog.ino](AuditLog.ino)), reproduced verbatim with
added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_AUDIT_LOG 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/security/audit_log/audit_log.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// Durable forwarding: runs once per record at append time. Point it wherever you
// keep authoritative logs.
static void audit_sink(const pc_audit_entry *e)
{
    char line[256];
    if (pc_audit_format(e, line, sizeof(line)) > 0)
    {
        Serial.print("[AUDIT] ");
        Serial.println(line);
        // SD card:   File f = SD.open("/audit.log", FILE_APPEND); f.println(line); f.close();
        // Log svc:   pc_webhook_post("http://logs.example/ingest", line);  // PC_ENABLE_WEBHOOK
    }
}

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

    pc_audit_reset();
    pc_audit_set_sink(audit_sink);
    pc_audit_append(pc_audit_cat::PC_AUDIT_SYSTEM, "boot");

    server.on("/login", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *user = http_get_query(req, "user");
        const char *pass = http_get_query(req, "pass");
        char msg[PC_AUDIT_MSG_LEN];
        bool ok = pass && strcmp(pass, "secret") == 0;
        snprintf(msg, sizeof(msg), "login %s", user ? user : "?");
        pc_audit_append(ok ? pc_audit_cat::PC_AUDIT_AUTH : pc_audit_cat::PC_AUDIT_AUTH_FAIL, msg);
        server.send(id, ok ? 200 : 401, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    server.on("/config", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *port = http_get_query(req, "http_port");
        char msg[PC_AUDIT_MSG_LEN];
        snprintf(msg, sizeof(msg), "set http_port=%s", port ? port : "?");
        pc_audit_append(pc_audit_cat::PC_AUDIT_CONFIG, msg);
        server.send(id, 200, "application/json", "{\"ok\":true}");
    });

    server.on("/audit", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        char doc[2048];
        if (pc_audit_dump_json(doc, sizeof(doc)) > 0)
            server.send(id, 200, "application/json", doc);
        else
            server.send(id, 500, "application/json", "{\"error\":\"buffer\"}");
    });

    server.begin(80);
}

void loop()
{
    server.handle();
}
```
