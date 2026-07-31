// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the FTP client wire codec (RFC 959 + RFC 2428): pc_ftp_build_command (the
// device emits a control command), pc_ftp_parse_reply (decode the possibly-multiline 3-digit reply - the
// untrusted-input hot op), and pc_ftp_parse_pasv (decode the 227 passive-mode data address). All pure (no
// sockets, no heap), so they link standalone. The device number comes from the rig /bench endpoint; this
// host ns/op + MB/s is a RELATIVE baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_FTP=1 performance_benching/services/ftp/host.cpp \
//       src/services/file_transfer/ftp/ftp.cpp -o /tmp/bf && /tmp/bf

#define PC_ENABLE_FTP 1
#include "services/file_transfer/ftp/ftp.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

using clk = std::chrono::steady_clock;

template <typename F> static double bench_ns(uint64_t iters, F fn)
{
    auto t0 = clk::now();
    for (uint64_t i = 0; i < iters; i++)
    {
        fn();
    }
    auto t1 = clk::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    return ns / (double)iters;
}

static void row(const char *feature, const char *op, double ns_per_op, double bytes_per_op)
{
    double mbps = bytes_per_op > 0 ? (bytes_per_op / (ns_per_op * 1e-9)) / 1e6 : 0.0;
    printf("| %-12s | %-24s | %10.1f | %9.1f |\n", feature, op, ns_per_op, mbps);
}

int main()
{
    // A multiline FEAT reply (RFC 959 4.2): 211- head, continuation lines, 211<SP>End terminator.
    const char feat[] = "211-Features:\r\n PASV\r\n SIZE\r\n MDTM\r\n211 End\r\n";
    const size_t featlen = sizeof(feat) - 1;

    // A 227 passive-mode reply carrying the (h1,h2,h3,h4,p1,p2) data address.
    const char pasv[] = "227 Entering Passive Mode (192,168,1,223,201,54).\r\n";
    const size_t pasvlen = sizeof(pasv) - 1;

    char cmd[128];
    size_t clen = pc_ftp_build_command(cmd, sizeof(cmd), "STOR", "pc_rig.txt");

    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    // build a STOR command line.
    {
        volatile size_t sink = 0;
        double ns = bench_ns(2000000, [&] { sink += pc_ftp_build_command(cmd, sizeof(cmd), "STOR", "pc_rig.txt"); });
        row("ftp", "build STOR command", ns, (double)clen);
        (void)sink;
    }
    // parse a multiline reply (scan to the NNN<SP> terminator - the per-reply hot op).
    {
        volatile int sink = 0;
        double ns = bench_ns(2000000, [&] {
            int code = 0;
            size_t used = 0;
            sink += pc_ftp_parse_reply(feat, featlen, &code, &used) ? code : 0;
        });
        row("ftp", "parse multiline reply", ns, (double)featlen);
        (void)sink;
    }
    // parse the 227 passive-mode data address tuple.
    {
        volatile int sink = 0;
        double ns = bench_ns(2000000, [&] {
            uint8_t ip[4];
            uint16_t port = 0;
            sink += pc_ftp_parse_pasv(pasv, pasvlen, ip, &port) ? port : 0;
        });
        row("ftp", "parse 227 PASV address", ns, (double)pasvlen);
        (void)sink;
    }

    return 0;
}
