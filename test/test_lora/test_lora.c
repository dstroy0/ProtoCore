// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the LoRa codec + SX127x driver (services/radio/lora). The codec (RadioHead
// 4-byte header parse/build) is pure. The driver's register protocol is exercised against
// a mock SX127x: a register file plus a FIFO with the chip's auto-incrementing address
// pointer, so init / send / tx-done / set-rx / recv are verified without a radio (the RF
// link itself needs the module).

#include "services/radio/lora/lora.h"
#include <string.h>
#include <unity.h>

// --- Mock SX127x -----------------------------------------------------------------------

typedef struct
{
    uint8_t reg[128];
    uint8_t fifo[256];
    uint16_t fifo_ptr;
} MockChip;
static MockChip g_chip;

static uint8_t mock_read(uint8_t reg, void *ctx)
{
    MockChip *c = (MockChip *)ctx;
    if (reg == 0x00) // RegFifo: read at the pointer, then advance
    {
        return c->fifo[c->fifo_ptr++ & 0xFF];
    }
    return c->reg[reg & 0x7F];
}
static void mock_write(uint8_t reg, uint8_t val, void *ctx)
{
    MockChip *c = (MockChip *)ctx;
    if (reg == 0x0D) // RegFifoAddrPtr: sets the internal FIFO pointer
    {
        c->fifo_ptr = val;
        c->reg[0x0D] = val;
        return;
    }
    if (reg == 0x00) // RegFifo: write at the pointer, then advance
    {
        c->fifo[c->fifo_ptr++ & 0xFF] = val;
        return;
    }
    c->reg[reg & 0x7F] = val;
}

static pc_lora_bus g_bus = {mock_read, mock_write, &g_chip};

static pc_lora_config default_cfg()
{
    pc_lora_config c = {0};
    c.freq_hz = 915000000UL;
    c.spreading = 7;
    c.bandwidth = 7; // 125 kHz
    c.coding_rate = 1;
    c.sync_word = 0x12;
    c.tx_power = 17;
    return c;
}

void setUp()
{
    memset(&g_chip, 0, sizeof(g_chip));
    g_chip.reg[0x42] = 0x12; // RegVersion = SX127x id by default
}
void tearDown()
{
}

// --- Codec ------------------------------------------------------------------------------

void test_frame_build_then_parse()
{
    pc_lora_header h = {0xAA, 0x02, 0x03, 0x00};
    const uint8_t pay[3] = {'h', 'i', '!'};
    uint8_t frame[16];
    uint16_t n = pc_lora_frame_build(&h, pay, 3, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT16(7, n); // 4 header + 3 payload

    pc_lora_header out = {0};
    const uint8_t *p = NULL;
    uint16_t pl = 0;
    TEST_ASSERT_TRUE(pc_lora_frame_parse(frame, n, &out, &p, &pl));
    TEST_ASSERT_EQUAL_UINT8(0xAA, out.to);
    TEST_ASSERT_EQUAL_UINT8(0x02, out.from);
    TEST_ASSERT_EQUAL_UINT8(0x03, out.id);
    TEST_ASSERT_EQUAL_UINT16(3, pl);
    TEST_ASSERT_EQUAL_MEMORY(pay, p, 3);
}

void test_frame_parse_rejects_short()
{
    pc_lora_header h = {0};
    const uint8_t raw[3] = {1, 2, 3};
    TEST_ASSERT_FALSE(pc_lora_frame_parse(raw, 3, &h, NULL, NULL)); // shorter than the header
}

void test_frame_build_bounds()
{
    pc_lora_header h = {0};
    uint8_t pay[8] = {0};
    uint8_t small[5];
    TEST_ASSERT_EQUAL_UINT16(0, pc_lora_frame_build(&h, pay, 8, small, sizeof(small))); // 12 > cap 5
}

// --- Driver -----------------------------------------------------------------------------

void test_init_verifies_chip_and_lands_in_standby()
{
    pc_lora_config c = default_cfg();
    TEST_ASSERT_TRUE(pc_lora_init(&g_bus, &c));
    TEST_ASSERT_EQUAL_HEX8(0x81, g_chip.reg[0x01]); // RegOpMode = LoRa | STANDBY
    TEST_ASSERT_EQUAL_HEX8(0x12, g_chip.reg[0x39]); // RegSyncWord
}

void test_init_fails_on_wrong_version()
{
    g_chip.reg[0x42] = 0x00; // not an SX127x
    pc_lora_config c = default_cfg();
    TEST_ASSERT_FALSE(pc_lora_init(&g_bus, &c));
}

void test_init_programs_frequency()
{
    pc_lora_config c = default_cfg(); // 915 MHz
    pc_lora_init(&g_bus, &c);
    uint32_t frf = ((uint32_t)g_chip.reg[0x06] << 16) | ((uint32_t)g_chip.reg[0x07] << 8) | g_chip.reg[0x08];
    // freq = frf * FSTEP, FSTEP = 32e6 / 2^19; reconstruct and check it is ~915 MHz.
    uint32_t freq = (uint32_t)(((uint64_t)frf * 32000000UL) >> 19);
    TEST_ASSERT_UINT32_WITHIN(100, 915000000UL, freq);
}

void test_send_loads_fifo_and_starts_tx()
{
    pc_lora_config c = default_cfg();
    pc_lora_init(&g_bus, &c);
    const uint8_t frame[6] = {0x01, 0x02, 0x03, 0x00, 0xDE, 0xAD};
    TEST_ASSERT_TRUE(pc_lora_send(&g_bus, frame, 6));
    TEST_ASSERT_EQUAL_HEX8(0x83, g_chip.reg[0x01]); // RegOpMode = LoRa | TX
    TEST_ASSERT_EQUAL_UINT8(6, g_chip.reg[0x22]);   // RegPayloadLength
    TEST_ASSERT_EQUAL_MEMORY(frame, g_chip.fifo, 6);
}

void test_tx_done_flag()
{
    TEST_ASSERT_FALSE(pc_lora_tx_done(&g_bus)); // no flag
    g_chip.reg[0x12] = 0x08;                    // RegIrqFlags TxDone
    TEST_ASSERT_TRUE(pc_lora_tx_done(&g_bus));
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_chip.reg[0x12]); // flags cleared
}

void test_set_rx_enters_continuous()
{
    pc_lora_set_rx(&g_bus);
    TEST_ASSERT_EQUAL_HEX8(0x85, g_chip.reg[0x01]); // RegOpMode = LoRa | RX_CONTINUOUS
}

void test_recv_reads_frame_and_rssi()
{
    const uint8_t frame[5] = {0x02, 0x01, 0x07, 0x00, 0x5A};
    memcpy(g_chip.fifo, frame, 5);
    g_chip.reg[0x12] = 0x40; // RegIrqFlags RxDone
    g_chip.reg[0x13] = 5;    // RegRxNbBytes
    g_chip.reg[0x10] = 0;    // RegFifoRxCurrentAddr
    g_chip.reg[0x1A] = 120;  // RegPktRssiValue -> -157 + 120 = -37 dBm

    uint8_t buf[16];
    int16_t rssi = 0;
    int n = pc_lora_recv(&g_bus, buf, sizeof(buf), &rssi);
    TEST_ASSERT_EQUAL_INT(5, n);
    TEST_ASSERT_EQUAL_MEMORY(frame, buf, 5);
    TEST_ASSERT_EQUAL_INT16(-37, rssi);
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_chip.reg[0x12]); // IRQ cleared
}

void test_recv_no_packet()
{
    uint8_t buf[16];
    TEST_ASSERT_EQUAL_INT(-1, pc_lora_recv(&g_bus, buf, sizeof(buf), NULL)); // no RxDone
}

void test_recv_crc_error_dropped()
{
    g_chip.reg[0x12] = 0x40 | 0x20; // RxDone | PayloadCrcError
    g_chip.reg[0x13] = 4;
    uint8_t buf[16];
    TEST_ASSERT_EQUAL_INT(-1, pc_lora_recv(&g_bus, buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_chip.reg[0x12]); // IRQ still cleared
}

void test_recv_truncates_to_cap()
{
    uint8_t frame[10];
    for (int i = 0; i < 10; i++)
    {
        frame[i] = (uint8_t)(0x10 + i);
    }
    memcpy(g_chip.fifo, frame, 10);
    g_chip.reg[0x12] = 0x40;
    g_chip.reg[0x13] = 10;
    g_chip.reg[0x10] = 0;
    uint8_t buf[4];
    int n = pc_lora_recv(&g_bus, buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_INT(4, n); // capped
    TEST_ASSERT_EQUAL_MEMORY(frame, buf, 4);
}

void test_frame_parse_build_guards()
{
    pc_lora_header hdr = {0};
    const uint8_t *payload = NULL;
    uint16_t payload_len = 0;
    uint8_t too_short[1] = {0};
    TEST_ASSERT_FALSE(pc_lora_frame_parse(too_short, sizeof(too_short), &hdr, &payload, &payload_len)); // too short
    uint8_t out[4];
    uint8_t pay[8] = {0};
    TEST_ASSERT_EQUAL_UINT16(0, pc_lora_frame_build(&hdr, pay, sizeof(pay), out, 2)); // cap too small
}

// The codec's pointer guards: a null raw buffer or a null header is refused, while the payload and
// payload-length outputs are optional - a caller wanting only the header may omit both.
void test_frame_parse_null_guards_and_optional_outs()
{
    pc_lora_header h = {0};
    const uint8_t raw[6] = {0x11, 0x22, 0x33, 0x44, 0xAB, 0xCD};
    const uint8_t *p = NULL;
    uint16_t pl = 0;

    TEST_ASSERT_FALSE(pc_lora_frame_parse(NULL, sizeof(raw), &h, &p, &pl));
    TEST_ASSERT_FALSE(pc_lora_frame_parse(raw, sizeof(raw), NULL, &p, &pl));

    TEST_ASSERT_TRUE(pc_lora_frame_parse(raw, sizeof(raw), &h, NULL, NULL));
    TEST_ASSERT_EQUAL_UINT8(0x11, h.to);
    TEST_ASSERT_EQUAL_UINT8(0x44, h.flags);
    TEST_ASSERT_NULL(p); // the omitted outputs stay untouched
    TEST_ASSERT_EQUAL_UINT16(0, pl);
}

// pc_lora_frame_build() refuses a null header, a null output buffer, and a payload longer than
// the radio's maximum - each before writing a byte.
void test_frame_build_null_and_size_guards()
{
    pc_lora_header h = {0};
    uint8_t pay[8] = {0};
    uint8_t out[PC_LORA_MAX_PAYLOAD + 4];

    TEST_ASSERT_EQUAL_UINT16(0, pc_lora_frame_build(NULL, pay, 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT16(0, pc_lora_frame_build(&h, pay, 4, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT16(0, pc_lora_frame_build(&h, pay, PC_LORA_MAX_PAYLOAD + 1, out, sizeof(out)));
}

// pc_lora_init() refuses a null bus, a bus missing either access callback, and a null config -
// each before touching a single radio register.
void test_init_rejects_incomplete_bus()
{
    pc_lora_config cfg = default_cfg();
    pc_lora_bus no_read = {NULL, mock_write, &g_chip};
    pc_lora_bus no_write = {mock_read, NULL, &g_chip};

    TEST_ASSERT_FALSE(pc_lora_init(NULL, &cfg));
    TEST_ASSERT_FALSE(pc_lora_init(&no_read, &cfg));
    TEST_ASSERT_FALSE(pc_lora_init(&no_write, &cfg));
    TEST_ASSERT_FALSE(pc_lora_init(&g_bus, NULL));
    TEST_ASSERT_EQUAL_UINT8(0, g_chip.reg[0x01]); // RegOpMode never written
}

// SF11/SF12 symbols are long enough to need the low-data-rate optimization bit in RegModemConfig3;
// lower spreading factors must leave it clear.
void test_init_sets_low_data_rate_optimize_at_high_sf()
{
    pc_lora_config cfg = default_cfg();
    cfg.spreading = 12;
    TEST_ASSERT_TRUE(pc_lora_init(&g_bus, &cfg));
    TEST_ASSERT_EQUAL_HEX8(0x0C, g_chip.reg[0x26]); // LowDataRateOptimize | AgcAutoOn

    memset(&g_chip, 0, sizeof(g_chip));
    g_chip.reg[0x42] = 0x12;
    cfg.spreading = 7;
    TEST_ASSERT_TRUE(pc_lora_init(&g_bus, &cfg));
    TEST_ASSERT_EQUAL_HEX8(0x04, g_chip.reg[0x26]); // AgcAutoOn only
}

// Every driver entry point that takes a bus refuses a null one; send additionally refuses a null
// frame and a zero length. None of them writes a register on the way out.
void test_driver_entry_points_reject_null_bus()
{
    uint8_t frame[8] = {0};
    uint8_t buf[8];
    int16_t rssi = 0;

    TEST_ASSERT_FALSE(pc_lora_send(NULL, frame, sizeof(frame)));
    TEST_ASSERT_FALSE(pc_lora_send(&g_bus, NULL, sizeof(frame)));
    TEST_ASSERT_FALSE(pc_lora_send(&g_bus, frame, 0));
    TEST_ASSERT_FALSE(pc_lora_tx_done(NULL));
    pc_lora_set_rx(NULL);
    TEST_ASSERT_EQUAL_INT(-1, pc_lora_recv(NULL, buf, sizeof(buf), &rssi));
    TEST_ASSERT_EQUAL_INT(-1, pc_lora_recv(&g_bus, NULL, sizeof(buf), &rssi));
    TEST_ASSERT_EQUAL_UINT8(0, g_chip.reg[0x01]); // RegOpMode untouched by all of them
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_frame_parse_null_guards_and_optional_outs);
    RUN_TEST(test_frame_build_null_and_size_guards);
    RUN_TEST(test_init_rejects_incomplete_bus);
    RUN_TEST(test_init_sets_low_data_rate_optimize_at_high_sf);
    RUN_TEST(test_driver_entry_points_reject_null_bus);
    RUN_TEST(test_frame_build_then_parse);
    RUN_TEST(test_frame_parse_rejects_short);
    RUN_TEST(test_frame_build_bounds);
    RUN_TEST(test_init_verifies_chip_and_lands_in_standby);
    RUN_TEST(test_init_fails_on_wrong_version);
    RUN_TEST(test_init_programs_frequency);
    RUN_TEST(test_send_loads_fifo_and_starts_tx);
    RUN_TEST(test_tx_done_flag);
    RUN_TEST(test_set_rx_enters_continuous);
    RUN_TEST(test_recv_reads_frame_and_rssi);
    RUN_TEST(test_recv_no_packet);
    RUN_TEST(test_recv_crc_error_dropped);
    RUN_TEST(test_recv_truncates_to_cap);
    RUN_TEST(test_frame_parse_build_guards);
    return UNITY_END();
}
