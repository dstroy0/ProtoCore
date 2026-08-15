// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mdns_service.c
 * @brief mDNS / DNS-SD advertisement implementation (PROTOCORE_ENABLE_MDNS).
 *
 * Two backends behind one API, picked by PROTOCORE_HAS_VENDOR_MDNS: the vendor's own responder where the
 * SDK ships one, and otherwise the portable responder below, which answers over the UDP listener
 * like every other datagram service in the tree.
 */

#include "mdns_service.h"

#if PROTOCORE_ENABLE_MDNS

// Both backends' includes, ahead of either backend's code: what a translation unit reaches for is
// stated once at its top, whichever arm the capability selects.
#if PROTOCORE_HAS_VENDOR_MDNS
#include "mdns.h" // the vendor's responder, driven through its own component API
#else
#include "mmgr/protostr.h"                               // str: the bounded-run walks
#include "mmgr/rawmemcpy.h"                              // raw.read: every field moves whole
#include "mmgr/secure.h"                                 // protocore_secure_persist_span: this module's storage
#include "network_drivers/network/dns/dns_wire.h"        // the name codec both DNS halves share
#include "network_drivers/physical/physical.h"           // Physical.egress_ip: the address the A record carries
#include "network_drivers/transport/udp/server/server.h" // UdpListener: the 5353 group bind and the reply
#endif

#if PROTOCORE_HAS_VENDOR_MDNS

proto_bool protocore_mdns_begin(const char *hostname, uint16_t http_port)
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

proto_bool protocore_mdns_txt(const char *key, const char *value)
{
    if (key == NULL || value == NULL)
    {
        return PROTO_FALSE;
    }
    // Attach a TXT key/value to the _http._tcp service (Bonjour browsers show it).
    return mdns_service_txt_item_set("_http", "_tcp", key, value) == ESP_OK;
}

proto_bool protocore_mdns_add_service(const char *service_type, const char *proto, uint16_t port)
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
#define PROTOCORE_MDNS_GROUP "224.0.0.251"
#define PROTOCORE_MDNS_PORT 5353u

// The record types DNS-SD is built from (RFC 1035 sec 3.2.2, RFC 2782), and the QTYPE that asks for
// all of them.
#define PROTOCORE_MDNS_T_A 1u
#define PROTOCORE_MDNS_T_PTR 12u
#define PROTOCORE_MDNS_T_TXT 16u
#define PROTOCORE_MDNS_T_SRV 33u
#define PROTOCORE_MDNS_T_ANY 255u

// Class IN, and IN with the cache-flush bit a responder sets on a record it alone owns (RFC 6762
// sec 10.2). The shared PTRs never carry it; the host's A, SRV and TXT always do.
#define PROTOCORE_MDNS_C_IN 0x0001u
#define PROTOCORE_MDNS_C_FLUSH 0x8001u

// Seconds a resolver may cache each kind (RFC 6762 sec 10): a host record follows the address, a
// service record outlives it.
#define PROTOCORE_MDNS_TTL_HOST 120u
#define PROTOCORE_MDNS_TTL_SVC 4500u

/** @brief The name every DNS-SD browser walks to enumerate what a host offers (RFC 6763 sec 9). */
#define PROTOCORE_MDNS_ENUM_NAME "_services._dns-sd._udp.local"

/** @brief The parent of every name this responder owns. */
#define PROTOCORE_MDNS_DOMAIN "local"

/** @brief One advertised service: its DNS-SD type, its transport, and the port it answers on. */
typedef struct
{
    char type[PROTOCORE_MDNS_LABEL_MAX];  ///< "_http"
    char proto[PROTOCORE_MDNS_LABEL_MAX]; ///< "_tcp" / "_udp"
    uint16_t port;
    proto_bool used;
} MdnsSvc;

// All mDNS responder state, owned by one instance (internal linkage): the borrows the responder
// works in, how much TXT it holds, and whether it is bound. One named owner, unreachable cross-TU.
// The composition borrows are live only for the length of one handler call, and poll() runs the
// handler, so no two calls reach them at once.
typedef struct
{
    protocore_span host; ///< the label alone, no domain
    protocore_span fqdn; ///< "<host>.local", composed once by begin()
    protocore_span svc;  ///< the MdnsSvc table
    protocore_span txt;  ///< packed length-prefixed "key=value" strings
    size_t txt_len;
    protocore_span tx;        ///< the response being composed
    protocore_span rd;        ///< rdata staged for the record being written
    protocore_span qname;     ///< the question being answered
    protocore_span svc_name;  ///< "<type>.<proto>.local"
    protocore_span inst_name; ///< "<host>.<type>.<proto>.local"
    proto_bool running;
} MdnsCtx;
static MdnsCtx s_mdns;

// Take every borrow once and hold it for the life of the program. A responder answers for the names
// the device is reached by, so its table and the replies it composes come from the secure pool,
// whose release wipes. False when the pool cannot cover them; begin() fails closed on that.
static proto_bool mdns_mem_bind(void)
{
    if (span.has_storage(s_mdns.tx))
    {
        return PROTO_TRUE;
    }
    s_mdns.host = protocore_secure_persist_span(PROTOCORE_MDNS_LABEL_MAX);
    s_mdns.fqdn = protocore_secure_persist_span(PROTOCORE_DNS_NAME_MAX);
    s_mdns.svc = protocore_secure_persist_span(sizeof(MdnsSvc) * PROTOCORE_MDNS_MAX_SERVICES);
    s_mdns.txt = protocore_secure_persist_span(PROTOCORE_MDNS_TXT_MAX);
    s_mdns.tx = protocore_secure_persist_span(PROTOCORE_MDNS_TX_MAX);
    s_mdns.rd = protocore_secure_persist_span(PROTOCORE_DNS_NAME_MAX + 8);
    s_mdns.qname = protocore_secure_persist_span(PROTOCORE_DNS_NAME_MAX);
    s_mdns.svc_name = protocore_secure_persist_span(PROTOCORE_DNS_NAME_MAX);
    s_mdns.inst_name = protocore_secure_persist_span(PROTOCORE_DNS_NAME_MAX);
    return span.has_storage(s_mdns.host) && span.has_storage(s_mdns.fqdn) && span.has_storage(s_mdns.svc) &&
           span.has_storage(s_mdns.txt) && span.has_storage(s_mdns.tx) && span.has_storage(s_mdns.rd) &&
           span.has_storage(s_mdns.qname) && span.has_storage(s_mdns.svc_name) && span.has_storage(s_mdns.inst_name);
}

/** @brief Service @p i, over the borrow that holds the table. */
static MdnsSvc *mdns_svc(size_t i)
{
    return &((MdnsSvc *)s_mdns.svc.buf)[i];
}

/** @brief A borrowed span read as the dotted name it holds. */
static char *mdns_str(protocore_span s)
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
    size_t l = str.len(s, cap - n);
    if (n + l >= cap)
    {
        return cap;
    }
    raw.read(out + n, s, l);
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
    n = name_append(out, cap, n, PROTOCORE_MDNS_DOMAIN);
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
    DnsWire.text.dotted = owner;
    DnsWire.text.out = out + p;
    DnsWire.text.out_cap = cap - p;
    DnsWire.encode(DnsWire.internal);
    size_t w = DnsWire.n;
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
        raw.read(out + p, rdata, rdlen);
        p += rdlen;
    }
    *n = p;
    return PROTO_TRUE;
}

// The A record for this host, when the interface has an address to advertise. A responder with no
// address says nothing rather than claiming 0.0.0.0.
static uint16_t put_a(size_t *n)
{
    Physical.egress_ip(Physical.internal);
    uint32_t ip = Physical.u32;
    if (ip == 0)
    {
        return 0;
    }
    raw.read(s_mdns.rd.buf, &ip, 4); // network order already, as the stack keeps it
    if (!rr_put(s_mdns.tx.buf, s_mdns.tx.cap, n, mdns_str(s_mdns.fqdn), PROTOCORE_MDNS_T_A, PROTOCORE_MDNS_C_FLUSH,
                PROTOCORE_MDNS_TTL_HOST, s_mdns.rd.buf, 4))
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
    DnsWire.text.dotted = mdns_str(s_mdns.fqdn);
    DnsWire.text.out = rd + 6;
    DnsWire.text.out_cap = s_mdns.rd.cap - 6;
    DnsWire.encode(DnsWire.internal);
    size_t w = DnsWire.n;
    if (w == 0)
    {
        return 0;
    }
    if (!rr_put(s_mdns.tx.buf, s_mdns.tx.cap, n, mdns_str(s_mdns.inst_name), PROTOCORE_MDNS_T_SRV,
                PROTOCORE_MDNS_C_FLUSH, PROTOCORE_MDNS_TTL_HOST, rd, 6 + w))
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
    if (!rr_put(s_mdns.tx.buf, s_mdns.tx.cap, n, mdns_str(s_mdns.inst_name), PROTOCORE_MDNS_T_TXT,
                PROTOCORE_MDNS_C_FLUSH, PROTOCORE_MDNS_TTL_SVC, rd, rdlen))
    {
        return 0;
    }
    return 1;
}

// A PTR whose rdata is one name.
static uint16_t put_ptr(const char *owner, const char *target, size_t *n)
{
    DnsWire.text.dotted = target;
    DnsWire.text.out = s_mdns.rd.buf;
    DnsWire.text.out_cap = s_mdns.rd.cap;
    DnsWire.encode(DnsWire.internal);
    size_t w = DnsWire.n;
    if (w == 0)
    {
        return 0;
    }
    if (!rr_put(s_mdns.tx.buf, s_mdns.tx.cap, n, owner, PROTOCORE_MDNS_T_PTR, PROTOCORE_MDNS_C_IN,
                PROTOCORE_MDNS_TTL_SVC, s_mdns.rd.buf, w))
    {
        return 0;
    }
    return 1;
}

// True when a question of @p qtype is asking for @p want.
static proto_bool wants(uint16_t qtype, uint16_t want)
{
    return qtype == want || qtype == PROTOCORE_MDNS_T_ANY;
}

// Append every record this responder owns that answers @p qname / @p qtype, and report how many.
static uint16_t answer_for(const char *qname, uint16_t qtype, size_t *n)
{
    uint16_t added = 0;

    DnsWire.cmp.a = qname;
    DnsWire.cmp.b = mdns_str(s_mdns.fqdn);
    DnsWire.eq(DnsWire.internal);
    if (DnsWire.ok && wants(qtype, PROTOCORE_MDNS_T_A))
    {
        added += put_a(n);
    }

    for (size_t i = 0; i < PROTOCORE_MDNS_MAX_SERVICES; i++)
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
        DnsWire.cmp.a = qname;
        DnsWire.cmp.b = PROTOCORE_MDNS_ENUM_NAME;
        DnsWire.eq(DnsWire.internal);
        if (DnsWire.ok && wants(qtype, PROTOCORE_MDNS_T_PTR))
        {
            added += put_ptr(PROTOCORE_MDNS_ENUM_NAME, mdns_str(s_mdns.svc_name), n);
        }
        DnsWire.cmp.a = qname;
        DnsWire.cmp.b = mdns_str(s_mdns.svc_name);
        DnsWire.eq(DnsWire.internal);
        if (DnsWire.ok && wants(qtype, PROTOCORE_MDNS_T_PTR))
        {
            added += put_ptr(mdns_str(s_mdns.svc_name), mdns_str(s_mdns.inst_name), n);
        }
        DnsWire.cmp.a = qname;
        DnsWire.cmp.b = mdns_str(s_mdns.inst_name);
        DnsWire.eq(DnsWire.internal);
        if (DnsWire.ok)
        {
            if (wants(qtype, PROTOCORE_MDNS_T_SRV))
            {
                added += put_srv(s, n);
            }
            if (wants(qtype, PROTOCORE_MDNS_T_TXT))
            {
                added += put_txt(n);
            }
        }
    }
    return added;
}

// Answer each question the query carries. A response goes back to the group rather than to the
// sender, so every resolver on the link updates its cache from it (RFC 6762 sec 6).
static void mdns_udp_handler(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *ctx)
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
        DnsWire.msg.pkt = data;
        DnsWire.msg.len = len;
        DnsWire.msg.off = off;
        DnsWire.msg.out = mdns_str(s_mdns.qname);
        DnsWire.msg.out_cap = s_mdns.qname.cap;
        DnsWire.msg.allow_ptr = PROTO_TRUE;
        DnsWire.decode(DnsWire.internal);
        if (!DnsWire.ok)
        {
            return; // a malformed question: the rest of the message cannot be located
        }
        off = DnsWire.next;
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

    protocore_ip group = {PROTOCORE_IP_NONE, {0}};
    Ip.args.text = PROTOCORE_MDNS_GROUP;
    Ip.args.out = &group;
    Ip.parse(Ip.internal);
    if (Ip.ok)
    {
        UdpListener.port = PROTOCORE_MDNS_PORT;
        UdpListener.send_args.dst = &group;
        UdpListener.send_args.dst_port = PROTOCORE_MDNS_PORT;
        UdpListener.send_args.data = tx;
        UdpListener.send_args.len = n;
        UdpListener.sendto(UdpListener.internal);
    }
}

// Copy @p src into @p dst, bounded by @p cap. False when it does not fit whole: a truncated service
// type would advertise a name nothing resolves.
static proto_bool label_set(char *dst, size_t cap, const char *src)
{
    size_t n = str.len(src, cap);
    if (n == 0 || n >= cap)
    {
        return PROTO_FALSE;
    }
    raw.read(dst, src, n);
    dst[n] = '\0';
    return PROTO_TRUE;
}

proto_bool protocore_mdns_add_service(const char *service_type, const char *proto, uint16_t port)
{
    if (service_type == NULL || proto == NULL || !mdns_mem_bind())
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < PROTOCORE_MDNS_MAX_SERVICES; i++)
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

proto_bool protocore_mdns_txt(const char *key, const char *value)
{
    if (key == NULL || value == NULL || !mdns_mem_bind())
    {
        return PROTO_FALSE;
    }
    uint8_t *txt = s_mdns.txt.buf;
    size_t kl = str.len(key, s_mdns.txt.cap);
    size_t vl = str.len(value, s_mdns.txt.cap);
    size_t entry = kl + 1 + vl; // "key=value"
    if (kl == 0 || entry > 255 || s_mdns.txt_len + 1 + entry > s_mdns.txt.cap)
    {
        return PROTO_FALSE;
    }
    size_t n = s_mdns.txt_len;
    txt[n] = (uint8_t)entry; // each string carries its own length (RFC 6763 sec 6.1)
    n++;
    raw.read(txt + n, key, kl);
    n += kl;
    txt[n] = '=';
    n++;
    raw.read(txt + n, value, vl);
    n += vl;
    s_mdns.txt_len = n;
    return PROTO_TRUE;
}

proto_bool protocore_mdns_begin(const char *hostname, uint16_t http_port)
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
    for (size_t i = 0; i < PROTOCORE_MDNS_MAX_SERVICES; i++)
    {
        mdns_svc(i)->used = PROTO_FALSE;
    }
    s_mdns.txt_len = 0;
    if (!protocore_mdns_add_service("_http", "_tcp", http_port))
    {
        return PROTO_FALSE;
    }
    UdpListener.port = PROTOCORE_MDNS_PORT;
    UdpListener.bind.handler = mdns_udp_handler;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.bind.group_ip = PROTOCORE_MDNS_GROUP;
    UdpListener.listen_multicast(UdpListener.internal);
    s_mdns.running = UdpListener.ok;
    return s_mdns.running;
}

#endif // PROTOCORE_HAS_VENDOR_MDNS

#endif // PROTOCORE_ENABLE_MDNS
