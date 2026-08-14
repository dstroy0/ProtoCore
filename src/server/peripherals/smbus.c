// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file smbus.c
 * @brief SMBus 3.1 transaction shapes - implementation. See smbus.h.
 *
 * A read's PEC covers two spans that are not next to each other in any buffer: the command going
 * out and the data coming back, with an address byte in front of each. The CRC engine's
 * begin / update / final form walks them in place, so nothing is copied to checksum it.
 */

#include "server/peripherals/smbus.h"
#include "protocore_config.h"
#include "shared/crc/crc.h" // PROTOCORE_CRC8_SMBUS: the PEC polynomial, host-tested in test_crc

#if PROTOCORE_ENABLE_SMBUS

#if PROTOCORE_HAS_BUS
#include "server/peripherals/i2c.h"
#endif

// Two address bytes, a command, a count, the block, and the PEC: the longest byte sequence any
// shape puts on the wire or checksums.
#define PROTOCORE_SMBUS_FRAME_MAX (3u + 1u + PROTOCORE_SMBUS_BLOCK_MAX + 1u)

// All SMBus state, owned by one instance (internal linkage): whether the Packet Error Code is on,
// and the bus frame. The frame is a member rather than a local because a transaction is composed
// in place, and a block write is the widest thing this puts on the wire.
typedef struct
{
    proto_bool pec;
    uint8_t frame[PROTOCORE_SMBUS_FRAME_MAX];
} SmbusCtx;
static SmbusCtx s_smb = {.pec = PROTO_FALSE, .frame = {0}};

uint8_t protocore_smbus_addr_byte(uint8_t addr, uint8_t rw)
{
    return (uint8_t)(((addr & 0x7Fu) << 1) | (rw & 1u));
}

// The register a PEC starts from.
static uint32_t pec_begin(void)
{
    Crc.args.params = &PROTOCORE_CRC8_SMBUS;
    Crc.begin(Crc.internal);
    return Crc.value;
}

// Fold @p len octets at @p data into the running register @p crc.
static uint32_t pec_fold(uint32_t crc, const uint8_t *data, size_t len)
{
    Crc.args.params = &PROTOCORE_CRC8_SMBUS;
    Crc.args.crc = crc;
    Crc.args.data = data;
    Crc.args.len = len;
    Crc.update(Crc.internal);
    return Crc.value;
}

// The PEC octet a running register finishes to.
static uint8_t pec_final(uint32_t crc)
{
    Crc.args.params = &PROTOCORE_CRC8_SMBUS;
    Crc.args.crc = crc;
    Crc.final(Crc.internal);
    return (uint8_t)Crc.value;
}

uint8_t protocore_smbus_pec_write(uint8_t addr, const uint8_t *payload, size_t len)
{
    uint8_t a = protocore_smbus_addr_byte(addr, PROTOCORE_SMBUS_WRITE);
    uint32_t c = pec_fold(pec_begin(), &a, 1);
    if (payload != NULL && len > 0)
    {
        c = pec_fold(c, payload, len);
    }
    return pec_final(c);
}

uint8_t protocore_smbus_pec_read(uint8_t addr, const uint8_t *sent, size_t slen, const uint8_t *got, size_t glen)
{
    uint8_t aw = protocore_smbus_addr_byte(addr, PROTOCORE_SMBUS_WRITE);
    uint8_t ar = protocore_smbus_addr_byte(addr, PROTOCORE_SMBUS_READ);
    uint32_t c = pec_fold(pec_begin(), &aw, 1);
    if (sent != NULL && slen > 0)
    {
        c = pec_fold(c, sent, slen);
    }
    c = pec_fold(c, &ar, 1);
    if (got != NULL && glen > 0)
    {
        c = pec_fold(c, got, glen);
    }
    return pec_final(c);
}

void protocore_smbus_set_pec(proto_bool on)
{
    s_smb.pec = on;
}

proto_bool protocore_smbus_pec_enabled(void)
{
    return s_smb.pec;
}

#if PROTOCORE_HAS_BUS

proto_bool protocore_smbus_begin(void)
{
    return protocore_i2c_begin();
}

// Put @p n composed bytes on the wire, appending the PEC over them when it is on.
static proto_bool put(uint8_t addr, size_t n)
{
    if (s_smb.pec)
    {
        s_smb.frame[n] = protocore_smbus_pec_write(addr, s_smb.frame, n);
        n++;
    }
    return protocore_i2c_write(addr, s_smb.frame, n);
}

// Read @p n bytes answering the @p slen bytes already composed in the frame, checking the PEC that
// follows them when it is on. The reply lands at frame[slen] so the sent and received spans stay
// separate for the checksum.
static proto_bool take(uint8_t addr, size_t slen, size_t n)
{
    size_t want = s_smb.pec ? n + 1u : n;
    if (slen + want > sizeof(s_smb.frame))
    {
        return PROTO_FALSE;
    }
    if (!protocore_i2c_write_read(addr, s_smb.frame, slen, &s_smb.frame[slen], want))
    {
        return PROTO_FALSE;
    }
    if (!s_smb.pec)
    {
        return PROTO_TRUE;
    }
    uint8_t want_pec = protocore_smbus_pec_read(addr, s_smb.frame, slen, &s_smb.frame[slen], n);
    return s_smb.frame[slen + n] == want_pec;
}

proto_bool protocore_smbus_quick(uint8_t addr, uint8_t rw)
{
    // The direction bit is the whole payload, so this is an address cycle and nothing else. A
    // quick command carries no PEC: there are no data bytes for one to cover.
    return (rw & 1u) != 0 ? protocore_i2c_read(addr, s_smb.frame, 0) : protocore_i2c_probe(addr);
}

proto_bool protocore_smbus_send_byte(uint8_t addr, uint8_t value)
{
    s_smb.frame[0] = value;
    return put(addr, 1);
}

proto_bool protocore_smbus_receive_byte(uint8_t addr, uint8_t *out)
{
    if (out == NULL)
    {
        return PROTO_FALSE;
    }
    // No command goes out, so the PEC covers the read address byte and the data alone.
    size_t want = s_smb.pec ? 2u : 1u;
    if (!protocore_i2c_read(addr, s_smb.frame, want))
    {
        return PROTO_FALSE;
    }
    if (s_smb.pec && s_smb.frame[1] != protocore_smbus_pec_read(addr, NULL, 0, s_smb.frame, 1))
    {
        return PROTO_FALSE;
    }
    *out = s_smb.frame[0];
    return PROTO_TRUE;
}

proto_bool protocore_smbus_write_byte(uint8_t addr, uint8_t cmd, uint8_t value)
{
    s_smb.frame[0] = cmd;
    s_smb.frame[1] = value;
    return put(addr, 2);
}

proto_bool protocore_smbus_read_byte(uint8_t addr, uint8_t cmd, uint8_t *out)
{
    if (out == NULL)
    {
        return PROTO_FALSE;
    }
    s_smb.frame[0] = cmd;
    if (!take(addr, 1, 1))
    {
        return PROTO_FALSE;
    }
    *out = s_smb.frame[1];
    return PROTO_TRUE;
}

proto_bool protocore_smbus_write_word(uint8_t addr, uint8_t cmd, uint16_t value)
{
    s_smb.frame[0] = cmd;
    s_smb.frame[1] = (uint8_t)(value & 0xFFu); // low byte first, per the protocol
    s_smb.frame[2] = (uint8_t)(value >> 8);
    return put(addr, 3);
}

proto_bool protocore_smbus_read_word(uint8_t addr, uint8_t cmd, uint16_t *out)
{
    if (out == NULL)
    {
        return PROTO_FALSE;
    }
    s_smb.frame[0] = cmd;
    if (!take(addr, 1, 2))
    {
        return PROTO_FALSE;
    }
    *out = (uint16_t)((uint16_t)s_smb.frame[1] | ((uint16_t)s_smb.frame[2] << 8));
    return PROTO_TRUE;
}

proto_bool protocore_smbus_write_block(uint8_t addr, uint8_t cmd, const uint8_t *buf, size_t len)
{
    if (buf == NULL || len == 0 || len > PROTOCORE_SMBUS_BLOCK_MAX)
    {
        return PROTO_FALSE;
    }
    s_smb.frame[0] = cmd;
    s_smb.frame[1] = (uint8_t)len; // the count byte the protocol puts in front of the payload
    for (size_t i = 0; i < len; i++)
    {
        s_smb.frame[2 + i] = buf[i];
    }
    return put(addr, 2 + len);
}

proto_bool protocore_smbus_read_block(uint8_t addr, uint8_t cmd, uint8_t *out, size_t cap, size_t *len)
{
    if (out == NULL || len == NULL)
    {
        return PROTO_FALSE;
    }
    *len = 0;
    s_smb.frame[0] = cmd;
    // The count arrives before the payload, so the length is read first and the payload after it.
    if (!protocore_i2c_write_read(addr, s_smb.frame, 1, &s_smb.frame[1], 1))
    {
        return PROTO_FALSE;
    }
    size_t n = s_smb.frame[1];
    if (n == 0 || n > PROTOCORE_SMBUS_BLOCK_MAX || n > cap)
    {
        return PROTO_FALSE;
    }
    if (!take(addr, 1, n + 1u))
    {
        return PROTO_FALSE;
    }
    // frame[1] is the count the part repeated; the payload follows it.
    for (size_t i = 0; i < n; i++)
    {
        out[i] = s_smb.frame[2 + i];
    }
    *len = n;
    return PROTO_TRUE;
}

proto_bool protocore_smbus_process_call(uint8_t addr, uint8_t cmd, uint16_t value, uint16_t *out)
{
    if (out == NULL)
    {
        return PROTO_FALSE;
    }
    s_smb.frame[0] = cmd;
    s_smb.frame[1] = (uint8_t)(value & 0xFFu);
    s_smb.frame[2] = (uint8_t)(value >> 8);
    if (!take(addr, 3, 2))
    {
        return PROTO_FALSE;
    }
    *out = (uint16_t)((uint16_t)s_smb.frame[3] | ((uint16_t)s_smb.frame[4] << 8));
    return PROTO_TRUE;
}

proto_bool protocore_smbus_block_process_call(uint8_t addr, uint8_t cmd, const uint8_t *buf, size_t len, uint8_t *out,
                                              size_t cap, size_t *out_len)
{
    if (buf == NULL || out == NULL || out_len == NULL || len == 0 || len > PROTOCORE_SMBUS_BLOCK_MAX)
    {
        return PROTO_FALSE;
    }
    *out_len = 0;
    s_smb.frame[0] = cmd;
    s_smb.frame[1] = (uint8_t)len;
    for (size_t i = 0; i < len; i++)
    {
        s_smb.frame[2 + i] = buf[i];
    }
    size_t slen = 2 + len;
    // The reply opens with its own count byte, so one is read before the payload it sizes.
    if (!protocore_i2c_write_read(addr, s_smb.frame, slen, &s_smb.frame[slen], 1))
    {
        return PROTO_FALSE;
    }
    size_t n = s_smb.frame[slen];
    if (n == 0 || n > PROTOCORE_SMBUS_BLOCK_MAX || n > cap || slen + n + 2u > sizeof(s_smb.frame))
    {
        return PROTO_FALSE;
    }
    if (!take(addr, slen, n + 1u))
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < n; i++)
    {
        out[i] = s_smb.frame[slen + 1 + i];
    }
    *out_len = n;
    return PROTO_TRUE;
}

#else // no bus seam. The PEC above is host-tested.

proto_bool protocore_smbus_begin(void)
{
    return PROTO_FALSE;
}

proto_bool protocore_smbus_quick(uint8_t addr, uint8_t rw)
{
    (void)addr;
    (void)rw;
    return PROTO_FALSE;
}

proto_bool protocore_smbus_send_byte(uint8_t addr, uint8_t value)
{
    (void)addr;
    (void)value;
    return PROTO_FALSE;
}

proto_bool protocore_smbus_receive_byte(uint8_t addr, uint8_t *out)
{
    (void)addr;
    (void)out;
    return PROTO_FALSE;
}

proto_bool protocore_smbus_write_byte(uint8_t addr, uint8_t cmd, uint8_t value)
{
    (void)addr;
    (void)cmd;
    (void)value;
    return PROTO_FALSE;
}

proto_bool protocore_smbus_read_byte(uint8_t addr, uint8_t cmd, uint8_t *out)
{
    (void)addr;
    (void)cmd;
    (void)out;
    return PROTO_FALSE;
}

proto_bool protocore_smbus_write_word(uint8_t addr, uint8_t cmd, uint16_t value)
{
    (void)addr;
    (void)cmd;
    (void)value;
    return PROTO_FALSE;
}

proto_bool protocore_smbus_read_word(uint8_t addr, uint8_t cmd, uint16_t *out)
{
    (void)addr;
    (void)cmd;
    (void)out;
    return PROTO_FALSE;
}

proto_bool protocore_smbus_write_block(uint8_t addr, uint8_t cmd, const uint8_t *buf, size_t len)
{
    (void)addr;
    (void)cmd;
    (void)buf;
    (void)len;
    return PROTO_FALSE;
}

proto_bool protocore_smbus_read_block(uint8_t addr, uint8_t cmd, uint8_t *out, size_t cap, size_t *len)
{
    (void)addr;
    (void)cmd;
    (void)out;
    (void)cap;
    (void)len;
    return PROTO_FALSE;
}

proto_bool protocore_smbus_process_call(uint8_t addr, uint8_t cmd, uint16_t value, uint16_t *out)
{
    (void)addr;
    (void)cmd;
    (void)value;
    (void)out;
    return PROTO_FALSE;
}

proto_bool protocore_smbus_block_process_call(uint8_t addr, uint8_t cmd, const uint8_t *buf, size_t len, uint8_t *out,
                                              size_t cap, size_t *out_len)
{
    (void)addr;
    (void)cmd;
    (void)buf;
    (void)len;
    (void)out;
    (void)cap;
    (void)out_len;
    return PROTO_FALSE;
}

#endif // PROTOCORE_HAS_BUS

#endif // PROTOCORE_ENABLE_SMBUS
