// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sht3x.c
 * @brief Sensirion SHT3x temperature / humidity codec - implementation. See sht3x.h.
 */

#include "services/peripherals/sht3x/sht3x.h"
#include "protocore_config.h"
#include "server/clock/clock.h"    // pcdelay
#include "shared_primitives/crc.h" // PC_CRC8_NRSC5

#if PC_ENABLE_SHT3X

#if PROTOCORE_HOT || PC_PLATFORM_HAS_BUS
#include "services/peripherals/i2c.h"
#include "shared_primitives/endian.h" // pc_wr16be: the commands and words are big-endian
#endif
uint8_t pc_sht3x_crc8(const uint8_t *data, size_t len)
{
    // The Sensirion CRC-8 is the cataloge's CRC-8/NRSC-5 (poly 0x31, init 0xFF, no reflection, no final XOR).
    return (uint8_t)pc_crc(&PC_CRC8_NRSC5, data, len);
}

int32_t pc_sht3x_temp_mc(uint16_t raw)
{
    // T[C] = -45 + 175 * raw / 65535, in milli-degrees (64-bit to avoid overflow).
    return (int32_t)(-45000 + (int64_t)175000 * raw / 65535);
}

int32_t pc_sht3x_rh_mpct(uint16_t raw)
{
    int32_t v = (int32_t)((int64_t)100000 * raw / 65535); // RH[%] = 100 * raw / 65535
    return v > 100000 ? 100000 : v;
}

proto_bool pc_sht3x_parse(const uint8_t resp[6], int32_t *temp_mc, int32_t *rh_mpct)
{
    if (!resp || pc_sht3x_crc8(resp, 2) != resp[2] || pc_sht3x_crc8(resp + 3, 2) != resp[5])
    {
        return PROTO_FALSE;
    }
    uint16_t traw = (uint16_t)(((uint16_t)resp[0] << 8) | resp[1]);
    uint16_t hraw = (uint16_t)(((uint16_t)resp[3] << 8) | resp[4]);
    if (temp_mc)
    {
        *temp_mc = pc_sht3x_temp_mc(traw);
    }
    if (rh_mpct)
    {
        *rh_mpct = pc_sht3x_rh_mpct(hraw);
    }
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// I2C binding
// ---------------------------------------------------------------------------

#if PROTOCORE_HOT || PC_PLATFORM_HAS_BUS

// All SHT3x I2C-binding state, owned by one instance (internal linkage): the device address and
// the bus frame, so it is one named owner, unreachable from any other translation unit. The frame
// is a member rather than a local because a transfer is composed in place: six bytes is the widest
// this part moves, three 16-bit words each followed by its CRC.
typedef struct
{
    uint8_t addr;
    uint8_t frame[6];
} Sht3xCtx;
static Sht3xCtx s_sht = {.addr = PC_SHT3X_I2C_ADDR};

// A command is a bare 16-bit word, big-endian, with no register byte in front of it.
static proto_bool send_cmd(uint16_t cmd)
{
    (void)pc_wr16be(s_sht.frame, cmd);
    return pc_i2c_write(s_sht.addr, s_sht.frame, 2);
}

proto_bool pc_sht3x_begin(uint8_t addr)
{
    s_sht.addr = addr ? addr : (uint8_t)PC_SHT3X_I2C_ADDR;
    pc_i2c_begin();
    proto_bool ok = send_cmd(SHT3X_CMD_SOFT_RESET);
    pcdelay(2); // soft reset completes in < 1.5 ms
    return ok;
}

proto_bool pc_sht3x_read(int32_t *temp_mc, int32_t *rh_mpct)
{
    if (!send_cmd(SHT3X_CMD_SINGLE_HIGH))
    {
        return PROTO_FALSE;
    }
    pcdelay(20); // a high-repeatability measurement completes in < 15 ms
    if (!pc_i2c_read(s_sht.addr, s_sht.frame, sizeof(s_sht.frame)))
    {
        return PROTO_FALSE;
    }
    return pc_sht3x_parse(s_sht.frame, temp_mc, rh_mpct);
}

#else // host build: no I2C. The CRC + conversion above are host-tested.

proto_bool pc_sht3x_begin(uint8_t addr)
{
    (void)addr;
    return PROTO_FALSE;
}
proto_bool pc_sht3x_read(int32_t *temp_mc, int32_t *rh_mpct)
{
    (void)temp_mc;
    (void)rh_mpct;
    return PROTO_FALSE;
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_SHT3X
