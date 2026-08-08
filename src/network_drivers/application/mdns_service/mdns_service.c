// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mdns_service.c
 * @brief mDNS / DNS-SD advertisement implementation (PC_ENABLE_MDNS).
 *
 * Two backends behind one API, picked by PC_HAS_VENDOR_MDNS: the vendor's own responder where the
 * SDK ships one, and otherwise the portable responder below, which answers over the UDP listener
 * like every other datagram service in the tree.
 */

#include "mdns_service.h"

#if PC_ENABLE_MDNS

// Both backends' includes, ahead of either backend's code: what a translation unit reaches for is
// stated once at its top, whichever arm the capability selects.
#if PC_HAS_VENDOR_MDNS
#include "mdns.h" // the vendor's responder, driven through its own component API
#else
#include "mmgr/rawmemcpy.h"                       // proto_raw_read: every field moves whole
#include "mmgr/secure.h"                          // pc_secure_persist_span: this module's storage
#include "network_drivers/network/dns/dns_wire.h" // the name codec both DNS halves share
#include "network_drivers/physical/physical.h"    // pc_net_egress_ip: the address the A record carries
#include "network_drivers/transport/udp.h"        // Udp.listener: the 5353 group bind and the reply
#include "shared_primitives/runops.h"             // proto_scan_nul: where a caller's string ends
#endif

#if PC_HAS_VENDOR_MDNS

proto_bool pc_mdns_begin(const char *hostname, uint16_t http_port)
{
    if (hostname == NULL || hostname[0] == '\0')
    {
        return PROTO_FALSE;
    }
    if (mdns_init() != ESP_OK)
    {
        return PROTO_FALSE;
    }
    if (mdns_hostname_set(hostname) != ESP_OK)
    {
        return PROTO_FALSE;
    }
    // Advertise an HTTP service so browsers / DNS-SD tools discover the device.
    mdns_service_add(NULL, "_http", "_tcp", http_port, NULL, 0);
    return PROTO_TRUE;
}

proto_bool pc_mdns_txt(const char *key, const char *value)
{
    if (key == NULL || value == NULL)
    {
        return PROTO_FALSE;
    }
    // Attach a TXT key/value to the _http._tcp service (Bonjour browsers show it).
    return mdns_service_txt_item_set("_http", "_tcp", key, value) == ESP_OK;
}

proto_bool pc_mdns_add_service(const char *service_type, const char *proto, uint16_t port)
{
    if (service_type == NULL || proto == NULL)
    {
        return PROTO_FALSE;
    }
    // Advertise an additional service, e.g. ("_https", "_tcp", 443).
    return mdns_service_add(NULL, service_type, proto, port, NULL, 0) == ESP_OK;
}

#else // the portable responder

/** @brief The link-local multicast group and port every mDNS message uses (RFC 6762 sec 3). */
#define PC_MDNS_GROUP "224.0.0.251"
#define PC_MDNS_PORT 5353u

// The record types DNS-SD is built from (RFC 1035 sec 3.2.2, RFC 2782), and the QTYPE that asks for
// all of them.
#define PC_MDNS_T_A 1u
#define PC_MDNS_T_PTR 12u
#define PC_MDNS_T_TXT 16u
#define PC_MDNS_T_SRV 33u
#define PC_MDNS_T_ANY 255u

// Class IN, and IN with the cache-flush bit a responder sets on a record it alone owns (RFC 6762
// sec 10.2). The shared PTRs never carry it; the host's A, SRV and TXT always do.
#define PC_MDNS_C_IN 0x0001u
#define PC_MDNS_C_FLUSH 0x8001u

// Seconds a resolver may cache each kind (RFC 6762 sec 10): a host record follows the address, a
// service record outlives it.
#define PC_MDNS_TTL_HOST 120u
#define PC_MDNS_TTL_SVC 4500u

/** @brief The name every DNS-SD browser walks to enumerate what a host offers (RFC 6763 sec 9). */
#define PC_MDNS_ENUM_NAME "_services._dns-sd._udp.local"

/** @brief The parent of every name this responder owns. */
#define PC_MDNS_DOMAIN "local"

/** @brief One advertised service: its DNS-SD type, its transport, and the port it answers on. */
typedef struct
{
    char type[PC_MDNS_LABEL_MAX];  ///< "_http"
    char proto[PC_MDNS_LABEL_MAX]; ///< "_tcp" / "_udp"
    uint16_t port;
    proto_bool used;
} MdnsSvc;

// All mDNS responder state, owned by one instance (internal linkage): the borrows the responder
// works in, how much TXT it holds, and whether it is bound. One named owner, unreachable cross-TU.
// The composition borrows are live only for the length of one handler call, and poll() runs the
// handler, so no two calls reach them at once.
typedef struct
{
    pc_span host; ///< the label alone, no domain
    pc_span fqdn; ///< "<host>.local", composed once by begin()
    pc_span svc;  ///< the MdnsSvc table
    pc_span txt;  ///< packed length-prefixed "key=value" strings
    size_t txt_len;
    pc_span tx;        ///< the response being composed
    pc_span rd;        ///< rdata staged for the record being written
    pc_span qname;     ///< the question being answered
    pc_span svc_name;  ///< "<type>.<proto>.local"
    pc_span inst_name; ///< "<host>.<type>.<proto>.local"
    proto_bool running;
} MdnsCtx;
static MdnsCtx s_mdns;

// Take every borrow once and hold it for the life of the program. A responder answers for the names
// the device is reached by, so its table and the replies it composes come from the secure pool,
// whose release wipes. False when the pool cannot cover them; begin() fails closed on that.
static proto_bool mdns_mem_bind(void)
{
    if (pc_span_has_storage(s_mdns.tx))
    {
        return PROTO_TRUE;
    }
    s_mdns.host = pc_secure_persist_span(PC_MDNS_LABEL_MAX);
    s_mdns.fqdn = pc_secure_persist_span(PC_DNS_NAME_MAX);
    s_mdns.svc = pc_secure_persist_span(sizeof(MdnsSvc) * PC_MDNS_MAX_SERVICES);
    s_mdns.txt = pc_secure_persist_span(PC_MDNS_TXT_MAX);
    s_mdns.tx = pc_secure_persist_span(PC_MDNS_TX_MAX);
    s_mdns.rd = pc_secure_persist_span(PC_DNS_NAME_MAX + 8);
    s_mdns.qname = pc_secure_persist_span(PC_DNS_NAME_MAX);
    s_mdns.svc_name = pc_secure_persist_span(PC_DNS_NAME_MAX);
    s_mdns.inst_name = pc_secure_persist_span(PC_DNS_NAME_MAX);
    return pc_span_has_storage(s_mdns.host) && pc_span_has_storage(s_mdns.fqdn) && pc_span_has_storage(s_mdns.svc) &&
           pc_span_has_storage(s_mdns.txt) && pc_span_has_storage(s_mdns.tx) && pc_span_has_storage(s_mdns.rd) &&
           pc_span_has_storage(s_mdns.qname) && pc_span_has_storage(s_mdns.svc_name) &&
           pc_span_has_storage(s_mdns.inst_name);
}

/** @brief Service @p i, over the borrow that holds the table. */
static MdnsSvc *mdns_svc(size_t i)
{
    return &((MdnsSvc *)s_mdns.svc.buf)[i];
}

/** @brief A borrowed span read as the dotted name it holds. */
static char *mdns_str(pc_span s)
{
    return (char *)s.buf;
}

// Append @p s to the dotted name in @p out as one more label, and report the new length. Returns
// @p cap when it will not fit, which every caller treats as "compose failed".
static size_t name_append(char *out, size_t cap, size_t n, const char *s)
{
    if (s == NULL)
    {
        return n; // a part the caller left out is simply not a label
    }
    if (n >= cap)
    {
        return cap;
    }
    if (n != 0)
    {
        if (n + 1 >= cap)
        {
            return cap;
        }
        out[n] = '.';
        n++;
    }
    size_t l = proto_scan_nul(s, cap - n);
    if (n + l >= cap)
    {
        return cap;
    }
    proto_raw_read(out + n, s, l);
    n += l;
    out[n] = '\0';
    return n;
}

// Compose "<a>.<b>.<c>.local" into @p out, skipping a null part. False when it does not fit.
static proto_bool name_of(char *out, size_t cap, const char *a, const char *b, const char *c)
{
    size_t n = 0;
    out[0] = '\0';
    n = name_append(out, cap, n, a);
    n = name_append(out, cap, n, b);
    n = name_append(out, cap, n, c);
    n = name_append(out, cap, n, PC_MDNS_DOMAIN);
    return n < cap;
}

// Write one resource record: the owner name, then type / class / ttl / rdlength / rdata. False when
// the record does not fit what is left of the stage.
static proto_bool rr_put(uint8_t *out, size_t cap, size_t *n, const char *owner, uint16_t type, uint16_t cls,
                         uint32_t ttl, const uint8_t *rdata, size_t rdlen)
{
    size_t p = *n;
    if (p >= cap)
    {
        return PROTO_FALSE;
    }
    size_t w = pc_dns_name_encode(out + p, cap - p, owner);
    if (w == 0)
    {
        return PROTO_FALSE;
    }
    p += w;
    if (p + 10 + rdlen > cap)
    {
        return PROTO_FALSE;
    }
    out[p] = (uint8_t)(type >> 8);
    p++;
    out[p] = (uint8_t)type;
    p++;
    out[p] = (uint8_t)(cls >> 8);
    p++;
    out[p] = (uint8_t)cls;
    p++;
    out[p] = (uint8_t)(ttl >> 24);
    p++;
    out[p] = (uint8_t)(ttl >> 16);
    p++;
    out[p] = (uint8_t)(ttl >> 8);
    p++;
    out[p] = (uint8_t)ttl;
    p++;
    out[p] = (uint8_t)(rdlen >> 8);
    p++;
    out[p] = (uint8_t)rdlen;
    p++;
    if (rdlen != 0)
    {
        proto_raw_read(out + p, rdata, rdlen);
        p += rdlen;
    }
    *n = p;
    return PROTO_TRUE;
}

// The A record for this host, when the interface has an address to advertise. A responder with no
// address says nothing rather than claiming 0.0.0.0.
static uint16_t put_a(size_t *n)
{
    uint32_t ip = pc_net_egress_ip();
    if (ip == 0)
    {
        return 0;
    }
    proto_raw_read(s_mdns.rd.buf, &ip, 4); // network order already, as the stack keeps it
    if (!rr_put(s_mdns.tx.buf, s_mdns.tx.cap, n, mdns_str(s_mdns.fqdn), PC_MDNS_T_A, PC_MDNS_C_FLUSH, PC_MDNS_TTL_HOST,
                s_mdns.rd.buf, 4))
    {
        return 0;
    }
    return 1;
}

// SRV: priority, weight, port, then the target host name (RFC 2782).
static uint16_t put_srv(const MdnsSvc *s, size_t *n)
{
    uint8_t *rd = s_mdns.rd.buf;
    rd[0] = 0;
    rd[1] = 0;
    rd[2] = 0;
    rd[3] = 0;
    rd[4] = (uint8_t)(s->port >> 8);
    rd[5] = (uint8_t)s->port;
    size_t w = pc_dns_name_encode(rd + 6, s_mdns.rd.cap - 6, mdns_str(s_mdns.fqdn));
    if (w == 0)
    {
        return 0;
    }
    if (!rr_put(s_mdns.tx.buf, s_mdns.tx.cap, n, mdns_str(s_mdns.inst_name), PC_MDNS_T_SRV, PC_MDNS_C_FLUSH,
                PC_MDNS_TTL_HOST, rd, 6 + w))
    {
        return 0;
    }
    return 1;
}

// TXT: the packed key=value strings, or one empty string when none were added - a DNS-SD TXT is
// never zero-length (RFC 6763 sec 6.1).
static uint16_t put_txt(size_t *n)
{
    const uint8_t *rd = s_mdns.txt.buf;
    size_t rdlen = s_mdns.txt_len;
    if (rdlen == 0)
    {
        s_mdns.rd.buf[0] = 0;
        rd = s_mdns.rd.buf;
        rdlen = 1;
    }
    if (!rr_put(s_mdns.tx.buf, s_mdns.tx.cap, n, mdns_str(s_mdns.inst_name), PC_MDNS_T_TXT, PC_MDNS_C_FLUSH,
                PC_MDNS_TTL_SVC, rd, rdlen))
    {
        return 0;
    }
    return 1;
}

// A PTR whose rdata is one name.
static uint16_t put_ptr(const char *owner, const char *target, size_t *n)
{
    size_t w = pc_dns_name_encode(s_mdns.rd.buf, s_mdns.rd.cap, target);
    if (w == 0)
    {
        return 0;
    }
    if (!rr_put(s_mdns.tx.buf, s_mdns.tx.cap, n, owner, PC_MDNS_T_PTR, PC_MDNS_C_IN, PC_MDNS_TTL_SVC, s_mdns.rd.buf, w))
    {
        return 0;
    }
    return 1;
}

// True when a question of @p qtype is asking for @p want.
static proto_bool wants(uint16_t qtype, uint16_t want)
{
    return qtype == want || qtype == PC_MDNS_T_ANY;
}

// Append every record this responder owns that answers @p qname / @p qtype, and report how many.
static uint16_t answer_for(const char *qname, uint16_t qtype, size_t *n)
{
    uint16_t added = 0;

    if (pc_dns_name_eq(qname, mdns_str(s_mdns.fqdn)) && wants(qtype, PC_MDNS_T_A))
    {
        added += put_a(n);
    }

    for (size_t i = 0; i < PC_MDNS_MAX_SERVICES; i++)
    {
        const MdnsSvc *s = mdns_svc(i);
        if (!s->used)
        {
            continue;
        }
        if (!name_of(mdns_str(s_mdns.svc_name), s_mdns.svc_name.cap, s->type, s->proto, NULL) ||
            !name_of(mdns_str(s_mdns.inst_name), s_mdns.inst_name.cap, mdns_str(s_mdns.host), s->type, s->proto))
        {
            continue;
        }
        // The enumeration name lists the types on offer, not the instances.
        if (pc_dns_name_eq(qname, PC_MDNS_ENUM_NAME) && wants(qtype, PC_MDNS_T_PTR))
        {
            added += put_ptr(PC_MDNS_ENUM_NAME, mdns_str(s_mdns.svc_name), n);
        }
        if (pc_dns_name_eq(qname, mdns_str(s_mdns.svc_name)) && wants(qtype, PC_MDNS_T_PTR))
        {
            added += put_ptr(mdns_str(s_mdns.svc_name), mdns_str(s_mdns.inst_name), n);
        }
        if (pc_dns_name_eq(qname, mdns_str(s_mdns.inst_name)))
        {
            if (wants(qtype, PC_MDNS_T_SRV))
            {
                added += put_srv(s, n);
            }
            if (wants(qtype, PC_MDNS_T_TXT))
            {
                added += put_txt(n);
            }
        }
    }
    return added;
}

// Answer each question the query carries. A response goes back to the group rather than to the
// sender, so every resolver on the link updates its cache from it (RFC 6762 sec 6).
static void mdns_udp_handler(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    (void)ctx;
    (void)peer;
    if (!s_mdns.running || len < 12)
    {
        return;
    }
    if ((data[2] & 0x80u) != 0)
    {
        return; // a response, not a query: this responder does not cache
    }
    uint16_t qd = (uint16_t)(((uint16_t)data[4] << 8) | data[5]);
    if (qd == 0)
    {
        return;
    }

    // The response header: no id to echo, no questions repeated, QR and AA set (RFC 6762 sec 18).
    uint8_t *tx = s_mdns.tx.buf;
    for (size_t i = 0; i < 12; i++)
    {
        tx[i] = 0;
    }
    tx[2] = 0x84;
    size_t n = 12;

    uint16_t an = 0;
    size_t off = 12;
    for (uint16_t q = 0; q < qd; q++)
    {
        if (!pc_dns_name_decode(data, len, off, mdns_str(s_mdns.qname), s_mdns.qname.cap, &off, PROTO_TRUE))
        {
            return; // a malformed question: the rest of the message cannot be located
        }
        if (off + 4 > len)
        {
            return;
        }
        uint16_t qtype = (uint16_t)(((uint16_t)data[off] << 8) | data[off + 1]);
        off += 4;
        an += answer_for(mdns_str(s_mdns.qname), qtype, &n);
    }
    if (an == 0)
    {
        return; // nothing of ours was asked for, so nothing is said
    }
    tx[6] = (uint8_t)(an >> 8);
    tx[7] = (uint8_t)an;

    pc_ip group = {PC_IP_NONE, {0}};
    if (Ip.parse(PC_MDNS_GROUP, &group))
    {
        (void)Udp.listener->sendto(PC_MDNS_PORT, &group, PC_MDNS_PORT, tx, n);
    }
}

// Copy @p src into @p dst, bounded by @p cap. False when it does not fit whole: a truncated service
// type would advertise a name nothing resolves.
static proto_bool label_set(char *dst, size_t cap, const char *src)
{
    size_t n = proto_scan_nul(src, cap);
    if (n == 0 || n >= cap)
    {
        return PROTO_FALSE;
    }
    proto_raw_read(dst, src, n);
    dst[n] = '\0';
    return PROTO_TRUE;
}

proto_bool pc_mdns_add_service(const char *service_type, const char *proto, uint16_t port)
{
    if (service_type == NULL || proto == NULL || !mdns_mem_bind())
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < PC_MDNS_MAX_SERVICES; i++)
    {
        MdnsSvc *s = mdns_svc(i);
        if (s->used)
        {
            continue;
        }
        if (!label_set(s->type, sizeof s->type, service_type) || !label_set(s->proto, sizeof s->proto, proto))
        {
            return PROTO_FALSE;
        }
        s->port = port;
        s->used = PROTO_TRUE;
        return PROTO_TRUE;
    }
    return PROTO_FALSE; // the table is full
}

proto_bool pc_mdns_txt(const char *key, const char *value)
{
    if (key == NULL || value == NULL || !mdns_mem_bind())
    {
        return PROTO_FALSE;
    }
    uint8_t *txt = s_mdns.txt.buf;
    size_t kl = proto_scan_nul(key, s_mdns.txt.cap);
    size_t vl = proto_scan_nul(value, s_mdns.txt.cap);
    size_t entry = kl + 1 + vl; // "key=value"
    if (kl == 0 || entry > 255 || s_mdns.txt_len + 1 + entry > s_mdns.txt.cap)
    {
        return PROTO_FALSE;
    }
    size_t n = s_mdns.txt_len;
    txt[n] = (uint8_t)entry; // each string carries its own length (RFC 6763 sec 6.1)
    n++;
    proto_raw_read(txt + n, key, kl);
    n += kl;
    txt[n] = '=';
    n++;
    proto_raw_read(txt + n, value, vl);
    n += vl;
    s_mdns.txt_len = n;
    return PROTO_TRUE;
}

proto_bool pc_mdns_begin(const char *hostname, uint16_t http_port)
{
    if (hostname == NULL || hostname[0] == '\0' || !mdns_mem_bind())
    {
        return PROTO_FALSE;
    }
    if (!label_set(mdns_str(s_mdns.host), s_mdns.host.cap, hostname))
    {
        return PROTO_FALSE;
    }
    if (!name_of(mdns_str(s_mdns.fqdn), s_mdns.fqdn.cap, mdns_str(s_mdns.host), NULL, NULL))
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < PC_MDNS_MAX_SERVICES; i++)
    {
        mdns_svc(i)->used = PROTO_FALSE;
    }
    s_mdns.txt_len = 0;
    if (!pc_mdns_add_service("_http", "_tcp", http_port))
    {
        return PROTO_FALSE;
    }
    s_mdns.running = Udp.listener->listen_multicast(PC_MDNS_GROUP, PC_MDNS_PORT, mdns_udp_handler, NULL);
    return s_mdns.running;
}

#endif // PC_HAS_VENDOR_MDNS

#endif // PC_ENABLE_MDNS
