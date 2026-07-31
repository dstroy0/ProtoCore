// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file lora.cpp
 * @brief LoRa codec + SX127x driver - implementation.
 *
 * The codec is the RadioHead 4-byte header. The driver speaks the SX1276/77/78/79 LoRa
 * register protocol (datasheet register map below) through the caller's register-access
 * bus, so the sequence is host-testable with a mock register file and portable across SPI
 * peripherals. The RF link itself needs the module.
 */

#include "services/radio/lora/lora.h"

#if PC_ENABLE_LORA

namespace
{
// SX127x LoRa register map (SX1276 datasheet, Table 41).
struct LoraReg
{
    static constexpr uint8_t REG_FIFO = 0x00;
    static constexpr uint8_t REG_OP_MODE = 0x01;
    static constexpr uint8_t REG_FRF_MSB = 0x06;
    static constexpr uint8_t REG_FRF_MID = 0x07;
    static constexpr uint8_t REG_FRF_LSB = 0x08;
    static constexpr uint8_t REG_PA_CONFIG = 0x09;
    static constexpr uint8_t REG_FIFO_ADDR_PTR = 0x0D;
    static constexpr uint8_t REG_FIFO_TX_BASE = 0x0E;
    static constexpr uint8_t REG_FIFO_RX_BASE = 0x0F;
    static constexpr uint8_t REG_FIFO_RX_CURRENT = 0x10;
    static constexpr uint8_t REG_IRQ_FLAGS = 0x12;
    static constexpr uint8_t REG_RX_NB_BYTES = 0x13;
    static constexpr uint8_t REG_PKT_RSSI = 0x1A;
    static constexpr uint8_t REG_MODEM_CONFIG1 = 0x1D;
    static constexpr uint8_t REG_MODEM_CONFIG2 = 0x1E;
    static constexpr uint8_t REG_PREAMBLE_MSB = 0x20;
    static constexpr uint8_t REG_PREAMBLE_LSB = 0x21;
    static constexpr uint8_t REG_PAYLOAD_LENGTH = 0x22;
    static constexpr uint8_t REG_MODEM_CONFIG3 = 0x26;
    static constexpr uint8_t REG_SYNC_WORD = 0x39;
    static constexpr uint8_t REG_VERSION = 0x42;
};

// RegOpMode: LongRangeMode bit + transceiver mode.
struct LoraMode
{
    static constexpr uint8_t MODE_LORA = 0x80;
    static constexpr uint8_t MODE_SLEEP = 0x00;
    static constexpr uint8_t MODE_STDBY = 0x01;
    static constexpr uint8_t MODE_TX = 0x03;
    static constexpr uint8_t MODE_RX_CONT = 0x05;
};

// RegIrqFlags.
struct LoraIrq
{
    static constexpr uint8_t IRQ_TX_DONE = 0x08;
    static constexpr uint8_t IRQ_PAYLOAD_CRC_ERROR = 0x20;
    static constexpr uint8_t IRQ_RX_DONE = 0x40;
};

const uint8_t SX127X_VERSION = 0x12;

inline uint8_t rd(const pc_lora_bus *b, uint8_t reg)
{
    return b->read(reg, b->ctx);
}
inline void wr(const pc_lora_bus *b, uint8_t reg, uint8_t val)
{
    b->write(reg, val, b->ctx);
}
} // namespace

bool pc_lora_frame_parse(const uint8_t *raw, uint16_t len, pc_lora_header *hdr, const uint8_t **payload,
                         uint16_t *payload_len)
{
    if (!raw || !hdr || len < 4)
    {
        return false;
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
    return true;
}

uint16_t pc_lora_frame_build(const pc_lora_header *hdr, const uint8_t *payload, uint16_t len, uint8_t *out,
                             uint16_t cap)
{
    if (!hdr || !out || len > PC_LORA_MAX_PAYLOAD || (uint32_t)len + 4 > cap)
    {
        return 0;
    }
    out[0] = hdr->to;
    out[1] = hdr->from;
    out[2] = hdr->id;
    out[3] = hdr->flags;
    for (uint16_t i = 0; i < len; i++)
    {
        out[4 + i] = payload[i];
    }
    return (uint16_t)(len + 4);
}

bool pc_lora_init(const pc_lora_bus *bus, const pc_lora_config *cfg)
{
    if (!bus || !bus->read || !bus->write || !cfg)
    {
        return false;
    }
    if (rd(bus, LoraReg::REG_VERSION) != SX127X_VERSION)
    {
        return false; // the bus is not talking to an SX127x
    }

    // Switch to LoRa mode (only settable from sleep), then standby.
    wr(bus, LoraReg::REG_OP_MODE, LoraMode::MODE_SLEEP);
    wr(bus, LoraReg::REG_OP_MODE, LoraMode::MODE_LORA | LoraMode::MODE_SLEEP);
    wr(bus, LoraReg::REG_OP_MODE, LoraMode::MODE_LORA | LoraMode::MODE_STDBY);

    // Carrier frequency: Frf = freq / FSTEP, FSTEP = 32 MHz / 2^19.
    uint32_t frf = (uint32_t)(((uint64_t)cfg->freq_hz << 19) / 32000000UL);
    wr(bus, LoraReg::REG_FRF_MSB, (uint8_t)(frf >> 16));
    wr(bus, LoraReg::REG_FRF_MID, (uint8_t)(frf >> 8));
    wr(bus, LoraReg::REG_FRF_LSB, (uint8_t)frf);

    wr(bus, LoraReg::REG_FIFO_TX_BASE, 0x00);
    wr(bus, LoraReg::REG_FIFO_RX_BASE, 0x00);

    // Modem config: explicit header, CRC on, AGC auto; low-data-rate optimize at SF11/12.
    wr(bus, LoraReg::REG_MODEM_CONFIG1, (uint8_t)((cfg->bandwidth << 4) | (cfg->coding_rate << 1)));
    wr(bus, LoraReg::REG_MODEM_CONFIG2, (uint8_t)((cfg->spreading << 4) | 0x04));
    wr(bus, LoraReg::REG_MODEM_CONFIG3, (uint8_t)((cfg->spreading >= 11 ? 0x08 : 0x00) | 0x04));

    wr(bus, LoraReg::REG_PREAMBLE_MSB, 0x00);
    wr(bus, LoraReg::REG_PREAMBLE_LSB, 0x08);
    wr(bus, LoraReg::REG_SYNC_WORD, cfg->sync_word);
    wr(bus, LoraReg::REG_PA_CONFIG, (uint8_t)(0x80 | ((cfg->tx_power - 2) & 0x0F))); // PA_BOOST pin

    wr(bus, LoraReg::REG_OP_MODE, LoraMode::MODE_LORA | LoraMode::MODE_STDBY);
    return true;
}

bool pc_lora_send(const pc_lora_bus *bus, const uint8_t *frame, uint8_t len)
{
    if (!bus || !frame || len == 0 || len > PC_LORA_MAX_PAYLOAD + 4)
    {
        return false;
    }
    wr(bus, LoraReg::REG_OP_MODE, LoraMode::MODE_LORA | LoraMode::MODE_STDBY);
    wr(bus, LoraReg::REG_FIFO_ADDR_PTR, 0x00);
    for (uint8_t i = 0; i < len; i++)
    {
        wr(bus, LoraReg::REG_FIFO, frame[i]);
    }
    wr(bus, LoraReg::REG_PAYLOAD_LENGTH, len);
    wr(bus, LoraReg::REG_OP_MODE, LoraMode::MODE_LORA | LoraMode::MODE_TX);
    return true;
}

bool pc_lora_tx_done(const pc_lora_bus *bus)
{
    if (!bus)
    {
        return false;
    }
    if (rd(bus, LoraReg::REG_IRQ_FLAGS) & LoraIrq::IRQ_TX_DONE)
    {
        wr(bus, LoraReg::REG_IRQ_FLAGS, 0xFF); // clear all IRQ flags
        return true;
    }
    return false;
}

void pc_lora_set_rx(const pc_lora_bus *bus)
{
    if (!bus)
    {
        return;
    }
    wr(bus, LoraReg::REG_FIFO_ADDR_PTR, 0x00);
    wr(bus, LoraReg::REG_OP_MODE, LoraMode::MODE_LORA | LoraMode::MODE_RX_CONT);
}

int pc_lora_recv(const pc_lora_bus *bus, uint8_t *buf, uint8_t cap, int16_t *rssi)
{
    if (!bus || !buf)
    {
        return -1;
    }
    uint8_t flags = rd(bus, LoraReg::REG_IRQ_FLAGS);
    if (!(flags & LoraIrq::IRQ_RX_DONE))
    {
        return -1; // nothing received
    }
    if (flags & LoraIrq::IRQ_PAYLOAD_CRC_ERROR)
    {
        wr(bus, LoraReg::REG_IRQ_FLAGS, 0xFF);
        return -1; // corrupt frame, dropped
    }
    uint8_t len = rd(bus, LoraReg::REG_RX_NB_BYTES);
    wr(bus, LoraReg::REG_FIFO_ADDR_PTR, rd(bus, LoraReg::REG_FIFO_RX_CURRENT));
    uint8_t n = 0;
    for (uint8_t i = 0; i < len; i++)
    {
        uint8_t b = rd(bus, LoraReg::REG_FIFO); // advances the FIFO pointer
        if (n < cap)
        {
            buf[n++] = b;
        }
    }
    if (rssi)
    {
        *rssi = (int16_t)(-157 + rd(bus, LoraReg::REG_PKT_RSSI)); // HF port (868/915 MHz)
    }
    wr(bus, LoraReg::REG_IRQ_FLAGS, 0xFF);
    return (int)n;
}

#endif // PC_ENABLE_LORA
