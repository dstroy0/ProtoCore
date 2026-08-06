// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file main_smb.cpp
 * @brief Slim SMB2-client test-rig firmware (env:rig_s3_smb) - the target for the SMB2 interop peer +
 *        the malicious-SMB2-server attack in penetration_testing/pc_pentest.py.
 *
 * The device is the SMB2 *client*: the /smb/probe endpoint connects OUT to the server named in the query
 * (?host=&port=&user=&pass=&share=&path=) over the shared outbound transport (pc_client_*), runs the
 * smb_client.h dialogue (NEGOTIATE -> NTLMv2 SESSION_SETUP -> TREE_CONNECT -> CREATE -> READ -> CLOSE) and
 * reports each step. Against a real samba it is the interop oracle; against pc_pentest.py's fake server
 * it exercises the response parsers (the malicious-server harness, like the FTP / SMTP / NATS probes).
 *
 * Deliberately minimal (HTTP/80 + the one probe) so it stays DRAM-light on the stock arduino-esp32 core -
 * the full 20-protocol rig is DRAM-full. Built ONLY by env:rig_s3_smb (build_src_filter). WiFi creds come
 * from RIG_WIFI_SSID / RIG_WIFI_PASS via the WIFI_SSID / WIFI_PASS macros (never committed).
 */
#include "network_drivers/application/smb/smb2.h"       // Smb2Access / Smb2Disposition masks
#include "network_drivers/application/smb/smb_client.h" // smb_open / smb_read / smb_close dialogue engine
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/client.h" // pc_client_* (device-as-SMB2-client probe transport)
#include "protocore.h"
#include <Arduino.h>
#include <WiFi.h>

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_SSID"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "YOUR_PASSWORD"
#endif

static const char *SSID = WIFI_SSID;
static const char *PASSWORD = WIFI_PASS;

PC server;

// Transport seam: the SMB2 engine moves raw bytes only through these two, wired to the outbound client
// socket (ctx carries the connection id). recv loops until data or a bounded timeout, so a stalled or
// malicious server cannot wedge the probe.
static int smb_probe_send(void *ctx, const uint8_t *data, size_t len)
{
    int cid = (int)(intptr_t)ctx;
    return pc_client_send(cid, data, len) ? (int)len : -1;
}
static int smb_probe_recv(void *ctx, uint8_t *buf, size_t cap)
{
    int cid = (int)(intptr_t)ctx;
    uint32_t t0 = millis();
    while (millis() - t0 < 4000)
    {
        size_t got = pc_client_read(cid, buf, cap);
        if (got > 0)
        {
            return (int)got;
        }
        delay(5);
    }
    return -1; // timeout -> SMB_ERR_IO
}

static uint16_t parse_u16(const char *s)
{
    uint16_t v = 0;
    for (; s && *s >= '0' && *s <= '9'; s++)
    {
        v = (uint16_t)(v * 10 + (*s - '0'));
    }
    return v;
}

// SMB2 device-as-client probe: NEGOTIATE -> NTLMv2 SESSION_SETUP -> TREE_CONNECT -> CREATE -> READ -> CLOSE
// against ?host=&port=&user=&pass=&share=&path= . Reports each step; on a real samba it is the interop
// oracle, on the pentest fake server it drives the response parsers.
static void h_smb_probe(uint8_t id, HttpReq *r)
{
    const char *host = http_get_query(r, "host");
    const char *ports = http_get_query(r, "port");
    if (!host || !ports)
    {
        server.send(id, 400, "application/json", "{\"err\":\"host+port required\"}");
        return;
    }
    uint16_t port = parse_u16(ports);
    const char *user = http_get_query(r, "user");
    const char *pass = http_get_query(r, "pass");
    const char *sharen = http_get_query(r, "share");
    const char *path = http_get_query(r, "path");
    if (!user)
    {
        user = "pc";
    }
    if (!pass)
    {
        pass = "pc";
    }
    if (!sharen)
    {
        sharen = "share";
    }
    if (!path)
    {
        path = "test.txt";
    }

    char unc[128];
    snprintf(unc, sizeof(unc), "\\\\%s\\%s", host, sharen);

    int cid = pc_client_open(host, port, 4000);
    if (cid < 0)
    {
        server.send(id, 200, "application/json", "{\"connected\":0}");
        return;
    }

    SmbConfig cfg = {};
    cfg.user = user;
    cfg.pass = pass;
    cfg.domain = nullptr;
    cfg.workstation = "PROTOCORE-RIG";
    cfg.share = unc;
    cfg.path = path;
    cfg.desired_access = SMB2_FILE_GENERIC_READ;
    cfg.disposition = SMB2_FILE_OPEN;
    // ?encrypt=1 forces SMB 3.x transport encryption on (needed for a share that requires it); ?cipher=<1..4>
    // pins a specific Smb2Cipher (1=AES128-CCM, 2=AES128-GCM, 3=AES256-CCM, 4=AES256-GCM) so each can be
    // exercised against a real server.
    const char *enc = http_get_query(r, "encrypt");
    const char *cip = http_get_query(r, "cipher");
    cfg.encrypt = (enc && enc[0] == '1');
    cfg.cipher_pref = cip ? parse_u16(cip) : 0;

    void *ctx = (void *)(intptr_t)cid;
    SmbHandle h = {};
    SmbResult ro = smb_open(&cfg, &h, smb_probe_send, smb_probe_recv, ctx);

    int rlen = 0;
    SmbResult rr = SmbResult::SMB_OK;
    SmbResult rc = SmbResult::SMB_OK;
    unsigned long long fsize = 0;
    uint32_t fnv = 2166136261u; // FNV-1a of the read bytes: the peer recomputes it to byte-verify the content
    if (ro == SmbResult::SMB_OK)
    {
        static uint8_t fb[256];
        size_t got = 0;
        rr = smb_read(&h, 0, fb, sizeof(fb), &got, smb_probe_send, smb_probe_recv, ctx);
        rlen = (int)got;
        fsize = (unsigned long long)h.file_size;
        for (size_t i = 0; i < got; i++)
        {
            fnv = (fnv ^ fb[i]) * 16777619u;
        }
        rc = smb_close(&h, smb_probe_send, smb_probe_recv, ctx);
    }
    pc_client_close(cid);

    // (int) on the SmbResult members is the report/wire boundary (JSON status out), not an enum silencing.
    char b[288];
    snprintf(
        b, sizeof(b),
        "{\"connected\":1,\"enc\":%d,\"cipher\":%u,\"open\":%d,\"read\":%d,\"read_rc\":%d,\"size\":%llu,\"fnv\":%u,"
        "\"close\":%d,\"heap\":%u}",
        (int)h.encrypt_active, (unsigned)h.enc_cipher, (int)ro, rlen, (int)rr, fsize, (unsigned)fnv, (int)rc,
        (unsigned)ESP.getFreeHeap());
    server.send(id, 200, "application/json", b);
}

static void h_root(uint8_t id, HttpReq *)
{
    server.send(id, 200, "text/plain", "pc-s3-smb-rig");
}

// Free-heap JSON: the pentest tool's liveness + determinism oracle samples this over HTTP/80.
static void h_health(uint8_t id, HttpReq *)
{
    char b[64];
    snprintf(b, sizeof(b), "{\"heap\":%u}", (unsigned)ESP.getFreeHeap());
    server.send(id, 200, "application/json", b);
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    Physical.wifi->init(SSID, PASSWORD);
    uint32_t t0 = millis();
    while (!Physical.wifi->ready() && millis() - t0 < 30000)
    {
        delay(200);
    }
    WiFi.setSleep(false);
    Serial.print("RIG_IP=");
    Serial.println(WiFi.localIP());

    server.set_cors("*");
    server.on("/", HttpMethod::HTTP_GET, h_root);
    server.on("/health", HttpMethod::HTTP_GET, h_health);
    server.on("/smb/probe", HttpMethod::HTTP_GET, h_smb_probe);
    server.on("/diag", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.diag(id); });

    int32_t rc = server.begin(80);
    Serial.print("BEGIN=");
    Serial.println(rc);
}

void loop()
{
    server.handle();
}
