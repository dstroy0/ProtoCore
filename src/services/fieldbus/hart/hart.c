// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hart.c
 * @brief HART / HART-IP protocol codec (see hart.h).
 */

#include "services/fieldbus/hart/hart.h"

#if PC_ENABLE_HART

#include <string.h>

uint8_t pc_hart_checksum(const uint8_t *bytes, size_t len)
{
    uint8_t x = 0;
    for (size_t i = 0; i < len; i++)
    {
        x ^= bytes[i];
    }
    return x;
}

size_t pc_hart_build(uint8_t delimiter, const uint8_t *addr, size_t addr_len, uint8_t command, const uint8_t *data,
                     size_t data_len, uint8_t *out, size_t cap)
{
    if (addr_len != 1 && addr_len != 5)
    {
        return 0;
    }
    if (!addr || (data_len && !data))
    {
        return 0;
    }
    // delimiter + addr + command + byte-count + data + checksum
    size_t n = 1 + addr_len + 1 + 1 + data_len + 1;
    if (n > cap || data_len > 0xFF)
    {
        return 0;
    }

    size_t i = 0;
    out[i++] = delimiter;
    memcpy(out + i, addr, addr_len);
    i += addr_len;
    out[i++] = command;
    out[i++] = (uint8_t)data_len; // byte count
    if (data_len)
    {
        memcpy(out + i, data, data_len);
        i += data_len;
    }
    out[i] = pc_hart_checksum(out, i); // XOR over delimiter..last data byte
    i++;
    return i;
}

proto_bool pc_hart_parse(const uint8_t *frame, size_t len, HartFrame *out)
{
    if (!frame || !out)
    {
        return PROTO_FALSE;
    }
    size_t addr_len = (frame[0] & HART_DELIM_LONG_ADDR) ? 5 : 1;
    // delimiter + addr + command + byte-count + checksum = minimum with no data
    size_t min = 1 + addr_len + 1 + 1 + 1;
    if (len < min)
    {
        return PROTO_FALSE;
    }

    size_t bc_idx = 1 + addr_len + 1; // index of the byte-count field
    uint8_t byte_count = frame[bc_idx];
    size_t expect = 1 + addr_len + 1 + 1 + byte_count + 1; // full frame incl checksum
    if (len < expect)
    {
        return PROTO_FALSE;
    }

    uint8_t want = pc_hart_checksum(frame, expect - 1);
    if (want != frame[expect - 1])
    {
        return PROTO_FALSE;
    }

    out->delimiter = frame[0];
    out->addr = frame + 1;
    out->addr_len = addr_len;
    out->command = frame[1 + addr_len];
    out->byte_count = byte_count;
    out->data = byte_count ? (frame + bc_idx + 1) : NULL;
    out->data_len = byte_count;
    return PROTO_TRUE;
}

size_t pc_hartip_build_header(uint8_t msg_type, uint8_t msg_id, uint8_t status, uint16_t seq, uint16_t total_len,
                              uint8_t *out, size_t cap)
{
    if (cap < HARTIP_HEADER_LEN || !out)
    {
        return 0;
    }
    out[0] = 1; // HART-IP protocol version
    out[1] = msg_type;
    out[2] = msg_id;
    out[3] = status;
    out[4] = (uint8_t)(seq >> 8);
    out[5] = (uint8_t)seq;
    out[6] = (uint8_t)(total_len >> 8);
    out[7] = (uint8_t)total_len;
    return HARTIP_HEADER_LEN;
}

proto_bool pc_hartip_parse_header(const uint8_t *buf, size_t len, HartIpHeader *out)
{
    if (!buf || !out || len < HARTIP_HEADER_LEN)
    {
        return PROTO_FALSE;
    }
    uint16_t total = (uint16_t)((buf[6] << 8) | buf[7]);
    if (total < HARTIP_HEADER_LEN || total > len) // the byte count must include the header and be present
    {
        return PROTO_FALSE;
    }
    out->version = buf[0];
    out->msg_type = buf[1];
    out->msg_id = buf[2];
    out->status = buf[3];
    out->seq = (uint16_t)((buf[4] << 8) | buf[5]);
    out->total_len = total;
    out->payload_len = (size_t)(total - HARTIP_HEADER_LEN);
    out->payload = out->payload_len ? buf + HARTIP_HEADER_LEN : NULL;
    return PROTO_TRUE;
}

#endif // PC_ENABLE_HART
