// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file lsv2.c
 * @brief Heidenhain LSV/2 telegram codec (pure, host-tested).
 */

#include "services/machine_tool/lsv2/lsv2.h"

#if PC_ENABLE_LSV2

#include <string.h> // memcpy / memcmp / memset (framing + parsing are hand-rolled)

// Write the fixed 8-byte header (big-endian payload length + 4-char mnemonic) once the payload is in
// place, and return the total telegram length. Callers guarantee the buffer holds header + payload.
static size_t finalize(uint8_t *buf, const char *mnemonic, size_t payload_len)
{
    buf[0] = (uint8_t)(payload_len >> 24);
    buf[1] = (uint8_t)(payload_len >> 16);
    buf[2] = (uint8_t)(payload_len >> 8);
    buf[3] = (uint8_t)(payload_len);
    memcpy(buf + 4, mnemonic, PC_LSV2_MNEMONIC_LEN);
    return PC_LSV2_HEADER_LEN + payload_len;
}

// Append the bytes of NUL-terminated src plus one trailing NUL into buf at *pos, bounded by cap.
// Returns false on overflow (leaving *pos partially advanced - the caller fails the whole build).
static proto_bool append_cstr_nul(uint8_t *buf, size_t cap, size_t *pos, const char *src)
{
    size_t p = *pos;
    for (const char *s = src; *s != '\0'; ++s)
    {
        if (p >= cap)
        {
            return PROTO_FALSE;
        }
        buf[p++] = (uint8_t)*s;
    }
    if (p >= cap)
    {
        return PROTO_FALSE;
    }
    buf[p++] = 0x00;
    *pos = p;
    return PROTO_TRUE;
}

size_t pc_lsv2_build(uint8_t *buf, size_t cap, const char *mnemonic, const uint8_t *payload, size_t payload_len)
{
    if (!buf || !mnemonic || cap < PC_LSV2_HEADER_LEN)
    {
        return 0;
    }
    if (payload_len && !payload)
    {
        return 0;
    }
    if ((uint64_t)payload_len > 0xFFFFFFFFULL) // the length field is 32-bit
    {
        return 0;
    }
    if (payload_len > cap - PC_LSV2_HEADER_LEN)
    {
        return 0;
    }
    if (payload_len)
    {
        memcpy(buf + PC_LSV2_HEADER_LEN, payload, payload_len);
    }
    return finalize(buf, mnemonic, payload_len);
}

proto_bool pc_lsv2_parse(const uint8_t *buf, size_t len, Lsv2Telegram *out, size_t *consumed)
{
    if (!out)
    {
        return PROTO_FALSE;
    }
    memset(out->mnemonic, 0, PC_LSV2_MNEMONIC_LEN);
    out->payload = NULL;
    out->payload_len = 0;

    if (!buf || len < PC_LSV2_HEADER_LEN)
    {
        return PROTO_FALSE;
    }

    uint32_t plen = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
    size_t total = (size_t)PC_LSV2_HEADER_LEN + plen;
    if (len < total)
    {
        return PROTO_FALSE; // incomplete - caller accumulates more
    }

    memcpy(out->mnemonic, buf + 4, PC_LSV2_MNEMONIC_LEN);
    out->payload = plen ? buf + PC_LSV2_HEADER_LEN : NULL;
    out->payload_len = plen;
    if (consumed)
    {
        *consumed = total;
    }
    return PROTO_TRUE;
}

proto_bool pc_lsv2_is(const Lsv2Telegram *t, const char *mnemonic4)
{
    return t && mnemonic4 && memcmp(t->mnemonic, mnemonic4, PC_LSV2_MNEMONIC_LEN) == 0;
}

size_t pc_lsv2_build_login(uint8_t *buf, size_t cap, const char *login, const char *password)
{
    if (!buf || !login || cap < PC_LSV2_HEADER_LEN)
    {
        return 0;
    }
    size_t pos = PC_LSV2_HEADER_LEN;
    if (!append_cstr_nul(buf, cap, &pos, login))
    {
        return 0;
    }
    if (password && !append_cstr_nul(buf, cap, &pos, password))
    {
        return 0;
    }
    return finalize(buf, PC_LSV2_CMD_LOGIN, pos - PC_LSV2_HEADER_LEN);
}

size_t pc_lsv2_build_logout(uint8_t *buf, size_t cap, const char *login)
{
    if (!buf || cap < PC_LSV2_HEADER_LEN)
    {
        return 0;
    }
    size_t pos = PC_LSV2_HEADER_LEN;
    if (login && *login != '\0' && !append_cstr_nul(buf, cap, &pos, login))
    {
        return 0;
    }
    return finalize(buf, PC_LSV2_CMD_LOGOUT, pos - PC_LSV2_HEADER_LEN);
}

size_t pc_lsv2_build_filename(uint8_t *buf, size_t cap, const char *mnemonic, const char *filename)
{
    if (!buf || !mnemonic || !filename || cap < PC_LSV2_HEADER_LEN)
    {
        return 0;
    }
    size_t pos = PC_LSV2_HEADER_LEN;
    if (!append_cstr_nul(buf, cap, &pos, filename))
    {
        return 0;
    }
    return finalize(buf, mnemonic, pos - PC_LSV2_HEADER_LEN);
}

size_t pc_lsv2_build_run_info(uint8_t *buf, size_t cap, uint16_t info_code)
{
    if (!buf || cap < PC_LSV2_HEADER_LEN + 2)
    {
        return 0;
    }
    buf[PC_LSV2_HEADER_LEN] = (uint8_t)(info_code >> 8);
    buf[PC_LSV2_HEADER_LEN + 1] = (uint8_t)(info_code);
    return finalize(buf, PC_LSV2_CMD_RUN_INFO, 2);
}

proto_bool pc_lsv2_is_ok(const Lsv2Telegram *t)
{
    return pc_lsv2_is(t, PC_LSV2_RSP_OK);
}

proto_bool pc_lsv2_is_error(const Lsv2Telegram *t)
{
    return pc_lsv2_is(t, PC_LSV2_RSP_ERROR) || pc_lsv2_is(t, PC_LSV2_RSP_XFER_ERR);
}

proto_bool pc_lsv2_error(const Lsv2Telegram *t, uint8_t *err_class, uint8_t *err_code)
{
    if (!pc_lsv2_is_error(t) || t->payload_len != 2 || !t->payload)
    {
        return PROTO_FALSE;
    }
    if (err_class)
    {
        *err_class = t->payload[0];
    }
    if (err_code)
    {
        *err_code = t->payload[1];
    }
    return PROTO_TRUE;
}

#endif // PC_ENABLE_LSV2
