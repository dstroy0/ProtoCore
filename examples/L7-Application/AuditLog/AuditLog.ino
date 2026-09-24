// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file AuditLog.ino
 * @brief Tamper-evident, hash-chained audit log (PROTOCORE_ENABLE_AUDIT_LOG).
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
 *     build_flags = -DPROTOCORE_ENABLE_AUDIT_LOG=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PROTOCORE_ENABLE_AUDIT_LOG 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "server/security/audit_log/audit_log.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// Durable forwarding: runs once per record at append time. Point it wherever you
// keep authoritative logs.
static void audit_sink(const protocore_audit_entry *e)
{
    char line[256];
    AuditLogV.format_args.entry = e;
    AuditLogV.format_args.out = line;
    AuditLogV.format_args.cap = sizeof(line);
    AuditLog.format(protocore_audit_log_span());
    if (AuditLogV.n > 0)
    {
        Serial.print("[AUDIT] ");
        Serial.println(line);
        // SD card:   File f = SD.open("/audit.log", FILE_APPEND); f.println(line); f.close();
        // Log svc:   protocore_webhook_post("http://logs.example/ingest", line);  // PROTOCORE_ENABLE_WEBHOOK
    }
}

// Append one record to the chain (and hand it to the sink).
static void audit(protocore_audit_cat category, const char *msg)
{
    AuditLogV.append_args.category = category;
    AuditLogV.append_args.msg = msg;
    AuditLog.append(protocore_audit_log_span());
}

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

    AuditLog.reset(protocore_audit_log_span());
    AuditLogV.set_sink_args.sink = audit_sink;
    AuditLog.set_sink(protocore_audit_log_span());
    audit(protocore_audit_cat::PROTOCORE_AUDIT_SYSTEM, "boot");

    on_http("/login", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *user = HttpParser.get_query(protocore_http_parser_span(), req, "user");
        const char *pass = HttpParser.get_query(protocore_http_parser_span(), req, "pass");
        char msg[PROTOCORE_AUDIT_MSG_LEN];
        bool ok = pass && strcmp(pass, "secret") == 0;
        snprintf(msg, sizeof(msg), "login %s", user ? user : "?");
        audit(ok ? protocore_audit_cat::PROTOCORE_AUDIT_AUTH : protocore_audit_cat::PROTOCORE_AUDIT_AUTH_FAIL, msg);
        send_text(id, ok ? 200 : 401, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    on_http("/config", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *port = HttpParser.get_query(protocore_http_parser_span(), req, "http_port");
        char msg[PROTOCORE_AUDIT_MSG_LEN];
        snprintf(msg, sizeof(msg), "set http_port=%s", port ? port : "?");
        audit(protocore_audit_cat::PROTOCORE_AUDIT_CONFIG, msg);
        send_text(id, 200, "application/json", "{\"ok\":true}");
    });

    on_http("/audit", HTTP_GET, [](uint8_t id, HttpReq *) {
        char doc[2048];
        AuditLogV.dump_json_args.out = doc;
        AuditLogV.dump_json_args.cap = sizeof(doc);
        AuditLog.dump_json(protocore_audit_log_span());
        if (AuditLogV.n > 0)
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
