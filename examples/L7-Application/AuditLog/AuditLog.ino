// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file AuditLog.ino
 * @brief Tamper-evident, hash-chained audit log (PC_ENABLE_AUDIT_LOG).
 *
 * Records security-relevant events in an append-only log where each record
 * chains SHA-256(prev_hash || fields). Any tampering with a retained record
 * breaks the chain, which /audit reports via "intact":false + the first broken
 * sequence number.
 *
 *   GET /login?user=alice&pass=secret  -> logs auth / auth_fail
 *   GET /config?http_port=8080         -> logs a config change
 *   GET /audit                         -> JSON chain dump + integrity status
 *
 * A sink forwards every record - at the moment it is created, before the RAM
 * ring can ever evict it - to a durable / remote store. Here it just prints to
 * Serial; the commented lines show writing to an SD-card file or POSTing to a
 * log service. Because the sink gets the full record (including its chain hash),
 * the external copy keeps the same tamper-evident chain.
 *
 * NOTE: enable it for the whole build. In platformio.ini:
 *     build_flags = -DPC_ENABLE_AUDIT_LOG=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_AUDIT_LOG 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/security/audit_log/audit_log.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


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
    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
    {
        delay(250);
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    pc_audit_reset();
    pc_audit_set_sink(audit_sink);
    pc_audit_append(pc_audit_cat::PC_AUDIT_SYSTEM, "boot");

    on_http("/login", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *user = http_get_query(req, "user");
        const char *pass = http_get_query(req, "pass");
        char msg[PC_AUDIT_MSG_LEN];
        bool ok = pass && strcmp(pass, "secret") == 0;
        snprintf(msg, sizeof(msg), "login %s", user ? user : "?");
        pc_audit_append(ok ? pc_audit_cat::PC_AUDIT_AUTH : pc_audit_cat::PC_AUDIT_AUTH_FAIL, msg);
        send_text(id, ok ? 200 : 401, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    on_http("/config", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *port = http_get_query(req, "http_port");
        char msg[PC_AUDIT_MSG_LEN];
        snprintf(msg, sizeof(msg), "set http_port=%s", port ? port : "?");
        pc_audit_append(pc_audit_cat::PC_AUDIT_CONFIG, msg);
        send_text(id, 200, "application/json", "{\"ok\":true}");
    });

    on_http("/audit", HTTP_GET, [](uint8_t id, HttpReq *) {
        char doc[2048];
        if (pc_audit_dump_json(doc, sizeof(doc)) > 0)
        {
            send_text(id, 200, "application/json", doc);
        }
        else
        {
            send_text(id, 500, "application/json", "{\"error\":\"buffer\"}");
        }
    });

    begin_http(80, NULL);
}

void loop()
{
    handle();
}
