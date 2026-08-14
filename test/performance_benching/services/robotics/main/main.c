// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the OPC UA Robotics companion model (services/machine_tool/robotics): the
// Read resolver (node id -> value over a bound RoboticsMotionDeviceSystem) and the Browse of the
// address space. Pure model-walk over an in-RAM device system; the OPC UA transport is elsewhere.
//
// Build/flash:  idf.py -C test/performance_benching/robotics -t upload --upload-port COM7
#include "device_bench.h"
#include "services/fieldbus/opcua/opcua.h"
#include "services/machine_tool/robotics/robotics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static RoboticsMotionDeviceSystem g_mds;

void dbench_run(void)
{
    memset(&g_mds, 0, sizeof(g_mds));
    g_mds.name = "Robot-1";
    g_mds.device.manufacturer = "Acme Robotics";
    g_mds.device.model = "AR-6";
    g_mds.device.product_code = "AR6-STD";
    g_mds.device.serial_number = "SN-R-0007";
    g_mds.device.category = ROBOTICS_CAT_ARTICULATED_ROBOT;
    g_mds.device.on_path = true;
    g_mds.device.in_control = true;
    g_mds.device.speed_override = 75.0;
    g_mds.device.axis_count = 3;
    g_mds.device.axes[0].actual_position = 10.5;
    g_mds.device.axes[0].motion_profile = ROBOTICS_PROFILE_ROTARY;
    g_mds.device.axes[1].actual_position = -20.25;
    g_mds.device.axes[1].motion_profile = ROBOTICS_PROFILE_LINEAR;
    g_mds.device.axes[2].actual_position = 33.0;
    g_mds.controller.manufacturer = "Acme Controls";
    g_mds.controller.sw_revision = "4.2.0";
    g_mds.safety.operational_mode = ROBOTICS_MODE_AUTOMATIC;
    g_mds.safety.protective_stop = true;
    protocore_robotics_bind(&g_mds);

    for (;;)
    {
        DBENCH_BANNER("robotics");
        volatile int sink = 0;
        OpcUaVariant v;
        DBENCH_OP("protocore_robotics_read manufacturer", 200000,
                  sink +=
                  protocore_robotics_read(PROTOCORE_ROBOTICS_NS, 6201 /*N_MD_MANUFACTURER*/, OPCUA_ATTR_VALUE, &v));
        DBENCH_OP("protocore_robotics_read axis1 pos", 200000,
                  sink +=
                  protocore_robotics_read(PROTOCORE_ROBOTICS_NS, 6411 /*N_AXIS1_POSITION*/, OPCUA_ATTR_VALUE, &v));
        OpcUaReference refs[8];
        DBENCH_OP("protocore_robotics_browse (ObjectsFolder)", 200000,
                  sink += protocore_robotics_browse(0, 85, refs, 8));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("robotics")
