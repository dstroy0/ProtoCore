// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snmp_agent.c
 * @brief The command responder (RFC 1157, RFC 1901, RFC 3416) - implementation. See snmp_agent.h.
 */

#include "services/net/snmp/snmp_agent.h"
#include "mmgr/protomem.h" // mem.set / mem.cmp: the fixed-width compares and clears
#include "mmgr/protostr.h" // str.len / str.copy: the bounded community and string handling

#if PROTOCORE_ENABLE_SNMP

#if PROTOCORE_ENABLE_SNMP_V3
#include "services/net/snmp/snmp_v3.h"
#endif

#if PROTOCORE_HAS_NET_STACK
#include "network_drivers/transport/udp/server/server.h" // UdpListener: the port 161 bind and the reply
#include "server/clock/clock.h"                          // protocore_millis(): the library's clock seam
#endif

// sysObjectID.0 value: the private enterprise subtree this device answers under.
static const uint32_t g_sys_object_id[] = {1, 3, 6, 1, 4, 1, 49374};

// ---------------------------------------------------------------------------
// The MIB and the per-request scratch
// ---------------------------------------------------------------------------

/** @brief One registered object instance: its name, its value, and the two access paths. */
typedef struct
{
    uint32_t oid[SNMP_MAX_OID_LEN];
    size_t oid_len;
    SnmpValue val;    ///< the value held here, read when getter is NULL
    SnmpGetFn getter; ///< the value read through a callback instead
    SnmpSetFn setter; ///< the write path, NULL for a read-only object (RFC 2578 sec 7.3)
} SnmpMibEntry;

/** @brief One decoded request binding: its name, its value, and the OID that value may be. */
typedef struct
{
    uint32_t oid[SNMP_MAX_OID_LEN];
    size_t oid_len;
    SnmpValue val;
    uint32_t valoid[SNMP_MAX_OID_LEN]; ///< backing store when the value is an OBJECT IDENTIFIER
} InVb;

/** @brief One binding of the Response-PDU: a name that is referenced, and a value that is held. */
typedef struct
{
    const uint32_t *oid;
    size_t oid_len;
    SnmpValue val;
} OutVb;

/**
 * @brief The agent's compile-time storage: the MIB, the communities, and the request scratch.
 *
 * All of it BSS, so an object costs no heap and no message lands on a task stack. The scratch
 * serves one message at a time: the poll that drains the port runs the handler to completion, and
 * nothing re-enters it.
 *
 * @var SnmpAgentStorage::mib        the registered object instances
 * @var SnmpAgentStorage::mib_count  how many are registered
 * @var SnmpAgentStorage::ro         the community that authorizes reads (RFC 1157 sec 3.2.5)
 * @var SnmpAgentStorage::rw         the community that authorizes a SetRequest-PDU
 * @var SnmpAgentStorage::rw_set     a read-write community was configured
 * @var SnmpAgentStorage::in         the decoded request bindings
 * @var SnmpAgentStorage::out        the bindings the Response-PDU carries
 * @var SnmpAgentStorage::cur_oid    the walk cursor of each GetBulkRequest-PDU repeater
 * @var SnmpAgentStorage::cur_len    its subidentifier count
 * @var SnmpAgentStorage::ended      that repeater reached the end of the MIB view
 * @var SnmpAgentStorage::pdu_stage  one encoded Response-PDU, before the message wraps it
 * @var SnmpAgentStorage::tx         one response message, for the length of one handler call
 */
struct SnmpAgentStorage
{
    SnmpMibEntry mib[SNMP_MAX_MIB_ENTRIES];
    size_t mib_count;
    char ro[SNMP_COMMUNITY_MAX];
    char rw[SNMP_COMMUNITY_MAX];
    proto_bool rw_set;

    InVb in[SNMP_MAX_VARBINDS];
    OutVb out[SNMP_MAX_VARBINDS];
    const uint32_t *cur_oid[SNMP_MAX_VARBINDS];
    size_t cur_len[SNMP_MAX_VARBINDS];
    proto_bool ended[SNMP_MAX_VARBINDS];

    uint8_t pdu_stage[SNMP_MSG_BUF_SIZE];
    uint8_t tx[SNMP_MSG_BUF_SIZE];
};

/**
 * @brief The MIB and the calls that reach it - what SnmpAgentNs points at.
 *
 * @var SnmpAgentInternal::store  the MIB, the communities, and the request scratch
 * @var SnmpAgentInternal::ns     the handle a caller sets a call's members on
 */
struct SnmpAgentInternal
{
    struct SnmpAgentStorage *store;
    SnmpAgentNs *ns;
};

// The read-only community is the one field that does not start at zero; static storage zeroes the
// MIB, its count, the read-write community and rw_set.
static struct SnmpAgentStorage s_store = {.ro = PROTOCORE_SNMP_DEFAULT_RO_COMMUNITY};

static struct SnmpAgentInternal s_agent = {.store = &s_store, .ns = &SnmpAgent};

// community_match()'s non-empty test is a no-op only while the read-only community is never
// empty. It is seeded from this macro and init() falls back to it, so an override that emptied it
// would make an empty community match. That is a build error here rather than a behavior change.
_Static_assert(sizeof(PROTOCORE_SNMP_DEFAULT_RO_COMMUNITY) > 1,
               "PROTOCORE_SNMP_DEFAULT_RO_COMMUNITY must be a non-empty string literal");

// ---------------------------------------------------------------------------
// OID ordering and lookup. Lexicographic order over subidentifiers is what
// GetNextRequest-PDU walks (RFC 3416 sec 4.2.2).
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

// The entry whose name is exactly (oid, n), or NULL. Reads the MIB, so it takes ctx.
static const SnmpMibEntry *mib_find_exact(struct SnmpAgentInternal *restrict ctx, const uint32_t *oid, size_t n)
{
    for (size_t i = 0; i < ctx->store->mib_count; i++)
    {
        if (oid_cmp(ctx->store->mib[i].oid, ctx->store->mib[i].oid_len, oid, n) == 0)
        {
            return &ctx->store->mib[i];
        }
    }
    return NULL;
}

// The smallest registered name strictly greater than (oid, n), or NULL at the end of the MIB view.
static const SnmpMibEntry *mib_find_next(struct SnmpAgentInternal *restrict ctx, const uint32_t *oid, size_t n)
{
    const SnmpMibEntry *best = NULL;
    for (size_t i = 0; i < ctx->store->mib_count; i++)
    {
        if (oid_cmp(ctx->store->mib[i].oid, ctx->store->mib[i].oid_len, oid, n) > 0)
        {
            if (!best || oid_cmp(ctx->store->mib[i].oid, ctx->store->mib[i].oid_len, best->oid, best->oid_len) < 0)
            {
                best = &ctx->store->mib[i];
            }
        }
    }
    return best;
}

// RFC 3416 sec 4.2.1 splits an unbound name two ways: noSuchInstance when the object exists and
// only the instance is absent, noSuchObject when no such object exists. Every entry is a full
// instance name, so its object name is that minus the trailing instance subidentifier, and the
// request names a known object exactly when that prefix is a prefix of the request.
static proto_bool mib_object_exists(struct SnmpAgentInternal *restrict ctx, const uint32_t *oid, size_t n)
{
    for (size_t i = 0; i < ctx->store->mib_count; i++)
    {
        size_t objn = ctx->store->mib[i].oid_len - 1;
        if (objn <= n && oid_cmp(ctx->store->mib[i].oid, objn, oid, objn) == 0)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
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

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

// Claim the next table row for the name ns->object names, or NULL when the table is full or the
// name is unusable.
static SnmpMibEntry *mib_alloc(struct SnmpAgentInternal *restrict ctx)
{
    const uint32_t *oid = ctx->ns->object.oid;
    const size_t n = ctx->ns->object.oid_len;
    if (ctx->store->mib_count >= SNMP_MAX_MIB_ENTRIES || n < 2 || n > SNMP_MAX_OID_LEN)
    {
        return NULL;
    }
    SnmpMibEntry *e = &ctx->store->mib[ctx->store->mib_count++];
    mem.set(e, 0, sizeof(*e));
    for (size_t i = 0; i < n; i++)
    {
        e->oid[i] = oid[i];
    }
    e->oid_len = n;
    return e;
}

static void agent_init(struct SnmpAgentInternal *restrict ctx)
{
    ctx->store->mib_count = 0;
    ctx->store->rw_set = PROTO_FALSE;
    ctx->store->rw[0] = '\0';
    const char *ro = ctx->ns->community.ro;
    if (!ro || !ro[0])
    {
        ro = PROTOCORE_SNMP_DEFAULT_RO_COMMUNITY;
    }
    (void)str.copy(ctx->store->ro, ro, sizeof(ctx->store->ro));
    ctx->ns->ok = PROTO_TRUE;
}

static void set_rw_community(struct SnmpAgentInternal *restrict ctx)
{
    const char *rw = ctx->ns->community.rw;
    if (!rw || !rw[0])
    {
        ctx->store->rw_set = PROTO_FALSE;
        ctx->store->rw[0] = '\0';
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    (void)str.copy(ctx->store->rw, rw, sizeof(ctx->store->rw));
    ctx->store->rw_set = PROTO_TRUE;
    ctx->ns->ok = PROTO_TRUE;
}

static void add_string(struct SnmpAgentInternal *restrict ctx)
{
    SnmpMibEntry *e = mib_alloc(ctx);
    if (!e)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    const char *value = ctx->ns->object.text;
    e->val.type = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    e->val.str = value;
    e->val.str_len = value ? str.len(value, SNMP_MSG_BUF_SIZE) : 0;
    e->setter = ctx->ns->object.setter;
    ctx->ns->ok = PROTO_TRUE;
}

static void add_integer(struct SnmpAgentInternal *restrict ctx)
{
    SnmpMibEntry *e = mib_alloc(ctx);
    if (!e)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    e->val.type = (uint8_t)SNMP_TAG_BER_INTEGER;
    e->val.ival = ctx->ns->object.ival;
    e->setter = ctx->ns->object.setter;
    ctx->ns->ok = PROTO_TRUE;
}

static void add_dynamic(struct SnmpAgentInternal *restrict ctx)
{
    SnmpMibEntry *e = mib_alloc(ctx);
    if (!e)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    e->val.type = ctx->ns->object.type;
    e->getter = ctx->ns->object.getter;
    ctx->ns->ok = PROTO_TRUE;
}

// sysUpTime.0 is TimeTicks: hundredths of a second since the network management portion of the
// system was re-initialized (RFC 3418 sec 2, RFC 2578 sec 7.1.8).
static uint32_t snmp_uptime_cs(void)
{
#if PROTOCORE_HAS_NET_STACK
    return (uint32_t)(Clock.ms / 10ULL);
#else
    return 0; // no clock in this build
#endif
}

static proto_bool sys_uptime_get(SnmpValue *out)
{
    out->type = (uint8_t)SNMP_TAG_SNMP_TIMETICKS;
    out->uval = snmp_uptime_cs();
    return PROTO_TRUE;
}

// The system group, 1.3.6.1.2.1.1 (RFC 3418 sec 2): sysDescr.0 through sysServices.0. sysUpTime.0
// is dynamic; sysObjectID.0 is written straight into its row because no registration call carries
// an OBJECT IDENTIFIER value.
static void set_system(struct SnmpAgentInternal *restrict ctx)
{
    static const uint32_t o_descr[] = {1, 3, 6, 1, 2, 1, 1, 1, 0};
    static const uint32_t o_oid[] = {1, 3, 6, 1, 2, 1, 1, 2, 0};
    static const uint32_t o_uptime[] = {1, 3, 6, 1, 2, 1, 1, 3, 0};
    static const uint32_t o_contact[] = {1, 3, 6, 1, 2, 1, 1, 4, 0};
    static const uint32_t o_name[] = {1, 3, 6, 1, 2, 1, 1, 5, 0};
    static const uint32_t o_loc[] = {1, 3, 6, 1, 2, 1, 1, 6, 0};
    static const uint32_t o_svc[] = {1, 3, 6, 1, 2, 1, 1, 7, 0};

    const char *descr = ctx->ns->system.descr;
    const char *contact = ctx->ns->system.contact;
    const char *name = ctx->ns->system.name;
    const char *location = ctx->ns->system.location;
    const long services = ctx->ns->system.services;

    ctx->ns->object.setter = NULL;
    ctx->ns->object.oid = o_descr;
    ctx->ns->object.oid_len = 9;
    ctx->ns->object.text = descr;
    add_string(ctx);

    ctx->ns->object.oid = o_oid;
    ctx->ns->object.oid_len = 9;
    SnmpMibEntry *e = mib_alloc(ctx);
    if (e)
    {
        e->val.type = (uint8_t)SNMP_TAG_BER_OID;
        e->val.oid = g_sys_object_id;
        e->val.oid_len = sizeof(g_sys_object_id) / sizeof(g_sys_object_id[0]);
    }

    ctx->ns->object.oid = o_uptime;
    ctx->ns->object.oid_len = 9;
    ctx->ns->object.type = (uint8_t)SNMP_TAG_SNMP_TIMETICKS;
    ctx->ns->object.getter = sys_uptime_get;
    add_dynamic(ctx);
    ctx->ns->object.getter = NULL;

    ctx->ns->object.oid = o_contact;
    ctx->ns->object.oid_len = 9;
    ctx->ns->object.text = contact;
    add_string(ctx);

    ctx->ns->object.oid = o_name;
    ctx->ns->object.oid_len = 9;
    ctx->ns->object.text = name;
    add_string(ctx);

    ctx->ns->object.oid = o_loc;
    ctx->ns->object.oid_len = 9;
    ctx->ns->object.text = location;
    add_string(ctx);

    ctx->ns->object.oid = o_svc;
    ctx->ns->object.oid_len = 9;
    ctx->ns->object.ival = services;
    add_integer(ctx);
}

// ---------------------------------------------------------------------------
// Value encode and decode. Both run over the caller's cursor and a value, and
// touch no module state.
// ---------------------------------------------------------------------------

static void enc_value(BerEnc *e, const SnmpValue *v)
{
    SnmpBer.enc = e;
    switch (v->type)
    {
    case (uint8_t)SNMP_TAG_BER_INTEGER:
        SnmpBer.tlv.ival = v->ival;
        SnmpBer.put_integer(SnmpBer.internal);
        break;
    case (uint8_t)SNMP_TAG_BER_OCTET_STRING:
    case (uint8_t)SNMP_TAG_SNMP_OPAQUE:
        SnmpBer.tlv.tag = v->type;
        SnmpBer.tlv.bytes = (const uint8_t *)v->str;
        SnmpBer.tlv.len = v->str_len;
        SnmpBer.put_octet_string(SnmpBer.internal);
        break;
    case (uint8_t)SNMP_TAG_BER_OID:
        SnmpBer.tlv.arcs = v->oid;
        SnmpBer.tlv.arc_count = v->oid_len;
        SnmpBer.put_oid(SnmpBer.internal);
        break;
    case (uint8_t)SNMP_TAG_SNMP_TIMETICKS:
    case (uint8_t)SNMP_TAG_SNMP_COUNTER32:
    case (uint8_t)SNMP_TAG_SNMP_GAUGE32:
        SnmpBer.tlv.tag = v->type;
        SnmpBer.tlv.uval = v->uval;
        SnmpBer.put_uint(SnmpBer.internal);
        break;
    case (uint8_t)SNMP_TAG_SNMP_IPADDRESS: {
        // RFC 2578 sec 7.1.5: an OCTET STRING of length 4, network byte order.
        uint8_t ip[4] = {(uint8_t)(v->uval >> 24), (uint8_t)(v->uval >> 16), (uint8_t)(v->uval >> 8),
                         (uint8_t)(v->uval)};
        SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_SNMP_IPADDRESS;
        SnmpBer.tlv.bytes = ip;
        SnmpBer.tlv.len = 4;
        SnmpBer.put_octet_string(SnmpBer.internal);
        break;
    }
    case (uint8_t)SNMP_TAG_BER_NULL:
        SnmpBer.put_null(SnmpBer.internal);
        break;
    default:
        // A VarBind exception marker (RFC 3416 sec 3): the tag with a zero-length value.
        SnmpBer.tlv.tag = v->type;
        SnmpBer.tlv.bytes = NULL;
        SnmpBer.tlv.len = 0;
        SnmpBer.put_tlv(SnmpBer.internal);
        break;
    }
}

// One binding value at the decoder cursor into v, with an OBJECT IDENTIFIER value landing in
// oidbuf. Leaves the cursor past the value.
static proto_bool dec_value(BerDec *d, SnmpValue *v, uint32_t *oidbuf)
{
    mem.set(v, 0, sizeof(*v));
    size_t save = d->pos;
    SnmpBer.dec = d;
    SnmpBer.read_header(SnmpBer.internal);
    if (!SnmpBer.ok)
    {
        return PROTO_FALSE;
    }
    const uint8_t tag = SnmpBer.tag;
    const size_t len = SnmpBer.vlen;
    v->type = tag;
    switch (tag)
    {
    case (uint8_t)SNMP_TAG_BER_INTEGER:
        d->pos = save;
        SnmpBer.read_integer(SnmpBer.internal);
        v->ival = SnmpBer.ival;
        return SnmpBer.ok;
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
        SnmpBer.read_args.arc_out = oidbuf;
        SnmpBer.read_args.arc_cap = SNMP_MAX_OID_LEN;
        SnmpBer.read_oid(SnmpBer.internal);
        if (!SnmpBer.ok)
        {
            return PROTO_FALSE;
        }
        v->oid_len = SnmpBer.n;
        v->oid = oidbuf;
        return PROTO_TRUE;
    default: // NULL, the value a GetRequest-PDU binding carries, and anything else: step past it
        d->pos += len;
        return PROTO_TRUE;
    }
}

// ---------------------------------------------------------------------------
// PDU processing (RFC 3416 sec 4.2)
// ---------------------------------------------------------------------------

// Apply each binding of a SetRequest-PDU in order, stopping at the first failure and reporting it
// as error-status with the 1-based error-index (RFC 3416 sec 4.2.5). Both outputs are left alone
// when every binding succeeds.
static void apply_set_all(struct SnmpAgentInternal *restrict ctx, size_t nvb, proto_bool v2c, long *err_status,
                          long *err_index)
{
    for (size_t i = 0; i < nvb; i++)
    {
        const SnmpMibEntry *en = mib_find_exact(ctx, ctx->store->in[i].oid, ctx->store->in[i].oid_len);
        int e = 0;
        if (!en)
        {
            e = (int)SNMP_ERR_NO_SUCH_NAME;
        }
        else if (!en->setter)
        {
            e = (!v2c) ? (int)SNMP_ERR_READ_ONLY : (int)SNMP_ERR_NOT_WRITABLE;
        }
        else if (!en->setter(&ctx->store->in[i].val))
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

// The community a message names matches a configured one: same length, same octets. Reads the
// stored community it is handed and no module state.
static proto_bool community_match(const char *stored, const char *p, size_t len)
{
    return stored[0] != '\0' && str.len(stored, len + 1) == len && mem.cmp(stored, p, len) == 0;
}

// Response-PDU (RFC 3416 sec 4.2.4): request-id, error-status, error-index, then the first nout
// bindings of the output scratch. Reads that scratch, so it takes ctx.
static size_t encode_response(struct SnmpAgentInternal *restrict ctx, long request_id, long err_status, long err_index,
                              size_t nout, uint8_t *buf, size_t cap)
{
    BerEnc e;
    SnmpBer.enc = &e;
    SnmpBer.buf.out = buf;
    SnmpBer.buf.cap = cap;
    SnmpBer.enc_init(SnmpBer.internal);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_SNMP_PDU_RESPONSE;
    SnmpBer.seq_begin(SnmpBer.internal);
    const size_t pdu = SnmpBer.tlv.token;
    SnmpBer.tlv.ival = request_id;
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.tlv.ival = err_status;
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.tlv.ival = err_index;
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(SnmpBer.internal);
    const size_t vbl = SnmpBer.tlv.token;
    for (size_t i = 0; i < nout; i++)
    {
        SnmpBer.enc = &e;
        SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
        SnmpBer.seq_begin(SnmpBer.internal);
        const size_t vb = SnmpBer.tlv.token;
        SnmpBer.tlv.arcs = ctx->store->out[i].oid;
        SnmpBer.tlv.arc_count = ctx->store->out[i].oid_len;
        SnmpBer.put_oid(SnmpBer.internal);
        enc_value(&e, &ctx->store->out[i].val);
        SnmpBer.enc = &e;
        SnmpBer.tlv.token = vb;
        SnmpBer.seq_end(SnmpBer.internal);
    }
    SnmpBer.enc = &e;
    SnmpBer.tlv.token = vbl;
    SnmpBer.seq_end(SnmpBer.internal);
    SnmpBer.tlv.token = pdu;
    SnmpBer.seq_end(SnmpBer.internal);
    return e.ok ? e.len : 0;
}

// Decode the VarBindList of the request into the input scratch. Reports the binding count, or
// SNMP_MAX_VARBINDS + 1 when the list is malformed or longer than the scratch holds.
static size_t decode_varbinds(struct SnmpAgentInternal *restrict ctx, BerDec *d, size_t vbl_end)
{
    size_t nvb = 0;
    // Every read inside the loop that could clear d->ok is answered by its own guard first, so the
    // cursor is always ok on re-entry.
    while (d->pos < vbl_end && d->ok)
    {
        if (nvb >= SNMP_MAX_VARBINDS)
        {
            return SNMP_MAX_VARBINDS + 1;
        }
        SnmpBer.dec = d;
        SnmpBer.read_header(SnmpBer.internal);
        if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
        {
            return SNMP_MAX_VARBINDS + 1;
        }
        SnmpBer.read_args.arc_out = ctx->store->in[nvb].oid;
        SnmpBer.read_args.arc_cap = SNMP_MAX_OID_LEN;
        SnmpBer.read_oid(SnmpBer.internal);
        if (!SnmpBer.ok)
        {
            return SNMP_MAX_VARBINDS + 1;
        }
        ctx->store->in[nvb].oid_len = SnmpBer.n;
        if (!dec_value(d, &ctx->store->in[nvb].val, ctx->store->in[nvb].valoid))
        {
            return SNMP_MAX_VARBINDS + 1;
        }
        nvb++;
    }
    return d->ok ? nvb : (SNMP_MAX_VARBINDS + 1);
}

// Copy the request bindings into the response unchanged, which is what a v1 error report and every
// SetRequest-PDU response carry (RFC 1157 sec 4.1.1, RFC 3416 sec 4.2.5).
static void echo_varbinds(struct SnmpAgentInternal *restrict ctx, size_t nvb)
{
    for (size_t k = 0; k < nvb; k++)
    {
        ctx->store->out[k].oid = ctx->store->in[k].oid;
        ctx->store->out[k].oid_len = ctx->store->in[k].oid_len;
        ctx->store->out[k].val = ctx->store->in[k].val;
    }
}

// GetRequest-PDU (RFC 3416 sec 4.2.1) and GetNextRequest-PDU (sec 4.2.2). Reports the binding
// count written; a v1 name failure stops the walk and reports through err_status/err_index.
static size_t run_get(struct SnmpAgentInternal *restrict ctx, uint8_t pdu_tag, size_t nvb, proto_bool v2c,
                      long *err_status, long *err_index)
{
    const proto_bool is_next = (pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_GETNEXT);
    size_t nout = 0;
    for (size_t i = 0; i < nvb; i++)
    {
        const SnmpMibEntry *en = is_next ? mib_find_next(ctx, ctx->store->in[i].oid, ctx->store->in[i].oid_len)
                                         : mib_find_exact(ctx, ctx->store->in[i].oid, ctx->store->in[i].oid_len);
        SnmpValue val;
        proto_bool ok = en && fetch_value(en, &val);
        if (!ok)
        {
            if (!v2c)
            {
                // RFC 1157 sec 4.1.2: noSuchName names the failing binding by index, and the
                // response repeats the request's bindings.
                *err_status = (int)SNMP_ERR_NO_SUCH_NAME;
                *err_index = (long)(i + 1);
                echo_varbinds(ctx, nvb);
                return nvb;
            }
            // RFC 3416 sec 4.2.1 and sec 4.2.2: the exception rides in the binding's value. A Get
            // separates a missing instance of a known object from an unknown object; a Next past
            // the last name in the view is always endOfMibView.
            if (!is_next)
            {
                proto_bool inst = en || mib_object_exists(ctx, ctx->store->in[i].oid, ctx->store->in[i].oid_len);
                val.type = inst ? (uint8_t)SNMP_TAG_SNMP_NO_SUCH_INSTANCE : (uint8_t)SNMP_TAG_SNMP_NO_SUCH_OBJECT;
            }
            else
            {
                val.type = (uint8_t)SNMP_TAG_SNMP_END_OF_MIB_VIEW;
            }
            ctx->store->out[nout].oid = en ? en->oid : ctx->store->in[i].oid;
            ctx->store->out[nout].oid_len = en ? en->oid_len : ctx->store->in[i].oid_len;
            ctx->store->out[nout].val = val;
            nout++;
            continue;
        }
        // A Next answers under the name it found; a Get answers under the name it was asked.
        ctx->store->out[nout].oid = is_next ? en->oid : ctx->store->in[i].oid;
        ctx->store->out[nout].oid_len = is_next ? en->oid_len : ctx->store->in[i].oid_len;
        ctx->store->out[nout].val = val;
        nout++;
    }
    return nout;
}

// GetBulkRequest-PDU (RFC 3416 sec 4.2.3): the first non-repeaters bindings get one successor
// each, and each remaining binding is walked for up to max-repetitions successors. Reports the
// binding count written.
static size_t run_bulk(struct SnmpAgentInternal *restrict ctx, size_t nvb, long non_repeaters, long max_repetitions)
{
    long non_rep = non_repeaters < 0 ? 0 : non_repeaters;
    long max_rep = max_repetitions < 0 ? 0 : max_repetitions;
    if ((size_t)non_rep > nvb)
    {
        non_rep = (long)nvb;
    }
    size_t nout = 0;

    for (long i = 0; i < non_rep; i++)
    {
        if (nout >= SNMP_MAX_VARBINDS)
        {
            break;
        }
        const SnmpMibEntry *en = mib_find_next(ctx, ctx->store->in[i].oid, ctx->store->in[i].oid_len);
        if (en)
        {
            ctx->store->out[nout].oid = en->oid;
            ctx->store->out[nout].oid_len = en->oid_len;
            fetch_value(en, &ctx->store->out[nout].val);
        }
        else
        {
            ctx->store->out[nout].oid = ctx->store->in[i].oid;
            ctx->store->out[nout].oid_len = ctx->store->in[i].oid_len;
            ctx->store->out[nout].val.type = (uint8_t)SNMP_TAG_SNMP_END_OF_MIB_VIEW;
        }
        nout++;
    }

    const size_t nrep = nvb - (size_t)non_rep;
    for (size_t r = 0; r < nrep; r++)
    {
        ctx->store->cur_oid[r] = ctx->store->in[non_rep + r].oid;
        ctx->store->cur_len[r] = ctx->store->in[non_rep + r].oid_len;
        ctx->store->ended[r] = PROTO_FALSE;
    }
    for (long rep = 0; rep < max_rep && nout < SNMP_MAX_VARBINDS; rep++)
    {
        for (size_t r = 0; r < nrep && nout < SNMP_MAX_VARBINDS; r++)
        {
            if (ctx->store->ended[r])
            {
                ctx->store->out[nout].oid = ctx->store->cur_oid[r];
                ctx->store->out[nout].oid_len = ctx->store->cur_len[r];
                ctx->store->out[nout].val.type = (uint8_t)SNMP_TAG_SNMP_END_OF_MIB_VIEW;
                nout++;
                continue;
            }
            const SnmpMibEntry *en = mib_find_next(ctx, ctx->store->cur_oid[r], ctx->store->cur_len[r]);
            if (en)
            {
                ctx->store->out[nout].oid = en->oid;
                ctx->store->out[nout].oid_len = en->oid_len;
                fetch_value(en, &ctx->store->out[nout].val);
                ctx->store->cur_oid[r] = en->oid;
                ctx->store->cur_len[r] = en->oid_len;
            }
            else
            {
                ctx->store->out[nout].oid = ctx->store->cur_oid[r];
                ctx->store->out[nout].oid_len = ctx->store->cur_len[r];
                ctx->store->out[nout].val.type = (uint8_t)SNMP_TAG_SNMP_END_OF_MIB_VIEW;
                ctx->store->ended[r] = PROTO_TRUE;
            }
            nout++;
        }
    }
    return nout;
}

// One request PDU against the MIB, answered by one Response-PDU (RFC 3416 sec 4.2). The v1 and
// v2c community framing and the v3 USM layer both arrive here.
static void dispatch_pdu(struct SnmpAgentInternal *restrict ctx)
{
    const uint8_t *pdu = ctx->ns->pdu.req;
    const size_t pdu_len = ctx->ns->pdu.req_len;
    const proto_bool allow_write = ctx->ns->pdu.allow_write;
    const proto_bool v2c = ctx->ns->pdu.v2c;
    uint8_t *out = ctx->ns->pdu.out;
    const size_t out_cap = ctx->ns->pdu.out_cap;
    ctx->ns->n = 0;

    BerDec d;
    SnmpBer.dec = &d;
    SnmpBer.buf.in = pdu;
    SnmpBer.buf.cap = pdu_len;
    SnmpBer.dec_init(SnmpBer.internal);

    SnmpBer.read_header(SnmpBer.internal);
    if (!SnmpBer.ok)
    {
        return;
    }
    const uint8_t pdu_tag = SnmpBer.tag;

    // Positions 2 and 3: error-status and error-index in a PDU, non-repeaters and max-repetitions
    // in a BulkPDU (RFC 3416 sec 3).
    SnmpBer.read_integer(SnmpBer.internal);
    if (!SnmpBer.ok)
    {
        return;
    }
    const long request_id = SnmpBer.ival;
    SnmpBer.read_integer(SnmpBer.internal);
    if (!SnmpBer.ok)
    {
        return;
    }
    const long field2 = SnmpBer.ival;
    SnmpBer.read_integer(SnmpBer.internal);
    if (!SnmpBer.ok)
    {
        return;
    }
    const long field3 = SnmpBer.ival;

    SnmpBer.read_header(SnmpBer.internal);
    if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return;
    }
    const size_t vbl_end = d.pos + SnmpBer.vlen;

    const size_t nvb = decode_varbinds(ctx, &d, vbl_end);
    if (nvb > SNMP_MAX_VARBINDS)
    {
        return;
    }

    long err_status = (int)SNMP_ERR_NO_ERROR;
    long err_index = 0;
    size_t nout = 0;

    if (pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_GET || pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_GETNEXT)
    {
        nout = run_get(ctx, pdu_tag, nvb, v2c, &err_status, &err_index);
    }
    else if (pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_GETBULK)
    {
        if (!v2c) // the GetBulkRequest-PDU is SNMPv2c and later (RFC 3416 sec 4.2.3)
        {
            return;
        }
        nout = run_bulk(ctx, nvb, field2, field3);
    }
    else if (pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_SET)
    {
        // A SetRequest-PDU reports through error-status in both framings, and its response repeats
        // the request's bindings (RFC 3416 sec 4.2.5).
        nout = nvb;
        echo_varbinds(ctx, nvb);
        if (!allow_write)
        {
            err_status = (!v2c) ? (int)SNMP_ERR_NO_SUCH_NAME : (int)SNMP_ERR_NO_ACCESS;
            err_index = 1;
        }
        else
        {
            apply_set_all(ctx, nvb, v2c, &err_status, &err_index);
        }
    }
    else
    {
        return; // a PDU this responder does not answer
    }

    size_t n = encode_response(ctx, request_id, err_status, err_index, nout, out, out_cap);
    if (n == 0 && nout > 0)
    {
        // RFC 3416 sec 4.2.1: a response too large for its buffer is answered tooBig with an empty
        // VarBindList.
        n = encode_response(ctx, request_id, (int)SNMP_ERR_TOO_BIG, 0, 0, out, out_cap);
    }
    ctx->ns->n = n;
}

// ---------------------------------------------------------------------------
// Message framing: RFC 1157 sec 4 for v1, RFC 1901 sec 3 for v2c. A v3 message
// is the USM layer's.
// ---------------------------------------------------------------------------

static void agent_process(struct SnmpAgentInternal *restrict ctx)
{
    const uint8_t *req = ctx->ns->msg.req;
    const size_t req_len = ctx->ns->msg.req_len;
    uint8_t *resp = ctx->ns->msg.resp;
    const size_t resp_cap = ctx->ns->msg.resp_cap;
    ctx->ns->n = 0;

    BerDec d;
    SnmpBer.dec = &d;
    SnmpBer.buf.in = req;
    SnmpBer.buf.cap = req_len;
    SnmpBer.dec_init(SnmpBer.internal);

    SnmpBer.read_header(SnmpBer.internal);
    if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_SEQUENCE) // the message wrapper
    {
        return;
    }

    SnmpBer.read_integer(SnmpBer.internal);
    if (!SnmpBer.ok)
    {
        return;
    }
    const long version = SnmpBer.ival;

    if (version == (int)SNMP_V3)
    {
#if PROTOCORE_ENABLE_SNMP_V3
        SnmpV3.msg.req = req;
        SnmpV3.msg.req_len = req_len;
        SnmpV3.msg.resp = resp;
        SnmpV3.msg.resp_cap = resp_cap;
        SnmpV3.process(SnmpV3.internal);
        ctx->ns->n = SnmpV3.n;
#endif
        return; // without the USM layer a v3 message is answered with nothing
    }
    if (version != (int)SNMP_V1 && version != (int)SNMP_V2C)
    {
        return;
    }

    SnmpBer.read_header(SnmpBer.internal);
    if (!SnmpBer.ok || SnmpBer.tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return;
    }
    const char *community = (const char *)(d.buf + d.pos);
    const size_t community_len = SnmpBer.vlen;
    d.pos += community_len;

    // RFC 1157 sec 3.2.5: the message is authentic when it names a community it belongs to. The
    // read-write community also authorizes reads.
    const proto_bool is_rw = ctx->store->rw_set && community_match(ctx->store->rw, community, community_len);
    const proto_bool is_ro = is_rw || community_match(ctx->store->ro, community, community_len);
    if (!is_ro)
    {
        return; // RFC 1157 sec 4.1: an authentication failure discards the datagram
    }

    // The PDU is the rest of the datagram: dispatch it, then wrap the response in the same framing.
    const size_t pdu_off = d.pos;
    ctx->ns->pdu.req = req + pdu_off;
    ctx->ns->pdu.req_len = req_len - pdu_off;
    ctx->ns->pdu.allow_write = is_rw;
    ctx->ns->pdu.v2c = (version == (int)SNMP_V2C);
    ctx->ns->pdu.out = ctx->store->pdu_stage;
    ctx->ns->pdu.out_cap = sizeof(ctx->store->pdu_stage);
    dispatch_pdu(ctx);
    const size_t pn = ctx->ns->n;
    ctx->ns->n = 0;
    if (pn == 0)
    {
        return;
    }

    BerEnc e;
    SnmpBer.enc = &e;
    SnmpBer.buf.out = resp;
    SnmpBer.buf.cap = resp_cap;
    SnmpBer.enc_init(SnmpBer.internal);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(SnmpBer.internal);
    const size_t msg = SnmpBer.tlv.token;
    SnmpBer.tlv.ival = version;
    SnmpBer.put_integer(SnmpBer.internal);
    SnmpBer.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    SnmpBer.tlv.bytes = (const uint8_t *)community;
    SnmpBer.tlv.len = community_len;
    SnmpBer.put_octet_string(SnmpBer.internal);
    SnmpBer.tlv.bytes = ctx->store->pdu_stage;
    SnmpBer.tlv.len = pn;
    SnmpBer.put_raw(SnmpBer.internal);
    SnmpBer.tlv.token = msg;
    SnmpBer.seq_end(SnmpBer.internal);
    ctx->ns->n = e.ok ? e.len : 0;
}

// ---------------------------------------------------------------------------
// The UDP binding (RFC 3417 sec 3.2)
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_NET_STACK
// One received datagram: process it and answer the sender it came from. Runs inside the poll that
// drains the port, so the request scratch and the send stage serve it alone.
static void snmp_udp_handler(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *cbctx)
{
    (void)cbctx;
    SnmpAgent.msg.req = data;
    SnmpAgent.msg.req_len = len;
    SnmpAgent.msg.resp = s_store.tx;
    SnmpAgent.msg.resp_cap = sizeof(s_store.tx);
    agent_process(&s_agent);
    if (SnmpAgent.n == 0)
    {
        return;
    }
    UdpListener.peer_args.peer = peer;
    UdpListener.send_args.data = s_store.tx;
    UdpListener.send_args.len = SnmpAgent.n;
    UdpListener.reply(UdpListener.internal);
}
#endif // PROTOCORE_HAS_NET_STACK

static void agent_listen(struct SnmpAgentInternal *restrict ctx)
{
#if PROTOCORE_HAS_NET_STACK
    UdpListener.port = ctx->ns->port;
    UdpListener.bind.handler = snmp_udp_handler;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen(UdpListener.internal);
    ctx->ns->ok = UdpListener.ok;
#else
    ctx->ns->ok = PROTO_FALSE; // no transport in this build
#endif
}

// Designated, so a member's position in the struct does not decide what it binds to.
SnmpAgentNs SnmpAgent = {.init = agent_init,
                         .set_rw_community = set_rw_community,
                         .set_system = set_system,
                         .add_string = add_string,
                         .add_integer = add_integer,
                         .add_dynamic = add_dynamic,
                         .dispatch_pdu = dispatch_pdu,
                         .process = agent_process,
                         .listen = agent_listen,
                         .internal = &s_agent};

#endif // PROTOCORE_ENABLE_SNMP
