// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file smbus.c
 * @brief SMBus 3.1 transaction shapes - implementation. See smbus.h.
 *
 * A read's PEC covers two spans that are not next to each other in any buffer: the command going
 * out and the data coming back, with an address byte in front of each. The CRC engine's
 * begin / update / final form walks them in place, so nothing is copied to checksum it.
 */

#include "protocore_config.h" // the entry point: the widths

#if !PROTOCORE_HAS_BUS
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_SMBUS needs a bus master (an I2C master). Provide one in test/core_setup/hal/<vendor>, or\
 turn the driver off - there is no software stand-in for a part on the other end of a bus."
#endif

#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from
#include "server/peripherals/i2c/i2c.h"
#include "server/peripherals/smbus/smbus.h"
#include "shared/crc/crc.h" // PROTOCORE_CRC8_SMBUS: the PEC polynomial, host-tested in test_crc

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
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SMBUS_OFF_CTX 0u
static_assert(SMBUS_OFF_CTX + sizeof(SmbusCtx) <= PROTOCORE_SMBUS_BORROW,
              "PROTOCORE_SMBUS_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(SMBUS_OFF_CTX % _Alignof(SmbusCtx) == 0,
              "SMBUS_OFF_CTX is not a multiple of alignof(SmbusCtx) - SMBUS_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define SMBUS_CTX(w) ((SmbusCtx *)(void *)((w) + SMBUS_OFF_CTX))

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SMBUS_BORROW persistent bytes
} SmbusOwnCtx;
static SmbusOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_smbus_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_SMBUS_BORROW).buf;
    }
    return s_own.span;
}

uint8_t protocore_smbus_addr_byte(uint8_t *work, uint8_t addr, uint8_t rw)
{
    (void)work;

    return (uint8_t)(((addr & 0x7Fu) << 1) | (rw & 1u));
}

// The register a PEC starts from.
static uint32_t pec_begin(void)
{
    CrcV.args.params = &PROTOCORE_CRC8_SMBUS;
    Crc.begin(protocore_smbus_span());
    return CrcV.value;
}

// Fold @p len octets at @p data into the running register @p crc.
static uint32_t pec_fold(uint32_t crc, const uint8_t *data, size_t len)
{
    CrcV.args.params = &PROTOCORE_CRC8_SMBUS;
    CrcV.args.crc = crc;
    CrcV.args.data = data;
    CrcV.args.len = len;
    Crc.update(protocore_smbus_span());
    return CrcV.value;
}

// The PEC octet a running register finishes to.
static uint8_t pec_final(uint32_t crc)
{
    CrcV.args.params = &PROTOCORE_CRC8_SMBUS;
    CrcV.args.crc = crc;
    Crc.final(protocore_smbus_span());
    return (uint8_t)CrcV.value;
}

uint8_t protocore_smbus_pec_write(uint8_t *work, uint8_t addr, const uint8_t *payload, size_t len)
{

    uint8_t smbus_value = Smbus.addr_byte(work, addr, PROTOCORE_SMBUS_WRITE);
    uint8_t a = smbus_value;
    uint32_t c = pec_fold(pec_begin(), &a, 1);
    if (payload != NULL && len > 0)
    {
        c = pec_fold(c, payload, len);
    }
    smbus_value = pec_final(c);
}

uint8_t protocore_smbus_pec_read(uint8_t *work, uint8_t addr, const uint8_t *sent, size_t slen, const uint8_t *got,
                                 size_t glen)
{

    uint8_t smbus_value = Smbus.addr_byte(work, addr, PROTOCORE_SMBUS_WRITE);
    uint8_t aw = smbus_value;
    smbus_value = Smbus.addr_byte(work, addr, PROTOCORE_SMBUS_READ);
    uint8_t ar = smbus_value;
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
    smbus_value = pec_final(c);
}

void protocore_smbus_set_pec(uint8_t *work, proto_bool on)
{

    SMBUS_CTX(work)->pec = on;
}

proto_bool protocore_smbus_pec_enabled(uint8_t *work)
{

    return SMBUS_CTX(work)->pec;
}

proto_bool protocore_smbus_begin(uint8_t *work)
{
    (void)work;

    return protocore_i2c_begin();
}

// Put @p n composed bytes on the wire, appending the PEC over them when it is on.
static proto_bool put(uint8_t *work, uint8_t addr, size_t n)
{
    if (SMBUS_CTX(work)->pec)
    {
        uint8_t smbus_value = Smbus.pec_write(work, addr, SMBUS_CTX(work)->frame, n);
        SMBUS_CTX(work)->frame[n] = smbus_value;
        n++;
    }
    return protocore_i2c_write(addr, SMBUS_CTX(work)->frame, n);
}

// Read @p n bytes answering the @p slen bytes already composed in the frame, checking the PEC that
// follows them when it is on. The reply lands at frame[slen] so the sent and received spans stay
// separate for the checksum.
static proto_bool take(uint8_t *work, uint8_t addr, size_t slen, size_t n)
{
    size_t want = SMBUS_CTX(work)->pec ? n + 1u : n;
    if (slen + want > sizeof(SMBUS_CTX(work)->frame))
    {
        return PROTO_FALSE;
    }
    if (!protocore_i2c_write_read(addr, SMBUS_CTX(work)->frame, slen, &SMBUS_CTX(work)->frame[slen], want))
    {
        return PROTO_FALSE;
    }
    if (!SMBUS_CTX(work)->pec)
    {
        return PROTO_TRUE;
    }
    uint8_t smbus_value = Smbus.pec_read(work, addr, SMBUS_CTX(work)->frame, slen, &SMBUS_CTX(work)->frame[slen], n);
    uint8_t want_pec = smbus_value;
    return SMBUS_CTX(work)->frame[slen + n] == want_pec;
}

proto_bool protocore_smbus_quick(uint8_t *work, uint8_t addr, uint8_t rw)
{

    // The direction bit is the whole payload, so this is an address cycle and nothing else. A
    // quick command carries no PEC: there are no data bytes for one to cover.
    return (rw & 1u) != 0 ? protocore_i2c_read(addr, SMBUS_CTX(work)->frame, 0) : protocore_i2c_probe(addr);
}

proto_bool protocore_smbus_send_byte(uint8_t *work, uint8_t addr, uint8_t value)
{

    SMBUS_CTX(work)->frame[0] = value;
    return put(work, addr, 1);
}

proto_bool protocore_smbus_receive_byte(uint8_t *work, uint8_t addr, uint8_t *out)
{
    if (out == NULL)
    {
        return PROTO_FALSE;
    }
    // No command goes out, so the PEC covers the read address byte and the data alone.
    size_t want = SMBUS_CTX(work)->pec ? 2u : 1u;
    if (!protocore_i2c_read(addr, SMBUS_CTX(work)->frame, want))
    {
        return PROTO_FALSE;
    }
    // The checksum is computed inside the branch, not beside it: staged above the test it would run
    // whether or not the PEC is on, which is a CRC the original never took.
    if (SMBUS_CTX(work)->pec)
    {
        uint8_t smbus_value = Smbus.pec_read(work, addr, NULL, 0, SMBUS_CTX(work)->frame, 1);
        if (SMBUS_CTX(work)->frame[1] != smbus_value)
        {
            return PROTO_FALSE;
        }
    }
    *out = SMBUS_CTX(work)->frame[0];
    return PROTO_TRUE;
}

proto_bool protocore_smbus_write_byte(uint8_t *work, uint8_t addr, uint8_t cmd, uint8_t value)
{

    SMBUS_CTX(work)->frame[0] = cmd;
    SMBUS_CTX(work)->frame[1] = value;
    return put(work, addr, 2);
}

proto_bool protocore_smbus_read_byte(uint8_t *work, uint8_t addr, uint8_t cmd, uint8_t *out)
{
    if (out == NULL)
    {
        return PROTO_FALSE;
    }
    SMBUS_CTX(work)->frame[0] = cmd;
    if (!take(work, addr, 1, 1))
    {
        return PROTO_FALSE;
    }
    *out = SMBUS_CTX(work)->frame[1];
    return PROTO_TRUE;
}

proto_bool protocore_smbus_write_word(uint8_t *work, uint8_t addr, uint8_t cmd, uint16_t value)
{

    SMBUS_CTX(work)->frame[0] = cmd;
    SMBUS_CTX(work)->frame[1] = (uint8_t)(value & 0xFFu); // low byte first, per the protocol
    SMBUS_CTX(work)->frame[2] = (uint8_t)(value >> 8);
    return put(work, addr, 3);
}

proto_bool protocore_smbus_read_word(uint8_t *work, uint8_t addr, uint8_t cmd, uint16_t *out)
{
    if (out == NULL)
    {
        return PROTO_FALSE;
    }
    SMBUS_CTX(work)->frame[0] = cmd;
    if (!take(work, addr, 1, 2))
    {
        return PROTO_FALSE;
    }
    *out = (uint16_t)((uint16_t)SMBUS_CTX(work)->frame[1] | ((uint16_t)SMBUS_CTX(work)->frame[2] << 8));
    return PROTO_TRUE;
}

proto_bool protocore_smbus_write_block(uint8_t *work, uint8_t addr, uint8_t cmd, const uint8_t *buf, size_t len)
{
    if (buf == NULL || len == 0 || len > PROTOCORE_SMBUS_BLOCK_MAX)
    {
        return PROTO_FALSE;
    }
    SMBUS_CTX(work)->frame[0] = cmd;
    SMBUS_CTX(work)->frame[1] = (uint8_t)len; // the count byte the protocol puts in front of the payload
    for (size_t i = 0; i < len; i++)
    {
        SMBUS_CTX(work)->frame[2 + i] = buf[i];
    }
    return put(work, addr, 2 + len);
}

proto_bool protocore_smbus_read_block(uint8_t *work, uint8_t addr, uint8_t cmd, uint8_t *out, size_t cap, size_t *len)
{
    if (out == NULL || len == NULL)
    {
        return PROTO_FALSE;
    }
    *len = 0;
    SMBUS_CTX(work)->frame[0] = cmd;
    // The count arrives before the payload, so the length is read first and the payload after it.
    if (!protocore_i2c_write_read(addr, SMBUS_CTX(work)->frame, 1, &SMBUS_CTX(work)->frame[1], 1))
    {
        return PROTO_FALSE;
    }
    size_t n = SMBUS_CTX(work)->frame[1];
    if (n == 0 || n > PROTOCORE_SMBUS_BLOCK_MAX || n > cap)
    {
        return PROTO_FALSE;
    }
    if (!take(work, addr, 1, n + 1u))
    {
        return PROTO_FALSE;
    }
    // frame[1] is the count the part repeated; the payload follows it.
    for (size_t i = 0; i < n; i++)
    {
        out[i] = SMBUS_CTX(work)->frame[2 + i];
    }
    *len = n;
    return PROTO_TRUE;
}

proto_bool protocore_smbus_process_call(uint8_t *work, uint8_t addr, uint8_t cmd, uint16_t value, uint16_t *out)
{
    if (out == NULL)
    {
        return PROTO_FALSE;
    }
    SMBUS_CTX(work)->frame[0] = cmd;
    SMBUS_CTX(work)->frame[1] = (uint8_t)(value & 0xFFu);
    SMBUS_CTX(work)->frame[2] = (uint8_t)(value >> 8);
    if (!take(work, addr, 3, 2))
    {
        return PROTO_FALSE;
    }
    *out = (uint16_t)((uint16_t)SMBUS_CTX(work)->frame[3] | ((uint16_t)SMBUS_CTX(work)->frame[4] << 8));
    return PROTO_TRUE;
}

proto_bool protocore_smbus_block_process_call(uint8_t *work, uint8_t addr, uint8_t cmd, const uint8_t *buf, size_t len,
                                              uint8_t *out, size_t cap, size_t *out_len)
{
    if (buf == NULL || out == NULL || out_len == NULL || len == 0 || len > PROTOCORE_SMBUS_BLOCK_MAX)
    {
        return PROTO_FALSE;
    }
    *out_len = 0;
    SMBUS_CTX(work)->frame[0] = cmd;
    SMBUS_CTX(work)->frame[1] = (uint8_t)len;
    for (size_t i = 0; i < len; i++)
    {
        SMBUS_CTX(work)->frame[2 + i] = buf[i];
    }
    size_t slen = 2 + len;
    // The reply opens with its own count byte, so one is read before the payload it sizes.
    if (!protocore_i2c_write_read(addr, SMBUS_CTX(work)->frame, slen, &SMBUS_CTX(work)->frame[slen], 1))
    {
        return PROTO_FALSE;
    }
    size_t n = SMBUS_CTX(work)->frame[slen];
    if (n == 0 || n > PROTOCORE_SMBUS_BLOCK_MAX || n > cap || slen + n + 2u > sizeof(SMBUS_CTX(work)->frame))
    {
        return PROTO_FALSE;
    }
    if (!take(work, addr, slen, n + 1u))
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < n; i++)
    {
        out[i] = SMBUS_CTX(work)->frame[slen + 1 + i];
    }
    *out_len = n;
    return PROTO_TRUE;
}
