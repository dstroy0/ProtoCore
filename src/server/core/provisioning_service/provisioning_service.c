// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file provisioning_service.c
 * @brief First-boot WiFi provisioning / captive portal (PROTOCORE_ENABLE_PROVISIONING).
 *
 * The catch-all DNS responder uses the transport-layer UDP service (no add-on library);
 * credentials persist through hal/nvs.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t hex_work[16]; // the borrow an entry takes; Hex never reads it

#if PROTOCORE_ENABLE_PROVISIONING

#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h" // str: the bounded-run walks
#include "mmgr/secure/secure.h"     // the persistent end this module's state is taken from
#include "provisioning_service.h"
#include "server/clock/clock.h" // pcdelay
#include "shared/hex/hex.h"
#include "shared/mime/mime.h"

// ---------------------------------------------------------------------------
// Form-field parser: the only non-trivial pure logic here, and what the unit tests drive.
// ---------------------------------------------------------------------------

#include "network_drivers/application/web_assets/web_assets.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/transport/udp/server/server.h" // UdpListener: the catch-all DNS binds a port
#include "protocore.h"
#include "test/core_setup/hal/nvs.h" // the credentials outlive the reboot that applies them

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_PROVISIONING_BORROW persistent bytes
} ProvOwnCtx;
static ProvOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_provisioning_service_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_PROVISIONING_BORROW).buf;
    }
    return s_own.span;
}

void protocore_provisioning_service_form_field(uint8_t *restrict work);

void protocore_provisioning_service_form_field(uint8_t *restrict work)
{
    (void)work;
    const char *body = ProvV.form_field_args.body;
    const char *key = ProvV.form_field_args.key;
    char *out = ProvV.form_field_args.out;
    size_t cap = ProvV.form_field_args.cap;

    if (out && cap)
    {
        out[0] = '\0';
    }
    if (!body || !key || !out || cap == 0)
    {
        ProvV.ok = PROTO_FALSE;
        return;
    }

    size_t blen = 0;
    while (body[blen] != '\0')
    {
        blen++;
    }
    size_t klen = str.len(key, blen); // a name longer than the body cannot occur in it
    const char *val = NULL;
    for (const char *p = body; *p; p++)
    {
        // Match the key only as a whole field name: at the start or after '&'.
        if ((p == body || p[-1] == '&') && str.starts(p, key, klen, PROTO_FALSE) && p[klen] == '=')
        {
            val = p + klen + 1;
            break;
        }
    }
    if (!val)
    {
        ProvV.ok = PROTO_FALSE;
        return;
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
            HexV.args.ch = q[1];
            Hex.val(hex_work);
            int h = HexV.i8;
            int l = -1;
            if (h >= 0)
            {
                HexV.args.ch = q[2];
                Hex.val(hex_work);
                l = HexV.i8;
            }
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
    ProvV.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// The captive portal: softAP + a catch-all UDP DNS + the form / save routes
// ---------------------------------------------------------------------------

// All provisioning-service state, owned by one instance (internal linkage): the server handle
// and the softAP IP the captive-portal DNS answers with. Grouped so it is one named owner,
// unreachable cross-TU.
typedef struct
{
    uint8_t ap_ip[4];
} ProvCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define PROVISIONING_SERVICE_OFF_CTX 0u
static_assert(PROVISIONING_SERVICE_OFF_CTX + sizeof(ProvCtx) <= PROTOCORE_PROVISIONING_BORROW,
              "PROTOCORE_PROVISIONING_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(PROVISIONING_SERVICE_OFF_CTX % _Alignof(ProvCtx) == 0,
              "PROVISIONING_SERVICE_OFF_CTX is not a multiple of alignof(ProvCtx) - PROVISIONING_SERVICE_CTX() would "
              "return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define PROVISIONING_SERVICE_CTX(w) ((ProvCtx *)(void *)((w) + PROVISIONING_SERVICE_OFF_CTX))

// One octet of the softAP address the catch-all DNS answers with. All-zero is "begin() has not
// stamped one yet", which reads as the 192.168.4.1 a softAP comes up on - stated here rather than as
// an initializer so the context carries none and can live in a borrow that arrives zeroed.
static uint8_t ap_octet(uint8_t *restrict work, uint32_t i)
{
    static const uint8_t softap_default[4] = {192, 168, 4, 1};
    const uint8_t *ip = PROVISIONING_SERVICE_CTX(work)->ap_ip;
    return (ip[0] || ip[1] || ip[2] || ip[3]) ? ip[i] : softap_default[i];
}

// The NVS namespace + credential keys (PROTOCORE_PROV_NVS_NAMESPACE / _KEY_SSID / _KEY_PSK) live in
// protocore_config.h under PROTOCORE_ENABLE_PROVISIONING so a deployment can override them; used across
// the read / clear / save paths (and, for ssid/psk, as the HTML form field names).

// Catch-all DNS: answer every query with our softAP IP (captive-portal hijack).
static void prov_dns_recv(const uint8_t *req, size_t qlen, const struct protocore_udp_peer *peer, void *ctx)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_provisioning_service_span();

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
    mem.cpy(resp, req, qend);
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
    resp[n++] = ap_octet(work, 0);
    resp[n++] = ap_octet(work, 1);
    resp[n++] = ap_octet(work, 2);
    resp[n++] = ap_octet(work, 3);

    UdpListenerV.peer_args.peer = peer;
    UdpListenerV.send_args.data = resp;
    UdpListenerV.send_args.len = n;
    UdpListener.reply(protocore_udp_listener_span());
}

void protocore_provisioning_service_load(uint8_t *restrict work)
{
    (void)work;
    char *ssid = ProvV.load_args.ssid;
    size_t ssid_cap = ProvV.load_args.ssid_cap;
    char *psk = ProvV.load_args.psk;
    size_t psk_cap = ProvV.load_args.psk_cap;

    if (ssid && ssid_cap)
    {
        ssid[0] = '\0';
    }
    if (psk && psk_cap)
    {
        psk[0] = '\0';
    }
    if (!ssid || ssid_cap == 0 ||
        protocore_nvs_get_str(PROTOCORE_PROV_NVS_NAMESPACE, PROTOCORE_PROV_KEY_SSID, ssid, ssid_cap) == 0)
    {
        ProvV.ok = PROTO_FALSE;
        return;
    }
    if (psk && psk_cap)
    {
        (void)protocore_nvs_get_str(PROTOCORE_PROV_NVS_NAMESPACE, PROTOCORE_PROV_KEY_PSK, psk,
                                    psk_cap); // an open AP has none
    }
    ProvV.ok = PROTO_TRUE;
}

void protocore_provisioning_service_clear(uint8_t *restrict work)
{
    (void)work;

    (void)protocore_nvs_clear(PROTOCORE_PROV_NVS_NAMESPACE);
}

static void prov_form_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    send_text(slot_id, 200, PROTOCORE_MIME_TEXT_HTML, PROTOCORE_PROV_FORM);
}

static void prov_save_handler(uint8_t slot_id, HttpReq *req)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_provisioning_service_span();

    char ssid[33];
    char psk[64];
    ProvV.form_field_args.body = (const char *)req->body;
    ProvV.form_field_args.key = PROTOCORE_PROV_KEY_SSID;
    ProvV.form_field_args.out = ssid;
    ProvV.form_field_args.cap = sizeof(ssid);
    protocore_provisioning_service_form_field(work);
    proto_bool have_ssid = ProvV.ok && ssid[0] != '\0';
    ProvV.form_field_args.body = (const char *)req->body;
    ProvV.form_field_args.key = PROTOCORE_PROV_KEY_PSK;
    ProvV.form_field_args.out = psk;
    ProvV.form_field_args.cap = sizeof(psk);
    protocore_provisioning_service_form_field(work);
    if (!have_ssid)
    {
        send_text(slot_id, 400, PROTOCORE_MIME_TEXT_PLAIN, "SSID required");
        return;
    }
    (void)protocore_nvs_put_str(PROTOCORE_PROV_NVS_NAMESPACE, PROTOCORE_PROV_KEY_SSID, ssid);
    (void)protocore_nvs_put_str(PROTOCORE_PROV_NVS_NAMESPACE, PROTOCORE_PROV_KEY_PSK, psk);
    send_text(slot_id, 200, PROTOCORE_MIME_TEXT_HTML, PROTOCORE_PROV_SAVED_HTML);
    pcdelay(500);
    protocore_platform_restart();
}

void protocore_provisioning_service_begin(uint8_t *restrict work)
{
    const char *ap_ssid = ProvV.begin_args.ap_ssid;

    PhysicalV.wifi.ssid = ap_ssid;
    PhysicalV.wifi.password = NULL;
    Physical.wifi_ap_init(protocore_physical_span()); // AP mode is implied by which bring-up you call
    Physical.wifi_ap_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // network byte order
    PROVISIONING_SERVICE_CTX(work)->ap_ip[0] = (uint8_t)(ip & 0xFF);
    PROVISIONING_SERVICE_CTX(work)->ap_ip[1] = (uint8_t)((ip >> 8) & 0xFF);
    PROVISIONING_SERVICE_CTX(work)->ap_ip[2] = (uint8_t)((ip >> 16) & 0xFF);
    PROVISIONING_SERVICE_CTX(work)->ap_ip[3] = (uint8_t)((ip >> 24) & 0xFF);

    // Catch-all DNS on UDP/53 via the transport-layer UDP service (callback-driven).
    UdpListenerV.port = 53;
    UdpListenerV.bind.handler = prov_dns_recv;
    UdpListenerV.bind.handler_ctx = NULL;
    UdpListenerV.bind.group_ip = NULL;
    UdpListener.listen(protocore_udp_listener_span());

    on_http("/save", HTTP_POST, prov_save_handler);
    on_http("/*", HTTP_GET, prov_form_handler); // any other path -> the form
}

/** @brief The operands and the outcome. */
ProvVars ProvV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PROVISIONING
