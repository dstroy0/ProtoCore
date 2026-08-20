// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dds.c
 * @brief The DDSI-RTPS Message framing codec: the Header build, the Submessage build, and the walk.
 *
 * header() and submessage() write the two fixed structures of DDSI-RTPS sec 8.3.3 into the caller's
 * buffer. parse() checks the Header against sec 8.3.6.3 and walks the Submessages by
 * octetsToNextHeader, handing each to the caller's sink.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_DDS

#include "services/iot/dds/dds.h"

#include "mmgr/protomem/protomem.h" // mem.cpy: the guidPrefix and the Submessage contents

PROTOCORE_BEGIN_DECLS

// SubmessageHeader: submessageId 1 + flags 1 + octetsToNextHeader 2 (sec 9.4.5.1).
#define RTPS_SUBMESSAGE_HEADER_LEN 4

const uint8_t RTPS_VERSION[2] = {2, 4};

// Build the 20-octet Header (sec 9.4.4) into ns->out, and report its length in ns->n.
void protocore_rtps_header(uint8_t *restrict work)
{
    (void)work;
    RtpsV.n = 0;
    if (!RtpsV.hdr.guid_prefix || !RtpsV.hdr.vendor_id || !RtpsV.out.buf || RtpsV.out.cap < RTPS_HEADER_LEN)
    {
        return;
    }
    uint8_t *out = RtpsV.out.buf;
    // PROTOCOL_RTPS is the octets 'R' 'T' 'P' 'S' (sec 9.3.2.1), then version, vendorId, guidPrefix.
    out[0] = 'R';
    out[1] = 'T';
    out[2] = 'P';
    out[3] = 'S';
    out[4] = RTPS_VERSION[0];
    out[5] = RTPS_VERSION[1];
    out[6] = RtpsV.hdr.vendor_id[0];
    out[7] = RtpsV.hdr.vendor_id[1];
    mem.cpy(out + 8, RtpsV.hdr.guid_prefix, RTPS_GUIDPREFIX_LEN);
    RtpsV.n = RTPS_HEADER_LEN;
}

// Build one Submessage, its SubmessageHeader then its contents (sec 9.4.5.1), into ns->out.
void protocore_rtps_submessage(uint8_t *restrict work)
{
    (void)work;
    RtpsV.n = 0;
    const uint16_t contents_len = RtpsV.sub.contents_len;
    if (!RtpsV.out.buf || (contents_len && !RtpsV.sub.contents))
    {
        return;
    }
    const size_t total = RTPS_SUBMESSAGE_HEADER_LEN + (size_t)contents_len;
    if (total > RtpsV.out.cap)
    {
        return;
    }
    uint8_t *out = RtpsV.out.buf;
    out[0] = RtpsV.sub.submessage_id;
    out[1] = RtpsV.sub.flags;
    // octetsToNextHeader is a CDR ushort in the Submessage's own byte order, the EndiannessFlag in
    // bit 0 of flags deciding it: E=1 little-endian, E=0 big-endian (sec 9.4.5.1).
    if (RtpsV.sub.flags & RTPS_FLAG_ENDIAN)
    {
        out[2] = (uint8_t)contents_len;
        out[3] = (uint8_t)(contents_len >> 8);
    }
    else
    {
        out[2] = (uint8_t)(contents_len >> 8);
        out[3] = (uint8_t)contents_len;
    }
    if (contents_len)
    {
        mem.cpy(out + RTPS_SUBMESSAGE_HEADER_LEN, RtpsV.sub.contents, contents_len);
    }
    RtpsV.n = total;
}

// Validate the Header and walk the Submessages, reporting the verdict in ns->ok.
void protocore_rtps_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *msg = RtpsV.msg.msg;
    const size_t len = RtpsV.msg.len;

    RtpsV.ok = PROTO_FALSE;
    // sec 8.3.6.3: a Header is invalid with fewer octets than the PSM's 20, with a protocol that is
    // not PROTOCOL_RTPS, or with a major version above the one this implementation supports. The
    // minor version is not one of the three, so every minor parses.
    if (!msg || len < RTPS_HEADER_LEN)
    {
        return;
    }
    if (msg[0] != 'R' || msg[1] != 'T' || msg[2] != 'P' || msg[3] != 'S')
    {
        return;
    }
    if (msg[4] != RTPS_VERSION[0])
    {
        return;
    }

    size_t off = RTPS_HEADER_LEN;
    // sec 8.3.4.1 rule 1: a Submessage whose header does not fit invalidates the rest of the
    // Message, which the check after the loop reports - the loop itself only stops.
    while (off + RTPS_SUBMESSAGE_HEADER_LEN <= len)
    {
        const uint8_t submessage_id = msg[off];
        const uint8_t flags = msg[off + 1];
        const size_t contents_off = off + RTPS_SUBMESSAGE_HEADER_LEN;
        const uint16_t octets_to_next_header = (flags & RTPS_FLAG_ENDIAN)
                                                   ? (uint16_t)(msg[off + 2] | (msg[off + 3] << 8))
                                                   : (uint16_t)((msg[off + 2] << 8) | msg[off + 3]);

        // sec 9.4.5.1: octetsToNextHeader 0 makes the Submessage the last one and runs it to the end
        // of the Message, except on PAD and INFO_TS, where the next SubmessageHeader starts
        // immediately after this one.
        size_t contents_len = octets_to_next_header;
        if (octets_to_next_header == 0 && submessage_id != RTPS_SM_PAD && submessage_id != RTPS_SM_INFO_TS)
        {
            contents_len = len - contents_off;
        }
        // sec 8.3.4.1 rule 2: contents past the end of the Message invalidate the rest of it.
        if (contents_off + contents_len > len)
        {
            return;
        }
        if (RtpsV.sink.on_submessage)
        {
            RtpsV.sink.on_submessage(submessage_id, flags, contents_len ? (msg + contents_off) : NULL, contents_len,
                                     RtpsV.sink.arg);
        }
        // A run to the end of the Message lands off at len, which fails the loop test.
        off = contents_off + contents_len;
    }
    // Octets left over are fewer than a SubmessageHeader, so rule 1 refuses the rest of the
    // Message. Landing exactly at len is the clean end of the walk.
    if (off != len)
    {
        return;
    }
    RtpsV.ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
RtpsVars RtpsV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DDS
