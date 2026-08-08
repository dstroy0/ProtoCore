// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file vxi11.c
 * @brief VXI-11 codec over ONC RPC / XDR (pure, host-tested).
 */

#include "services/instrumentation/vxi11/vxi11.h"

#if PC_ENABLE_VXI11

#include "mmgr/endian.h"

static const uint32_t RPC_MSGTYPE_CALL = 0;
static const uint32_t RPC_MSGTYPE_REPLY = 1;

// ── XDR cursor helpers (big-endian, 4-byte aligned) ─────────────────────────────────────────────

typedef struct
{
    uint8_t *p;
    size_t cap;
    size_t off;
    proto_bool ok;
} XdrW;

static void xw_u32(XdrW *w, uint32_t v)
{
    if (!w->ok || w->off + 4 > w->cap)
    {
        w->ok = PROTO_FALSE;
        return;
    }
    pc_wr32be(w->p + w->off, v);
    w->off += 4;
}

// A length-prefixed opaque/string: 4-byte length, the bytes, then 0-3 zero pad bytes to a word.
static void xw_bytes(XdrW *w, const uint8_t *d, size_t n)
{
    size_t pad = (4 - (n & 3)) & 3;
    if (!w->ok || w->off + 4 + n + pad > w->cap)
    {
        w->ok = PROTO_FALSE;
        return;
    }
    pc_wr32be(w->p + w->off, (uint32_t)n);
    w->off += 4;
    if (n)
    {
        memcpy(w->p + w->off, d, n);
        w->off += n;
    }
    for (size_t i = 0; i < pad; i++)
    {
        w->p[w->off++] = 0;
    }
}

typedef struct
{
    const uint8_t *p;
    size_t len;
    size_t off;
    proto_bool ok;
} XdrR;

static uint32_t xr_u32(XdrR *r)
{
    if (!r->ok || r->off + 4 > r->len)
    {
        r->ok = PROTO_FALSE;
        return 0;
    }
    uint32_t v = pc_rd32be(r->p + r->off);
    r->off += 4;
    return v;
}

// Read a length-prefixed opaque; @p d/@p n may be null (to skip). Advances past the pad.
static proto_bool xr_bytes(XdrR *r, const uint8_t **d, size_t *n)
{
    uint32_t len = xr_u32(r);
    if (!r->ok)
    {
        return PROTO_FALSE;
    }
    size_t pad = (4 - (len & 3)) & 3;
    if (r->off + len + pad > r->len)
    {
        r->ok = PROTO_FALSE;
        return PROTO_FALSE;
    }
    if (d)
    {
        *d = r->p + r->off;
    }
    if (n)
    {
        *n = len;
    }
    r->off += len + pad;
    return PROTO_TRUE;
}

// ── ONC RPC framing ─────────────────────────────────────────────────────────────────────────────

size_t pc_rpc_record_mark(uint8_t *buf, size_t cap, uint32_t payload_len)
{
    if (!buf || cap < 4 || (payload_len & 0x80000000u)) // the length must fit in 31 bits
    {
        return 0;
    }
    pc_wr32be(buf, 0x80000000u | payload_len); // last-fragment flag set
    return 4;
}

proto_bool pc_rpc_parse_record_mark(const uint8_t *buf, size_t len, proto_bool *last, uint32_t *frag_len)
{
    if (!buf || len < 4)
    {
        return PROTO_FALSE;
    }
    uint32_t rm = pc_rd32be(buf);
    if (last)
    {
        *last = (rm & 0x80000000u) != 0;
    }
    if (frag_len)
    {
        *frag_len = rm & 0x7FFFFFFFu;
    }
    return PROTO_TRUE;
}

// Start a CALL: reserve 4 bytes for the record mark, then write the RPC call header (through the
// AUTH_NONE cred + verf). Procedure parameters are written next; finish_call backfills the mark.
static XdrW begin_call(uint8_t *buf, size_t cap, uint32_t xid, uint32_t prog, uint32_t vers, uint32_t proc)
{
    XdrW w = {buf, cap, 4, buf != NULL};
    xw_u32(&w, xid);
    xw_u32(&w, RPC_MSGTYPE_CALL);
    xw_u32(&w, 2); // rpcvers
    xw_u32(&w, prog);
    xw_u32(&w, vers);
    xw_u32(&w, proc);
    xw_u32(&w, PC_RPC_AUTH_NONE); // cred.flavor
    xw_u32(&w, 0);                // cred.length
    xw_u32(&w, PC_RPC_AUTH_NONE); // verf.flavor
    xw_u32(&w, 0);                // verf.length
    return w;
}

static size_t finish_call(XdrW *w)
{
    if (!w->ok)
    {
        return 0;
    }
    pc_rpc_record_mark(w->p, w->cap, (uint32_t)(w->off - 4));
    return w->off;
}

proto_bool pc_rpc_parse_reply(const uint8_t *rpc, size_t len, uint32_t *xid, uint32_t *accept_stat, size_t *result_off)
{
    if (!rpc)
    {
        return PROTO_FALSE;
    }
    XdrR r = {rpc, len, 0, PROTO_TRUE};
    uint32_t x = xr_u32(&r);
    uint32_t mtype = xr_u32(&r);
    uint32_t reply_stat = xr_u32(&r);
    xr_u32(&r);               // verf.flavor
    xr_bytes(&r, NULL, NULL); // verf.body
    uint32_t astat = xr_u32(&r);
    if (!r.ok || mtype != RPC_MSGTYPE_REPLY || reply_stat != PC_RPC_MSG_ACCEPTED)
    {
        return PROTO_FALSE;
    }
    if (xid)
    {
        *xid = x;
    }
    if (accept_stat)
    {
        *accept_stat = astat;
    }
    if (result_off)
    {
        *result_off = r.off;
    }
    return PROTO_TRUE;
}

// Parse the reply header and position a reader at the results; returns false unless SUCCESS.
static proto_bool reply_results(const uint8_t *rpc, size_t len, XdrR *r)
{
    uint32_t astat = 0;
    size_t off = 0;
    if (!pc_rpc_parse_reply(rpc, len, NULL, &astat, &off) || astat != PC_RPC_ACCEPT_SUCCESS)
    {
        return PROTO_FALSE;
    }
    r->p = rpc;
    r->len = len;
    r->off = off;
    r->ok = PROTO_TRUE;
    return PROTO_TRUE;
}

// ── portmapper ──────────────────────────────────────────────────────────────────────────────────

size_t pc_vxi11_build_getport(uint8_t *buf, size_t cap, uint32_t xid, uint32_t prog, uint32_t vers, uint32_t proto)
{
    XdrW w = begin_call(buf, cap, xid, PC_RPC_PMAP_PROG, PC_RPC_PMAP_VERS, PC_RPC_PMAP_GETPORT);
    xw_u32(&w, prog); // mapping fields in order: prog, vers, prot, then port zero
    xw_u32(&w, vers);
    xw_u32(&w, proto);
    xw_u32(&w, 0);
    return finish_call(&w);
}

proto_bool pc_vxi11_parse_getport_resp(const uint8_t *rpc, size_t len, uint32_t *port)
{
    XdrR r;
    if (!reply_results(rpc, len, &r))
    {
        return PROTO_FALSE;
    }
    uint32_t p = xr_u32(&r);
    if (!r.ok)
    {
        return PROTO_FALSE;
    }
    if (port)
    {
        *port = p;
    }
    return PROTO_TRUE;
}

// ── VXI-11 DEVICE_CORE ──────────────────────────────────────────────────────────────────────────

size_t pc_vxi11_build_create_link(uint8_t *buf, size_t cap, uint32_t xid, int32_t client_id, proto_bool lock_device,
                                  uint32_t lock_timeout, const char *device)
{
    XdrW w = begin_call(buf, cap, xid, PC_VXI11_CORE_PROG, PC_VXI11_CORE_VERS, (uint32_t)VXI11_PROC_CREATE_LINK);
    xw_u32(&w, (uint32_t)client_id);
    xw_u32(&w, lock_device ? 1 : 0);
    xw_u32(&w, lock_timeout);
    size_t dlen = device ? strnlen(device, cap) : 0;
    xw_bytes(&w, (const uint8_t *)device, dlen);
    return finish_call(&w);
}

proto_bool pc_vxi11_parse_create_link_resp(const uint8_t *rpc, size_t len, Vxi11CreateLinkResp *out)
{
    XdrR r;
    if (!out || !reply_results(rpc, len, &r))
    {
        return PROTO_FALSE;
    }
    out->error = (int32_t)xr_u32(&r);
    out->lid = (int32_t)xr_u32(&r);
    out->abort_port = xr_u32(&r);
    out->max_recv_size = xr_u32(&r);
    return r.ok;
}

size_t pc_vxi11_build_device_write(uint8_t *buf, size_t cap, uint32_t xid, int32_t lid, uint32_t io_timeout,
                                   uint32_t lock_timeout, uint32_t flags, const uint8_t *data, size_t data_len)
{
    if (data_len && !data)
    {
        return 0;
    }
    XdrW w = begin_call(buf, cap, xid, PC_VXI11_CORE_PROG, PC_VXI11_CORE_VERS, (uint32_t)VXI11_PROC_DEVICE_WRITE);
    xw_u32(&w, (uint32_t)lid);
    xw_u32(&w, io_timeout);
    xw_u32(&w, lock_timeout);
    xw_u32(&w, flags);
    xw_bytes(&w, data, data_len);
    return finish_call(&w);
}

proto_bool pc_vxi11_parse_write_resp(const uint8_t *rpc, size_t len, Vxi11WriteResp *out)
{
    XdrR r;
    if (!out || !reply_results(rpc, len, &r))
    {
        return PROTO_FALSE;
    }
    out->error = (int32_t)xr_u32(&r);
    out->size = xr_u32(&r);
    return r.ok;
}

size_t pc_vxi11_build_device_read(uint8_t *buf, size_t cap, uint32_t xid, int32_t lid, uint32_t request_size,
                                  uint32_t io_timeout, uint32_t lock_timeout, uint32_t flags, uint8_t term_char)
{
    XdrW w = begin_call(buf, cap, xid, PC_VXI11_CORE_PROG, PC_VXI11_CORE_VERS, (uint32_t)VXI11_PROC_DEVICE_READ);
    xw_u32(&w, (uint32_t)lid);
    xw_u32(&w, request_size);
    xw_u32(&w, io_timeout);
    xw_u32(&w, lock_timeout);
    xw_u32(&w, flags);
    xw_u32(&w, term_char); // char is a full 4-byte XDR word
    return finish_call(&w);
}

proto_bool pc_vxi11_parse_read_resp(const uint8_t *rpc, size_t len, Vxi11ReadResp *out)
{
    XdrR r;
    if (!out || !reply_results(rpc, len, &r))
    {
        return PROTO_FALSE;
    }
    out->error = (int32_t)xr_u32(&r);
    out->reason = (int32_t)xr_u32(&r);
    out->data = NULL;
    out->data_len = 0;
    xr_bytes(&r, &out->data, &out->data_len);
    return r.ok;
}

size_t pc_vxi11_build_device_readstb(uint8_t *buf, size_t cap, uint32_t xid, int32_t lid, uint32_t flags,
                                     uint32_t lock_timeout, uint32_t io_timeout)
{
    XdrW w = begin_call(buf, cap, xid, PC_VXI11_CORE_PROG, PC_VXI11_CORE_VERS, (uint32_t)VXI11_PROC_DEVICE_READSTB);
    xw_u32(&w, (uint32_t)lid); // Device_GenericParms fields in order: lid, flags, lock_timeout, io_timeout
    xw_u32(&w, flags);
    xw_u32(&w, lock_timeout);
    xw_u32(&w, io_timeout);
    return finish_call(&w);
}

proto_bool pc_vxi11_parse_readstb_resp(const uint8_t *rpc, size_t len, Vxi11ReadStbResp *out)
{
    XdrR r;
    if (!out || !reply_results(rpc, len, &r))
    {
        return PROTO_FALSE;
    }
    out->error = (int32_t)xr_u32(&r);
    out->stb = (uint8_t)xr_u32(&r); // stb is a full 4-byte XDR word; the value is the low byte
    return r.ok;
}

size_t pc_vxi11_build_device_clear(uint8_t *buf, size_t cap, uint32_t xid, int32_t lid, uint32_t flags,
                                   uint32_t lock_timeout, uint32_t io_timeout)
{
    XdrW w = begin_call(buf, cap, xid, PC_VXI11_CORE_PROG, PC_VXI11_CORE_VERS, (uint32_t)VXI11_PROC_DEVICE_CLEAR);
    xw_u32(&w, (uint32_t)lid); // Device_GenericParms: lid, flags, lock_timeout, io_timeout
    xw_u32(&w, flags);
    xw_u32(&w, lock_timeout);
    xw_u32(&w, io_timeout);
    return finish_call(&w);
}

size_t pc_vxi11_build_device_trigger(uint8_t *buf, size_t cap, uint32_t xid, int32_t lid, uint32_t flags,
                                     uint32_t lock_timeout, uint32_t io_timeout)
{
    XdrW w = begin_call(buf, cap, xid, PC_VXI11_CORE_PROG, PC_VXI11_CORE_VERS, (uint32_t)VXI11_PROC_DEVICE_TRIGGER);
    xw_u32(&w, (uint32_t)lid); // Device_GenericParms: lid, flags, lock_timeout, io_timeout
    xw_u32(&w, flags);
    xw_u32(&w, lock_timeout);
    xw_u32(&w, io_timeout);
    return finish_call(&w);
}

size_t pc_vxi11_build_destroy_link(uint8_t *buf, size_t cap, uint32_t xid, int32_t lid)
{
    XdrW w = begin_call(buf, cap, xid, PC_VXI11_CORE_PROG, PC_VXI11_CORE_VERS, (uint32_t)VXI11_PROC_DESTROY_LINK);
    xw_u32(&w, (uint32_t)lid); // Device_Link
    return finish_call(&w);
}

proto_bool pc_vxi11_parse_error_resp(const uint8_t *rpc, size_t len, int32_t *error)
{
    XdrR r;
    if (!reply_results(rpc, len, &r))
    {
        return PROTO_FALSE;
    }
    int32_t e = (int32_t)xr_u32(&r);
    if (!r.ok)
    {
        return PROTO_FALSE;
    }
    if (error)
    {
        *error = e;
    }
    return PROTO_TRUE;
}

const char *pc_vxi11_error_str(int32_t error)
{
    switch (error)
    {
    case PC_VXI11_ERR_NONE:
        return "no error";
    case PC_VXI11_ERR_SYNTAX:
        return "syntax error";
    case PC_VXI11_ERR_NOT_ACCESSIBLE:
        return "device not accessible";
    case PC_VXI11_ERR_INVALID_LINK:
        return "invalid link identifier";
    case PC_VXI11_ERR_PARAMETER:
        return "parameter error";
    case 6:
        return "channel not established";
    case 8:
        return "operation not supported";
    case 9:
        return "out of resources";
    case 11:
        return "device locked by another link";
    case PC_VXI11_ERR_NO_LOCK:
        return "no lock held by this link";
    case PC_VXI11_ERR_IO_TIMEOUT:
        return "I/O timeout";
    case PC_VXI11_ERR_IO_ERROR:
        return "I/O error";
    case 21:
        return "invalid address";
    case PC_VXI11_ERR_ABORT:
        return "abort";
    case 29:
        return "channel already established";
    default:
        return "unknown error";
    }
}

#endif // PC_ENABLE_VXI11
