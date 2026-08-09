// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark scaffold for services/mdns_service.
//
// DELIBERATELY EMPTY OF TIMED OPS (a NOTE-4 service). mdns_service is a thin wrapper over the
// ESP-IDF `mdns` component: pc_mdns_begin() -> mdns_init()/mdns_hostname_set()/mdns_service_add(),
// pc_mdns_txt() -> mdns_service_txt_item_set(), pc_mdns_add_service() -> mdns_service_add(). Every
// real code path is a direct pass-through into the mDNS responder, which performs real network/OS
// I/O (starts the responder, joins the multicast group, transmits DNS-SD records). The only pure
// CPU-side logic in the module is a couple of trivial null / empty-string argument guards - not a
// separable codec, checksum, or packer worth timing. Per the performance_benching/device NOTE-4 policy we do NOT
// fabricate a benchmark for such a service; this sketch exists only to compile-verify the real
// production mdns_service.cpp on-device (built with -DPC_ENABLE_MDNS=1) and prints an explanatory
// banner. This rig has no network attached, so none of the pc_mdns_* transport functions are ever
// called here.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/mdns_service -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "network_drivers/application/mdns_service/mdns_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("mdns_service");
        // No timed operations: mdns_service is a 100% pass-through wrapper over the ESP-IDF `mdns`
        // component (real network/OS I/O), with no pure, hardware-independent codec to bench. See
        // the file header for the full rationale. Calling pc_mdns_begin()/pc_mdns_txt()/
        // pc_mdns_add_service() would start the real mDNS responder and transmit on the network,
        // which is out of scope for this peripheral-less bench rig.
        printf("DB note: mdns_service is a pure ESP-IDF mdns pass-through - nothing to bench"
               "\n");
        DBENCH_DONE();
    }
}

DBENCH_MAIN("mdns_service")
