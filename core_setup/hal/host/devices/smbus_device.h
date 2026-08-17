// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host device model: an SMBus slave.
//
// Not a part but a protocol, so this models a conformant device rather than a particular chip: it
// answers the transaction shapes, keeps a word per command code and one block, and checks the
// Packet Error Code the way a slave that implements it must.
//
// The governing document is the SMBus specification version 3.1 (19 March 2018), cached at
// docs/learn/datasheets/smbus31.pdf. Every rule below carries the section it came from:
//
//   6.4    "The PEC is a CRC-8 error-checking byte, calculated on all the message bytes
//          (including addresses and read/write bits)."
//   6.4.1  "A device that acts as a slave and supports the PEC must always be prepared to perform
//          the slave transfer with or without a PEC, verify the correctness of the PEC if present,
//          and only process the message if the PEC is correct."
//   6.4.2  the PEC is a CRC-8 over C(x) = x^8 + x^2 + x^1 + 1, "calculated in the order of the bits
//          as received" - so polynomial 0x07, no reflection, and the library's
//          PROTOCORE_CRC8_SMBUS is exactly that
//   6.5.7  Block Write is [addr+Wr][command][count = N][data 1..N], the count NOT including the
//          PEC; Block Read repeats START and the count comes back from the slave
//
// The address inclusion is what this model is for. A PEC taken over the payload alone still
// produces a byte, and a suite that asserts a hand-computed expected value can agree with a driver
// that computed the same wrong thing. Here the slave recomputes it independently over the whole
// message, so a checksum that skips the address byte is rejected.
//
// test/ style: plain host C, like the rest of the host HAL.

#ifndef PROTOCORE_HOST_DEVICE_SMBUS_H
#define PROTOCORE_HOST_DEVICE_SMBUS_H

#include "protocore_net_host.h" // protocore_bus_host_attach

#include <stdint.h>

#define PROTOCORE_SMBUS_DEV_BLOCK_MAX 32u

typedef struct
{
    uint16_t reg[256];                          /**< a word per command code; a byte access uses the low half */
    uint8_t block[PROTOCORE_SMBUS_DEV_BLOCK_MAX]; /**< the block last written, and what a block read returns */
    uint8_t block_len;
    uint8_t addr;      /**< the address it answers to, which the PEC covers */
    uint8_t pec;       /**< the master and this slave agreed to use a Packet Error Code */
    uint8_t rejected;  /**< messages refused for a wrong PEC, per 6.4.1 */
    uint8_t last_cmd;  /**< the command code of the last transaction */
    uint8_t block_cmd; /**< the command code that carries a block; every other one is a byte or a word */
    // 6.5.6 Process Call: the master sends a word and the slave answers with one it computed, which
    // is a different value from the one it was sent. A slave knows which command code does that
    // from its own datasheet, so the suite states it here.
    uint8_t process_cmd;
    uint16_t process_reply;
    // Corrupt the PEC this slave supplies, which is the line noise 6.4 exists to catch. A slave
    // does not do this on purpose; it is how a suite reaches the master's verify path.
    uint8_t corrupt_pec;
} protocore_smbus_dev;

/** @brief 6.4.2: CRC-8 with polynomial 0x07, MSB first, no reflection, seeded from @p crc. */
static inline uint8_t protocore_smbus_dev_crc(uint8_t crc, const uint8_t *b, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        crc = (uint8_t)(crc ^ b[i]);
        for (uint32_t k = 0; k < 8u; k++)
        {
            crc = (uint8_t)((crc & 0x80u) ? ((crc << 1) ^ 0x07u) : (crc << 1));
        }
    }
    return crc;
}

/** @brief Power-up state: every command code zero, no block, no PEC agreed. */
static inline void protocore_smbus_dev_init(protocore_smbus_dev *d, uint8_t addr)
{
    for (uint32_t i = 0; i < 256u; i++)
    {
        d->reg[i] = 0;
    }
    for (uint32_t i = 0; i < PROTOCORE_SMBUS_DEV_BLOCK_MAX; i++)
    {
        d->block[i] = 0;
    }
    d->block_len = 0;
    d->addr = addr;
    d->pec = 0;
    d->rejected = 0;
    d->last_cmd = 0;
    d->block_cmd = 0xFFu;
    d->process_cmd = 0xFEu;
    d->process_reply = 0;
    d->corrupt_pec = 0;
}

// One transfer. The seam hands over a whole transaction: what the master wrote, and how many bytes
// it then wants back. 6.4 puts the address bytes inside the checksum, so the write half is folded
// in behind the write address and the read half behind the read address.
static inline uint32_t protocore_smbus_dev_txn(void *ctx, const uint8_t *w, uint32_t wlen, uint8_t *r, uint32_t rlen)
{
    protocore_smbus_dev *d = (protocore_smbus_dev *)ctx;
    const uint8_t wr_addr = (uint8_t)((d->addr << 1) | 0u);
    const uint8_t rd_addr = (uint8_t)((d->addr << 1) | 1u);

    if (w && wlen)
    {
        uint32_t body = wlen;
        if (d->pec && rlen == 0u)
        {
            // 6.4.1: a write carrying a PEC is only processed when the PEC is correct.
            body = wlen - 1u;
            uint8_t crc = protocore_smbus_dev_crc(0u, &wr_addr, 1u);
            crc = protocore_smbus_dev_crc(crc, w, body);
            if (crc != w[wlen - 1u])
            {
                d->rejected++;
                return 0u;
            }
        }
        d->last_cmd = w[0];
        if (w[0] == d->block_cmd && body >= 2u && w[1] == body - 2u && w[1] <= PROTOCORE_SMBUS_DEV_BLOCK_MAX)
        {
            // 6.5.7 Block Write: the count is the data length and excludes the PEC.
            d->block_len = w[1];
            for (uint32_t i = 0; i < d->block_len; i++)
            {
                d->block[i] = w[2u + i];
            }
        }
        else if (body == 2u)
        {
            d->reg[w[0]] = w[1]; // write byte
        }
        else if (body == 3u)
        {
            d->reg[w[0]] = (uint16_t)(w[1] | ((uint16_t)w[2] << 8)); // write word, low byte first
        }
    }
    if (rlen == 0u)
    {
        return 0u;
    }

    // The read half. 6.5.7 puts the count in front of a block, and the master reads it on its own
    // first to learn the length - so which shape this is comes from the command code, the way a
    // real slave knows it from its own datasheet, not from how many bytes were asked for.
    uint8_t out[PROTOCORE_SMBUS_DEV_BLOCK_MAX + 2u];
    uint32_t n = 0;
    const uint32_t want = d->pec ? rlen - 1u : rlen;
    if (d->last_cmd == d->block_cmd)
    {
        out[n++] = d->block_len;
        for (uint32_t i = 0; i < d->block_len && n < sizeof(out) && n < want; i++)
        {
            out[n++] = d->block[i];
        }
    }
    else
    {
        // 6.5.6: a process call answers with the word the slave computed, not the one it was sent.
        const uint16_t v = (d->last_cmd == d->process_cmd) ? d->process_reply : d->reg[d->last_cmd];
        out[n++] = (uint8_t)(v & 0xFFu);
        if (want > 1u)
        {
            out[n++] = (uint8_t)(v >> 8);
        }
    }
    if (d->pec)
    {
        // 6.4: the checksum spans the whole message - the write address and what was written, then
        // the read address and what is coming back.
        uint8_t crc = protocore_smbus_dev_crc(0u, &wr_addr, 1u);
        if (w && wlen)
        {
            crc = protocore_smbus_dev_crc(crc, w, wlen);
        }
        crc = protocore_smbus_dev_crc(crc, &rd_addr, 1u);
        crc = protocore_smbus_dev_crc(crc, out, n);
        if (n < sizeof(out))
        {
            out[n++] = (uint8_t)(d->corrupt_pec ? (crc ^ 0xFFu) : crc);
        }
    }
    uint32_t got = 0;
    while (got < rlen && got < n)
    {
        r[got] = out[got];
        got++;
    }
    return got;
}

/** @brief Reset the model and put it on the I2C bus at one address. 0 when the table is full. */
static inline int protocore_smbus_dev_place(protocore_smbus_dev *d, uint16_t addr)
{
    protocore_smbus_dev_init(d, (uint8_t)addr);
    return protocore_bus_host_attach(PROTOCORE_BUS_HOST_I2C, addr, d, protocore_smbus_dev_txn);
}

#endif // PROTOCORE_HOST_DEVICE_SMBUS_H
