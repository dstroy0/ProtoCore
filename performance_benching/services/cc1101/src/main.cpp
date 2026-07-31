// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the CC1101 sub-GHz radio driver (services/radio/cc1101): the TI SPI
// header-protocol framing around init/detect, variable-length send, TX-done poll, set-rx, packet
// recv (length byte + payload + RSSI decode), and the RSSI-to-dBm conversion math - all pure CPU-side
// work. Worked example for performance_benching/device/<service>/ peripheral drivers: this rig has no CC1101 module
// attached, so pc_cc1101_bus::spi is a tiny canned-response stub (same shape as test/test_cc1101's
// mock_spi) that satisfies the linker without ever doing a real bus transaction - only the
// deterministic framing/parsing code that wraps each SPI call is in scope here.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/cc1101 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/radio/cc1101/cc1101.h"
#include <Arduino.h>

namespace
{
// Canned-response SPI stub (same shape as test/test_cc1101's mock_spi): returns just enough fixed
// data for init to detect the chip and recv to find one waiting packet. Never a real bus transaction
// - this rig has no CC1101 module attached, so every other address is a plain no-op.
uint8_t g_rxfifo[6] = {3, 0x11, 0x22, 0x33, 0x50, 0x80}; // len=3, payload, RSSI raw, LQI (test data)

void bench_spi(const uint8_t *tx, uint8_t *rx, uint8_t len, void *)
{
    uint8_t hdr = tx[0];
    uint8_t addr = (uint8_t)(hdr & 0x3F);
    bool read = (hdr & 0x80) != 0;
    rx[0] = 0x00;                  // chip status byte: state=IDLE, fifo-bytes-available=0
    if (addr == 0x31 && read)      // VERSION status register
        rx[1] = 0x14;              // typical CC1101 VERSION (test/test_cc1101 data)
    else if (addr == 0x3B && read) // RXBYTES status register
        rx[1] = 6;                 // bytes waiting (test/test_cc1101 data)
    else if (addr == 0x3F && read) // FIFO read (length byte, or burst payload+status)
        for (uint8_t i = 1; i < len; i++)
            rx[i] = g_rxfifo[i - 1];
    // Config-register writes, command strobes, and the TX FIFO burst write all fall through as
    // no-ops: the real bus is never touched, only the caller's framing/parsing cycles are measured.
}

pc_cc1101_bus g_bus = {bench_spi, nullptr};

// A minimal SmartRF-style register table (test/test_cc1101's default_cfg() literal).
const pc_cc1101_reg REGS[] = {{0x00, 0x29}, {0x08, 0x05}};
} // namespace

static void cc1101_bench_task(void *)
{
    pc_cc1101_config cfg = {};
    cfg.regs = REGS;
    cfg.nregs = 2;
    cfg.channel = 20;

    static const uint8_t send_data[3] = {0xAA, 0xBB, 0xCC}; // test/test_cc1101 literal
    static uint8_t recv_buf[16];
    static int16_t recv_rssi = 0;

    for (;;)
    {
        Serial.printf("DB ==== cc1101 device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile bool sinkb = false;
        volatile int sinki = 0;
        volatile int16_t sink16 = 0;
        DBENCH_OP("pc_cc1101_init", 20000, sinkb = pc_cc1101_init(&g_bus, &cfg));
        DBENCH_OP("pc_cc1101_send", 50000, sinkb = pc_cc1101_send(&g_bus, send_data, sizeof(send_data)));
        DBENCH_OP("pc_cc1101_tx_done", 100000, sinkb = pc_cc1101_tx_done(&g_bus));
        DBENCH_OP("pc_cc1101_set_rx", 50000, pc_cc1101_set_rx(&g_bus));
        DBENCH_OP("pc_cc1101_recv", 50000, sinki = pc_cc1101_recv(&g_bus, recv_buf, sizeof(recv_buf), &recv_rssi));
        DBENCH_OP("pc_cc1101_rssi_dbm", 200000, sink16 = pc_cc1101_rssi_dbm(0x50));
        (void)sinkb;
        (void)sinki;
        (void)sink16;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: cc1101 device microbench");
    xTaskCreatePinnedToCore(cc1101_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
