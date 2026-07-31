// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the LoRa codec + SX127x driver (services/radio/lora). Two layers are
// benched, both pure CPU work:
//   - Codec: the RadioHead-compatible 4-byte frame header. pc_lora_frame_build() prepends it,
//     pc_lora_frame_parse() splits a received frame back into header + payload - no hardware.
//   - Driver: the SX1276/77/78/79 register protocol (init / send / recv). The real driver reaches
//     the chip through a caller-supplied register-access bus (two read/write callbacks over SPI +
//     chip-select). This rig has no radio wired, so - exactly as the host test test_lora does - the
//     bus is a mock SX127x: a register file plus a FIFO with the chip's auto-incrementing address
//     pointer. That stub satisfies the driver's bus with plain memory accesses, so the benched cycles
//     are the register-sequence CPU cost only; the actual RF link (SPI transactions, PA, air time) is
//     deliberately out of scope everywhere here.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/lora -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/radio/lora/lora.h"
#include <Arduino.h>
#include <string.h>

// --- Mock SX127x (the driver's register bus, memory-only - copied from test/test_lora) -------------
// A register file plus a FIFO whose pointer auto-increments on RegFifo (0x00) access, mirroring the
// chip. No SPI, no radio: every "bus" access is a plain array read/write, so only the driver's own
// register-sequencing CPU cost is measured.
struct MockChip
{
    uint8_t reg[128];
    uint8_t fifo[256];
    uint16_t fifo_ptr;
};

static MockChip g_chip;      // init + send exercise this one
static MockChip g_recv_chip; // recv exercises this one, kept permanently "a frame is ready"

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

// A recv-side variant of the mock: RegIrqFlags always reports RxDone (no CRC error) and the driver's
// IRQ-clear writes are ignored, so a frame stays ready and pc_lora_recv() runs its full FIFO-drain +
// RSSI path on every iteration instead of short-circuiting on the second call. Everything else behaves
// like the plain mock.
static uint8_t recv_read(uint8_t reg, void *ctx)
{
    MockChip *c = (MockChip *)ctx;
    if (reg == 0x00) // RegFifo
    {
        return c->fifo[c->fifo_ptr++ & 0xFF];
    }
    if (reg == 0x12) // RegIrqFlags: RxDone set, PayloadCrcError clear
    {
        return 0x40;
    }
    return c->reg[reg & 0x7F];
}
static void recv_write(uint8_t reg, uint8_t val, void *ctx)
{
    MockChip *c = (MockChip *)ctx;
    if (reg == 0x0D) // RegFifoAddrPtr
    {
        c->fifo_ptr = val;
        return;
    }
    if (reg == 0x12) // swallow the IRQ-flag clear so the frame stays permanently ready
    {
        return;
    }
    if (reg == 0x00) // RegFifo
    {
        c->fifo[c->fifo_ptr++ & 0xFF] = val;
        return;
    }
    c->reg[reg & 0x7F] = val;
}

static pc_lora_bus g_bus = {mock_read, mock_write, &g_chip};
static pc_lora_bus g_recv_bus = {recv_read, recv_write, &g_recv_chip};

static pc_lora_config default_cfg()
{
    pc_lora_config c = {};
    c.freq_hz = 915000000UL; // 915 MHz ISM
    c.spreading = 7;         // SF7
    c.bandwidth = 7;         // 125 kHz
    c.coding_rate = 1;       // 4/5
    c.sync_word = 0x12;      // private
    c.tx_power = 17;         // dBm, PA_BOOST
    return c;
}

// A realistic 20-byte on-air frame: RadioHead header {to=broadcast, from=2, id=0x10, flags=0} plus a
// 16-byte sensor payload (spec-conformant shape, same header layout the test builds/parses).
static const uint8_t g_frame[20] = {0xFF, 0x02, 0x10, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                    0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xF0, 0x0F};

static void lora_bench_task(void *)
{
    // Prime the init/send chip: RegVersion reads back the SX127x id so pc_lora_init() accepts it.
    memset(&g_chip, 0, sizeof(g_chip));
    g_chip.reg[0x42] = 0x12; // RegVersion = SX127x id

    // Prime the recv chip with a 20-byte frame permanently ready to be drained.
    memset(&g_recv_chip, 0, sizeof(g_recv_chip));
    memcpy(g_recv_chip.fifo, g_frame, sizeof(g_frame));
    g_recv_chip.reg[0x13] = (uint8_t)sizeof(g_frame); // RegRxNbBytes
    g_recv_chip.reg[0x10] = 0x00;                     // RegFifoRxCurrentAddr
    g_recv_chip.reg[0x1A] = 120;                      // RegPktRssiValue -> -157 + 120 = -37 dBm

    pc_lora_config cfg = default_cfg();
    pc_lora_header hdr = {g_frame[0], g_frame[1], g_frame[2], g_frame[3]};
    static uint8_t out[PC_LORA_MAX_PAYLOAD + 4];
    static uint8_t rxbuf[PC_LORA_MAX_PAYLOAD + 4];

    for (;;)
    {
        Serial.printf("DB ==== lora device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile uint32_t usink = 0;
        volatile int isink = 0;
        volatile bool bsink = false;

        // Codec (pure, no bus): parse a 20-byte frame into header+payload, and build one back.
        pc_lora_header ph = {};
        const uint8_t *ppl = nullptr;
        uint16_t plen = 0;
        DBENCH_OP("pc_lora_frame_parse", 200000,
                  bsink ^= pc_lora_frame_parse(g_frame, (uint16_t)sizeof(g_frame), &ph, &ppl, &plen));
        DBENCH_OP("pc_lora_frame_build", 200000,
                  usink += pc_lora_frame_build(&hdr, g_frame + 4, 16, out, (uint16_t)sizeof(out)));

        // Driver register protocol against the mock bus (memory-only; no SPI/radio).
        DBENCH_OP("pc_lora_init", 50000, bsink ^= pc_lora_init(&g_bus, &cfg));
        DBENCH_OP("pc_lora_send", 50000, bsink ^= pc_lora_send(&g_bus, g_frame, (uint8_t)sizeof(g_frame)));
        DBENCH_OP("pc_lora_recv", 50000, isink += pc_lora_recv(&g_recv_bus, rxbuf, (uint8_t)sizeof(rxbuf), nullptr));

        (void)usink;
        (void)isink;
        (void)bsink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: lora device microbench");
    xTaskCreatePinnedToCore(lora_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
