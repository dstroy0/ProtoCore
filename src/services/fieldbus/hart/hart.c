// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hart.c
 * @brief HART / HART-IP protocol codec (see hart.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HART

#include "mmgr/protomem.h"
#include "services/fieldbus/hart/hart.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void hart_checksum(uint8_t *restrict work);

static void hart_checksum(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *bytes = Hart.checksum_args.bytes;
    size_t len = Hart.checksum_args.len;

    uint8_t x = 0;
    for (size_t i = 0; i < len; i++)
    {
        x ^= bytes[i];
    }
    Hart.value = x;
}

static void hart_build(uint8_t *restrict work)
{
    uint8_t delimiter = Hart.build_args.delimiter;
    const uint8_t *addr = Hart.build_args.addr;
    size_t addr_len = Hart.build_args.addr_len;
    uint8_t command = Hart.build_args.command;
    const uint8_t *data = Hart.build_args.data;
    size_t data_len = Hart.build_args.data_len;
    uint8_t *out = Hart.build_args.out;
    size_t cap = Hart.build_args.cap;

    if (addr_len != 1 && addr_len != 5)
    {
        Hart.n = 0;
        return;
    }
    if (!addr || (data_len && !data))
    {
        Hart.n = 0;
        return;
    }
    // delimiter + addr + command + byte-count + data + checksum
    size_t n = 1 + addr_len + 1 + 1 + data_len + 1;
    if (n > cap || data_len > 0xFF)
    {
        Hart.n = 0;
        return;
    }

    size_t i = 0;
    out[i++] = delimiter;
    mem.cpy(out + i, addr, addr_len);
    i += addr_len;
    out[i++] = command;
    out[i++] = (uint8_t)data_len; // byte count
    if (data_len)
    {
        mem.cpy(out + i, data, data_len);
        i += data_len;
    }
    Hart.checksum_args.bytes = out;
    Hart.checksum_args.len = i;
    hart_checksum(work);
    out[i] = Hart.value; // XOR over delimiter..last data byte
    i++;
    Hart.n = i;
}

static void hart_parse(uint8_t *restrict work)
{
    const uint8_t *frame = Hart.parse_args.frame;
    size_t len = Hart.parse_args.len;
    HartFrame *out = Hart.parse_args.out;

    if (!frame || !out)
    {
        Hart.ok = PROTO_FALSE;
        return;
    }
    size_t addr_len = (frame[0] & HART_DELIM_LONG_ADDR) ? 5 : 1;
    // delimiter + addr + command + byte-count + checksum = minimum with no data
    size_t min = 1 + addr_len + 1 + 1 + 1;
    if (len < min)
    {
        Hart.ok = PROTO_FALSE;
        return;
    }

    size_t bc_idx = 1 + addr_len + 1; // index of the byte-count field
    uint8_t byte_count = frame[bc_idx];
    size_t expect = 1 + addr_len + 1 + 1 + byte_count + 1; // full frame incl checksum
    if (len < expect)
    {
        Hart.ok = PROTO_FALSE;
        return;
    }

    Hart.checksum_args.bytes = frame;
    Hart.checksum_args.len = expect - 1;
    hart_checksum(work);
    uint8_t want = Hart.value;
    if (want != frame[expect - 1])
    {
        Hart.ok = PROTO_FALSE;
        return;
    }

    out->delimiter = frame[0];
    out->addr = frame + 1;
    out->addr_len = addr_len;
    out->command = frame[1 + addr_len];
    out->byte_count = byte_count;
    out->data = byte_count ? (frame + bc_idx + 1) : NULL;
    out->data_len = byte_count;
    Hart.ok = PROTO_TRUE;
}

static void hart_ip_build_header(uint8_t *restrict work)
{
    (void)work;
    uint8_t msg_type = Hart.ip_build_header_args.msg_type;
    uint8_t msg_id = Hart.ip_build_header_args.msg_id;
    uint8_t status = Hart.ip_build_header_args.status;
    uint16_t seq = Hart.ip_build_header_args.seq;
    uint16_t total_len = Hart.ip_build_header_args.total_len;
    uint8_t *out = Hart.ip_build_header_args.out;
    size_t cap = Hart.ip_build_header_args.cap;

    if (cap < HARTIP_HEADER_LEN || !out)
    {
        Hart.n = 0;
        return;
    }
    out[0] = 1; // HART-IP protocol version
    out[1] = msg_type;
    out[2] = msg_id;
    out[3] = status;
    out[4] = (uint8_t)(seq >> 8);
    out[5] = (uint8_t)seq;
    out[6] = (uint8_t)(total_len >> 8);
    out[7] = (uint8_t)total_len;
    Hart.n = HARTIP_HEADER_LEN;
}

static void hart_ip_parse_header(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = Hart.ip_parse_header_args.buf;
    size_t len = Hart.ip_parse_header_args.len;
    HartIpHeader *out = Hart.ip_parse_header_args.out;

    if (!buf || !out || len < HARTIP_HEADER_LEN)
    {
        Hart.ok = PROTO_FALSE;
        return;
    }
    uint16_t total = (uint16_t)((buf[6] << 8) | buf[7]);
    if (total < HARTIP_HEADER_LEN || total > len) // the byte count must include the header and be present
    {
        Hart.ok = PROTO_FALSE;
        return;
    }
    out->version = buf[0];
    out->msg_type = buf[1];
    out->msg_id = buf[2];
    out->status = buf[3];
    out->seq = (uint16_t)((buf[4] << 8) | buf[5]);
    out->total_len = total;
    out->payload_len = (size_t)(total - HARTIP_HEADER_LEN);
    out->payload = out->payload_len ? buf + HARTIP_HEADER_LEN : NULL;
    Hart.ok = PROTO_TRUE;
}

HartNs Hart = {.checksum = hart_checksum,
               .build = hart_build,
               .parse = hart_parse,
               .ip_build_header = hart_ip_build_header,
               .ip_parse_header = hart_ip_parse_header};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HART
