// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SFTP v3 wire codec (network_drivers/application/sftp): the zero-heap reader
// (u32 / string) and writer (u32 / string / finish) used to parse SSH_FXP requests and build
// responses. Pure; the SSH channel is elsewhere.
//
// Build/flash:  idf.py -C test/performance_benching/sftp -t upload --upload-port COM7
#include "device_bench.h"
#include "network_drivers/application/sftp/sftp/sftp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t sftp_work[16]; // the borrow an entry takes; Sftp never reads it

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
            Sftp.rd_init_args.r = &r;
            Sftp.rd_init_args.payload = payload;
            Sftp.rd_init_args.len = sizeof(payload);
            Sftp.rd_init(sftp_work);
            Sftp.rd_u32_args.r = &r;
            Sftp.rd_u32(sftp_work);
            uint32_t id = Sftp.u32;
            const uint8_t *nm;
            uint32_t nl;
            Sftp.rd_string_args.r = &r;
            Sftp.rd_string_args.out = &nm;
            Sftp.rd_string_args.out_len = &nl;
            Sftp.rd_string(sftp_work);
            sink += id + nl;
        });
        static uint8_t out[64];
        DBENCH_OP("protocore_sftp writer (u32+string+finish)", 200000, {
            SftpWriter w;
            Sftp.wr_init_args.w = &w;
            Sftp.wr_init_args.out = out;
            Sftp.wr_init_args.cap = sizeof(out);
            Sftp.wr_init(sftp_work);
            Sftp.wr_u32_args.w = &w;
            Sftp.wr_u32_args.v = 42;
            Sftp.wr_u32(sftp_work);
            Sftp.wr_string_args.w = &w;
            Sftp.wr_string_args.s = "/log.txt";
            Sftp.wr_string_args.n = 8;
            Sftp.wr_string(sftp_work);
            Sftp.wr_finish_args.w = &w;
            Sftp.wr_finish(sftp_work);
            sink += Sftp.n;
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("sftp")
