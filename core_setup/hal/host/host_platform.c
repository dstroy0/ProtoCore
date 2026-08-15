// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file host_platform.c
 * @brief Host backend for the device seams in protocore_platform.h, so every caller above them runs.
 *
 * One table in BSS per seam, and a setter beside each so a test states the device fact it wants and
 * then drives the real module against it. Nothing here talks to hardware: the point is that
 * device_id, ota_rollback, power_mgmt, exc_coredump and bus_capture execute their own logic on the
 * host instead of compiling out.
 *
 * A build reaches this arm by raising the matching PROTOCORE_HAS_VENDOR_* capability, each of which is
 * #ifndef-guarded so an env defines it on the command line. Left at its host default of 0 the
 * capability compiles the seam away and the owning module takes its refusing arm, exactly as before.
 */

#include "core_setup/board_profiles/protocore_platform.h"

#if !PROTOCORE_VENDOR_SILICON

#include "core_setup/hal/host/host_platform.h"

#include <stdint.h> // INT16_MIN: what a part with no die sensor reports

// Every device fact the seams above answer with, owned by one instance (internal linkage): one
// named owner, unreachable from any other translation unit.
typedef struct
{
#if PROTOCORE_HAS_VENDOR_MAC
    uint8_t mac[6];
    int mac_ok;
#endif
#if PROTOCORE_HAS_VENDOR_HEAP_INFO
    uint32_t heap_free;
    uint32_t heap_min_free;
    uint32_t heap_size;
    uint32_t heap_max_alloc;
    uint32_t stack_free;
#endif
#if PROTOCORE_HAS_VENDOR_PM
    int brownout;
    int16_t die_temp_c;
    uint16_t cpu_mhz;
#endif
#if PROTOCORE_HAS_VENDOR_BT
    int bt_released;
#endif
#if PROTOCORE_HAS_VENDOR_OTA
    uint8_t img_state;
    int img_committed;
    int img_rolled_back;
#endif
#if PROTOCORE_HAS_VENDOR_COREDUMP
    uint8_t crash[PROTOCORE_HOST_CRASHDUMP_CAP];
    uint32_t crash_len;
    uint32_t crash_pc;
    uint32_t crash_addr;
    char crash_task[PROTOCORE_PLATFORM_CRASH_TASK_MAX];
    uint32_t crash_frame_pc[PROTOCORE_PLATFORM_CRASH_FRAMES];
    uint32_t crash_frames;
    int crash_has_summary;
#endif
#if PROTOCORE_HAS_VENDOR_CAN
    protocore_can_frame can[PROTOCORE_HOST_CAN_DEPTH];
    uint32_t can_head;
    uint32_t can_tail;
    int can_open;
    uint32_t can_bitrate;
#endif
    int nonempty; ///< every group above can be compiled out; C forbids an empty struct
} HostPlatformCtx;

static HostPlatformCtx s_hp;

void protocore_host_platform_reset(void)
{
    HostPlatformCtx empty = {0};
    s_hp = empty;
#if PROTOCORE_HAS_VENDOR_PM
    s_hp.die_temp_c = INT16_MIN; // no sensor stated yet, which is what a part without one reports
#endif
#if PROTOCORE_HAS_VENDOR_OTA
    s_hp.img_state = PROTOCORE_PLATFORM_IMG_UNDEFINED;
#endif
}

// --- what a test states ----------------------------------------------------

#if PROTOCORE_HAS_VENDOR_MAC
void protocore_host_set_mac(const uint8_t mac[6])
{
    for (int i = 0; i < 6; i++)
    {
        s_hp.mac[i] = mac[i];
    }
    s_hp.mac_ok = 1;
}
#endif

#if PROTOCORE_HAS_VENDOR_HEAP_INFO
void protocore_host_set_heap(uint32_t free_now, uint32_t min_free, uint32_t total, uint32_t max_alloc)
{
    s_hp.heap_free = free_now;
    s_hp.heap_min_free = min_free;
    s_hp.heap_size = total;
    s_hp.heap_max_alloc = max_alloc;
}

void protocore_host_set_stack(uint32_t free_bytes)
{
    s_hp.stack_free = free_bytes;
}
#endif

#if PROTOCORE_HAS_VENDOR_PM
void protocore_host_set_brownout(int on)
{
    s_hp.brownout = on;
}

void protocore_host_set_die_temp_c(int16_t c)
{
    s_hp.die_temp_c = c;
}

void protocore_host_set_cpu_mhz(uint16_t mhz)
{
    s_hp.cpu_mhz = mhz;
}
#endif

#if PROTOCORE_HAS_VENDOR_OTA
void protocore_host_set_img_state(uint8_t state)
{
    s_hp.img_state = state;
}

int protocore_host_img_committed(void)
{
    return s_hp.img_committed;
}

int protocore_host_img_rolled_back(void)
{
    return s_hp.img_rolled_back;
}
#endif

#if PROTOCORE_HAS_VENDOR_BT
int protocore_host_bt_released(void)
{
    return s_hp.bt_released;
}
#endif

#if PROTOCORE_HAS_VENDOR_COREDUMP
void protocore_host_set_crashdump(const uint8_t *image, uint32_t len)
{
    s_hp.crash_len = len > PROTOCORE_HOST_CRASHDUMP_CAP ? PROTOCORE_HOST_CRASHDUMP_CAP : len;
    for (uint32_t i = 0; i < s_hp.crash_len; i++)
    {
        s_hp.crash[i] = image[i];
    }
}

void protocore_host_set_crash_summary(uint32_t pc, uint32_t addr, const char *task)
{
    s_hp.crash_pc = pc;
    s_hp.crash_addr = addr;
    uint32_t i = 0;
    while (task && task[i] != '\0' && i + 1 < PROTOCORE_PLATFORM_CRASH_TASK_MAX)
    {
        s_hp.crash_task[i] = task[i];
        i++;
    }
    s_hp.crash_task[i] = '\0';
    s_hp.crash_has_summary = 1;
}

void protocore_host_set_crash_frames(const uint32_t *pc, uint32_t count)
{
    s_hp.crash_frames = count > PROTOCORE_PLATFORM_CRASH_FRAMES ? PROTOCORE_PLATFORM_CRASH_FRAMES : count;
    for (uint32_t i = 0; i < s_hp.crash_frames; i++)
    {
        s_hp.crash_frame_pc[i] = pc[i];
    }
}
#endif

#if PROTOCORE_HAS_VENDOR_CAN
void protocore_host_can_push(const protocore_can_frame *f)
{
    uint32_t next = (s_hp.can_head + 1u) % PROTOCORE_HOST_CAN_DEPTH;
    if (next == s_hp.can_tail)
    {
        return; // full: a dropped frame is what the driver queue does too
    }
    s_hp.can[s_hp.can_head] = *f;
    s_hp.can_head = next;
}

uint32_t protocore_host_can_bitrate(void)
{
    return s_hp.can_bitrate;
}
#endif

// --- the seams -------------------------------------------------------------

#if PROTOCORE_HAS_VENDOR_MAC
int protocore_platform_mac_read(uint8_t mac[6])
{
    if (!s_hp.mac_ok)
    {
        return 0;
    }
    for (int i = 0; i < 6; i++)
    {
        mac[i] = s_hp.mac[i];
    }
    return 1;
}
#endif

#if PROTOCORE_HAS_VENDOR_HEAP_INFO
uint32_t protocore_platform_heap_free(void)
{
    return s_hp.heap_free;
}

uint32_t protocore_platform_heap_min_free(void)
{
    return s_hp.heap_min_free;
}

uint32_t protocore_platform_heap_size(void)
{
    return s_hp.heap_size;
}

uint32_t protocore_platform_heap_max_alloc(void)
{
    return s_hp.heap_max_alloc;
}

uint32_t protocore_platform_stack_free(void)
{
    return s_hp.stack_free;
}
#endif

#if PROTOCORE_HAS_VENDOR_PM
int protocore_platform_reset_was_brownout(void)
{
    return s_hp.brownout;
}

int16_t protocore_platform_die_temp_c(void)
{
    return s_hp.die_temp_c;
}

uint16_t protocore_platform_cpu_mhz(void)
{
    return s_hp.cpu_mhz;
}

int protocore_platform_set_cpu_mhz(uint32_t mhz)
{
    s_hp.cpu_mhz = (uint16_t)mhz;
    return 1;
}
#endif

#if PROTOCORE_HAS_VENDOR_BT
int protocore_platform_bt_release(void)
{
    if (s_hp.bt_released)
    {
        return 0; // already handed back, so this call released nothing
    }
    s_hp.bt_released = 1;
    return 1;
}
#endif

#if PROTOCORE_HAS_VENDOR_OTA
uint8_t protocore_platform_img_state(void)
{
    return s_hp.img_state;
}

void protocore_platform_img_commit(void)
{
    s_hp.img_committed = 1;
    s_hp.img_state = PROTOCORE_PLATFORM_IMG_VALID;
}

void protocore_platform_img_rollback(void)
{
    // The device call does not return; the host records it and does, so a test can assert on it.
    s_hp.img_rolled_back = 1;
    s_hp.img_state = PROTOCORE_PLATFORM_IMG_INVALID;
}
#endif

#if PROTOCORE_HAS_VENDOR_COREDUMP
uint32_t protocore_platform_crashdump_size(void)
{
    return s_hp.crash_len;
}

int protocore_platform_crashdump_read(uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (offset > s_hp.crash_len || len > s_hp.crash_len - offset)
    {
        return 0;
    }
    for (uint32_t i = 0; i < len; i++)
    {
        buf[i] = s_hp.crash[offset + i];
    }
    return 1;
}

int protocore_platform_crashdump_erase(void)
{
    s_hp.crash_len = 0;
    s_hp.crash_has_summary = 0;
    return 1;
}

int protocore_platform_crashdump_summary(protocore_crash_summary *out)
{
    if (!s_hp.crash_has_summary)
    {
        return 0;
    }
    out->pc = s_hp.crash_pc;
    out->fault_addr = s_hp.crash_addr;
    out->has_fault_addr = 1;
    uint32_t i = 0;
    while (i + 1 < PROTOCORE_PLATFORM_CRASH_TASK_MAX && s_hp.crash_task[i] != '\0')
    {
        out->task[i] = s_hp.crash_task[i];
        i++;
    }
    out->task[i] = '\0';
    for (uint32_t f = 0; f < s_hp.crash_frames; f++)
    {
        out->frame_pc[f] = s_hp.crash_frame_pc[f];
    }
    out->frame_count = (uint8_t)s_hp.crash_frames;
    return 1;
}
#endif

#if PROTOCORE_HAS_VENDOR_CAN
int protocore_platform_can_open(int tx_pin, int rx_pin, uint32_t bitrate)
{
    (void)tx_pin;
    (void)rx_pin;
    // The same four rates the controller has timing terms for; an unlisted one is refused here too,
    // so a test sees the device's answer rather than a host that accepts anything.
    if (s_hp.can_open || (bitrate != 1000000u && bitrate != 500000u && bitrate != 250000u && bitrate != 125000u))
    {
        return 0;
    }
    s_hp.can_open = 1;
    s_hp.can_bitrate = bitrate;
    return 1;
}

int protocore_platform_can_recv(protocore_can_frame *out)
{
    if (!s_hp.can_open || s_hp.can_tail == s_hp.can_head)
    {
        return 0;
    }
    *out = s_hp.can[s_hp.can_tail];
    s_hp.can_tail = (s_hp.can_tail + 1u) % PROTOCORE_HOST_CAN_DEPTH;
    return 1;
}

void protocore_platform_can_close(void)
{
    s_hp.can_open = 0;
    s_hp.can_bitrate = 0;
    s_hp.can_head = 0;
    s_hp.can_tail = 0;
}
#endif

#endif // !PROTOCORE_VENDOR_SILICON
