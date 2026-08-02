// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_ntrip_caster_listener.cpp
 * @brief Server-side NTRIP caster listener (see pc_ntrip_caster_listener.h). Answers PROTO_NTRIP_CASTER rover
 *        requests via the pure pc_ntrip_caster codec and fans RTCM corrections out to subscribed rovers.
 */

#include "services/timing_position/gnss/ntrip_caster_listener.h"

#if PC_ENABLE_NTRIP_CASTER

#include "network_drivers/session/proto_handler.h"
#include "network_drivers/transport/tcp.h"
#include <string.h>

// One published mountpoint on a listener.
typedef struct
{
    proto_bool active;
    uint8_t listener_id;
    char name[PC_NTRIP_MOUNT_MAX];
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
    char req[PC_NTRIP_REQ_MAX];
    uint16_t req_len;
} CasterRover;

typedef struct
{
    CasterMount mounts[PC_NTRIP_MAX_MOUNTS];
    CasterRover rovers[PC_NTRIP_MAX_ROVERS];
    proto_bool registered;
} NtripCasterCtx;
static NtripCasterCtx s_ctx;

static CasterRover *rover_by_conn(uint8_t slot)
{
    for (int i = 0; i < PC_NTRIP_MAX_ROVERS; i++)
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
    for (int i = 0; i < PC_NTRIP_MAX_ROVERS; i++)
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
    for (int i = 0; i < PC_NTRIP_MAX_MOUNTS; i++)
    {
        if (!s_ctx.mounts[i].active)
        {
            continue;
        }
        if (listener_id >= 0 && s_ctx.mounts[i].listener_id != (uint8_t)listener_id)
        {
            continue;
        }
        if (strcmp(s_ctx.mounts[i].name, name) == 0)
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
    for (int i = 0; i < PC_NTRIP_MAX_MOUNTS && k < cap; i++)
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
    if (len && pc_conn_active(r->conn_slot))
    {
        pc_conn_send(r->conn_slot, resp, (u16_t)len);
    }
    r->active = PROTO_FALSE;
    pc_conn_close(r->conn_slot);
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
    size_t el = strnlen(expect, req->auth_b64_len + 1);
    if (el != req->auth_b64_len)
    {
        return PROTO_FALSE;
    }
    return memcmp(req->auth_b64, expect, el) == 0;
}

static void serve_sourcetable(CasterRover *r, NtripVersion version)
{
    NtripMount list[PC_NTRIP_MAX_MOUNTS];
    size_t nm = mounts_on_listener(pc_conn_listener_id(r->conn_slot), list, PC_NTRIP_MAX_MOUNTS);
    char buf[PC_NTRIP_REQ_MAX + 256];
    size_t n = pc_ntrip_build_sourcetable(buf, sizeof(buf), version, list, nm);
    reply_and_close(r, buf, n);
}

// A completed request has been parsed; dispatch it.
static void dispatch(CasterRover *r, const NtripRequest *req)
{
    char buf[192];
    if (!req->is_get)
    {
        size_t n = pc_ntrip_build_error_response(buf, sizeof(buf), req->version);
        reply_and_close(r, buf, n);
        return;
    }
    if (req->want_sourcetable)
    {
        serve_sourcetable(r, req->version);
        return;
    }
    int mi = mount_index(req->mountpoint, (int)pc_conn_listener_id(r->conn_slot));
    if (mi < 0)
    {
        serve_sourcetable(r, req->version); // unknown mount -> advertise the available ones
        return;
    }
    if (!auth_ok(req, s_ctx.mounts[mi].auth_b64))
    {
        size_t n = pc_ntrip_build_unauthorized_response(buf, sizeof(buf), req->version);
        reply_and_close(r, buf, n);
        return;
    }
    size_t n = pc_ntrip_build_stream_response(buf, sizeof(buf), req->version);
    if (n == 0 || !pc_conn_active(r->conn_slot) || !pc_conn_send(r->conn_slot, buf, (u16_t)n))
    {
        r->active = PROTO_FALSE;
        pc_conn_close(r->conn_slot);
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
        pc_conn_close(slot); // rover table full
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
        pc_conn_close(slot);
        return;
    }
    if (r->streaming)
    {
        // A streaming rover may send periodic GGA; the base already knows its position, so drain & ignore.
        uint8_t sink[64];
        while (pc_conn_available(slot))
        {
            if (pc_conn_read(slot, sink, sizeof(sink)) == 0)
            {
                break;
            }
        }
        return;
    }
    // Still reading the request: append what is available, then try to parse.
    while (pc_conn_available(slot) && r->req_len < sizeof(r->req) - 1)
    {
        size_t got = pc_conn_read(slot, (uint8_t *)r->req + r->req_len, sizeof(r->req) - 1 - r->req_len);
        if (got == 0)
        {
            break;
        }
        r->req_len += (uint16_t)got;
    }
    NtripRequest req;
    if (pc_ntrip_request_parse(r->req, r->req_len, &req))
    {
        dispatch(r, &req);
        return;
    }
    if (r->req_len >= sizeof(r->req) - 1) // request too large without a header terminator
    {
        char buf[64];
        size_t n = pc_ntrip_build_error_response(buf, sizeof(buf), NTRIP_V1);
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

static const ProtoHandler s_caster_handler = {caster_on_accept, caster_on_data, caster_on_close, NULL};

proto_bool pc_ntrip_caster_add_mount(uint8_t listener_id, const NtripMount *mount, const char *auth_b64)
{
    if (!mount || !mount->mountpoint)
    {
        return PROTO_FALSE;
    }
    size_t nl = strnlen(mount->mountpoint, PC_NTRIP_MOUNT_MAX + 1);
    if (nl == 0 || nl >= PC_NTRIP_MOUNT_MAX)
    {
        return PROTO_FALSE;
    }
    int idx = -1;
    for (int i = 0; i < PC_NTRIP_MAX_MOUNTS; i++)
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
    memcpy(m->name, mount->mountpoint, nl + 1);
    m->cfg = *mount;
    m->cfg.mountpoint = m->name; // point the copied cfg at our owned name
    m->auth_b64 = auth_b64;
    if (!s_ctx.registered)
    {
        proto_register(ConnProto::PROTO_NTRIP_CASTER, &s_caster_handler);
        s_ctx.registered = PROTO_TRUE;
    }
    return PROTO_TRUE;
}

int pc_ntrip_caster_broadcast(const char *mountpoint, const uint8_t *data, size_t len)
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
    for (int i = 0; i < PC_NTRIP_MAX_ROVERS; i++)
    {
        CasterRover *r = &s_ctx.rovers[i];
        if (!r->active || !r->streaming || r->mount_idx != mi)
        {
            continue;
        }
        if (!pc_conn_active(r->conn_slot))
        {
            continue;
        }
        if (pc_conn_send(r->conn_slot, data, (u16_t)len))
        {
            sent++;
        }
    }
    return sent;
}

int pc_ntrip_caster_subscriber_count(const char *mountpoint)
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
    for (int i = 0; i < PC_NTRIP_MAX_ROVERS; i++)
    {
        if (s_ctx.rovers[i].active && s_ctx.rovers[i].streaming && s_ctx.rovers[i].mount_idx == mi)
        {
            n++;
        }
    }
    return n;
}

void pc_ntrip_caster_reset(void)
{
    for (int i = 0; i < PC_NTRIP_MAX_MOUNTS; i++)
    {
        s_ctx.mounts[i].active = PROTO_FALSE;
    }
    for (int i = 0; i < PC_NTRIP_MAX_ROVERS; i++)
    {
        s_ctx.rovers[i].active = PROTO_FALSE;
    }
}

#endif // PC_ENABLE_NTRIP_CASTER
