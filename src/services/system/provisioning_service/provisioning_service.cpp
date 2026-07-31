// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file provisioning_service.cpp
 * @brief First-boot WiFi provisioning / captive portal (PC_ENABLE_PROVISIONING).
 *
 * The catch-all DNS responder uses the transport-layer UDP service (no add-on library);
 * credentials persist to NVS via Preferences.
 */

#include "provisioning_service.h"
#include "services/system/clock.h" // pcdelay
#include "shared_primitives/hex.h"
#include "shared_primitives/mime.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Form-field parser (always compiled; the only non-trivial logic, unit-tested).
// ---------------------------------------------------------------------------

#if PC_ENABLE_PROVISIONING && defined(ARDUINO)
#include "network_drivers/application/web_assets.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/udp.h"
#include "protocore.h"
#include <Arduino.h>
#include <Preferences.h>
#endif
bool pc_prov_form_field(const char *body, const char *key, char *out, size_t cap)
{
    if (out && cap)
    {
        out[0] = '\0';
    }
    if (!body || !key || !out || cap == 0)
    {
        return false;
    }

    size_t klen = strnlen(key, cap); // a form field name longer than the value buffer cannot yield a value
    const char *val = nullptr;
    for (const char *p = body; *p; p++)
    {
        // Match the key only as a whole field name: at the start or after '&'.
        if ((p == body || p[-1] == '&') && strncmp(p, key, klen) == 0 && p[klen] == '=')
        {
            val = p + klen + 1;
            break;
        }
    }
    if (!val)
    {
        return false;
    }

    size_t o = 0;
    for (const char *q = val; *q && *q != '&'; q++)
    {
        char c = *q;
        if (c == '+')
        {
            c = ' ';
        }
        else if (c == '%')
        {
            int h = pc_hex_val(q[1]);
            int l = (h >= 0) ? pc_hex_val(q[2]) : -1;
            if (h >= 0 && l >= 0)
            {
                c = (char)((h << 4) | l);
                q += 2;
            }
        }
        if (o + 1 < cap)
        {
            out[o++] = c;
        }
        else
        {
            break;
        }
    }
    out[o] = '\0';
    return true;
}

// ---------------------------------------------------------------------------
// ESP32 captive portal (softAP + lwIP UDP DNS + form/save routes)
// ---------------------------------------------------------------------------

#if PC_ENABLE_PROVISIONING && defined(ARDUINO)

// All provisioning-service state, owned by one instance (internal linkage): the server handle
// and the softAP IP the captive-portal DNS answers with. Grouped so it is one named owner,
// unreachable cross-TU.
struct ProvCtx
{
    PC *server = nullptr;
    uint8_t ap_ip[4] = {192, 168, 4, 1};
};
static ProvCtx s_prov;

// The NVS namespace + credential keys (PC_PROV_NVS_NAMESPACE / _KEY_SSID / _KEY_PSK) live in
// protocore_config.h under PC_ENABLE_PROVISIONING so a deployment can override them; used across
// the read / clear / save paths (and, for ssid/psk, as the HTML form field names).

// Catch-all DNS: answer every query with our softAP IP (captive-portal hijack).
static void prov_dns_recv(const uint8_t *req, size_t qlen, const struct pc_udp_peer *peer, void *ctx)
{
    (void)ctx;
    if (qlen < 12)
    {
        return; // smaller than a DNS header
    }

    // Walk the (single) question to find where it ends: labels until a 0 byte,
    // then 2-byte QTYPE + 2-byte QCLASS.
    size_t qend = 12;
    while (qend < qlen && req[qend] != 0)
    {
        qend += (size_t)req[qend] + 1;
    }
    qend += 1 + 4;
    if (qend > qlen)
    {
        return;
    }

    uint8_t resp[300];
    if (qend + 16 > sizeof(resp))
    {
        return;
    }
    memcpy(resp, req, qend);
    resp[2] = 0x81; // QR=1, opcode 0, RD copied
    resp[3] = 0x80; // RA=1, RCODE 0
    resp[6] = 0x00;
    resp[7] = 0x01; // ANCOUNT = 1
    resp[8] = resp[9] = resp[10] = resp[11] = 0x00;

    size_t n = qend;
    resp[n++] = 0xC0; // name: pointer to the question at offset 0x0C
    resp[n++] = 0x0C;
    resp[n++] = 0x00;
    resp[n++] = 0x01; // TYPE A
    resp[n++] = 0x00;
    resp[n++] = 0x01; // CLASS IN
    resp[n++] = 0x00;
    resp[n++] = 0x00;
    resp[n++] = 0x00;
    resp[n++] = 0x3C; // TTL 60s
    resp[n++] = 0x00;
    resp[n++] = 0x04; // RDLENGTH 4
    resp[n++] = s_prov.ap_ip[0];
    resp[n++] = s_prov.ap_ip[1];
    resp[n++] = s_prov.ap_ip[2];
    resp[n++] = s_prov.ap_ip[3];

    pc_udp_send(peer, resp, n);
}

bool pc_provisioning_load(char *ssid, size_t ssid_cap, char *psk, size_t psk_cap)
{
    if (ssid && ssid_cap)
    {
        ssid[0] = '\0';
    }
    if (psk && psk_cap)
    {
        psk[0] = '\0';
    }
    Preferences prefs;
    if (!prefs.begin(PC_PROV_NVS_NAMESPACE, true))
    {
        return false;
    }
    String s = prefs.getString(PC_PROV_KEY_SSID, "");
    String k = prefs.getString(PC_PROV_KEY_PSK, "");
    prefs.end();
    if (s.length() == 0)
    {
        return false;
    }
    strncpy(ssid, s.c_str(), ssid_cap - 1);
    ssid[ssid_cap - 1] = '\0';
    strncpy(psk, k.c_str(), psk_cap - 1);
    psk[psk_cap - 1] = '\0';
    return true;
}

void pc_provisioning_clear()
{
    Preferences prefs;
    if (prefs.begin(PC_PROV_NVS_NAMESPACE, false))
    {
        prefs.clear();
        prefs.end();
    }
}

static void prov_form_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    s_prov.server->send(slot_id, 200, PC_MIME_TEXT_HTML, PC_PROV_FORM);
}

static void prov_save_handler(uint8_t slot_id, HttpReq *req)
{
    char ssid[33];
    char psk[64];
    bool have_ssid =
        pc_prov_form_field((const char *)req->body, PC_PROV_KEY_SSID, ssid, sizeof(ssid)) && ssid[0] != '\0';
    pc_prov_form_field((const char *)req->body, PC_PROV_KEY_PSK, psk, sizeof(psk));
    if (!have_ssid)
    {
        s_prov.server->send(slot_id, 400, PC_MIME_TEXT_PLAIN, "SSID required");
        return;
    }
    Preferences prefs;
    prefs.begin(PC_PROV_NVS_NAMESPACE, false);
    prefs.putString(PC_PROV_KEY_SSID, ssid);
    prefs.putString(PC_PROV_KEY_PSK, psk);
    prefs.end();
    s_prov.server->send(slot_id, 200, PC_MIME_TEXT_HTML, PC_PROV_SAVED_HTML);
    pcdelay(500);
    ESP.restart();
}

void pc_provisioning_begin(PC &server, const char *ap_ssid)
{
    s_prov.server = &server;
    init_wifi_ap_physical(ap_ssid, nullptr); // AP mode is implied by which bring-up you call
    uint32_t ip = pc_net_ap_ip();            // network byte order
    s_prov.ap_ip[0] = (uint8_t)(ip & 0xFF);
    s_prov.ap_ip[1] = (uint8_t)((ip >> 8) & 0xFF);
    s_prov.ap_ip[2] = (uint8_t)((ip >> 16) & 0xFF);
    s_prov.ap_ip[3] = (uint8_t)((ip >> 24) & 0xFF);

    // Catch-all DNS on UDP/53 via the transport-layer UDP service (callback-driven).
    pc_udp_listen(53, prov_dns_recv, nullptr);

    server.on("/save", HttpMethod::HTTP_POST, prov_save_handler);
    server.on("/*", HttpMethod::HTTP_GET, prov_form_handler); // any other path -> the form
}

#else // disabled / non-Arduino: stubs (form-field parser above stays available)

bool pc_provisioning_load(char *ssid, size_t ssid_cap, char *psk, size_t psk_cap)
{
    if (ssid && ssid_cap)
    {
        ssid[0] = '\0';
    }
    if (psk && psk_cap)
    {
        psk[0] = '\0';
    }
    return false;
}
// server can't be a const ref: the Arduino build's pc_provisioning_begin() registers routes via
// server.on(); this host stub just ignores it (hence the const-ref suggestion here is a false positive).
void pc_provisioning_begin(PC &server, const char *ap_ssid) // NOSONAR
{
    (void)server;
    (void)ap_ssid;
}
void pc_provisioning_clear()
{
}

#endif // PC_ENABLE_PROVISIONING && ARDUINO
