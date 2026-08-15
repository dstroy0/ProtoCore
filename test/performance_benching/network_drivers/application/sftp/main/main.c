// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SFTP v3 wire codec (network_drivers/application/sftp): the zero-heap reader
// (u32 / string) and writer (u32 / string / finish) used to parse SSH_FXP requests and build
// responses. Pure; the SSH channel is elsewhere.
//
// Build/flash:  idf.py -C test/performance_benching/sftp -t upload --upload-port COM7
#include "device_bench.h"
#include "network_drivers/application/sftp/sftp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // A representative PROTOCORE_SSH_FXP_OPEN-ish payload: id(u32) + filename(string) + pflags(u32).
    static const uint8_t payload[] = {0x00, 0x00, 0x00, 0x2A, // id = 42
                                      0x00, 0x00, 0x00, 0x08, // string len = 8
                                      '/',  'l',  'o',  'g',  '.', 't', 'x', 't', 0x00, 0x00, 0x00, 0x01}; // pflags

    for (;;)
    {
        DBENCH_BANNER("sftp");
        volatile size_t sink = 0;
        DBENCH_OP("protocore_sftp reader (u32+string+u32)", 200000, {
            SftpReader r;
            protocore_sftp_rd_init(&r, payload, sizeof(payload));
            uint32_t id = protocore_sftp_rd_u32(&r);
            const uint8_t *nm;
            uint32_t nl;
            protocore_sftp_rd_string(&r, &nm, &nl);
            sink += id + nl;
        });
        static uint8_t out[64];
        DBENCH_OP("protocore_sftp writer (u32+string+finish)", 200000, {
            SftpWriter w;
            protocore_sftp_wr_init(&w, out, sizeof(out));
            protocore_sftp_wr_u32(&w, 42);
            protocore_sftp_wr_string(&w, "/log.txt", 8);
            sink += protocore_sftp_wr_finish(&w);
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("sftp")
