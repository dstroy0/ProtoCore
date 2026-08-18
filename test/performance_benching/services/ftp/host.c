// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the FTP client wire codec (RFC 959 + RFC 2428): protocore_ftp_build_command (the
// device emits a control command), protocore_ftp_parse_reply (decode the possibly-multiline 3-digit reply - the
// untrusted-input hot op), and protocore_ftp_parse_pasv (decode the 227 passive-mode data address). All pure (no
// sockets, no heap), so they link standalone. The device number comes from the rig /bench endpoint; this
// host ns/op + MB/s is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_FTP=1 test/performance_benching/services/ftp/host.c
//   src/services/file_transfer/ftp/ftp.c src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/bf && /tmp/bf

#define PROTOCORE_ENABLE_FTP 1
#include "services/file_transfer/ftp/ftp.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

static uint8_t ftp_work[16]; // the borrow an entry takes; Ftp never reads it

int main(void)
{
    // A multiline FEAT reply (RFC 959 4.2): 211- head, continuation lines, 211<SP>End terminator.
    const char feat[] = "211-Features:\r\n PASV\r\n SIZE\r\n MDTM\r\n211 End\r\n";
    const size_t featlen = sizeof(feat) - 1;

    // A 227 passive-mode reply carrying the (h1,h2,h3,h4,p1,p2) data address.
    const char pasv[] = "227 Entering Passive Mode (192,168,1,223,201,54).\r\n";
    const size_t pasvlen = sizeof(pasv) - 1;

    char cmd[128];
    Ftp.build_command_args.buf = cmd;
    Ftp.build_command_args.cap = sizeof(cmd);
    Ftp.build_command_args.verb = "STOR";
    Ftp.build_command_args.arg = "protocore_rig.txt";
    Ftp.build_command(ftp_work);
    size_t clen = Ftp.n;

    hbench_header();

    // build a STOR command line.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        Ftp.build_command_args.buf = cmd;
        Ftp.build_command_args.cap = sizeof(cmd);
        Ftp.build_command_args.verb = "STOR";
        Ftp.build_command_args.arg = "protocore_rig.txt";
        Ftp.build_command(ftp_work);
        HBENCH_NS(2000000, sink += Ftp.n, ns);
        hbench_row("ftp", "build STOR command", ns, (double)clen);
        (void)sink;
    }
    // parse a multiline reply (scan to the NNN<SP> terminator - the per-reply hot op).
    {
        volatile int sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            2000000,
            {
                int code = 0;
                size_t used = 0;
                Ftp.parse_reply_args.buf = feat;
                Ftp.parse_reply_args.len = featlen;
                Ftp.parse_reply_args.code = &code;
                Ftp.parse_reply_args.consumed = &used;
                Ftp.parse_reply(ftp_work);
                sink += Ftp.ok ? code : 0;
            },
            ns);
        hbench_row("ftp", "parse multiline reply", ns, (double)featlen);
        (void)sink;
    }
    // parse the 227 passive-mode data address tuple.
    {
        volatile int sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            2000000,
            {
                uint8_t ip[4];
                uint16_t port = 0;
                Ftp.parse_pasv_args.buf = pasv;
                Ftp.parse_pasv_args.len = pasvlen;
                Ftp.parse_pasv_args.ip = ip;
                Ftp.parse_pasv_args.port = &port;
                Ftp.parse_pasv(ftp_work);
                sink += Ftp.ok ? port : 0;
            },
            ns);
        hbench_row("ftp", "parse 227 PASV address", ns, (double)pasvlen);
        (void)sink;
    }

    return 0;
}
