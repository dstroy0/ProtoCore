// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_ntrip_caster_listener.c
 * @brief Server-side NTRIP caster listener (see protocore_ntrip_caster_listener.h). Answers PROTO_NTRIP_CASTER rover
 *        requests via the pure protocore_ntrip_caster codec and fans RTCM corrections out to subscribed rovers.
 */

#include "services/timing_position/gnss/ntrip_caster_listener/ntrip_caster_listener.h"
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h"

#if PROTOCORE_ENABLE_NTRIP_CASTER

#include "network_drivers/session/session.h"                 // Session.proto->add: the handler registration
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the accepted slot
#include "network_drivers/transport/tcp/tcp.h"
#include "server/core/proto_handler.h"

// One published mountpoint on a listener.
typedef struct
{
    proto_bool active;
    uint8_t listener_id;
    char name[PROTOCORE_NTRIP_MOUNT_MAX];
    NtripMount cfg;       // source-table description (string fields referenced from the caller)
    const char *auth_b64; // required HTTP Basic credentials, or null for open access
} CasterMount;

// One rover connection: reading its request, then streaming a mountpoint.
typedef struct
{
    proto_bool active;
    proto_bool streaming;
    uint8_t conn_slot;
    int mount_idx; // index into s_ctx.mounts once streaming, else -1
    char req[PROTOCORE_NTRIP_REQ_MAX];
    uint16_t req_len;
} CasterRover;

typedef struct
{
    CasterMount mounts[PROTOCORE_NTRIP_MAX_MOUNTS];
    CasterRover rovers[PROTOCORE_NTRIP_MAX_ROVERS];
    proto_bool registered;
} NtripCasterCtx;
static NtripCasterCtx s_ctx;

static CasterRover *rover_by_conn(uint8_t slot)
{
    for (int i = 0; i < PROTOCORE_NTRIP_MAX_ROVERS; i++)
    {
        if (s_ctx.rovers[i].active && s_ctx.rovers[i].conn_slot == slot)
        {
            return &s_ctx.rovers[i];
        }
    }
    return NULL;
}

static int rover_find_free()
{
    for (int i = 0; i < PROTOCORE_NTRIP_MAX_ROVERS; i++)
    {
        if (!s_ctx.rovers[i].active)
        {
            return i;
        }
    }
    return -1;
}

// Find a mount by name, optionally constrained to a given listener (-1 = any).
static int mount_index(const char *name, int listener_id)
{
    const size_t nl = str.len(name, PROTOCORE_NTRIP_MOUNT_MAX);
    if (nl >= PROTOCORE_NTRIP_MOUNT_MAX)
    {
        return -1;
    }
    for (int i = 0; i < PROTOCORE_NTRIP_MAX_MOUNTS; i++)
    {
        if (!s_ctx.mounts[i].active)
        {
            continue;
        }
        if (listener_id >= 0 && s_ctx.mounts[i].listener_id != (uint8_t)listener_id)
        {
            continue;
        }
        if (str.eq(s_ctx.mounts[i].name, name, nl + 1, PROTO_FALSE))
        {
            return i;
        }
    }
    return -1;
}

// Count published mounts on a listener and write the matching NtripMount descriptors into out (for the
// source table). Returns the count (bounded by cap).
static size_t mounts_on_listener(uint8_t listener_id, NtripMount *out, size_t cap)
{
    size_t k = 0;
    for (int i = 0; i < PROTOCORE_NTRIP_MAX_MOUNTS && k < cap; i++)
    {
        if (s_ctx.mounts[i].active && s_ctx.mounts[i].listener_id == listener_id)
        {
            out[k++] = s_ctx.mounts[i].cfg;
        }
    }
    return k;
}

// Send a response and close the rover (a control reply is small; a single send is fine).
static void reply_and_close(CasterRover *r, const char *resp, size_t len)
{
    ConnPool.slot = r->conn_slot;
    ConnPool.active(protocore_conn_pool_span());
    if (len && ConnPool.ok)
    {
        ConnPool.slot = r->conn_slot;
        ConnPool.io.data = resp;
        ConnPool.io.len = (proto_u16)len;
        ConnPool.send(protocore_conn_pool_span());
    }
    r->active = PROTO_FALSE;
    ConnPool.slot = r->conn_slot;
    ConnPool.close(protocore_conn_pool_span());
}

// Constant-length credential compare (auth strings are short and app-configured).
static proto_bool auth_ok(const NtripRequest *req, const char *expect)
{
    if (!expect)
    {
        return PROTO_TRUE; // open access
    }
    if (!req->auth_b64)
    {
        return PROTO_FALSE;
    }
    size_t el = str.len(expect, req->auth_b64_len + 1);
    if (el != req->auth_b64_len)
    {
        return PROTO_FALSE;
    }
    return mem.cmp(req->auth_b64, expect, el) == 0;
}

static void serve_sourcetable(CasterRover *r, NtripVersion version)
{
    NtripMount list[PROTOCORE_NTRIP_MAX_MOUNTS];
    ConnPool.slot = r->conn_slot;
    ConnPool.listener_id(protocore_conn_pool_span());
    size_t nm = mounts_on_listener(ConnPool.u8, list, PROTOCORE_NTRIP_MAX_MOUNTS);
    char buf[PROTOCORE_NTRIP_REQ_MAX + 256];
    size_t n = protocore_ntrip_build_sourcetable(buf, sizeof(buf), version, list, nm);
    reply_and_close(r, buf, n);
}

// A completed request has been parsed; dispatch it.
static void dispatch(CasterRover *r, const NtripRequest *req)
{
    char buf[192];
    if (!req->is_get)
    {
        size_t n = protocore_ntrip_build_error_response(buf, sizeof(buf), req->version);
        reply_and_close(r, buf, n);
        return;
    }
    if (req->want_sourcetable)
    {
        serve_sourcetable(r, req->version);
        return;
    }
    ConnPool.slot = r->conn_slot;
    ConnPool.listener_id(protocore_conn_pool_span());
    int mi = mount_index(req->mountpoint, (int)ConnPool.u8);
    if (mi < 0)
    {
        serve_sourcetable(r, req->version); // unknown mount -> advertise the available ones
        return;
    }
    if (!auth_ok(req, s_ctx.mounts[mi].auth_b64))
    {
        size_t n = protocore_ntrip_build_unauthorized_response(buf, sizeof(buf), req->version);
        reply_and_close(r, buf, n);
        return;
    }
    size_t n = protocore_ntrip_build_stream_response(buf, sizeof(buf), req->version);
    ConnPool.slot = r->conn_slot;
    ConnPool.active(protocore_conn_pool_span());
    const proto_bool up = ConnPool.ok;
    ConnPool.slot = r->conn_slot;
    ConnPool.io.data = buf;
    ConnPool.io.len = (proto_u16)n;
    ConnPool.send(protocore_conn_pool_span());
    if (n == 0 || !up || !ConnPool.ok)
    {
        r->active = PROTO_FALSE;
        ConnPool.slot = r->conn_slot;
        ConnPool.close(protocore_conn_pool_span());
        return;
    }
    r->streaming = PROTO_TRUE;
    r->mount_idx = mi; // corrections for this mount now fan out to this rover
}

static void caster_on_accept(uint8_t slot)
{
    int idx = rover_find_free();
    if (idx < 0)
    {
        ConnPool.slot = slot;
        ConnPool.close(protocore_conn_pool_span()); // rover table full
        return;
    }
    CasterRover *r = &s_ctx.rovers[idx];
    r->active = PROTO_TRUE;
    r->streaming = PROTO_FALSE;
    r->conn_slot = slot;
    r->mount_idx = -1;
    r->req_len = 0;
}

static void caster_on_data(uint8_t slot)
{
    CasterRover *r = rover_by_conn(slot);
    if (!r)
    {
        ConnPool.slot = slot;
        ConnPool.close(protocore_conn_pool_span());
        return;
    }
    if (r->streaming)
    {
        // A streaming rover may send periodic GGA; the base already knows its position, so drain & ignore.
        uint8_t sink[64];
        for (;;)
        {
            ConnPool.slot = slot;
            ConnPool.available(protocore_conn_pool_span());
            if (ConnPool.n == 0)
            {
                break;
            }
            ConnPool.slot = slot;
            ConnPool.io.buf = sink;
            ConnPool.io.cap = sizeof(sink);
            ConnPool.read(protocore_conn_pool_span());
            if (ConnPool.n == 0)
            {
                break;
            }
        }
        return;
    }
    // Still reading the request: append what is available, then try to parse.
    for (;;)
    {
        ConnPool.slot = slot;
        ConnPool.available(protocore_conn_pool_span());
        if (ConnPool.n == 0 || r->req_len >= sizeof(r->req) - 1)
        {
            break;
        }
        ConnPool.slot = slot;
        ConnPool.io.buf = (uint8_t *)r->req + r->req_len;
        ConnPool.io.cap = sizeof(r->req) - 1 - r->req_len;
        ConnPool.read(protocore_conn_pool_span());
        if (ConnPool.n == 0)
        {
            break;
        }
        r->req_len += (uint16_t)ConnPool.n;
    }
    NtripRequest req;
    if (protocore_ntrip_request_parse(r->req, r->req_len, &req))
    {
        dispatch(r, &req);
        return;
    }
    if (r->req_len >= sizeof(r->req) - 1) // request too large without a header terminator
    {
        char buf[64];
        size_t n = protocore_ntrip_build_error_response(buf, sizeof(buf), NTRIP_V1);
        reply_and_close(r, buf, n);
    }
}

static void caster_on_close(uint8_t slot)
{
    CasterRover *r = rover_by_conn(slot);
    if (r)
    {
        r->active = PROTO_FALSE; // the transport owns the closing slot
    }
}

// Designated, so a member's position in the struct does not decide what it binds to. on_abort and
// on_poll are unset: a null on_abort falls back to on_close, and this protocol is not polled.
static const ProtoHandler s_caster_handler = {
    .on_accept = caster_on_accept, .on_data = caster_on_data, .on_close = caster_on_close};

proto_bool protocore_ntrip_caster_add_mount(uint8_t listener_id, const NtripMount *mount, const char *auth_b64)
{
    if (!mount || !mount->mountpoint)
    {
        return PROTO_FALSE;
    }
    size_t nl = str.len(mount->mountpoint, PROTOCORE_NTRIP_MOUNT_MAX + 1);
    if (nl == 0 || nl >= PROTOCORE_NTRIP_MOUNT_MAX)
    {
        return PROTO_FALSE;
    }
    int idx = -1;
    for (int i = 0; i < PROTOCORE_NTRIP_MAX_MOUNTS; i++)
    {
        if (!s_ctx.mounts[i].active)
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
    {
        return PROTO_FALSE;
    }
    CasterMount *m = &s_ctx.mounts[idx];
    m->active = PROTO_TRUE;
    m->listener_id = listener_id;
    mem.cpy(m->name, mount->mountpoint, nl + 1);
    m->cfg = *mount;
    m->cfg.mountpoint = m->name; // point the copied cfg at our owned name
    m->auth_b64 = auth_b64;
    if (!s_ctx.registered)
    {
        SessionV.proto->proto = PROTO_NTRIP_CASTER;
        SessionV.proto->h = &s_caster_handler;
        SessionV.proto->add(protocore_session_span());
        s_ctx.registered = PROTO_TRUE;
    }
    return PROTO_TRUE;
}

int protocore_ntrip_caster_broadcast(const char *mountpoint, const uint8_t *data, size_t len)
{
    if (!mountpoint || !data || len == 0)
    {
        return 0;
    }
    int mi = mount_index(mountpoint, -1);
    if (mi < 0)
    {
        return 0;
    }
    int sent = 0;
    for (int i = 0; i < PROTOCORE_NTRIP_MAX_ROVERS; i++)
    {
        CasterRover *r = &s_ctx.rovers[i];
        if (!r->active || !r->streaming || r->mount_idx != mi)
        {
            continue;
        }
        ConnPool.slot = r->conn_slot;
        ConnPool.active(protocore_conn_pool_span());
        if (!ConnPool.ok)
        {
            continue;
        }
        ConnPool.slot = r->conn_slot;
        ConnPool.io.data = data;
        ConnPool.io.len = (proto_u16)len;
        ConnPool.send(protocore_conn_pool_span());
        if (ConnPool.ok)
        {
            sent++;
        }
    }
    return sent;
}

int protocore_ntrip_caster_subscriber_count(const char *mountpoint)
{
    if (!mountpoint)
    {
        return 0;
    }
    int mi = mount_index(mountpoint, -1);
    if (mi < 0)
    {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < PROTOCORE_NTRIP_MAX_ROVERS; i++)
    {
        if (s_ctx.rovers[i].active && s_ctx.rovers[i].streaming && s_ctx.rovers[i].mount_idx == mi)
        {
            n++;
        }
    }
    return n;
}

void protocore_ntrip_caster_reset(void)
{
    for (int i = 0; i < PROTOCORE_NTRIP_MAX_MOUNTS; i++)
    {
        s_ctx.mounts[i].active = PROTO_FALSE;
    }
    for (int i = 0; i < PROTOCORE_NTRIP_MAX_ROVERS; i++)
    {
        s_ctx.rovers[i].active = PROTO_FALSE;
    }
}

#endif // PROTOCORE_ENABLE_NTRIP_CASTER
