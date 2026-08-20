// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snmp_agent.c
 * @brief The command responder (RFC 1157, RFC 1901, RFC 3416) - implementation. See snmp_agent.h.
 */

#include "services/net/snmp/snmp_agent/snmp_agent.h"
#include "mmgr/protomem/protomem.h" // mem.set / mem.cmp: the fixed-width compares and clears
#include "mmgr/protostr/protostr.h" // str.len / str.copy: the bounded community and string handling
#include "mmgr/secure/secure.h"     // the persistent end this module's key material is taken from
#include "services/net/snmp/snmp_v3/snmp_v3.h"

static uint8_t snmp_ber_work[16]; // the borrow an entry takes; SnmpBer never reads it

#if PROTOCORE_ENABLE_SNMP

#if PROTOCORE_ENABLE_SNMP_V3
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

// The read-only community is the one field that does not start at zero; static storage zeroes the
// MIB, its count, the read-write community and rw_set.
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SNMP_AGENT_OFF_CTX 0u
static_assert(SNMP_AGENT_OFF_CTX + sizeof(struct SnmpAgentStorage) <= PROTOCORE_SNMP_AGENT_BORROW,
              "PROTOCORE_SNMP_AGENT_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define SNMP_AGENT_CTX(w) ((struct SnmpAgentStorage *)(void *)((w) + SNMP_AGENT_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SNMP_AGENT_BORROW persistent bytes
} SnmpAgentOwnCtx;
static SnmpAgentOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_snmp_agent_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_SNMP_AGENT_BORROW).buf;
        // A borrow arrives zeroed, and these do not start at zero.
        (void)str.copy(SNMP_AGENT_CTX(s_own.span)->ro, PROTOCORE_SNMP_DEFAULT_RO_COMMUNITY, SNMP_COMMUNITY_MAX);
    }
    return s_own.span;
}

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
static const SnmpMibEntry *mib_find_exact(uint8_t *restrict work, const uint32_t *oid, size_t n)
{
    for (size_t i = 0; i < SNMP_AGENT_CTX(work)->mib_count; i++)
    {
        if (oid_cmp(SNMP_AGENT_CTX(work)->mib[i].oid, SNMP_AGENT_CTX(work)->mib[i].oid_len, oid, n) == 0)
        {
            return &SNMP_AGENT_CTX(work)->mib[i];
        }
    }
    return NULL;
}

// The smallest registered name strictly greater than (oid, n), or NULL at the end of the MIB view.
static const SnmpMibEntry *mib_find_next(uint8_t *restrict work, const uint32_t *oid, size_t n)
{
    const SnmpMibEntry *best = NULL;
    for (size_t i = 0; i < SNMP_AGENT_CTX(work)->mib_count; i++)
    {
        if (oid_cmp(SNMP_AGENT_CTX(work)->mib[i].oid, SNMP_AGENT_CTX(work)->mib[i].oid_len, oid, n) > 0)
        {
            if (!best || oid_cmp(SNMP_AGENT_CTX(work)->mib[i].oid, SNMP_AGENT_CTX(work)->mib[i].oid_len, best->oid,
                                 best->oid_len) < 0)
            {
                best = &SNMP_AGENT_CTX(work)->mib[i];
            }
        }
    }
    return best;
}

// RFC 3416 sec 4.2.1 splits an unbound name two ways: noSuchInstance when the object exists and
// only the instance is absent, noSuchObject when no such object exists. Every entry is a full
// instance name, so its object name is that minus the trailing instance subidentifier, and the
// request names a known object exactly when that prefix is a prefix of the request.
static proto_bool mib_object_exists(uint8_t *restrict work, const uint32_t *oid, size_t n)
{
    for (size_t i = 0; i < SNMP_AGENT_CTX(work)->mib_count; i++)
    {
        size_t objn = SNMP_AGENT_CTX(work)->mib[i].oid_len - 1;
        if (objn <= n && oid_cmp(SNMP_AGENT_CTX(work)->mib[i].oid, objn, oid, objn) == 0)
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
static SnmpMibEntry *mib_alloc(uint8_t *restrict work)
{
    const uint32_t *oid = SnmpAgent.object.oid;
    const size_t n = SnmpAgent.object.oid_len;
    if (SNMP_AGENT_CTX(work)->mib_count >= SNMP_MAX_MIB_ENTRIES || n < 2 || n > SNMP_MAX_OID_LEN)
    {
        return NULL;
    }
    SnmpMibEntry *e = &SNMP_AGENT_CTX(work)->mib[SNMP_AGENT_CTX(work)->mib_count++];
    mem.set(e, 0, sizeof(*e));
    for (size_t i = 0; i < n; i++)
    {
        e->oid[i] = oid[i];
    }
    e->oid_len = n;
    return e;
}

static void agent_init(uint8_t *restrict work)
{
    SNMP_AGENT_CTX(work)->mib_count = 0;
    SNMP_AGENT_CTX(work)->rw_set = PROTO_FALSE;
    SNMP_AGENT_CTX(work)->rw[0] = '\0';
    const char *ro = SnmpAgent.community.ro;
    if (!ro || !ro[0])
    {
        ro = PROTOCORE_SNMP_DEFAULT_RO_COMMUNITY;
    }
    (void)str.copy(SNMP_AGENT_CTX(work)->ro, ro, sizeof(SNMP_AGENT_CTX(work)->ro));
    SnmpAgent.ok = PROTO_TRUE;
}

static void set_rw_community(uint8_t *restrict work)
{
    const char *rw = SnmpAgent.community.rw;
    if (!rw || !rw[0])
    {
        SNMP_AGENT_CTX(work)->rw_set = PROTO_FALSE;
        SNMP_AGENT_CTX(work)->rw[0] = '\0';
        SnmpAgent.ok = PROTO_FALSE;
        return;
    }
    (void)str.copy(SNMP_AGENT_CTX(work)->rw, rw, sizeof(SNMP_AGENT_CTX(work)->rw));
    SNMP_AGENT_CTX(work)->rw_set = PROTO_TRUE;
    SnmpAgent.ok = PROTO_TRUE;
}

static void add_string(uint8_t *restrict work)
{
    SnmpMibEntry *e = mib_alloc(work);
    if (!e)
    {
        SnmpAgent.ok = PROTO_FALSE;
        return;
    }
    const char *value = SnmpAgent.object.text;
    e->val.type = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    e->val.str = value;
    e->val.str_len = value ? str.len(value, SNMP_MSG_BUF_SIZE) : 0;
    e->setter = SnmpAgent.object.setter;
    SnmpAgent.ok = PROTO_TRUE;
}

static void add_integer(uint8_t *restrict work)
{
    SnmpMibEntry *e = mib_alloc(work);
    if (!e)
    {
        SnmpAgent.ok = PROTO_FALSE;
        return;
    }
    e->val.type = (uint8_t)SNMP_TAG_BER_INTEGER;
    e->val.ival = SnmpAgent.object.ival;
    e->setter = SnmpAgent.object.setter;
    SnmpAgent.ok = PROTO_TRUE;
}

static void add_dynamic(uint8_t *restrict work)
{
    SnmpMibEntry *e = mib_alloc(work);
    if (!e)
    {
        SnmpAgent.ok = PROTO_FALSE;
        return;
    }
    e->val.type = SnmpAgent.object.type;
    e->getter = SnmpAgent.object.getter;
    SnmpAgent.ok = PROTO_TRUE;
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
static void set_system(uint8_t *restrict work)
{
    static const uint32_t o_descr[] = {1, 3, 6, 1, 2, 1, 1, 1, 0};
    static const uint32_t o_oid[] = {1, 3, 6, 1, 2, 1, 1, 2, 0};
    static const uint32_t o_uptime[] = {1, 3, 6, 1, 2, 1, 1, 3, 0};
    static const uint32_t o_contact[] = {1, 3, 6, 1, 2, 1, 1, 4, 0};
    static const uint32_t o_name[] = {1, 3, 6, 1, 2, 1, 1, 5, 0};
    static const uint32_t o_loc[] = {1, 3, 6, 1, 2, 1, 1, 6, 0};
    static const uint32_t o_svc[] = {1, 3, 6, 1, 2, 1, 1, 7, 0};

    const char *descr = SnmpAgent.system.descr;
    const char *contact = SnmpAgent.system.contact;
    const char *name = SnmpAgent.system.name;
    const char *location = SnmpAgent.system.location;
    const long services = SnmpAgent.system.services;

    SnmpAgent.object.setter = NULL;
    SnmpAgent.object.oid = o_descr;
    SnmpAgent.object.oid_len = 9;
    SnmpAgent.object.text = descr;
    add_string(work);

    SnmpAgent.object.oid = o_oid;
    SnmpAgent.object.oid_len = 9;
    SnmpMibEntry *e = mib_alloc(work);
    if (e)
    {
        e->val.type = (uint8_t)SNMP_TAG_BER_OID;
        e->val.oid = g_sys_object_id;
        e->val.oid_len = sizeof(g_sys_object_id) / sizeof(g_sys_object_id[0]);
    }

    SnmpAgent.object.oid = o_uptime;
    SnmpAgent.object.oid_len = 9;
    SnmpAgent.object.type = (uint8_t)SNMP_TAG_SNMP_TIMETICKS;
    SnmpAgent.object.getter = sys_uptime_get;
    add_dynamic(work);
    SnmpAgent.object.getter = NULL;

    SnmpAgent.object.oid = o_contact;
    SnmpAgent.object.oid_len = 9;
    SnmpAgent.object.text = contact;
    add_string(work);

    SnmpAgent.object.oid = o_name;
    SnmpAgent.object.oid_len = 9;
    SnmpAgent.object.text = name;
    add_string(work);

    SnmpAgent.object.oid = o_loc;
    SnmpAgent.object.oid_len = 9;
    SnmpAgent.object.text = location;
    add_string(work);

    SnmpAgent.object.oid = o_svc;
    SnmpAgent.object.oid_len = 9;
    SnmpAgent.object.ival = services;
    add_integer(work);
}

// ---------------------------------------------------------------------------
// Value encode and decode. Both run over the caller's cursor and a value, and
// touch no module state.
// ---------------------------------------------------------------------------

static void enc_value(BerEnc *e, const SnmpValue *v)
{
    SnmpBerV.enc = e;
    switch (v->type)
    {
    case (uint8_t)SNMP_TAG_BER_INTEGER:
        SnmpBerV.tlv.ival = v->ival;
        SnmpBer.put_integer(snmp_ber_work);
        break;
    case (uint8_t)SNMP_TAG_BER_OCTET_STRING:
    case (uint8_t)SNMP_TAG_SNMP_OPAQUE:
        SnmpBerV.tlv.tag = v->type;
        SnmpBerV.tlv.bytes = (const uint8_t *)v->str;
        SnmpBerV.tlv.len = v->str_len;
        SnmpBer.put_octet_string(snmp_ber_work);
        break;
    case (uint8_t)SNMP_TAG_BER_OID:
        SnmpBerV.tlv.arcs = v->oid;
        SnmpBerV.tlv.arc_count = v->oid_len;
        SnmpBer.put_oid(snmp_ber_work);
        break;
    case (uint8_t)SNMP_TAG_SNMP_TIMETICKS:
    case (uint8_t)SNMP_TAG_SNMP_COUNTER32:
    case (uint8_t)SNMP_TAG_SNMP_GAUGE32:
        SnmpBerV.tlv.tag = v->type;
        SnmpBerV.tlv.uval = v->uval;
        SnmpBer.put_uint(snmp_ber_work);
        break;
    case (uint8_t)SNMP_TAG_SNMP_IPADDRESS: {
        // RFC 2578 sec 7.1.5: an OCTET STRING of length 4, network byte order.
        uint8_t ip[4] = {(uint8_t)(v->uval >> 24), (uint8_t)(v->uval >> 16), (uint8_t)(v->uval >> 8),
                         (uint8_t)(v->uval)};
        SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_SNMP_IPADDRESS;
        SnmpBerV.tlv.bytes = ip;
        SnmpBerV.tlv.len = 4;
        SnmpBer.put_octet_string(snmp_ber_work);
        break;
    }
    case (uint8_t)SNMP_TAG_BER_NULL:
        SnmpBer.put_null(snmp_ber_work);
        break;
    default:
        // A VarBind exception marker (RFC 3416 sec 3): the tag with a zero-length value.
        SnmpBerV.tlv.tag = v->type;
        SnmpBerV.tlv.bytes = NULL;
        SnmpBerV.tlv.len = 0;
        SnmpBer.put_tlv(snmp_ber_work);
        break;
    }
}

// One binding value at the decoder cursor into v, with an OBJECT IDENTIFIER value landing in
// oidbuf. Leaves the cursor past the value.
static proto_bool dec_value(BerDec *d, SnmpValue *v, uint32_t *oidbuf)
{
    mem.set(v, 0, sizeof(*v));
    size_t save = d->pos;
    SnmpBerV.dec = d;
    SnmpBer.read_header(snmp_ber_work);
    if (!SnmpBerV.ok)
    {
        return PROTO_FALSE;
    }
    const uint8_t tag = SnmpBerV.tag;
    const size_t len = SnmpBerV.vlen;
    v->type = tag;
    switch (tag)
    {
    case (uint8_t)SNMP_TAG_BER_INTEGER:
        d->pos = save;
        SnmpBer.read_integer(snmp_ber_work);
        v->ival = SnmpBerV.ival;
        return SnmpBerV.ok;
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
        SnmpBerV.read_args.arc_out = oidbuf;
        SnmpBerV.read_args.arc_cap = SNMP_MAX_OID_LEN;
        SnmpBer.read_oid(snmp_ber_work);
        if (!SnmpBerV.ok)
        {
            return PROTO_FALSE;
        }
        v->oid_len = SnmpBerV.n;
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
static void apply_set_all(uint8_t *restrict work, size_t nvb, proto_bool v2c, long *err_status, long *err_index)
{
    for (size_t i = 0; i < nvb; i++)
    {
        const SnmpMibEntry *en =
            mib_find_exact(work, SNMP_AGENT_CTX(work)->in[i].oid, SNMP_AGENT_CTX(work)->in[i].oid_len);
        int e = 0;
        if (!en)
        {
            e = (int)SNMP_ERR_NO_SUCH_NAME;
        }
        else if (!en->setter)
        {
            e = (!v2c) ? (int)SNMP_ERR_READ_ONLY : (int)SNMP_ERR_NOT_WRITABLE;
        }
        else if (!en->setter(&SNMP_AGENT_CTX(work)->in[i].val))
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
static size_t encode_response(uint8_t *restrict work, long request_id, long err_status, long err_index, size_t nout,
                              uint8_t *buf, size_t cap)
{
    BerEnc e;
    SnmpBerV.enc = &e;
    SnmpBerV.buf.out = buf;
    SnmpBerV.buf.cap = cap;
    SnmpBer.enc_init(snmp_ber_work);
    SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_SNMP_PDU_RESPONSE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t pdu = SnmpBerV.tlv.token;
    SnmpBerV.tlv.ival = request_id;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.tlv.ival = err_status;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.tlv.ival = err_index;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t vbl = SnmpBerV.tlv.token;
    for (size_t i = 0; i < nout; i++)
    {
        SnmpBerV.enc = &e;
        SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
        SnmpBer.seq_begin(snmp_ber_work);
        const size_t vb = SnmpBerV.tlv.token;
        SnmpBerV.tlv.arcs = SNMP_AGENT_CTX(work)->out[i].oid;
        SnmpBerV.tlv.arc_count = SNMP_AGENT_CTX(work)->out[i].oid_len;
        SnmpBer.put_oid(snmp_ber_work);
        enc_value(&e, &SNMP_AGENT_CTX(work)->out[i].val);
        SnmpBerV.enc = &e;
        SnmpBerV.tlv.token = vb;
        SnmpBer.seq_end(snmp_ber_work);
    }
    SnmpBerV.enc = &e;
    SnmpBerV.tlv.token = vbl;
    SnmpBer.seq_end(snmp_ber_work);
    SnmpBerV.tlv.token = pdu;
    SnmpBer.seq_end(snmp_ber_work);
    return e.ok ? e.len : 0;
}

// Decode the VarBindList of the request into the input scratch. Reports the binding count, or
// SNMP_MAX_VARBINDS + 1 when the list is malformed or longer than the scratch holds.
static size_t decode_varbinds(uint8_t *restrict work, BerDec *d, size_t vbl_end)
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
        SnmpBerV.dec = d;
        SnmpBer.read_header(snmp_ber_work);
        if (!SnmpBerV.ok || SnmpBerV.tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
        {
            return SNMP_MAX_VARBINDS + 1;
        }
        SnmpBerV.read_args.arc_out = SNMP_AGENT_CTX(work)->in[nvb].oid;
        SnmpBerV.read_args.arc_cap = SNMP_MAX_OID_LEN;
        SnmpBer.read_oid(snmp_ber_work);
        if (!SnmpBerV.ok)
        {
            return SNMP_MAX_VARBINDS + 1;
        }
        SNMP_AGENT_CTX(work)->in[nvb].oid_len = SnmpBerV.n;
        if (!dec_value(d, &SNMP_AGENT_CTX(work)->in[nvb].val, SNMP_AGENT_CTX(work)->in[nvb].valoid))
        {
            return SNMP_MAX_VARBINDS + 1;
        }
        nvb++;
    }
    return d->ok ? nvb : (SNMP_MAX_VARBINDS + 1);
}

// Copy the request bindings into the response unchanged, which is what a v1 error report and every
// SetRequest-PDU response carry (RFC 1157 sec 4.1.1, RFC 3416 sec 4.2.5).
static void echo_varbinds(uint8_t *restrict work, size_t nvb)
{
    for (size_t k = 0; k < nvb; k++)
    {
        SNMP_AGENT_CTX(work)->out[k].oid = SNMP_AGENT_CTX(work)->in[k].oid;
        SNMP_AGENT_CTX(work)->out[k].oid_len = SNMP_AGENT_CTX(work)->in[k].oid_len;
        SNMP_AGENT_CTX(work)->out[k].val = SNMP_AGENT_CTX(work)->in[k].val;
    }
}

// GetRequest-PDU (RFC 3416 sec 4.2.1) and GetNextRequest-PDU (sec 4.2.2). Reports the binding
// count written; a v1 name failure stops the walk and reports through err_status/err_index.
static size_t run_get(uint8_t *restrict work, uint8_t pdu_tag, size_t nvb, proto_bool v2c, long *err_status,
                      long *err_index)
{
    const proto_bool is_next = (pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_GETNEXT);
    size_t nout = 0;
    for (size_t i = 0; i < nvb; i++)
    {
        const SnmpMibEntry *en =
            is_next ? mib_find_next(work, SNMP_AGENT_CTX(work)->in[i].oid, SNMP_AGENT_CTX(work)->in[i].oid_len)
                    : mib_find_exact(work, SNMP_AGENT_CTX(work)->in[i].oid, SNMP_AGENT_CTX(work)->in[i].oid_len);
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
                echo_varbinds(work, nvb);
                return nvb;
            }
            // RFC 3416 sec 4.2.1 and sec 4.2.2: the exception rides in the binding's value. A Get
            // separates a missing instance of a known object from an unknown object; a Next past
            // the last name in the view is always endOfMibView.
            if (!is_next)
            {
                proto_bool inst =
                    en || mib_object_exists(work, SNMP_AGENT_CTX(work)->in[i].oid, SNMP_AGENT_CTX(work)->in[i].oid_len);
                val.type = inst ? (uint8_t)SNMP_TAG_SNMP_NO_SUCH_INSTANCE : (uint8_t)SNMP_TAG_SNMP_NO_SUCH_OBJECT;
            }
            else
            {
                val.type = (uint8_t)SNMP_TAG_SNMP_END_OF_MIB_VIEW;
            }
            SNMP_AGENT_CTX(work)->out[nout].oid = en ? en->oid : SNMP_AGENT_CTX(work)->in[i].oid;
            SNMP_AGENT_CTX(work)->out[nout].oid_len = en ? en->oid_len : SNMP_AGENT_CTX(work)->in[i].oid_len;
            SNMP_AGENT_CTX(work)->out[nout].val = val;
            nout++;
            continue;
        }
        // A Next answers under the name it found; a Get answers under the name it was asked.
        SNMP_AGENT_CTX(work)->out[nout].oid = is_next ? en->oid : SNMP_AGENT_CTX(work)->in[i].oid;
        SNMP_AGENT_CTX(work)->out[nout].oid_len = is_next ? en->oid_len : SNMP_AGENT_CTX(work)->in[i].oid_len;
        SNMP_AGENT_CTX(work)->out[nout].val = val;
        nout++;
    }
    return nout;
}

// GetBulkRequest-PDU (RFC 3416 sec 4.2.3): the first non-repeaters bindings get one successor
// each, and each remaining binding is walked for up to max-repetitions successors. Reports the
// binding count written.
static size_t run_bulk(uint8_t *restrict work, size_t nvb, long non_repeaters, long max_repetitions)
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
        const SnmpMibEntry *en =
            mib_find_next(work, SNMP_AGENT_CTX(work)->in[i].oid, SNMP_AGENT_CTX(work)->in[i].oid_len);
        if (en)
        {
            SNMP_AGENT_CTX(work)->out[nout].oid = en->oid;
            SNMP_AGENT_CTX(work)->out[nout].oid_len = en->oid_len;
            fetch_value(en, &SNMP_AGENT_CTX(work)->out[nout].val);
        }
        else
        {
            SNMP_AGENT_CTX(work)->out[nout].oid = SNMP_AGENT_CTX(work)->in[i].oid;
            SNMP_AGENT_CTX(work)->out[nout].oid_len = SNMP_AGENT_CTX(work)->in[i].oid_len;
            SNMP_AGENT_CTX(work)->out[nout].val.type = (uint8_t)SNMP_TAG_SNMP_END_OF_MIB_VIEW;
        }
        nout++;
    }

    const size_t nrep = nvb - (size_t)non_rep;
    for (size_t r = 0; r < nrep; r++)
    {
        SNMP_AGENT_CTX(work)->cur_oid[r] = SNMP_AGENT_CTX(work)->in[non_rep + r].oid;
        SNMP_AGENT_CTX(work)->cur_len[r] = SNMP_AGENT_CTX(work)->in[non_rep + r].oid_len;
        SNMP_AGENT_CTX(work)->ended[r] = PROTO_FALSE;
    }
    for (long rep = 0; rep < max_rep && nout < SNMP_MAX_VARBINDS; rep++)
    {
        for (size_t r = 0; r < nrep && nout < SNMP_MAX_VARBINDS; r++)
        {
            if (SNMP_AGENT_CTX(work)->ended[r])
            {
                SNMP_AGENT_CTX(work)->out[nout].oid = SNMP_AGENT_CTX(work)->cur_oid[r];
                SNMP_AGENT_CTX(work)->out[nout].oid_len = SNMP_AGENT_CTX(work)->cur_len[r];
                SNMP_AGENT_CTX(work)->out[nout].val.type = (uint8_t)SNMP_TAG_SNMP_END_OF_MIB_VIEW;
                nout++;
                continue;
            }
            const SnmpMibEntry *en =
                mib_find_next(work, SNMP_AGENT_CTX(work)->cur_oid[r], SNMP_AGENT_CTX(work)->cur_len[r]);
            if (en)
            {
                SNMP_AGENT_CTX(work)->out[nout].oid = en->oid;
                SNMP_AGENT_CTX(work)->out[nout].oid_len = en->oid_len;
                fetch_value(en, &SNMP_AGENT_CTX(work)->out[nout].val);
                SNMP_AGENT_CTX(work)->cur_oid[r] = en->oid;
                SNMP_AGENT_CTX(work)->cur_len[r] = en->oid_len;
            }
            else
            {
                SNMP_AGENT_CTX(work)->out[nout].oid = SNMP_AGENT_CTX(work)->cur_oid[r];
                SNMP_AGENT_CTX(work)->out[nout].oid_len = SNMP_AGENT_CTX(work)->cur_len[r];
                SNMP_AGENT_CTX(work)->out[nout].val.type = (uint8_t)SNMP_TAG_SNMP_END_OF_MIB_VIEW;
                SNMP_AGENT_CTX(work)->ended[r] = PROTO_TRUE;
            }
            nout++;
        }
    }
    return nout;
}

// One request PDU against the MIB, answered by one Response-PDU (RFC 3416 sec 4.2). The v1 and
// v2c community framing and the v3 USM layer both arrive here.
static void dispatch_pdu(uint8_t *restrict work)
{
    const uint8_t *pdu = SnmpAgent.pdu.req;
    const size_t pdu_len = SnmpAgent.pdu.req_len;
    const proto_bool allow_write = SnmpAgent.pdu.allow_write;
    const proto_bool v2c = SnmpAgent.pdu.v2c;
    uint8_t *out = SnmpAgent.pdu.out;
    const size_t out_cap = SnmpAgent.pdu.out_cap;
    SnmpAgent.n = 0;

    BerDec d;
    SnmpBerV.dec = &d;
    SnmpBerV.buf.in = pdu;
    SnmpBerV.buf.cap = pdu_len;
    SnmpBer.dec_init(snmp_ber_work);

    SnmpBer.read_header(snmp_ber_work);
    if (!SnmpBerV.ok)
    {
        return;
    }
    const uint8_t pdu_tag = SnmpBerV.tag;

    // Positions 2 and 3: error-status and error-index in a PDU, non-repeaters and max-repetitions
    // in a BulkPDU (RFC 3416 sec 3).
    SnmpBer.read_integer(snmp_ber_work);
    if (!SnmpBerV.ok)
    {
        return;
    }
    const long request_id = SnmpBerV.ival;
    SnmpBer.read_integer(snmp_ber_work);
    if (!SnmpBerV.ok)
    {
        return;
    }
    const long field2 = SnmpBerV.ival;
    SnmpBer.read_integer(snmp_ber_work);
    if (!SnmpBerV.ok)
    {
        return;
    }
    const long field3 = SnmpBerV.ival;

    SnmpBer.read_header(snmp_ber_work);
    if (!SnmpBerV.ok || SnmpBerV.tag != (uint8_t)SNMP_TAG_BER_SEQUENCE)
    {
        return;
    }
    const size_t vbl_end = d.pos + SnmpBerV.vlen;

    const size_t nvb = decode_varbinds(work, &d, vbl_end);
    if (nvb > SNMP_MAX_VARBINDS)
    {
        return;
    }

    long err_status = (int)SNMP_ERR_NO_ERROR;
    long err_index = 0;
    size_t nout = 0;

    if (pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_GET || pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_GETNEXT)
    {
        nout = run_get(work, pdu_tag, nvb, v2c, &err_status, &err_index);
    }
    else if (pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_GETBULK)
    {
        if (!v2c) // the GetBulkRequest-PDU is SNMPv2c and later (RFC 3416 sec 4.2.3)
        {
            return;
        }
        nout = run_bulk(work, nvb, field2, field3);
    }
    else if (pdu_tag == (uint8_t)SNMP_TAG_SNMP_PDU_SET)
    {
        // A SetRequest-PDU reports through error-status in both framings, and its response repeats
        // the request's bindings (RFC 3416 sec 4.2.5).
        nout = nvb;
        echo_varbinds(work, nvb);
        if (!allow_write)
        {
            err_status = (!v2c) ? (int)SNMP_ERR_NO_SUCH_NAME : (int)SNMP_ERR_NO_ACCESS;
            err_index = 1;
        }
        else
        {
            apply_set_all(work, nvb, v2c, &err_status, &err_index);
        }
    }
    else
    {
        return; // a PDU this responder does not answer
    }

    size_t n = encode_response(work, request_id, err_status, err_index, nout, out, out_cap);
    if (n == 0 && nout > 0)
    {
        // RFC 3416 sec 4.2.1: a response too large for its buffer is answered tooBig with an empty
        // VarBindList.
        n = encode_response(work, request_id, (int)SNMP_ERR_TOO_BIG, 0, 0, out, out_cap);
    }
    SnmpAgent.n = n;
}

// ---------------------------------------------------------------------------
// Message framing: RFC 1157 sec 4 for v1, RFC 1901 sec 3 for v2c. A v3 message
// is the USM layer's.
// ---------------------------------------------------------------------------

static void agent_process(uint8_t *restrict work)
{
    const uint8_t *req = SnmpAgent.msg.req;
    const size_t req_len = SnmpAgent.msg.req_len;
    uint8_t *resp = SnmpAgent.msg.resp;
    const size_t resp_cap = SnmpAgent.msg.resp_cap;
    SnmpAgent.n = 0;

    BerDec d;
    SnmpBerV.dec = &d;
    SnmpBerV.buf.in = req;
    SnmpBerV.buf.cap = req_len;
    SnmpBer.dec_init(snmp_ber_work);

    SnmpBer.read_header(snmp_ber_work);
    if (!SnmpBerV.ok || SnmpBerV.tag != (uint8_t)SNMP_TAG_BER_SEQUENCE) // the message wrapper
    {
        return;
    }

    SnmpBer.read_integer(snmp_ber_work);
    if (!SnmpBerV.ok)
    {
        return;
    }
    const long version = SnmpBerV.ival;

    if (version == (int)SNMP_V3)
    {
#if PROTOCORE_ENABLE_SNMP_V3
        SnmpV3V.msg.req = req;
        SnmpV3V.msg.req_len = req_len;
        SnmpV3V.msg.resp = resp;
        SnmpV3V.msg.resp_cap = resp_cap;
        SnmpV3.process(protocore_snmp_v3_span());
        SnmpAgent.n = SnmpV3V.n;
#endif
        return; // without the USM layer a v3 message is answered with nothing
    }
    if (version != (int)SNMP_V1 && version != (int)SNMP_V2C)
    {
        return;
    }

    SnmpBer.read_header(snmp_ber_work);
    if (!SnmpBerV.ok || SnmpBerV.tag != (uint8_t)SNMP_TAG_BER_OCTET_STRING)
    {
        return;
    }
    const char *community = (const char *)(d.buf + d.pos);
    const size_t community_len = SnmpBerV.vlen;
    d.pos += community_len;

    // RFC 1157 sec 3.2.5: the message is authentic when it names a community it belongs to. The
    // read-write community also authorizes reads.
    const proto_bool is_rw =
        SNMP_AGENT_CTX(work)->rw_set && community_match(SNMP_AGENT_CTX(work)->rw, community, community_len);
    const proto_bool is_ro = is_rw || community_match(SNMP_AGENT_CTX(work)->ro, community, community_len);
    if (!is_ro)
    {
        return; // RFC 1157 sec 4.1: an authentication failure discards the datagram
    }

    // The PDU is the rest of the datagram: dispatch it, then wrap the response in the same framing.
    const size_t pdu_off = d.pos;
    SnmpAgent.pdu.req = req + pdu_off;
    SnmpAgent.pdu.req_len = req_len - pdu_off;
    SnmpAgent.pdu.allow_write = is_rw;
    SnmpAgent.pdu.v2c = (version == (int)SNMP_V2C);
    SnmpAgent.pdu.out = SNMP_AGENT_CTX(work)->pdu_stage;
    SnmpAgent.pdu.out_cap = sizeof(SNMP_AGENT_CTX(work)->pdu_stage);
    dispatch_pdu(work);
    const size_t pn = SnmpAgent.n;
    SnmpAgent.n = 0;
    if (pn == 0)
    {
        return;
    }

    BerEnc e;
    SnmpBerV.enc = &e;
    SnmpBerV.buf.out = resp;
    SnmpBerV.buf.cap = resp_cap;
    SnmpBer.enc_init(snmp_ber_work);
    SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_BER_SEQUENCE;
    SnmpBer.seq_begin(snmp_ber_work);
    const size_t msg = SnmpBerV.tlv.token;
    SnmpBerV.tlv.ival = version;
    SnmpBer.put_integer(snmp_ber_work);
    SnmpBerV.tlv.tag = (uint8_t)SNMP_TAG_BER_OCTET_STRING;
    SnmpBerV.tlv.bytes = (const uint8_t *)community;
    SnmpBerV.tlv.len = community_len;
    SnmpBer.put_octet_string(snmp_ber_work);
    SnmpBerV.tlv.bytes = SNMP_AGENT_CTX(work)->pdu_stage;
    SnmpBerV.tlv.len = pn;
    SnmpBer.put_raw(snmp_ber_work);
    SnmpBerV.tlv.token = msg;
    SnmpBer.seq_end(snmp_ber_work);
    SnmpAgent.n = e.ok ? e.len : 0;
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
    SnmpAgent.msg.resp = SNMP_AGENT_CTX(protocore_snmp_agent_span())->tx;
    SnmpAgent.msg.resp_cap = sizeof(((struct SnmpAgentStorage *)0)->tx);
    agent_process(protocore_snmp_agent_span());
    if (SnmpAgent.n == 0)
    {
        return;
    }
    UdpListenerV.peer_args.peer = peer;
    UdpListenerV.send_args.data = SNMP_AGENT_CTX(protocore_snmp_agent_span())->tx;
    UdpListenerV.send_args.len = SnmpAgent.n;
    UdpListener.reply(protocore_udp_listener_span());
}
#endif // PROTOCORE_HAS_NET_STACK

static void agent_listen(uint8_t *restrict work)
{
    (void)work;
#if PROTOCORE_HAS_NET_STACK
    UdpListenerV.port = SnmpAgent.port;
    UdpListenerV.bind.handler = snmp_udp_handler;
    UdpListenerV.bind.handler_ctx = NULL;
    UdpListener.listen(protocore_udp_listener_span());
    SnmpAgent.ok = UdpListenerV.ok;
#else
    SnmpAgent.ok = PROTO_FALSE; // no transport in this build
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
                         .listen = agent_listen};

#endif // PROTOCORE_ENABLE_SNMP
