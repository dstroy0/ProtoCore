// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the parameterized CRC engine (shared/crc/crc.h): the
// Rocksoft/Williams begin/update/final over a 1 KiB buffer, for two presets (CRC-16/MODBUS reflected,
// CRC-16/XMODEM non-reflected). This is the shared checksum core the fieldbus codecs build on. Pure,
// header-only. Build/flash: pio run -d performance_benching/core/crc -t upload
#include "device_bench.h"
#include "shared/crc/crc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint32_t crc_one(const protocore_crc_params *p, const uint8_t *d, size_t n)
{
    Crc.args.params = p;
    Crc.args.data = d;
    Crc.args.len = n;
    Crc.begin(Crc.internal);
    Crc.args.crc = Crc.value;
    Crc.update(Crc.internal);
    Crc.args.crc = Crc.value;
    Crc.final(Crc.internal);
    return Crc.value;
}

void dbench_run(void)
{
    static uint8_t buf[1024];
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        buf[i] = (uint8_t)(i * 31 + 7);
    }
    for (;;)
    {
        DBENCH_BANNER("crc");
        volatile uint32_t sink = 0;
        DBENCH_BULK("protocore_crc CRC-16/MODBUS (1 KiB)", 20000, 1024,
                    sink += crc_one(&PROTOCORE_CRC16_MODBUS, buf, 1024));
        DBENCH_BULK("protocore_crc CRC-16/XMODEM (1 KiB)", 20000, 1024,
                    sink += crc_one(&PROTOCORE_CRC16_XMODEM, buf, 1024));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("crc")
