// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sht3x.c
 * @brief Sensirion SHT3x temperature / humidity codec - implementation. See sht3x.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t crc_work[16]; // the borrow an entry takes; Crc never reads it

#if PROTOCORE_ENABLE_SHT3X

#if !PROTOCORE_HAS_BUS
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_SHT3X needs a bus master (an I2C master). Provide one in test/core_setup/hal/<vendor>, or\
 turn the driver off - there is no software stand-in for a part on the other end of a bus."
#endif

#include "mmgr/endian/endian.h" // endian.wr16be: the commands and words are big-endian
#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from
#include "server/clock/clock.h" // pcdelay
#include "server/peripherals/i2c.h"
#include "server/peripherals/sht3x/sht3x.h"
#include "shared/crc/crc.h" // PROTOCORE_CRC8_NRSC5

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_I2C_DEVICE_BORROW persistent bytes
} Sht3xOwnCtx;
static Sht3xOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_sht3x_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_I2C_DEVICE_BORROW).buf;
    }
    return s_own.span;
}

static void sht3x_crc8(uint8_t *restrict work);
static void sht3x_parse(uint8_t *restrict work);
static void sht3x_rh_mpct(uint8_t *restrict work);
static void sht3x_temp_mc(uint8_t *restrict work);

static void sht3x_crc8(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *data = Sht3x.crc8_args.data;
    size_t len = Sht3x.crc8_args.len;

    // The Sensirion CRC-8 is the cataloge's CRC-8/NRSC-5 (poly 0x31, init 0xFF, no reflection, no final XOR).
    CrcV.args.params = &PROTOCORE_CRC8_NRSC5;
    CrcV.args.data = data;
    CrcV.args.len = len;
    Crc.compute(crc_work);
    Sht3x.crc = (uint8_t)CrcV.value;
}

static void sht3x_temp_mc(uint8_t *restrict work)
{
    (void)work;
    uint16_t raw = Sht3x.temp_mc_args.raw;

    // T[C] = -45 + 175 * raw / 65535, in milli-degrees (64-bit to avoid overflow).
    Sht3x.milli = (int32_t)(-45000 + (int64_t)175000 * raw / 65535);
}

static void sht3x_rh_mpct(uint8_t *restrict work)
{
    (void)work;
    uint16_t raw = Sht3x.rh_mpct_args.raw;

    int32_t v = (int32_t)((int64_t)100000 * raw / 65535); // RH[%] = 100 * raw / 65535
    Sht3x.milli = v > 100000 ? 100000 : v;
}

static void sht3x_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *resp = Sht3x.parse_args.resp;
    int32_t *temp_mc = Sht3x.parse_args.temp_mc;
    int32_t *rh_mpct = Sht3x.parse_args.rh_mpct;

    if (!resp)
    {
        Sht3x.ok = PROTO_FALSE;
        return;
    }
    // Each checksum is captured before the next runs: both report through the one namespace, so
    // testing them in a single expression would test the second one twice.
    Sht3x.crc8_args.data = resp;
    Sht3x.crc8_args.len = 2;
    sht3x_crc8(work);
    const uint8_t crc_t = Sht3x.crc;
    Sht3x.crc8_args.data = resp + 3;
    Sht3x.crc8_args.len = 2;
    sht3x_crc8(work);
    const uint8_t crc_h = Sht3x.crc;
    if (crc_t != resp[2] || crc_h != resp[5])
    {
        Sht3x.ok = PROTO_FALSE;
        return;
    }
    uint16_t traw = (uint16_t)(((uint16_t)resp[0] << 8) | resp[1]);
    uint16_t hraw = (uint16_t)(((uint16_t)resp[3] << 8) | resp[4]);
    if (temp_mc)
    {
        Sht3x.temp_mc_args.raw = traw;
        sht3x_temp_mc(work);
        *temp_mc = Sht3x.milli;
    }
    if (rh_mpct)
    {
        Sht3x.rh_mpct_args.raw = hraw;
        sht3x_rh_mpct(work);
        *rh_mpct = Sht3x.milli;
    }
    Sht3x.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// I2C binding
// ---------------------------------------------------------------------------

// All SHT3x I2C-binding state, owned by one instance (internal linkage): the device address and
// the bus frame, so it is one named owner, unreachable from any other translation unit. The frame
// is a member rather than a local because a transfer is composed in place: six bytes is the widest
// this part moves, three 16-bit words each followed by its CRC.
typedef struct
{
    uint8_t addr;
    uint8_t frame[6];
} Sht3xCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SHT3X_OFF_CTX 0u
static_assert(SHT3X_OFF_CTX + sizeof(Sht3xCtx) <= PROTOCORE_I2C_DEVICE_BORROW,
              "PROTOCORE_I2C_DEVICE_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(SHT3X_OFF_CTX % _Alignof(Sht3xCtx) == 0,
              "SHT3X_OFF_CTX is not a multiple of alignof(Sht3xCtx) - SHT3X_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define SHT3X_CTX(w) ((Sht3xCtx *)(void *)((w) + SHT3X_OFF_CTX))

// Zero is "no address set yet", which is the default address - stated here rather than on the
// declaration so the context carries no initializer and can live in a borrow that arrives zeroed.
// begin() applies the same default to the address it is handed.
static uint8_t dev_addr(uint8_t *restrict work)
{
    return SHT3X_CTX(work)->addr ? SHT3X_CTX(work)->addr : (uint8_t)PROTOCORE_SHT3X_I2C_ADDR;
}

// A command is a bare 16-bit word, big-endian, with no register byte in front of it.
static proto_bool send_cmd(uint8_t *restrict work, uint16_t cmd)
{
    (void)endian.wr16be(SHT3X_CTX(work)->frame, cmd);
    return protocore_i2c_write(dev_addr(work), SHT3X_CTX(work)->frame, 2);
}

static void sht3x_begin(uint8_t *restrict work)
{
    uint8_t addr = Sht3x.begin_args.addr;

    SHT3X_CTX(work)->addr = addr ? addr : (uint8_t)PROTOCORE_SHT3X_I2C_ADDR;
    protocore_i2c_begin();
    proto_bool ok = send_cmd(work, SHT3X_CMD_SOFT_RESET);
    pcdelay(2); // soft reset completes in < 1.5 ms
    Sht3x.ok = ok;
}

static void sht3x_read(uint8_t *restrict work)
{
    int32_t *temp_mc = Sht3x.read_args.temp_mc;
    int32_t *rh_mpct = Sht3x.read_args.rh_mpct;

    if (!send_cmd(work, SHT3X_CMD_SINGLE_HIGH))
    {
        Sht3x.ok = PROTO_FALSE;
        return;
    }
    pcdelay(20); // a high-repeatability measurement completes in < 15 ms
    if (!protocore_i2c_read(dev_addr(work), SHT3X_CTX(work)->frame, sizeof(SHT3X_CTX(work)->frame)))
    {
        Sht3x.ok = PROTO_FALSE;
        return;
    }
    Sht3x.parse_args.resp = SHT3X_CTX(work)->frame;
    Sht3x.parse_args.temp_mc = temp_mc;
    Sht3x.parse_args.rh_mpct = rh_mpct;
    sht3x_parse(work);
}

Sht3xNs Sht3x = {.crc8 = sht3x_crc8,
                 .temp_mc = sht3x_temp_mc,
                 .rh_mpct = sht3x_rh_mpct,
                 .parse = sht3x_parse,
                 .begin = sht3x_begin,
                 .read = sht3x_read};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SHT3X
