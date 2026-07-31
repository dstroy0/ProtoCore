// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the nRF24L01+ radio driver (services/radio/nrf24): the Nordic SPI
// command protocol framing around init (the full config register burst + RF_CH read-back detect),
// static-width send (W_TX_PAYLOAD, zero-padded to PC_NRF24_PAYLOAD), TX-done poll (STATUS +
// write-1-to-clear), set-rx (PRX + CE), and payload recv (R_RX_PAYLOAD + STATUS pipe decode) - all
// pure CPU-side work. Worked example for performance_benching/device/<service>/ peripheral drivers: this rig has no
// nRF24 module attached, so the nrf_bus spi/ce callbacks are a tiny canned-response stub (same shape
// as test/test_nrf24's mock_spi: STATUS shifts out on the command byte, R_REGISTER returns the
// configured channel so init detects the "chip", R_RX_PAYLOAD returns a fixed payload) that satisfies
// the linker without ever doing a real SPI/CE transaction - only the deterministic framing/parsing
// code that wraps each bus call is in scope here. The RF link itself is out of scope everywhere.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/nrf24 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/radio/nrf24/nrf24.h"
#include <Arduino.h>

namespace
{
// Canned-response bus stub (same shape as test/test_nrf24's mock_spi). Never a real SPI/CE
// transaction - this rig has no nRF24 module attached. Every command sees STATUS on the first byte;
// a register read returns the configured channel (so init's RF_CH read-back detects the chip) and a
// payload read returns fixed test bytes. Reporting RX_DR|TX_DS|pipe2 in STATUS makes recv/tx_done
// take their full protocol path on every iteration without any stateful re-arming.
constexpr uint8_t kStatus = 0x40 | (2 << 1) | 0x20; // RX_DR | pipe 2 | TX_DS = 0x64
constexpr uint8_t kChannel = 76;                    // test/test_nrf24 default_cfg() channel

void bench_spi(const uint8_t *tx, uint8_t *rx, uint8_t len, void *)
{
    uint8_t c = tx[0];
    rx[0] = kStatus; // STATUS is shifted out on the command byte
    if (c <= 0x1F)   // R_REGISTER (0x00 | reg)
    {
        uint8_t reg = (uint8_t)(c & 0x1F);
        for (uint8_t i = 1; i < len; i++)
        {
            rx[i] = (reg == 0x05) ? kChannel : 0x00; // RF_CH (0x05) reads back the configured channel
        }
    }
    else if (c == 0x61) // R_RX_PAYLOAD
    {
        for (uint8_t i = 1; i < len; i++)
        {
            rx[i] = (uint8_t)(0x20 + (i - 1)); // canned payload (test/test_nrf24 data)
        }
    }
    // W_REGISTER / W_TX_PAYLOAD / FLUSH_TX / FLUSH_RX / NOP: STATUS-only (already in rx[0]); the
    // real bus is never touched, only the caller's framing/parsing cycles are measured.
}

void bench_ce(bool, void *)
{
    // CE keying is a GPIO toggle on real hardware; a no-op here (no radio attached).
}

nrf_bus g_bus = {bench_spi, bench_ce, nullptr};

const uint8_t ADDR[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7}; // test/test_nrf24 ADDR literal
} // namespace

static void nrf24_bench_task(void *)
{
    nrf_config cfg = {};
    cfg.address = ADDR;
    cfg.channel = kChannel;
    cfg.data_rate = 0; // 1 Mbps
    cfg.tx_power = 3;

    static const uint8_t send_data[3] = {0xAB, 0xCD, 0xEF}; // test/test_nrf24 send literal
    static uint8_t recv_buf[16];
    static uint8_t recv_pipe = 0;

    for (;;)
    {
        Serial.printf("DB ==== nrf24 device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile bool sinkb = false;
        volatile int sinki = 0;
        DBENCH_OP("pc_nrf24_init", 20000, sinkb = pc_nrf24_init(&g_bus, &cfg));
        DBENCH_OP("pc_nrf24_send", 50000, sinkb = pc_nrf24_send(&g_bus, send_data, sizeof(send_data)));
        DBENCH_OP("pc_nrf24_tx_done", 100000, sinkb = pc_nrf24_tx_done(&g_bus));
        DBENCH_OP("pc_nrf24_set_rx", 50000, pc_nrf24_set_rx(&g_bus));
        DBENCH_OP("pc_nrf24_recv", 50000, sinki = pc_nrf24_recv(&g_bus, recv_buf, sizeof(recv_buf), &recv_pipe));
        (void)sinkb;
        (void)sinki;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: nrf24 device microbench");
    xTaskCreatePinnedToCore(nrf24_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
