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

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t crc_work[16]; // the borrow an entry takes; Crc never reads it

#if PROTOCORE_ENABLE_SMBUS

#if !PROTOCORE_HAS_BUS
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_SMBUS needs a bus master (an I2C master). Provide one in test/core_setup/hal/<vendor>, or\
 turn the driver off - there is no software stand-in for a part on the other end of a bus."
#endif

#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from
#include "server/peripherals/i2c.h"
#include "server/peripherals/smbus/smbus.h"
#include "shared/crc/crc.h" // PROTOCORE_CRC8_SMBUS: the PEC polynomial, host-tested in test_crc

PROTOCORE_BEGIN_DECLS

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

void protocore_smbus_addr_byte(uint8_t *restrict work);
void protocore_smbus_pec_read(uint8_t *restrict work);
void protocore_smbus_pec_write(uint8_t *restrict work);

void protocore_smbus_addr_byte(uint8_t *restrict work)
{
    (void)work;
    uint8_t addr = SmbusV.addr_byte_args.addr;
    uint8_t rw = SmbusV.addr_byte_args.rw;

    SmbusV.value = (uint8_t)(((addr & 0x7Fu) << 1) | (rw & 1u));
}

// The register a PEC starts from.
static uint32_t pec_begin(void)
{
    Crc.args.params = &PROTOCORE_CRC8_SMBUS;
    Crc.begin(crc_work);
    return Crc.value;
}

// Fold @p len octets at @p data into the running register @p crc.
static uint32_t pec_fold(uint32_t crc, const uint8_t *data, size_t len)
{
    Crc.args.params = &PROTOCORE_CRC8_SMBUS;
    Crc.args.crc = crc;
    Crc.args.data = data;
    Crc.args.len = len;
    Crc.update(crc_work);
    return Crc.value;
}

// The PEC octet a running register finishes to.
static uint8_t pec_final(uint32_t crc)
{
    Crc.args.params = &PROTOCORE_CRC8_SMBUS;
    Crc.args.crc = crc;
    Crc.final(crc_work);
    return (uint8_t)Crc.value;
}

void protocore_smbus_pec_write(uint8_t *restrict work)
{
    uint8_t addr = SmbusV.pec_write_args.addr;
    const uint8_t *payload = SmbusV.pec_write_args.payload;
    size_t len = SmbusV.pec_write_args.len;

    SmbusV.addr_byte_args.addr = addr;
    SmbusV.addr_byte_args.rw = PROTOCORE_SMBUS_WRITE;
    protocore_smbus_addr_byte(work);
    uint8_t a = SmbusV.value;
    uint32_t c = pec_fold(pec_begin(), &a, 1);
    if (payload != NULL && len > 0)
    {
        c = pec_fold(c, payload, len);
    }
    SmbusV.value = pec_final(c);
}

void protocore_smbus_pec_read(uint8_t *restrict work)
{
    uint8_t addr = SmbusV.pec_read_args.addr;
    const uint8_t *sent = SmbusV.pec_read_args.sent;
    size_t slen = SmbusV.pec_read_args.slen;
    const uint8_t *got = SmbusV.pec_read_args.got;
    size_t glen = SmbusV.pec_read_args.glen;

    SmbusV.addr_byte_args.addr = addr;
    SmbusV.addr_byte_args.rw = PROTOCORE_SMBUS_WRITE;
    protocore_smbus_addr_byte(work);
    uint8_t aw = SmbusV.value;
    SmbusV.addr_byte_args.addr = addr;
    SmbusV.addr_byte_args.rw = PROTOCORE_SMBUS_READ;
    protocore_smbus_addr_byte(work);
    uint8_t ar = SmbusV.value;
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
    SmbusV.value = pec_final(c);
}

void protocore_smbus_set_pec(uint8_t *restrict work)
{
    proto_bool on = SmbusV.set_pec_args.on;

    SMBUS_CTX(work)->pec = on;
}

void protocore_smbus_pec_enabled(uint8_t *restrict work)
{

    SmbusV.ok = SMBUS_CTX(work)->pec;
}

void protocore_smbus_begin(uint8_t *restrict work)
{
    (void)work;

    SmbusV.ok = protocore_i2c_begin();
}

// Put @p n composed bytes on the wire, appending the PEC over them when it is on.
static proto_bool put(uint8_t *restrict work, uint8_t addr, size_t n)
{
    if (SMBUS_CTX(work)->pec)
    {
        SmbusV.pec_write_args.addr = addr;
        SmbusV.pec_write_args.payload = SMBUS_CTX(work)->frame;
        SmbusV.pec_write_args.len = n;
        protocore_smbus_pec_write(work);
        SMBUS_CTX(work)->frame[n] = SmbusV.value;
        n++;
    }
    return protocore_i2c_write(addr, SMBUS_CTX(work)->frame, n);
}

// Read @p n bytes answering the @p slen bytes already composed in the frame, checking the PEC that
// follows them when it is on. The reply lands at frame[slen] so the sent and received spans stay
// separate for the checksum.
static proto_bool take(uint8_t *restrict work, uint8_t addr, size_t slen, size_t n)
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
    SmbusV.pec_read_args.addr = addr;
    SmbusV.pec_read_args.sent = SMBUS_CTX(work)->frame;
    SmbusV.pec_read_args.slen = slen;
    SmbusV.pec_read_args.got = &SMBUS_CTX(work)->frame[slen];
    SmbusV.pec_read_args.glen = n;
    protocore_smbus_pec_read(work);
    uint8_t want_pec = SmbusV.value;
    return SMBUS_CTX(work)->frame[slen + n] == want_pec;
}

void protocore_smbus_quick(uint8_t *restrict work)
{
    uint8_t addr = SmbusV.quick_args.addr;
    uint8_t rw = SmbusV.quick_args.rw;

    // The direction bit is the whole payload, so this is an address cycle and nothing else. A
    // quick command carries no PEC: there are no data bytes for one to cover.
    SmbusV.ok = (rw & 1u) != 0 ? protocore_i2c_read(addr, SMBUS_CTX(work)->frame, 0) : protocore_i2c_probe(addr);
}

void protocore_smbus_send_byte(uint8_t *restrict work)
{
    uint8_t addr = SmbusV.send_byte_args.addr;
    uint8_t value = SmbusV.send_byte_args.value;

    SMBUS_CTX(work)->frame[0] = value;
    SmbusV.ok = put(work, addr, 1);
}

void protocore_smbus_receive_byte(uint8_t *restrict work)
{
    uint8_t addr = SmbusV.receive_byte_args.addr;
    uint8_t *out = SmbusV.receive_byte_args.out;

    if (out == NULL)
    {
        SmbusV.ok = PROTO_FALSE;
        return;
    }
    // No command goes out, so the PEC covers the read address byte and the data alone.
    size_t want = SMBUS_CTX(work)->pec ? 2u : 1u;
    if (!protocore_i2c_read(addr, SMBUS_CTX(work)->frame, want))
    {
        SmbusV.ok = PROTO_FALSE;
        return;
    }
    // The checksum is computed inside the branch, not beside it: staged above the test it would run
    // whether or not the PEC is on, which is a CRC the original never took.
    if (SMBUS_CTX(work)->pec)
    {
        SmbusV.pec_read_args.addr = addr;
        SmbusV.pec_read_args.sent = NULL;
        SmbusV.pec_read_args.slen = 0;
        SmbusV.pec_read_args.got = SMBUS_CTX(work)->frame;
        SmbusV.pec_read_args.glen = 1;
        protocore_smbus_pec_read(work);
        if (SMBUS_CTX(work)->frame[1] != SmbusV.value)
        {
            SmbusV.ok = PROTO_FALSE;
            return;
        }
    }
    *out = SMBUS_CTX(work)->frame[0];
    SmbusV.ok = PROTO_TRUE;
}

void protocore_smbus_write_byte(uint8_t *restrict work)
{
    uint8_t addr = SmbusV.write_byte_args.addr;
    uint8_t cmd = SmbusV.write_byte_args.cmd;
    uint8_t value = SmbusV.write_byte_args.value;

    SMBUS_CTX(work)->frame[0] = cmd;
    SMBUS_CTX(work)->frame[1] = value;
    SmbusV.ok = put(work, addr, 2);
}

void protocore_smbus_read_byte(uint8_t *restrict work)
{
    uint8_t addr = SmbusV.read_byte_args.addr;
    uint8_t cmd = SmbusV.read_byte_args.cmd;
    uint8_t *out = SmbusV.read_byte_args.out;

    if (out == NULL)
    {
        SmbusV.ok = PROTO_FALSE;
        return;
    }
    SMBUS_CTX(work)->frame[0] = cmd;
    if (!take(work, addr, 1, 1))
    {
        SmbusV.ok = PROTO_FALSE;
        return;
    }
    *out = SMBUS_CTX(work)->frame[1];
    SmbusV.ok = PROTO_TRUE;
}

void protocore_smbus_write_word(uint8_t *restrict work)
{
    uint8_t addr = SmbusV.write_word_args.addr;
    uint8_t cmd = SmbusV.write_word_args.cmd;
    uint16_t value = SmbusV.write_word_args.value;

    SMBUS_CTX(work)->frame[0] = cmd;
    SMBUS_CTX(work)->frame[1] = (uint8_t)(value & 0xFFu); // low byte first, per the protocol
    SMBUS_CTX(work)->frame[2] = (uint8_t)(value >> 8);
    SmbusV.ok = put(work, addr, 3);
}

void protocore_smbus_read_word(uint8_t *restrict work)
{
    uint8_t addr = SmbusV.read_word_args.addr;
    uint8_t cmd = SmbusV.read_word_args.cmd;
    uint16_t *out = SmbusV.read_word_args.out;

    if (out == NULL)
    {
        SmbusV.ok = PROTO_FALSE;
        return;
    }
    SMBUS_CTX(work)->frame[0] = cmd;
    if (!take(work, addr, 1, 2))
    {
        SmbusV.ok = PROTO_FALSE;
        return;
    }
    *out = (uint16_t)((uint16_t)SMBUS_CTX(work)->frame[1] | ((uint16_t)SMBUS_CTX(work)->frame[2] << 8));
    SmbusV.ok = PROTO_TRUE;
}

void protocore_smbus_write_block(uint8_t *restrict work)
{
    uint8_t addr = SmbusV.write_block_args.addr;
    uint8_t cmd = SmbusV.write_block_args.cmd;
    const uint8_t *buf = SmbusV.write_block_args.buf;
    size_t len = SmbusV.write_block_args.len;

    if (buf == NULL || len == 0 || len > PROTOCORE_SMBUS_BLOCK_MAX)
    {
        SmbusV.ok = PROTO_FALSE;
        return;
    }
    SMBUS_CTX(work)->frame[0] = cmd;
    SMBUS_CTX(work)->frame[1] = (uint8_t)len; // the count byte the protocol puts in front of the payload
    for (size_t i = 0; i < len; i++)
    {
        SMBUS_CTX(work)->frame[2 + i] = buf[i];
    }
    SmbusV.ok = put(work, addr, 2 + len);
}

void protocore_smbus_read_block(uint8_t *restrict work)
{
    uint8_t addr = SmbusV.read_block_args.addr;
    uint8_t cmd = SmbusV.read_block_args.cmd;
    uint8_t *out = SmbusV.read_block_args.out;
    size_t cap = SmbusV.read_block_args.cap;
    size_t *len = SmbusV.read_block_args.len;

    if (out == NULL || len == NULL)
    {
        SmbusV.ok = PROTO_FALSE;
        return;
    }
    *len = 0;
    SMBUS_CTX(work)->frame[0] = cmd;
    // The count arrives before the payload, so the length is read first and the payload after it.
    if (!protocore_i2c_write_read(addr, SMBUS_CTX(work)->frame, 1, &SMBUS_CTX(work)->frame[1], 1))
    {
        SmbusV.ok = PROTO_FALSE;
        return;
    }
    size_t n = SMBUS_CTX(work)->frame[1];
    if (n == 0 || n > PROTOCORE_SMBUS_BLOCK_MAX || n > cap)
    {
        SmbusV.ok = PROTO_FALSE;
        return;
    }
    if (!take(work, addr, 1, n + 1u))
    {
        SmbusV.ok = PROTO_FALSE;
        return;
    }
    // frame[1] is the count the part repeated; the payload follows it.
    for (size_t i = 0; i < n; i++)
    {
        out[i] = SMBUS_CTX(work)->frame[2 + i];
    }
    *len = n;
    SmbusV.ok = PROTO_TRUE;
}

void protocore_smbus_process_call(uint8_t *restrict work)
{
    uint8_t addr = SmbusV.process_call_args.addr;
    uint8_t cmd = SmbusV.process_call_args.cmd;
    uint16_t value = SmbusV.process_call_args.value;
    uint16_t *out = SmbusV.process_call_args.out;

    if (out == NULL)
    {
        SmbusV.ok = PROTO_FALSE;
        return;
    }
    SMBUS_CTX(work)->frame[0] = cmd;
    SMBUS_CTX(work)->frame[1] = (uint8_t)(value & 0xFFu);
    SMBUS_CTX(work)->frame[2] = (uint8_t)(value >> 8);
    if (!take(work, addr, 3, 2))
    {
        SmbusV.ok = PROTO_FALSE;
        return;
    }
    *out = (uint16_t)((uint16_t)SMBUS_CTX(work)->frame[3] | ((uint16_t)SMBUS_CTX(work)->frame[4] << 8));
    SmbusV.ok = PROTO_TRUE;
}

void protocore_smbus_block_process_call(uint8_t *restrict work)
{
    uint8_t addr = SmbusV.block_process_call_args.addr;
    uint8_t cmd = SmbusV.block_process_call_args.cmd;
    const uint8_t *buf = SmbusV.block_process_call_args.buf;
    size_t len = SmbusV.block_process_call_args.len;
    uint8_t *out = SmbusV.block_process_call_args.out;
    size_t cap = SmbusV.block_process_call_args.cap;
    size_t *out_len = SmbusV.block_process_call_args.out_len;

    if (buf == NULL || out == NULL || out_len == NULL || len == 0 || len > PROTOCORE_SMBUS_BLOCK_MAX)
    {
        SmbusV.ok = PROTO_FALSE;
        return;
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
        SmbusV.ok = PROTO_FALSE;
        return;
    }
    size_t n = SMBUS_CTX(work)->frame[slen];
    if (n == 0 || n > PROTOCORE_SMBUS_BLOCK_MAX || n > cap || slen + n + 2u > sizeof(SMBUS_CTX(work)->frame))
    {
        SmbusV.ok = PROTO_FALSE;
        return;
    }
    if (!take(work, addr, slen, n + 1u))
    {
        SmbusV.ok = PROTO_FALSE;
        return;
    }
    for (size_t i = 0; i < n; i++)
    {
        out[i] = SMBUS_CTX(work)->frame[slen + 1 + i];
    }
    *out_len = n;
    SmbusV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
SmbusVars SmbusV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SMBUS
