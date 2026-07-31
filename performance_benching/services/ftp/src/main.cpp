// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the FTP client wire codec (services/file_transfer/ftp): the
// control-command builders (generic verb + PORT + EPRT), the single/multi-line 3-digit reply
// parser, and the PASV / EPSV data-address decoders. Pure (no heap, no stdlib, no sockets) - the
// two sockets (control + data) are entirely the application's, so every call here exercises the
// real production code path with no stubbing needed (contrast with performance_benching/device/ads1115, a
// peripheral driver where the bus transaction itself is stubbed). Reply/PASV/EPSV strings below
// are copied verbatim from test/test_ftp/test_ftp.cpp (authentic test.rebex.net server captures).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/ftp -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/file_transfer/ftp/ftp.h"
#include <Arduino.h>

static void ftp_bench_task(void *)
{
    static char cmdbuf[64];
    static char portbuf[64];
    static char eprtbuf[64];
    const uint8_t ip[4] = {192, 168, 1, 50};

    // Real FEAT reply: many indented continuation lines, terminated by "211 End." (authentic
    // test.rebex.net capture, see test/test_ftp/test_ftp.cpp::test_reply_multiline_feat).
    static const char feat[] = "211-Supported extensions:\r\n"
                               " AUTH TLS;SSL;\r\n"
                               " EPSV\r\n"
                               " EPRT\r\n"
                               " PASV\r\n"
                               " PORT\r\n"
                               " REST STREAM\r\n"
                               " SIZE\r\n"
                               "211 End.\r\n";
    static const char pasv_reply[] = "227 Entering Passive Mode (194,108,117,16,4,6)\r\n";
    static const char epsv_reply[] = "229 Entering Extended Passive Mode (|||1050|)\r\n";

    for (;;)
    {
        Serial.printf("DB ==== ftp device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sinkz = 0;
        volatile bool sinkb = false;
        int code = 0;
        size_t consumed = 0;
        uint8_t out_ip[4] = {0, 0, 0, 0};
        uint16_t out_port = 0;

        DBENCH_OP("pc_ftp_build_command", 100000,
                  sinkz += pc_ftp_build_command(cmdbuf, sizeof(cmdbuf), "RETR", "/programs/O1234.nc"));
        DBENCH_OP("pc_ftp_build_port", 100000, sinkz += pc_ftp_build_port(portbuf, sizeof(portbuf), ip, 40000));
        DBENCH_OP("pc_ftp_build_eprt", 100000,
                  sinkz += pc_ftp_build_eprt(eprtbuf, sizeof(eprtbuf), "132.235.1.2", false, 6275));
        DBENCH_OP("pc_ftp_parse_reply multiline", 50000,
                  sinkb = pc_ftp_parse_reply(feat, sizeof(feat) - 1, &code, &consumed));
        DBENCH_OP("pc_ftp_parse_pasv", 100000,
                  sinkb = pc_ftp_parse_pasv(pasv_reply, sizeof(pasv_reply) - 1, out_ip, &out_port));
        DBENCH_OP("pc_ftp_parse_epsv", 100000,
                  sinkb = pc_ftp_parse_epsv(epsv_reply, sizeof(epsv_reply) - 1, &out_port));
        (void)sinkz;
        (void)sinkb;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: ftp device microbench");
    xTaskCreatePinnedToCore(ftp_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
