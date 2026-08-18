// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file lora.c
 * @brief LoRa codec + SX127x driver - implementation.
 *
 * The codec is the RadioHead 4-byte header. The driver speaks the SX1276/77/78/79 LoRa
 * register protocol (datasheet register map below) through the caller's register-access
 * bus, so the sequence is host-testable with a mock register file and portable across SPI
 * peripherals. The RF link itself needs the module.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_LORA

#include "services/radio/lora/lora.h"

PROTOCORE_BEGIN_DECLS

// SX127x LoRa register map (SX1276 datasheet, Table 41).
#define REG_FIFO 0x00
#define REG_OP_MODE 0x01
#define REG_FRF_MSB 0x06
#define REG_FRF_MID 0x07
#define REG_FRF_LSB 0x08
#define REG_PA_CONFIG 0x09
#define REG_FIFO_ADDR_PTR 0x0D
#define REG_FIFO_TX_BASE 0x0E
#define REG_FIFO_RX_BASE 0x0F
#define REG_FIFO_RX_CURRENT 0x10
#define REG_IRQ_FLAGS 0x12
#define REG_RX_NB_BYTES 0x13
#define REG_PKT_RSSI 0x1A
#define REG_MODEM_CONFIG1 0x1D
#define REG_MODEM_CONFIG2 0x1E
#define REG_PREAMBLE_MSB 0x20
#define REG_PREAMBLE_LSB 0x21
#define REG_PAYLOAD_LENGTH 0x22
#define REG_MODEM_CONFIG3 0x26
#define REG_SYNC_WORD 0x39
#define REG_VERSION 0x42

// RegOpMode: LongRangeMode bit + transceiver mode.
#define MODE_LORA 0x80
#define MODE_SLEEP 0x00
#define MODE_STDBY 0x01
#define MODE_TX 0x03
#define MODE_RX_CONT 0x05

// RegIrqFlags.
#define IRQ_TX_DONE 0x08
#define IRQ_PAYLOAD_CRC_ERROR 0x20
#define IRQ_RX_DONE 0x40

static const uint8_t SX127X_VERSION = 0x12;

static inline uint8_t rd(const protocore_lora_bus *b, uint8_t reg)
{
    return b->read(reg, b->ctx);
}
static inline void wr(const protocore_lora_bus *b, uint8_t reg, uint8_t val)
{
    b->write(reg, val, b->ctx);
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void lora_frame_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *raw = Lora.frame_parse_args.raw;
    uint16_t len = Lora.frame_parse_args.len;
    protocore_lora_header *hdr = Lora.frame_parse_args.hdr;
    const uint8_t **payload = Lora.frame_parse_args.payload;
    uint16_t *payload_len = Lora.frame_parse_args.payload_len;

    if (!raw || !hdr || len < 4)
    {
        Lora.ok = PROTO_FALSE;
        return;
    }
    hdr->to = raw[0];
    hdr->from = raw[1];
    hdr->id = raw[2];
    hdr->flags = raw[3];
    if (payload)
    {
        *payload = raw + 4;
    }
    if (payload_len)
    {
        *payload_len = (uint16_t)(len - 4);
    }
    Lora.ok = PROTO_TRUE;
}

static void lora_frame_build(uint8_t *restrict work)
{
    (void)work;
    const protocore_lora_header *hdr = Lora.frame_build_args.hdr;
    const uint8_t *payload = Lora.frame_build_args.payload;
    uint16_t len = Lora.frame_build_args.len;
    uint8_t *out = Lora.frame_build_args.out;
    uint16_t cap = Lora.frame_build_args.cap;

    if (!hdr || !out || len > PROTOCORE_LORA_MAX_PAYLOAD || (uint32_t)len + 4 > cap)
    {
        Lora.value = 0;
        return;
    }
    out[0] = hdr->to;
    out[1] = hdr->from;
    out[2] = hdr->id;
    out[3] = hdr->flags;
    for (uint16_t i = 0; i < len; i++)
    {
        out[4 + i] = payload[i];
    }
    Lora.value = (uint16_t)(len + 4);
}

static void lora_init(uint8_t *restrict work)
{
    (void)work;
    const protocore_lora_bus *bus = Lora.init_args.bus;
    const protocore_lora_config *cfg = Lora.init_args.cfg;

    if (!bus || !bus->read || !bus->write || !cfg)
    {
        Lora.ok = PROTO_FALSE;
        return;
    }
    if (rd(bus, REG_VERSION) != SX127X_VERSION)
    {
        Lora.ok = PROTO_FALSE; // the bus is not talking to an SX127x
        return;
    }

    // Switch to LoRa mode (only settable from sleep), then standby.
    wr(bus, REG_OP_MODE, MODE_SLEEP);
    wr(bus, REG_OP_MODE, MODE_LORA | MODE_SLEEP);
    wr(bus, REG_OP_MODE, MODE_LORA | MODE_STDBY);

    // Carrier frequency: Frf = freq / FSTEP, FSTEP = 32 MHz / 2^19.
    uint32_t frf = (uint32_t)(((uint64_t)cfg->freq_hz << 19) / 32000000UL);
    wr(bus, REG_FRF_MSB, (uint8_t)(frf >> 16));
    wr(bus, REG_FRF_MID, (uint8_t)(frf >> 8));
    wr(bus, REG_FRF_LSB, (uint8_t)frf);

    wr(bus, REG_FIFO_TX_BASE, 0x00);
    wr(bus, REG_FIFO_RX_BASE, 0x00);

    // Modem config: explicit header, CRC on, AGC auto; low-data-rate optimize at SF11/12.
    wr(bus, REG_MODEM_CONFIG1, (uint8_t)((cfg->bandwidth << 4) | (cfg->coding_rate << 1)));
    wr(bus, REG_MODEM_CONFIG2, (uint8_t)((cfg->spreading << 4) | 0x04));
    wr(bus, REG_MODEM_CONFIG3, (uint8_t)((cfg->spreading >= 11 ? 0x08 : 0x00) | 0x04));

    wr(bus, REG_PREAMBLE_MSB, 0x00);
    wr(bus, REG_PREAMBLE_LSB, 0x08);
    wr(bus, REG_SYNC_WORD, cfg->sync_word);
    wr(bus, REG_PA_CONFIG, (uint8_t)(0x80 | ((cfg->tx_power - 2) & 0x0F))); // PA_BOOST pin

    wr(bus, REG_OP_MODE, MODE_LORA | MODE_STDBY);
    Lora.ok = PROTO_TRUE;
}

static void lora_send(uint8_t *restrict work)
{
    (void)work;
    const protocore_lora_bus *bus = Lora.send_args.bus;
    const uint8_t *frame = Lora.send_args.frame;
    uint8_t len = Lora.send_args.len;

    if (!bus || !frame || len == 0 || len > PROTOCORE_LORA_MAX_PAYLOAD + 4)
    {
        Lora.ok = PROTO_FALSE;
        return;
    }
    wr(bus, REG_OP_MODE, MODE_LORA | MODE_STDBY);
    wr(bus, REG_FIFO_ADDR_PTR, 0x00);
    for (uint8_t i = 0; i < len; i++)
    {
        wr(bus, REG_FIFO, frame[i]);
    }
    wr(bus, REG_PAYLOAD_LENGTH, len);
    wr(bus, REG_OP_MODE, MODE_LORA | MODE_TX);
    Lora.ok = PROTO_TRUE;
}

static void lora_tx_done(uint8_t *restrict work)
{
    (void)work;
    const protocore_lora_bus *bus = Lora.tx_done_args.bus;

    if (!bus)
    {
        Lora.ok = PROTO_FALSE;
        return;
    }
    if (rd(bus, REG_IRQ_FLAGS) & IRQ_TX_DONE)
    {
        wr(bus, REG_IRQ_FLAGS, 0xFF); // clear all IRQ flags
        Lora.ok = PROTO_TRUE;
        return;
    }
    Lora.ok = PROTO_FALSE;
}

static void lora_set_rx(uint8_t *restrict work)
{
    (void)work;
    const protocore_lora_bus *bus = Lora.set_rx_args.bus;

    if (!bus)
    {
        return;
    }
    wr(bus, REG_FIFO_ADDR_PTR, 0x00);
    wr(bus, REG_OP_MODE, MODE_LORA | MODE_RX_CONT);
}

static void lora_recv(uint8_t *restrict work)
{
    (void)work;
    const protocore_lora_bus *bus = Lora.recv_args.bus;
    uint8_t *buf = Lora.recv_args.buf;
    uint8_t cap = Lora.recv_args.cap;
    int16_t *rssi = Lora.recv_args.rssi;

    if (!bus || !buf)
    {
        Lora.n = -1;
        return;
    }
    uint8_t flags = rd(bus, REG_IRQ_FLAGS);
    if (!(flags & IRQ_RX_DONE))
    {
        Lora.n = -1; // nothing received
        return;
    }
    if (flags & IRQ_PAYLOAD_CRC_ERROR)
    {
        wr(bus, REG_IRQ_FLAGS, 0xFF);
        Lora.n = -1; // corrupt frame, dropped
        return;
    }
    uint8_t len = rd(bus, REG_RX_NB_BYTES);
    wr(bus, REG_FIFO_ADDR_PTR, rd(bus, REG_FIFO_RX_CURRENT));
    uint8_t n = 0;
    for (uint8_t i = 0; i < len; i++)
    {
        uint8_t b = rd(bus, REG_FIFO); // advances the FIFO pointer
        if (n < cap)
        {
            buf[n++] = b;
        }
    }
    if (rssi)
    {
        *rssi = (int16_t)(-157 + rd(bus, REG_PKT_RSSI)); // HF port (868/915 MHz)
    }
    wr(bus, REG_IRQ_FLAGS, 0xFF);
    Lora.n = (int)n;
}

LoraNs Lora = {.frame_parse = lora_frame_parse,
               .frame_build = lora_frame_build,
               .init = lora_init,
               .send = lora_send,
               .tx_done = lora_tx_done,
               .set_rx = lora_set_rx,
               .recv = lora_recv};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_LORA
