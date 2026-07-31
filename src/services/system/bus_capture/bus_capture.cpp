// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bus_capture.cpp
 * @brief bus_capture implementation: the pure SocketCAN framer + the ESP32 TWAI listen-only bind.
 */

#include "services/system/bus_capture/bus_capture.h"

#if PC_ENABLE_BUS_CAPTURE

#if defined(ARDUINO)
#include "driver/twai.h"
#endif
size_t can_to_socketcan(const CanFrame *f, uint8_t *out, size_t cap)
{
    if (!f || !out || cap < PC_SOCKETCAN_FRAME_LEN)
    {
        return 0;
    }

    uint32_t id = f->id & (f->extended ? PC_CAN_EXT_ID_MASK : PC_CAN_STD_ID_MASK);
    if (f->extended)
    {
        id |= PC_CAN_EFF_FLAG;
    }
    if (f->rtr)
    {
        id |= PC_CAN_RTR_FLAG;
    }

    out[0] = (uint8_t)(id >> 24); // can_id, big-endian
    out[1] = (uint8_t)(id >> 16);
    out[2] = (uint8_t)(id >> 8);
    out[3] = (uint8_t)id;

    uint8_t dlc = f->dlc > PC_CAN_MAX_DLC ? PC_CAN_MAX_DLC : f->dlc;
    out[4] = dlc; // length
    out[5] = 0;   // __pad
    out[6] = 0;   // __res0
    out[7] = 0;   // len8_dlc / __res1
    for (int i = 0; i < PC_CAN_MAX_DLC; i++)
    {
        out[8 + i] = (i < dlc && !f->rtr) ? f->data[i] : 0;
    }
    return PC_SOCKETCAN_FRAME_LEN;
}

// --- ESP32 TWAI (CAN) binding ------------------------------------------------------------
#ifdef ARDUINO

namespace
{
// All bus-capture bind state, owned by one instance (internal linkage): the frame sink and
// the running flag, grouped so it is one named owner, unreachable from any other TU.
struct BusCaptureCtx
{
    bus_capture_sink_fn sink = nullptr;
    bool running = false;
};
BusCaptureCtx s_bus;

bool timing_for(uint32_t bitrate, twai_timing_config_t *t)
{
    switch (bitrate)
    {
    case 1000000: {
        twai_timing_config_t c = TWAI_TIMING_CONFIG_1MBITS();
        *t = c;
        return true;
    }
    case 500000: {
        twai_timing_config_t c = TWAI_TIMING_CONFIG_500KBITS();
        *t = c;
        return true;
    }
    case 250000: {
        twai_timing_config_t c = TWAI_TIMING_CONFIG_250KBITS();
        *t = c;
        return true;
    }
    case 125000: {
        twai_timing_config_t c = TWAI_TIMING_CONFIG_125KBITS();
        *t = c;
        return true;
    }
    default:
        return false;
    }
}
} // namespace

bool bus_capture_begin(int tx_pin, int rx_pin, uint32_t bitrate, bus_capture_sink_fn sink)
{
    if (!sink)
    {
        return false;
    }
    twai_timing_config_t timing;
    if (!timing_for(bitrate, &timing))
    {
        return false;
    }
    twai_general_config_t gen =
        TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)tx_pin, (gpio_num_t)rx_pin, TWAI_MODE_LISTEN_ONLY);
    twai_filter_config_t filt = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&gen, &timing, &filt) != ESP_OK)
    {
        return false;
    }
    if (twai_start() != ESP_OK)
    {
        twai_driver_uninstall();
        return false;
    }
    s_bus.sink = sink;
    s_bus.running = true;
    return true;
}

void bus_capture_poll(void)
{
    if (!s_bus.running || !s_bus.sink)
    {
        return;
    }
    twai_message_t m;
    while (twai_receive(&m, 0) == ESP_OK) // 0 ticks = non-blocking drain
    {
        CanFrame f;
        f.id = m.identifier;
        f.extended = m.extd;
        f.rtr = m.rtr;
        f.dlc = m.data_length_code > PC_CAN_MAX_DLC ? PC_CAN_MAX_DLC : m.data_length_code;
        for (int i = 0; i < PC_CAN_MAX_DLC; i++)
        {
            f.data[i] = (i < f.dlc) ? m.data[i] : 0;
        }
        s_bus.sink(&f);
    }
}

void bus_capture_end(void)
{
    if (!s_bus.running)
    {
        return;
    }
    twai_stop();
    twai_driver_uninstall();
    s_bus.running = false;
    s_bus.sink = nullptr;
}

#else // host build - no TWAI controller

bool bus_capture_begin(int, int, uint32_t, bus_capture_sink_fn)
{
    return false;
}
void bus_capture_poll(void)
{
    // host build: no TWAI controller, nothing to drain
}
void bus_capture_end(void)
{
    // host build: no TWAI controller, nothing to stop
}

#endif // ARDUINO

#endif // PC_ENABLE_BUS_CAPTURE
