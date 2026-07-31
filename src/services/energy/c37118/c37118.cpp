// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file c37118.cpp
 * @brief IEEE C37.118.2 synchrophasor frame builder + parser (pure, host-tested).
 */

#include "services/energy/c37118/c37118.h"

#if PC_ENABLE_C37118

#include <string.h>

#include "shared_primitives/crc.h" // PC_CRC16_IBM_3740
#include "shared_primitives/endian.h"

uint16_t pc_c37118_crc(const uint8_t *data, size_t len)
{
    // IEEE C37.118 uses CRC-CCITT (poly 0x1021, init 0xFFFF, unreflected), cataloged as
    // CRC-16/IBM-3740. test_crc diffs the shared engine against the loop that used to live here
    // over every length 0..64, so this is byte-identical to it.
    return (uint16_t)pc_crc(&PC_CRC16_IBM_3740, data, len);
}

size_t pc_c37118_build_frame(uint8_t *buf, size_t cap, uint8_t type, uint8_t version, uint16_t idcode, uint32_t soc,
                             uint32_t fracsec, const uint8_t *payload, size_t payload_len)
{
    if (!buf || (payload_len && !payload))
    {
        return 0;
    }
    size_t total = C37118_MIN_FRAME + payload_len; // 14 header + payload + 2 CHK
    if (total > 0xFFFF || total > cap)
    {
        return 0;
    }
    size_t p = 0;
    buf[p++] = C37118_SYNC_LEADER;
    buf[p++] = (uint8_t)(((type & C37118_TYPE_MASK) << C37118_TYPE_SHIFT) | (version & C37118_VERSION_MASK));
    p += pc_wr16be(buf + p, (uint16_t)total);
    p += pc_wr16be(buf + p, idcode);
    p += pc_wr32be(buf + p, soc);
    p += pc_wr32be(buf + p, fracsec);
    if (payload_len)
    {
        memcpy(buf + p, payload, payload_len);
        p += payload_len;
    }
    uint16_t crc = pc_c37118_crc(buf, p); // over everything before CHK
    p += pc_wr16be(buf + p, crc);
    return p;
}

size_t pc_c37118_build_command(uint8_t *buf, size_t cap, uint16_t idcode, uint32_t soc, uint32_t fracsec, uint16_t cmd)
{
    uint8_t payload[2];
    pc_wr16be(payload, cmd);
    return pc_c37118_build_frame(buf, cap, C37118_TYPE_CMD, C37118_VERSION_2011, idcode, soc, fracsec, payload, 2);
}

bool pc_c37118_parse_frame(const uint8_t *buf, size_t len, C37118Frame *out)
{
    if (!buf || !out || len < C37118_MIN_FRAME)
    {
        return false;
    }
    if (buf[0] != C37118_SYNC_LEADER)
    {
        return false;
    }
    uint16_t framesize = pc_rd16be(buf + 2);
    if (framesize < C37118_MIN_FRAME || framesize > len)
    {
        return false; // out of range / not fully buffered
    }
    uint16_t want = pc_c37118_crc(buf, (size_t)framesize - 2);
    uint16_t got = pc_rd16be(buf + framesize - 2);
    if (want != got)
    {
        return false; // CHK mismatch
    }
    out->type = (uint8_t)((buf[1] >> C37118_TYPE_SHIFT) & C37118_TYPE_MASK);
    out->version = (uint8_t)(buf[1] & C37118_VERSION_MASK);
    out->framesize = framesize;
    out->idcode = pc_rd16be(buf + 4);
    out->soc = pc_rd32be(buf + 6);
    out->fracsec = pc_rd32be(buf + 10);
    out->data = buf + 14;
    out->data_len = (size_t)framesize - C37118_MIN_FRAME;
    return true;
}

bool pc_c37118_parse_command(const C37118Frame *f, uint16_t *cmd)
{
    if (!f || f->type != C37118_TYPE_CMD || f->data_len < 2)
    {
        return false;
    }
    if (cmd)
    {
        *cmd = pc_rd16be(f->data);
    }
    return true;
}

bool pc_c37118_decode_stat(const C37118Frame *f, C37118Stat *out)
{
    if (!f || !out || f->type != C37118_TYPE_DATA || f->data_len < 2)
    {
        return false;
    }
    uint16_t s = pc_rd16be(f->data); // STAT is the first word of the data payload, big-endian
    out->raw = s;
    out->data_valid = (s & 0x8000u) == 0;             // bit 15: 0 = valid
    out->pmu_error = (s & 0x4000u) != 0;              // bit 14
    out->in_sync = (s & 0x2000u) == 0;                // bit 13: 0 = in sync
    out->sorted_by_arrival = (s & 0x1000u) != 0;      // bit 12
    out->trigger = (s & 0x0800u) != 0;                // bit 11
    out->config_change = (s & 0x0400u) != 0;          // bit 10
    out->data_modified = (s & 0x0200u) != 0;          // bit 9
    out->time_quality = (uint8_t)((s >> 6) & 0x07u);  // bits 8-6
    out->unlocked_time = (uint8_t)((s >> 4) & 0x03u); // bits 5-4
    out->trigger_reason = (uint8_t)(s & 0x0Fu);       // bits 3-0
    return true;
}

#endif // PC_ENABLE_C37118
