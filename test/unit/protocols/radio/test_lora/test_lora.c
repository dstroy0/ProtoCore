// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/radio/lora/lora.h"
#include <string.h>

#include <unity.h>

static uint8_t lora_work[16]; // the borrow an entry takes; Lora never reads it

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
    if (reg == 0x00)
    {
        return c->fifo[c->fifo_ptr++ & 0xFF];
    }
    return c->reg[reg & 0x7F];
}
static void mock_write(uint8_t reg, uint8_t val, void *ctx)
{
    MockChip *c = (MockChip *)ctx;
    if (reg == 0x0D)
    {
        c->fifo_ptr = val;
        c->reg[0x0D] = val;
        return;
    }
    if (reg == 0x00)
    {
        c->fifo[c->fifo_ptr++ & 0xFF] = val;
        return;
    }
    c->reg[reg & 0x7F] = val;
}

static protocore_lora_bus g_bus = {mock_read, mock_write, &g_chip};

static protocore_lora_config default_cfg()
{
    protocore_lora_config c = {0};
    c.freq_hz = 915000000UL;
    c.spreading = 7;
    c.bandwidth = 7;
    c.coding_rate = 1;
    c.sync_word = 0x12;
    c.tx_power = 17;
    return c;
}

void setUp()
{
    memset(&g_chip, 0, sizeof(g_chip));
    g_chip.reg[0x42] = 0x12;
}
void tearDown()
{
}

void test_frame_build_then_parse()
{
    protocore_lora_header h = {0xAA, 0x02, 0x03, 0x00};
    const uint8_t pay[3] = {'h', 'i', '!'};
    uint8_t frame[16];
    Lora.frame_build_args.hdr = &h;
    Lora.frame_build_args.payload = pay;
    Lora.frame_build_args.len = 3;
    Lora.frame_build_args.out = frame;
    Lora.frame_build_args.cap = sizeof(frame);
    Lora.frame_build(lora_work);
    uint16_t n = Lora.value;
    TEST_ASSERT_EQUAL_UINT16(7, n);

    protocore_lora_header out = {0};
    const uint8_t *p = NULL;
    uint16_t pl = 0;
    Lora.frame_parse_args.raw = frame;
    Lora.frame_parse_args.len = n;
    Lora.frame_parse_args.hdr = &out;
    Lora.frame_parse_args.payload = &p;
    Lora.frame_parse_args.payload_len = &pl;
    Lora.frame_parse(lora_work);
    TEST_ASSERT_TRUE(Lora.ok);
    TEST_ASSERT_EQUAL_UINT8(0xAA, out.to);
    TEST_ASSERT_EQUAL_UINT8(0x02, out.from);
    TEST_ASSERT_EQUAL_UINT8(0x03, out.id);
    TEST_ASSERT_EQUAL_UINT16(3, pl);
    TEST_ASSERT_EQUAL_MEMORY(pay, p, 3);
}

void test_frame_parse_rejects_short()
{
    protocore_lora_header h = {0};
    const uint8_t raw[3] = {1, 2, 3};
    Lora.frame_parse_args.raw = raw;
    Lora.frame_parse_args.len = 3;
    Lora.frame_parse_args.hdr = &h;
    Lora.frame_parse_args.payload = NULL;
    Lora.frame_parse_args.payload_len = NULL;
    Lora.frame_parse(lora_work);
    TEST_ASSERT_FALSE(Lora.ok);
}

void test_frame_build_bounds()
{
    protocore_lora_header h = {0};
    uint8_t pay[8] = {0};
    uint8_t small[5];
    Lora.frame_build_args.hdr = &h;
    Lora.frame_build_args.payload = pay;
    Lora.frame_build_args.len = 8;
    Lora.frame_build_args.out = small;
    Lora.frame_build_args.cap = sizeof(small);
    Lora.frame_build(lora_work);
    TEST_ASSERT_EQUAL_UINT16(0, Lora.value);
}

void test_init_verifies_chip_and_lands_in_standby()
{
    protocore_lora_config c = default_cfg();
    Lora.init_args.bus = &g_bus;
    Lora.init_args.cfg = &c;
    Lora.init(lora_work);
    TEST_ASSERT_TRUE(Lora.ok);
    TEST_ASSERT_EQUAL_HEX8(0x81, g_chip.reg[0x01]);
    TEST_ASSERT_EQUAL_HEX8(0x12, g_chip.reg[0x39]);
}

void test_init_fails_on_wrong_version()
{
    g_chip.reg[0x42] = 0x00;
    protocore_lora_config c = default_cfg();
    Lora.init_args.bus = &g_bus;
    Lora.init_args.cfg = &c;
    Lora.init(lora_work);
    TEST_ASSERT_FALSE(Lora.ok);
}

void test_init_programs_frequency()
{
    protocore_lora_config c = default_cfg();
    Lora.init_args.bus = &g_bus;
    Lora.init_args.cfg = &c;
    Lora.init(lora_work);
    uint32_t frf = ((uint32_t)g_chip.reg[0x06] << 16) | ((uint32_t)g_chip.reg[0x07] << 8) | g_chip.reg[0x08];

    uint32_t freq = (uint32_t)(((uint64_t)frf * 32000000UL) >> 19);
    TEST_ASSERT_UINT32_WITHIN(100, 915000000UL, freq);
}

void test_send_loads_fifo_and_starts_tx()
{
    protocore_lora_config c = default_cfg();
    Lora.init_args.bus = &g_bus;
    Lora.init_args.cfg = &c;
    Lora.init(lora_work);
    const uint8_t frame[6] = {0x01, 0x02, 0x03, 0x00, 0xDE, 0xAD};
    Lora.send_args.bus = &g_bus;
    Lora.send_args.frame = frame;
    Lora.send_args.len = 6;
    Lora.send(lora_work);
    TEST_ASSERT_TRUE(Lora.ok);
    TEST_ASSERT_EQUAL_HEX8(0x83, g_chip.reg[0x01]);
    TEST_ASSERT_EQUAL_UINT8(6, g_chip.reg[0x22]);
    TEST_ASSERT_EQUAL_MEMORY(frame, g_chip.fifo, 6);
}

void test_tx_done_flag()
{
    Lora.tx_done_args.bus = &g_bus;
    Lora.tx_done(lora_work);
    TEST_ASSERT_FALSE(Lora.ok);
    g_chip.reg[0x12] = 0x08;
    Lora.tx_done_args.bus = &g_bus;
    Lora.tx_done(lora_work);
    TEST_ASSERT_TRUE(Lora.ok);
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_chip.reg[0x12]);
}

void test_set_rx_enters_continuous()
{
    Lora.set_rx_args.bus = &g_bus;
    Lora.set_rx(lora_work);
    TEST_ASSERT_EQUAL_HEX8(0x85, g_chip.reg[0x01]);
}

void test_recv_reads_frame_and_rssi()
{
    const uint8_t frame[5] = {0x02, 0x01, 0x07, 0x00, 0x5A};
    memcpy(g_chip.fifo, frame, 5);
    g_chip.reg[0x12] = 0x40;
    g_chip.reg[0x13] = 5;
    g_chip.reg[0x10] = 0;
    g_chip.reg[0x1A] = 120;

    uint8_t buf[16];
    int16_t rssi = 0;
    Lora.recv_args.bus = &g_bus;
    Lora.recv_args.buf = buf;
    Lora.recv_args.cap = sizeof(buf);
    Lora.recv_args.rssi = &rssi;
    Lora.recv(lora_work);
    int n = Lora.n;
    TEST_ASSERT_EQUAL_INT(5, n);
    TEST_ASSERT_EQUAL_MEMORY(frame, buf, 5);
    TEST_ASSERT_EQUAL_INT16(-37, rssi);
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_chip.reg[0x12]);
}

void test_recv_no_packet()
{
    uint8_t buf[16];
    Lora.recv_args.bus = &g_bus;
    Lora.recv_args.buf = buf;
    Lora.recv_args.cap = sizeof(buf);
    Lora.recv_args.rssi = NULL;
    Lora.recv(lora_work);
    TEST_ASSERT_EQUAL_INT(-1, Lora.n);
}

void test_recv_crc_error_dropped()
{
    g_chip.reg[0x12] = 0x40 | 0x20;
    g_chip.reg[0x13] = 4;
    uint8_t buf[16];
    Lora.recv_args.bus = &g_bus;
    Lora.recv_args.buf = buf;
    Lora.recv_args.cap = sizeof(buf);
    Lora.recv_args.rssi = NULL;
    Lora.recv(lora_work);
    TEST_ASSERT_EQUAL_INT(-1, Lora.n);
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_chip.reg[0x12]);
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
    Lora.recv_args.bus = &g_bus;
    Lora.recv_args.buf = buf;
    Lora.recv_args.cap = sizeof(buf);
    Lora.recv_args.rssi = NULL;
    Lora.recv(lora_work);
    int n = Lora.n;
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_MEMORY(frame, buf, 4);
}

void test_frame_parse_build_guards()
{
    protocore_lora_header hdr = {0};
    const uint8_t *payload = NULL;
    uint16_t payload_len = 0;
    uint8_t too_short[1] = {0};
    Lora.frame_parse_args.raw = too_short;
    Lora.frame_parse_args.len = sizeof(too_short);
    Lora.frame_parse_args.hdr = &hdr;
    Lora.frame_parse_args.payload = &payload;
    Lora.frame_parse_args.payload_len = &payload_len;
    Lora.frame_parse(lora_work);
    TEST_ASSERT_FALSE(Lora.ok);
    uint8_t out[4];
    uint8_t pay[8] = {0};
    Lora.frame_build_args.hdr = &hdr;
    Lora.frame_build_args.payload = pay;
    Lora.frame_build_args.len = sizeof(pay);
    Lora.frame_build_args.out = out;
    Lora.frame_build_args.cap = 2;
    Lora.frame_build(lora_work);
    TEST_ASSERT_EQUAL_UINT16(0, Lora.value);
}

void test_frame_parse_null_guards_and_optional_outs()
{
    protocore_lora_header h = {0};
    const uint8_t raw[6] = {0x11, 0x22, 0x33, 0x44, 0xAB, 0xCD};
    const uint8_t *p = NULL;
    uint16_t pl = 0;

    Lora.frame_parse_args.raw = NULL;
    Lora.frame_parse_args.len = sizeof(raw);
    Lora.frame_parse_args.hdr = &h;
    Lora.frame_parse_args.payload = &p;
    Lora.frame_parse_args.payload_len = &pl;
    Lora.frame_parse(lora_work);
    TEST_ASSERT_FALSE(Lora.ok);
    Lora.frame_parse_args.raw = raw;
    Lora.frame_parse_args.len = sizeof(raw);
    Lora.frame_parse_args.hdr = NULL;
    Lora.frame_parse_args.payload = &p;
    Lora.frame_parse_args.payload_len = &pl;
    Lora.frame_parse(lora_work);
    TEST_ASSERT_FALSE(Lora.ok);

    Lora.frame_parse_args.raw = raw;
    Lora.frame_parse_args.len = sizeof(raw);
    Lora.frame_parse_args.hdr = &h;
    Lora.frame_parse_args.payload = NULL;
    Lora.frame_parse_args.payload_len = NULL;
    Lora.frame_parse(lora_work);
    TEST_ASSERT_TRUE(Lora.ok);
    TEST_ASSERT_EQUAL_UINT8(0x11, h.to);
    TEST_ASSERT_EQUAL_UINT8(0x44, h.flags);
    TEST_ASSERT_NULL(p);
    TEST_ASSERT_EQUAL_UINT16(0, pl);
}

void test_frame_build_null_and_size_guards()
{
    protocore_lora_header h = {0};
    uint8_t pay[8] = {0};
    uint8_t out[PROTOCORE_LORA_MAX_PAYLOAD + 4];

    Lora.frame_build_args.hdr = NULL;
    Lora.frame_build_args.payload = pay;
    Lora.frame_build_args.len = 4;
    Lora.frame_build_args.out = out;
    Lora.frame_build_args.cap = sizeof(out);
    Lora.frame_build(lora_work);
    TEST_ASSERT_EQUAL_UINT16(0, Lora.value);
    Lora.frame_build_args.hdr = &h;
    Lora.frame_build_args.payload = pay;
    Lora.frame_build_args.len = 4;
    Lora.frame_build_args.out = NULL;
    Lora.frame_build_args.cap = sizeof(out);
    Lora.frame_build(lora_work);
    TEST_ASSERT_EQUAL_UINT16(0, Lora.value);
    Lora.frame_build_args.hdr = &h;
    Lora.frame_build_args.payload = pay;
    Lora.frame_build_args.len = PROTOCORE_LORA_MAX_PAYLOAD + 1;
    Lora.frame_build_args.out = out;
    Lora.frame_build_args.cap = sizeof(out);
    Lora.frame_build(lora_work);
    TEST_ASSERT_EQUAL_UINT16(0, Lora.value);
}

void test_init_rejects_incomplete_bus()
{
    protocore_lora_config cfg = default_cfg();
    protocore_lora_bus no_read = {NULL, mock_write, &g_chip};
    protocore_lora_bus no_write = {mock_read, NULL, &g_chip};

    Lora.init_args.bus = NULL;
    Lora.init_args.cfg = &cfg;
    Lora.init(lora_work);
    TEST_ASSERT_FALSE(Lora.ok);
    Lora.init_args.bus = &no_read;
    Lora.init_args.cfg = &cfg;
    Lora.init(lora_work);
    TEST_ASSERT_FALSE(Lora.ok);
    Lora.init_args.bus = &no_write;
    Lora.init_args.cfg = &cfg;
    Lora.init(lora_work);
    TEST_ASSERT_FALSE(Lora.ok);
    Lora.init_args.bus = &g_bus;
    Lora.init_args.cfg = NULL;
    Lora.init(lora_work);
    TEST_ASSERT_FALSE(Lora.ok);
    TEST_ASSERT_EQUAL_UINT8(0, g_chip.reg[0x01]);
}

void test_init_sets_low_data_rate_optimize_at_high_sf()
{
    protocore_lora_config cfg = default_cfg();
    cfg.spreading = 12;
    Lora.init_args.bus = &g_bus;
    Lora.init_args.cfg = &cfg;
    Lora.init(lora_work);
    TEST_ASSERT_TRUE(Lora.ok);
    TEST_ASSERT_EQUAL_HEX8(0x0C, g_chip.reg[0x26]);

    memset(&g_chip, 0, sizeof(g_chip));
    g_chip.reg[0x42] = 0x12;
    cfg.spreading = 7;
    Lora.init_args.bus = &g_bus;
    Lora.init_args.cfg = &cfg;
    Lora.init(lora_work);
    TEST_ASSERT_TRUE(Lora.ok);
    TEST_ASSERT_EQUAL_HEX8(0x04, g_chip.reg[0x26]);
}

void test_driver_entry_points_reject_null_bus()
{
    uint8_t frame[8] = {0};
    uint8_t buf[8];
    int16_t rssi = 0;

    Lora.send_args.bus = NULL;
    Lora.send_args.frame = frame;
    Lora.send_args.len = sizeof(frame);
    Lora.send(lora_work);
    TEST_ASSERT_FALSE(Lora.ok);
    Lora.send_args.bus = &g_bus;
    Lora.send_args.frame = NULL;
    Lora.send_args.len = sizeof(frame);
    Lora.send(lora_work);
    TEST_ASSERT_FALSE(Lora.ok);
    Lora.send_args.bus = &g_bus;
    Lora.send_args.frame = frame;
    Lora.send_args.len = 0;
    Lora.send(lora_work);
    TEST_ASSERT_FALSE(Lora.ok);
    Lora.tx_done_args.bus = NULL;
    Lora.tx_done(lora_work);
    TEST_ASSERT_FALSE(Lora.ok);
    Lora.set_rx_args.bus = NULL;
    Lora.set_rx(lora_work);
    Lora.recv_args.bus = NULL;
    Lora.recv_args.buf = buf;
    Lora.recv_args.cap = sizeof(buf);
    Lora.recv_args.rssi = &rssi;
    Lora.recv(lora_work);
    TEST_ASSERT_EQUAL_INT(-1, Lora.n);
    Lora.recv_args.bus = &g_bus;
    Lora.recv_args.buf = NULL;
    Lora.recv_args.cap = sizeof(buf);
    Lora.recv_args.rssi = &rssi;
    Lora.recv(lora_work);
    TEST_ASSERT_EQUAL_INT(-1, Lora.n);
    TEST_ASSERT_EQUAL_UINT8(0, g_chip.reg[0x01]);
}

