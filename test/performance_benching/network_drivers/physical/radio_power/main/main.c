// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for services/radio_power. NOTE: this is a thin Wi-Fi power-save
// *control* service - its real work is esp_wifi_set_ps() radio calls (protocore_radio_power_apply /
// busy_hold / busy_release), which are hardware side effects, not a deterministic CPU codec. The
// only genuinely pure, side-effect-free operations are the power-save mode -> name lookup and the
// current-mode getter, so those are all that is benched here (kept for coverage/consistency across
// the performance_benching/device suite; the figure is not a meaningful throughput number).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/radio_power -t upload --upload-port COM7
#include "device_bench.h"
#include "network_drivers/physical/radio_power/radio_power.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("radio_power");
        volatile uintptr_t sink = 0;
        Radio.ps.mode = PROTOCORE_PHY_PS_MAX_MODEM;
        DBENCH_OP("Radio.ps_name", 200000, Radio.ps_name(protocore_radio_power_span()); sink += (uintptr_t)Radio.text);
        DBENCH_OP("Radio.ps_mode", 200000, Radio.ps_mode(protocore_radio_power_span()); sink += (uintptr_t)Radio.mode);
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("radio_power")
