// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the FANUC FOCAS Ethernet codec (services/machine_tool/focas): building
// the 10-octet big-endian frame envelope + a generic command request (selector + five i32 args),
// and parsing a command-response frame (envelope -> echoed selector/status/length -> SysInfo
// (ODBSYS) fields, plus the 8-octet `data / base^exp` numeric value decode). Pure codec, no
// sockets - the caller owns the TCP 8193 connection (pc_client_*), so that transport is out of
// scope everywhere here; every call below exercises the real production build/parse path. Worked
// example for performance_benching/device/<service>/: a pure protocol codec with no hardware involved, following
// the same pattern as performance_benching/device/modbus.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/focas -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/machine_tool/focas/focas.h"
#include <Arduino.h>

static void focas_bench_task(void *)
{
    static uint8_t buf[64];

    // Known-good SysInfo command-response frame (envelope + echoed selector 1/1/0x18 + status 0 +
    // 18-octet ODBSYS payload), lifted verbatim from test/test_focas/test_focas.cpp.
    static const uint8_t sysinfo_frame[] = {
        0xA0, 0xA0, 0xA0, 0xA0,             // magic
        0x00, 0x01,                         // version
        0x21, 0x02,                         // FTYPE_VAR_RESP
        0x00, 0x20,                         // payload length 32
        0x00, 0x01, 0x00, 0x01, 0x00, 0x18, // echoed selector
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // status (return code 0)
        0x00, 0x12,                         // data length 18
        0x00, 0x00,                         // addinfo 0
        0x00, 0x08,                         // maxaxis 8
        0x33, 0x30,                         // cnctype "30"
        0x20, 0x4D,                         // mttype " M"
        0x47, 0x30, 0x31, 0x41,             // series "G01A"
        0x32, 0x37, 0x2E, 0x31,             // version "27.1"
        0x30, 0x33                          // axes "03"
    };

    // 123.456 mm = 123456 / 10^3 - the FOCAS 8-octet scaled-value encoding.
    static const uint8_t value8[] = {0x00, 0x01, 0xE2, 0x40, 0x00, 0x0A, 0x00, 0x03};

    for (;;)
    {
        Serial.printf("DB ==== focas device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sinkz = 0;
        volatile bool sinkb = false;
        volatile float sinkf = 0.0f;

        DBENCH_OP("pc_focas_build_sysinfo", 50000, sinkz += pc_focas_build_sysinfo(buf, sizeof(buf)));
        DBENCH_OP("pc_focas_build_read_position", 50000,
                  sinkz += pc_focas_build_read_position(buf, sizeof(buf), FocasPosKind::absolute, 0));

        FocasResponse resp;
        DBENCH_OP("pc_focas_parse_command_frame", 50000,
                  sinkb = pc_focas_parse_command_frame(sysinfo_frame, sizeof(sysinfo_frame), &resp));

        FocasSysInfo si;
        DBENCH_OP("pc_focas_parse_sysinfo", 50000, sinkb = pc_focas_parse_sysinfo(resp.data, resp.data_len, &si));

        FocasValue fv;
        DBENCH_OP("pc_focas_decode8", 50000, sinkb = pc_focas_decode8(value8, sizeof(value8), &fv));
        DBENCH_OP("pc_focas_value_f", 50000, sinkf = pc_focas_value_f(&fv));

        (void)sinkz;
        (void)sinkb;
        (void)sinkf;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: focas device microbench");
    xTaskCreatePinnedToCore(focas_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
