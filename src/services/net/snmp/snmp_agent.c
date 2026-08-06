// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_snmp_agent.c
 * @brief SNMP v1/v2c agent: MIB table, PDU dispatch, and the UDP binding.
 */

#include "services/net/snmp/snmp_agent.h"

#if PC_ENABLE_SNMP

#if PC_ENABLE_SNMP_V3
#include "services/net/snmp/snmp_v3.h"
#endif

#include "network_drivers/transport/udp.h"
#if PROTOCORE_HOT
#include "server/clock/clock.h" // pc_millis() - the library's clock seam (ban 5: never bare millis)
static uint32_t pc_snmp_uptime_cs()
{
    return (uint32_t)(pc_millis() / 10ULL); // hundredths of a second since boot
}
#else
static uint32_t pc_snmp_uptime_cs()
{
    return 0; // no clock in this build; tests assert type, not value
}
#endif

// ---------------------------------------------------------------------------
// MIB table + community configuration (all in BSS - no heap)
// ---------------------------------------------------------------------------

typedef struct
{
    uint32_t oid[SNMP_MAX_OID_LEN];
    size_t oid_len;
    SnmpValue val;    // static value (used when getter == nullptr)
    SnmpGetFn getter; // dynamic value provider (optional)
    SnmpSetFn setter; // writable hook (optional)
} SnmpMibEntry;

// All persistent SNMP-agent config, owned by one instance (internal linkage): the MIB table
// and its count plus the read-only / read-write community strings, grouped so it is one named
// owner, unreachable from any other translation unit. The MIB lookups take a pointer to const
// so processing an attacker's PDU provably cannot mutate the MIB.
typedef struct
{
    SnmpMibEntry mib[SNMP_MAX_MIB_ENTRIES];
    size_t mib_count;
    char ro[SNMP_COMMUNITY_MAX];
    char rw[SNMP_COMMUNITY_MAX];
    proto_bool rw_set;
} SnmpAgentCtx;

// The read-only community is the one field that does not start at zero; static storage zeroes the
// MIB, its count, the read-write community and rw_set.
static SnmpAgentCtx s_agent = {.ro = PC_SNMP_DEFAULT_RO_COMMUNITY};

// community_eq()'s stored[0] != '\0' guard can only be a no-op if the read-only community is
// never empty. It is seeded from PC_SNMP_DEFAULT_RO_COMMUNITY (an overridable macro) and
// pc_snmp_agent_init() falls back to that same macro for a null/empty argument, so pin the
// macro non-empty here rather than let an override silently make an empty community matchable.
_Static_assert(sizeof(PC_SNMP_DEFAULT_RO_COMMUNITY) > 1,
               "PC_SNMP_DEFAULT_RO_COMMUNITY must be a non-empty string literal");

// sysObjectID value (private enterprise placeholder: 1.3.6.1.4.1.49374).
static const uint32_t g_sys_object_id[] = {1, 3, 6, 1, 4, 1, 49374};

// ---------------------------------------------------------------------------
// OID helpers
// ---------------------------------------------------------------------------

static int oid_cmp(const uint32_t *a, size_t an, const uint32_t *b, size_t bn)
{
    size_t n = an < bn ? an : bn;
    for (size_t i = 0; i < n; i++)
    {
        if (a[i] < b[i])
        {
            return -1;
        }
        if (a[i] > b[i])
        {
            return 1;
        }
    }
    if (an < bn)
    {
        return -1;
    }
    if (an > bn)
    {
        return 1;
    }
    return 0;
}

static const SnmpMibEntry *mib_find_exact(const SnmpAgentCtx *c, const uint32_t *oid, size_t n)
{
    for (size_t i = 0; i < c->mib_count; i++)
    {
        if (oid_cmp(c->mib[i].oid, c->mib[i].oid_len, oid, n) == 0)
        {
            return &c->mib[i];
        }
    }
    return NULL;
}

// Smallest registered OID that is strictly greater than (oid,n), or nullptr.
static const SnmpMibEntry *mib_find_next(const SnmpAgentCtx *c, const uint32_t *oid, size_t n)
{
    const SnmpMibEntry *best = NULL;
    for (size_t i = 0; i < c->mib_count; i++)
    {
        if (oid_cmp(c->mib[i].oid, c->mib[i].oid_len, oid, n) > 0)
        {
            if (!best || oid_cmp(c->mib[i].oid, c->mib[i].oid_len, best->oid, best->oid_len) < 0)
            {
                best = &c->mib[i];
            }
        }
    }
    return best;
}

static proto_bool fetch_value(const SnmpMibEntry *en, SnmpValue *out)
{
    if (en->getter)
    {
        return en->getter(out);
    }
    *out = en->val;
    return PROTO_TRUE;
}

// RFC 3416 4.2.1: a GetRequest for an unbound name reports noSuchInstance when the
// name's OBJECT IDENTIFIER prefix matches a known object (only the instance is
// absent), and noSuchObject when no such object exists at all. Every registered
// entry is a full instance OID, so its object-type prefix is its OID minus the
// final (instance) arc; the request names a known object iff that prefix is a
// prefix of the request.
static proto_bool mib_object_exists(const SnmpAgentCtx *c, const uint32_t *oid, size_t n)
{
    for (size_t i = 0; i < c->mib_count; i++)
    {
        size_t objn = c->mib[i].oid_len - 1; // drop the trailing instance arc
        if (objn <= n && oid_cmp(c->mib[i].oid, objn, oid, objn) == 0)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

static SnmpMibEntry *mib_alloc(SnmpAgentCtx *c, const uint32_t *oid, size_t n)
{
    if (c->mib_count >= SNMP_MAX_MIB_ENTRIES || n < 2 || n > SNMP_MAX_OID_LEN)
    {
        return NULL;
    }
    SnmpMibEntry *e = &c->mib[c->mib_count++];
    memset(e, 0, sizeof(*e));
    for (size_t i = 0; i < n; i++)
    {
        e->oid[i] = oid[i];
    }
    e->oid_len = n;
    return e;
}

void pc_snmp_agent_init(const char *ro_community)
{
    s_agent.mib_count = 0;
    s_agent.rw_set = PROTO_FALSE;
    s_agent.rw[0] = '\0';
    const char *ro = (ro_community && ro_community[0]) ? ro_community : PC_SNMP_DEFAULT_RO_COMMUNITY;
    strncpy(s_agent.ro, ro, sizeof(s_agent.ro) - 1);
    s_agent.ro[sizeof(s_agent.ro) - 1] = '\0';
}

void pc_snmp_agent_set_rw_community(const char *rw_community)
{
    if (!rw_community || !rw_community[0])
    {
        s_agent.rw_set = PROTO_FALSE;
        s_agent.rw[0] = '\0';
        return;
    }
    strncpy(s_agent.rw, rw_community, sizeof(s_agent.rw) - 1);
    s_agent.rw[sizeof(s_agent.rw) - 1] = '\0';
    s_agent.rw_set = PROTO_TRUE;
}

proto_bool pc_snmp_agent_add_string(const uint32_t *oid, size_t oid_len, const char *value, SnmpSetFn setter)
{
    SnmpMibEntry *e = mib_alloc(&s_agent, oid, oid_len);
    if (!e)
    {
        return PROTO_FALSE;
    }
    e->val.type = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    e->val.str = value;
    e->val.str_len = value ? strnlen(value, SNMP_MSG_BUF_SIZE) : 0;
    e->setter = setter;
    return PROTO_TRUE;
}

proto_bool pc_snmp_agent_add_integer(const uint32_t *oid, size_t oid_len, long value, SnmpSetFn setter)
{
    SnmpMibEntry *e = mib_alloc(&s_agent, oid, oid_len);
    if (!e)
    {
        return PROTO_FALSE;
    }
    e->val.type = (uint8_t)SNMP_TAG_BER_INTEGER;
    e->val.ival = value;
    e->setter = setter;
    return PROTO_TRUE;
}

proto_bool pc_snmp_agent_add_dynamic(const uint32_t *oid, size_t oid_len, uint8_t type, SnmpGetFn getter)
{
    SnmpMibEntry *e = mib_alloc(&s_agent, oid, oid_len);
    if (!e)
    {
        return PROTO_FALSE;
    }
    e->val.type = type;
    e->getter = getter;
    return PROTO_TRUE;
}

static proto_bool sys_uptime_get(SnmpValue *out)
{
    out->type = (uint8_t)SNMP_TAG_SNMP_TIMETICKS;
    out->uval = pc_snmp_uptime_cs();
    return PROTO_TRUE;
}

void pc_snmp_agent_set_system(const char *descr, const char *contact, const char *name, const char *location,
                              long services)
{
    static const uint32_t o_descr[] = {1, 3, 6, 1, 2, 1, 1, 1, 0};
    static const uint32_t o_oid[] = {1, 3, 6, 1, 2, 1, 1, 2, 0};
    static const uint32_t o_uptime[] = {1, 3, 6, 1, 2, 1, 1, 3, 0};
    static const uint32_t o_contact[] = {1, 3, 6, 1, 2, 1, 1, 4, 0};
    static const uint32_t o_name[] = {1, 3, 6, 1, 2, 1, 1, 5, 0};
    static const uint32_t o_loc[] = {1, 3, 6, 1, 2, 1, 1, 6, 0};
    static const uint32_t o_svc[] = {1, 3, 6, 1, 2, 1, 1, 7, 0};

    pc_snmp_agent_add_string(o_descr, 9, descr, NULL);

    SnmpMibEntry *e = mib_alloc(&s_agent, o_oid, 9);
    if (e)
    {
        e->val.type = (uint8_t)SNMP_TAG_BER_OID;
        e->val.oid = g_sys_object_id;
        e->val.oid_len = sizeof(g_sys_object_id) / sizeof(g_sys_object_id[0]);
    }

    pc_snmp_agent_add_dynamic(o_uptime, 9, (uint8_t)SNMP_TAG_SNMP_TIMETICKS, sys_uptime_get);
    pc_snmp_agent_add_string(o_contact, 9, contact, NULL);
    pc_snmp_agent_add_string(o_name, 9, name, NULL);
    pc_snmp_agent_add_string(o_loc, 9, location, NULL);
    pc_snmp_agent_add_integer(o_svc, 9, services, NULL);
}

// ---------------------------------------------------------------------------
// Value encode / decode
// ---------------------------------------------------------------------------

static void enc_value(BerEnc *e, const SnmpValue *v)
{
    switch (v->type)
    {
    case (uint8_t)SNMP_TAG_BER_INTEGER:
        pc_ber_put_integer(e, v->ival);
        break;
    case (uint8_t)SNMP_TAG_BER_OCTET_STRING:
    case (uint8_t)SNMP_TAG_SNMP_OPAQUE:
        pc_ber_put_octet_string(e, v->type, (const uint8_t *)v->str, v->str_len);
        break;
    case (uint8_t)SNMP_TAG_BER_OID:
        pc_ber_put_oid(e, v->oid, v->oid_len);
        break;
    case (uint8_t)SNMP_TAG_SNMP_TIMETICKS:
    case (uint8_t)SNMP_TAG_SNMP_COUNTER32:
    case (uint8_t)SNMP_TAG_SNMP_GAUGE32:
        pc_ber_put_uint(e, v->type, v->uval);
        break;
    case (uint8_t)SNMP_TAG_SNMP_IPADDRESS: {
        uint8_t ip[4] = {(uint8_t)(v->uval >> 24), (uint8_t)(v->uval >> 16), (uint8_t)(v->uval >> 8),
                         (uint8_t)(v->uval)};
        pc_ber_put_octet_string(e, (uint8_t)SNMP_TAG_SNMP_IPADDRESS, ip, 4);
        break;
    }
    case (uint8_t)SNMP_TAG_BER_NULL:
        pc_ber_put_null(e);
        break;
    default:
        // Exception markers (noSuchObject / noSuchInstance / endOfMibView): the
        // tag with a zero-length, no-value encoding.
        pc_ber_put_tlv(e, v->type, NULL, 0);
        break;
    }
}

// Read one varbind value TLV at the decoder cursor into @p v (OID values decode
// into @p oidbuf). Advances the cursor past the value.
static proto_bool dec_value(BerDec *d, SnmpValue *v, uint32_t *oidbuf)
{
    memset(v, 0, sizeof(*v));
    size_t save = d->pos;
    uint8_t tag;
    size_t len;
    if (!pc_ber_read_header(d, &tag, &len))
    {
        return PROTO_FALSE;
    }
    v->type = tag;
    switch (tag)
    {
    case (uint8_t)SNMP_TAG_BER_INTEGER:
        d->pos = save;
        return pc_ber_read_integer(d, &v->ival);
    case (uint8_t)SNMP_TAG_SNMP_TIMETICKS:
    case (uint8_t)SNMP_TAG_SNMP_COUNTER32:
    case (uint8_t)SNMP_TAG_SNMP_GAUGE32:
    case (uint8_t)SNMP_TAG_SNMP_IPADDRESS: {
        uint32_t acc = 0;
        for (size_t i = 0; i < len; i++)
        {
            acc = (acc << 8) | d->buf[d->pos + i];
        }
        v->uval = acc;
        d->pos += len;
        return PROTO_TRUE;
    }
    case (uint8_t)SNMP_TAG_BER_OCTET_STRING:
    case (uint8_t)SNMP_TAG_SNMP_OPAQUE:
        v->str = (const char *)(d->buf + d->pos);
        v->str_len = len;
        d->pos += len;
        return PROTO_TRUE;
    case (uint8_t)SNMP_TAG_BER_OID:
        d->pos = save;
        if (!pc_ber_read_oid(d, oidbuf, SNMP_MAX_OID_LEN, &v->oid_len))
        {
            return PROTO_FALSE;
        }
        v->oid = oidbuf;
        return PROTO_TRUE;
    default: // NULL (GET requests) and anything else: skip the value bytes
        d->pos += len;
        return PROTO_TRUE;
    }
}

// ---------------------------------------------------------------------------
// Per-request scratch (single-threaded: lwIP callback or test, never reentrant)
// ---------------------------------------------------------------------------

typedef struct
{
    uint32_t oid[SNMP_MAX_OID_LEN];
    size_t oid_len;
    SnmpValue val;
    uint32_t valoid[SNMP_MAX_OID_LEN]; // backing store if the value is an OID
} InVb;

typedef struct
{
    const uint32_t *oid;
    size_t oid_len;
    SnmpValue val;
} OutVb;

// All per-request decode/encode scratch, owned by one instance (internal linkage): the input
// and output varbind arrays. Single-threaded (the lwIP callback or a test, never reentrant),
// so it is one named owner, unreachable from any other translation unit.
typedef struct
{
    InVb in[SNMP_MAX_VARBINDS];
    OutVb out[SNMP_MAX_VARBINDS];
} SnmpReqCtx;
static SnmpReqCtx s_req;

// Run the SET varbind loop: apply each varbind, stopping at the first error. On failure writes the
// SnmpErr and the 1-based varbind index into the err_status and err_index outputs (left unchanged if
// all succeed; v2c selects the v2c error variant). Extracted so the per-varbind guards are not nested
// 4 deep (S134).
static void pc_snmp_apply_set_all(size_t nvb, proto_bool v2c, long *err_status, long *err_index)
{
    for (size_t i = 0; i < nvb; i++)
    {
        const SnmpMibEntry *en = mib_find_exact(&s_agent, s_req.in[i].oid, s_req.in[i].oid_len);
        int e = 0;
        if (!en)
        {
            e = (int)SNMP_ERR_NO_SUCH_NAME;
        }
        else if (!en->setter)
        {
            e = (!v2c) ? (int)SNMP_ERR_READ_ONLY : (int)SNMP_ERR_NOT_WRITABLE;
        }
        else if (!en->setter(&s_req.in[i].val))
        {
            e = (!v2c) ? (int)SNMP_ERR_BAD_VALUE : (int)SNMP_ERR_WRONG_TYPE;
        }
        if (!e)
        {
            continue;
        }
        *err_status = e;
        *err_index = (long)(i + 1);
        return;
    }
}

static proto_bool community_eq(const char *stored, const char *p, size_t len)
{
    // The two call sites pass s_agent.ro (always non-empty: pc_snmp_agent_init() falls back to the
    // static_assert'd non-empty default) and s_agent.rw (only reached when rw_set, which
    // pc_snmp_agent_set_rw_community() sets only for a non-empty string), so stored is never empty.
    return stored[0] != '\0' && strnlen(stored, len + 1) == len && memcmp(stored, p, len) == 0;
}

static size_t encode_pdu(long request_id, long err_status, long err_index, const OutVb *out, size_t nout, uint8_t *buf,
                         size_t cap)
{
    BerEnc e;
    pc_ber_enc_init(&e, buf, cap);
    size_t pdu = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_SNMP_PDU_RESPONSE);
    pc_ber_put_integer(&e, request_id);
    pc_ber_put_integer(&e, err_status);
    pc_ber_put_integer(&e, err_index);
    size_t vbl = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    for (size_t i = 0; i < nout; i++)
    {
        size_t vb = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
        pc_ber_put_oid(&e, out[i].oid, out[i].oid_len);
        enc_value(&e, &out[i].val);
        pc_ber_seq_end(&e, vb);
    }
    pc_ber_seq_end(&e, vbl);
    pc_ber_seq_end(&e, pdu);
    return e.ok ? e.len : 0;
}

// Process one request PDU (a Get/GetNext/GetBulk/Set TLV) against the MIB and
// emit one GetResponse PDU. Shared by the v1/v2c community framing and the v3
// USM layer. @p v2c selects v2c-style per-varbind exceptions over v1
// error-status; @p allow_write authorizes Set.
size_t pc_snmp_dispatch_pdu(const uint8_t *pdu, size_t pdu_len, proto_bool allow_write, proto_bool v2c, uint8_t *out,
                            size_t out_cap)
{
    BerDec d;
    pc_ber_dec_init(&d, pdu, pdu_len);

    uint8_t pdu_tag;
    size_t pdu_clen;
    if (!pc_ber_read_header(&d, &pdu_tag, &pdu_clen))
    {
        return 0;
    }

    long request_id;
    long field2;
    long field3;
    if (!pc_ber_read_integer(&d, &request_id) || !pc_ber_read_integer(&d, &field2) || !pc_ber_read_integer(&d, &field3))
    {
        return 0;
    }

    uint8_t vbl_tag;
    size_t vbl_len;
    if (!pc_ber_read_header(&d, &vbl_tag, &vbl_len) || vbl_tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return 0;
    }
    size_t vbl_end = d.pos + vbl_len;

    // Decode the request varbind list.
    size_t nvb = 0;
    // Every read inside the loop that could clear d.ok returns 0 through its own guard first, so the
    // decoder is always ok on re-entry - same defense-in-depth rationale as the !d.ok check below.
    while (d.pos < vbl_end && d.ok)
    {
        if (nvb >= SNMP_MAX_VARBINDS)
        {
            return 0;
        }
        uint8_t vt;
        size_t vlen;
        if (!pc_ber_read_header(&d, &vt, &vlen) || vt != (uint8_t)SNMP_TAG_BER_SEQUENCE)
        {
            return 0;
        }
        if (!pc_ber_read_oid(&d, s_req.in[nvb].oid, SNMP_MAX_OID_LEN, &s_req.in[nvb].oid_len))
        {
            return 0;
        }
        if (!dec_value(&d, &s_req.in[nvb].val, s_req.in[nvb].valoid))
        {
            return 0;
        }
        nvb++;
    }
    if (!d.ok)
    {
        return 0;
    }

    long err_status = (int)SNMP_ERR_NO_ERROR;
    long err_index = 0;
    size_t nout = 0;

    if (pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_GET || pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_GETNEXT)
    {
        for (size_t i = 0; i < nvb; i++)
        {
            const SnmpMibEntry *en = (pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_GET)
                                         ? mib_find_exact(&s_agent, s_req.in[i].oid, s_req.in[i].oid_len)
                                         : mib_find_next(&s_agent, s_req.in[i].oid, s_req.in[i].oid_len);
            SnmpValue val;
            proto_bool ok = en && fetch_value(en, &val);
            if (!ok)
            {
                if (!v2c)
                {
                    // Classic error reporting: echo the request varbinds.
                    err_status = (int)SNMP_ERR_NO_SUCH_NAME;
                    err_index = (long)(i + 1);
                    nout = nvb;
                    for (size_t k = 0; k < nvb; k++)
                    {
                        s_req.out[k].oid = s_req.in[k].oid;
                        s_req.out[k].oid_len = s_req.in[k].oid_len;
                        s_req.out[k].val = s_req.in[k].val;
                    }
                    break;
                }
                // v2c: per-varbind exception. For Get, distinguish a missing
                // instance of a known object (en found but its getter declined, or
                // the object's type-prefix is registered) from an unknown object;
                // GetNext past the end of the MIB is always endOfMibView.
                if (pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_GET)
                {
                    proto_bool inst = en || mib_object_exists(&s_agent, s_req.in[i].oid, s_req.in[i].oid_len);
                    val.type = inst ? (uint8_t)SNMP_TAG_SNMP_NO_SUCH_INSTANCE : (uint8_t)SNMP_TAG_SNMP_NO_SUCH_OBJECT;
                }
                else
                {
                    val.type = (uint8_t)SNMP_TAG_SNMP_END_OF_MIB_VIEW;
                }
                s_req.out[nout].oid = en ? en->oid : s_req.in[i].oid;
                s_req.out[nout].oid_len = en ? en->oid_len : s_req.in[i].oid_len;
                s_req.out[nout].val = val;
                nout++;
                continue;
            }
            s_req.out[nout].oid = (pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_GETNEXT) ? en->oid : s_req.in[i].oid;
            s_req.out[nout].oid_len =
                (pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_GETNEXT) ? en->oid_len : s_req.in[i].oid_len;
            s_req.out[nout].val = val;
            nout++;
        }
    }
    else if (pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_GETBULK)
    {
        if (!v2c) // GetBulk is v2c+
        {
            return 0;
        }
        long non_rep = field2 < 0 ? 0 : field2;
        long max_rep = field3 < 0 ? 0 : field3;
        if ((size_t)non_rep > nvb)
        {
            non_rep = (long)nvb;
        }

        // Non-repeaters: one GetNext each. nout is 0 on entry and rises in step with i, while
        // non_rep was clamped to nvb (itself capped at SNMP_MAX_VARBINDS by the decode loop), so
        // i < non_rep always fails first - the nout cap here is defense in depth, unlike the
        // repeater loops below where several varbinds are appended per repetition.
        for (long i = 0; i < non_rep; i++)
        {
            if (nout >= SNMP_MAX_VARBINDS)
            {
                break;
            }
            const SnmpMibEntry *en = mib_find_next(&s_agent, s_req.in[i].oid, s_req.in[i].oid_len);
            if (en)
            {
                s_req.out[nout].oid = en->oid;
                s_req.out[nout].oid_len = en->oid_len;
                fetch_value(en, &s_req.out[nout].val);
            }
            else
            {
                s_req.out[nout].oid = s_req.in[i].oid;
                s_req.out[nout].oid_len = s_req.in[i].oid_len;
                s_req.out[nout].val.type = (uint8_t)SNMP_TAG_SNMP_END_OF_MIB_VIEW;
            }
            nout++;
        }

        // Repeaters: walk each remaining varbind up to max_rep successors.
        size_t nrep = nvb - (size_t)non_rep;
        const uint32_t *cur_oid[SNMP_MAX_VARBINDS];
        size_t cur_len[SNMP_MAX_VARBINDS];
        proto_bool ended[SNMP_MAX_VARBINDS];
        for (size_t r = 0; r < nrep; r++)
        {
            cur_oid[r] = s_req.in[non_rep + r].oid;
            cur_len[r] = s_req.in[non_rep + r].oid_len;
            ended[r] = PROTO_FALSE;
        }
        for (long rep = 0; rep < max_rep && nout < SNMP_MAX_VARBINDS; rep++)
        {
            for (size_t r = 0; r < nrep && nout < SNMP_MAX_VARBINDS; r++)
            {
                if (ended[r])
                {
                    s_req.out[nout].oid = cur_oid[r];
                    s_req.out[nout].oid_len = cur_len[r];
                    s_req.out[nout].val.type = (uint8_t)SNMP_TAG_SNMP_END_OF_MIB_VIEW;
                    nout++;
                    continue;
                }
                const SnmpMibEntry *en = mib_find_next(&s_agent, cur_oid[r], cur_len[r]);
                if (en)
                {
                    s_req.out[nout].oid = en->oid;
                    s_req.out[nout].oid_len = en->oid_len;
                    fetch_value(en, &s_req.out[nout].val);
                    cur_oid[r] = en->oid;
                    cur_len[r] = en->oid_len;
                }
                else
                {
                    s_req.out[nout].oid = cur_oid[r];
                    s_req.out[nout].oid_len = cur_len[r];
                    s_req.out[nout].val.type = (uint8_t)SNMP_TAG_SNMP_END_OF_MIB_VIEW;
                    ended[r] = PROTO_TRUE;
                }
                nout++;
            }
        }
    }
    else if (pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_SET)
    {
        // SET uses error-status/error-index in both v1 and v2c; echo the varbinds.
        nout = nvb;
        for (size_t k = 0; k < nvb; k++)
        {
            s_req.out[k].oid = s_req.in[k].oid;
            s_req.out[k].oid_len = s_req.in[k].oid_len;
            s_req.out[k].val = s_req.in[k].val;
        }
        if (!allow_write)
        {
            err_status = (!v2c) ? (int)SNMP_ERR_NO_SUCH_NAME : (int)SNMP_ERR_NO_ACCESS;
            err_index = 1;
        }
        else
        {
            pc_snmp_apply_set_all(nvb, v2c, &err_status, &err_index);
        }
    }
    else
    {
        return 0; // unsupported PDU (Trap, Response, etc.)
    }

    size_t n = encode_pdu(request_id, err_status, err_index, s_req.out, nout, out, out_cap);
    if (n == 0 && nout > 0)
    {
        // Response didn't fit: report tooBig with an empty varbind list.
        n = encode_pdu(request_id, (int)SNMP_ERR_TOO_BIG, 0, s_req.out, 0, out, out_cap);
    }
    return n;
}

// ---------------------------------------------------------------------------
// Message wrapper: v1/v2c community framing (v3 delegates to the USM layer)
// ---------------------------------------------------------------------------

size_t pc_snmp_agent_process(const uint8_t *req, size_t req_len, uint8_t *resp, size_t pc_resp_cap)
{
    BerDec d;
    pc_ber_dec_init(&d, req, req_len);

    uint8_t tag;
    size_t len;
    if (!pc_ber_read_header(&d, &tag, &len) || tag != (uint8_t)SNMP_TAG_BER_SEQUENCE) // message wrapper
    {
        return 0;
    }

    long version;
    if (!pc_ber_read_integer(&d, &version))
    {
        return 0;
    }

    if (version == (int)SNMP_V3)
    {
#if PC_ENABLE_SNMP_V3
        return pc_snmp_v3_process(req, req_len, resp, pc_resp_cap);
#else
        return 0; // v3 needs the gated USM layer (PC_ENABLE_SNMP_V3)
#endif
    }
    if (version != (int)SNMP_V1 && version != (int)SNMP_V2C)
    {
        return 0;
    }

    uint8_t ctag;
    size_t clen;
    if (!pc_ber_read_header(&d, &ctag, &clen) || ctag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return 0;
    }
    const char *community = (const char *)(d.buf + d.pos);
    size_t community_len = clen;
    d.pos += clen;

    proto_bool is_rw = s_agent.rw_set && community_eq(s_agent.rw, community, community_len);
    proto_bool is_ro = is_rw || community_eq(s_agent.ro, community, community_len);
    if (!is_ro) // unknown community: silently drop, like a standard agent
    {
        return 0;
    }

    // The PDU is the remaining bytes of the datagram; dispatch and re-wrap.
    static uint8_t pdubuf[SNMP_MSG_BUF_SIZE];
    size_t pn =
        pc_snmp_dispatch_pdu(req + d.pos, req_len - d.pos, is_rw, version == (int)SNMP_V2C, pdubuf, sizeof(pdubuf));
    if (pn == 0)
    {
        return 0;
    }

    BerEnc e;
    pc_ber_enc_init(&e, resp, pc_resp_cap);
    size_t msg = pc_ber_seq_begin(&e, (uint8_t)SNMP_TAG_BER_SEQUENCE);
    pc_ber_put_integer(&e, version);
    pc_ber_put_octet_string(&e, (uint8_t)SNMP_TAG_BER_OCTET_STRING, (const uint8_t *)community, community_len);
    pc_ber_put_raw(&e, pdubuf, pn);
    pc_ber_seq_end(&e, msg);
    return e.ok ? e.len : 0;
}

// ---------------------------------------------------------------------------
// UDP binding (via the transport-layer UDP service - no lwIP here)
// ---------------------------------------------------------------------------

// All SNMP UDP-binding state, owned by one instance (internal linkage): the response scratch
// (the request is transport-owned), so it is one named owner, unreachable cross-TU.
typedef struct
{
    uint8_t tx[SNMP_MSG_BUF_SIZE];
} SnmpUdpCtx;
static SnmpUdpCtx s_snmp_udp;

static void pc_snmp_udp_handler(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    (void)ctx;
    size_t rn = pc_snmp_agent_process(data, len, s_snmp_udp.tx, sizeof(s_snmp_udp.tx));
    if (rn)
    {
        Udp.listener->reply(peer, s_snmp_udp.tx, rn);
    }
}

void pc_snmp_agent_begin_udp(uint16_t port)
{
    Udp.listener->listen(port, pc_snmp_udp_handler, NULL);
}

#endif // PC_ENABLE_SNMP
