// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the EUROMAP 77 (OPC 40077) IMM_MES_Interface model
// (services/machine_tool/euromap77): the Read resolver (protocore_em77_read) answering a String leaf and a UInt64
// production-counter leaf out of a bound EmImm, the Browse resolver (protocore_em77_browse) walking an
// 8-child container and the Objects-folder organizes reference, and the shared OPC UA Binary Variant
// encoder (protocore_ua_w_variant, services/fieldbus/opcua) serializing a resolved UInt64 counter to wire bytes -
// every call here is pure struct-field lookup / table walk / byte packing (no heap, no sockets).
// Worked-example precedent: like performance_benching/device/modbus (a pure protocol codec), everything benched here
// runs the real production code path; contrast performance_benching/device/ads1115, where the hardware-facing half is
// out of scope. This rig has no OPC UA client attached, so the TCP transport half of services/fieldbus/opcua
// (protocore_opcua_rx and friends, compiled in by the Library Dependency Finder because euromap77.h pulls in
// opcua.h) is never exercised - only the deterministic model + codec calls above are ever benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/euromap77 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/machine_tool/euromap77/euromap77.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Node ids (must track the internal enum in euromap77.cpp / test_euromap77.cpp).
enum : uint32_t
{
    N_IMM = 7000,
    N_MACHINEINFO = 7100,
    N_MI_MANUFACTURER = 7101,
    N_AJV_JOBCYCLECOUNTER = 7321,
};

static EmImm g_imm;

void dbench_run(void)
{
    // Sample data mirrors test/test_euromap77/test_euromap77.cpp's setUp() (known-good, spec-conformant).
    g_imm.name = "IMM-1";
    g_imm.info.manufacturer = "Acme Plastics";
    g_imm.info.model = "IM-200";
    g_imm.info.serial_number = "SN-IMM-0042";
    g_imm.info.product_code = "IM200-STD";
    g_imm.info.hardware_revision = "H1";
    g_imm.info.software_revision = "3.4.0";
    g_imm.info.device_revision = "D2";
    g_imm.info.manufacturer_uri = "urn:acme:plastics";
    g_imm.status.is_present = true;
    g_imm.status.machine_mode = EM_MODE_AUTOMATIC;
    g_imm.active_job.job_name = "JOB-A";
    g_imm.active_job.job_description = "widget run";
    g_imm.active_job.material = "ABS";
    g_imm.active_job.product_name = "Widget";
    g_imm.active_job.mould_id = "MLD-9";
    g_imm.active_job.expected_cycle_time = 12.5;
    g_imm.active_job.num_cavities = 4;
    g_imm.active_job.nominal_parts = 100000ULL;
    g_imm.active_job_values.job_cycle_counter = 5000000001ULL; // > 2^32 to prove UInt64
    g_imm.active_job_values.machine_cycle_counter = 9000000002ULL;
    g_imm.active_job_values.last_cycle_time = 12.7;
    g_imm.active_job_values.average_cycle_time = 12.6;
    g_imm.active_job_values.job_parts_counter = 20000000004ULL;
    g_imm.active_job_values.job_good_parts_counter = 19000000003ULL;
    g_imm.active_job_values.job_bad_parts_counter = 1000000001ULL;
    g_imm.active_job_values.job_status = EM_JOB_IN_PRODUCTION;
    protocore_em77_bind(&g_imm);

    static OpcUaReference refs[8];
    static OpcUaVariant v;
    static uint8_t wire[16];
    static UaWriter w;

    for (;;)
    {
        DBENCH_BANNER("euromap77");
        volatile bool sinkb = false;
        volatile int32_t sinki = 0;

        DBENCH_OP("protocore_em77_read (string leaf)", 100000,
                  sinkb = protocore_em77_read(PROTOCORE_EM77_NS, N_MI_MANUFACTURER, OPCUA_ATTR_VALUE, &v));
        DBENCH_OP("protocore_em77_read (uint64 counter)", 100000,
                  sinkb = protocore_em77_read(PROTOCORE_EM77_NS, N_AJV_JOBCYCLECOUNTER, OPCUA_ATTR_VALUE, &v));
        DBENCH_OP("protocore_em77_browse (8-child obj)", 50000,
                  sinki += protocore_em77_browse(PROTOCORE_EM77_NS, N_MACHINEINFO, refs, 8));
        DBENCH_OP("protocore_em77_browse (Objects->IMM)", 100000, sinki += protocore_em77_browse(0, 85, refs, 8));

        // Re-resolve the UInt64 counter, then encode the Variant it left in `v` to wire bytes - the
        // Read + Binary-encode pair a client's OPC UA Read Response actually walks.
        protocore_em77_read(PROTOCORE_EM77_NS, N_AJV_JOBCYCLECOUNTER, OPCUA_ATTR_VALUE, &v);
        DBENCH_BULK("protocore_ua_w_variant (uint64)", 100000, 9,
                    (w = (UaWriter){wire, sizeof(wire), 0, true}, protocore_ua_w_variant(&w, &v)));

        (void)sinkb;
        (void)sinki;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("euromap77")
