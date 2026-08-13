// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file host_platform.h
 * @brief What a test states before it drives a module that reads a device fact.
 *
 * The seams in protocore_platform.h answer questions only silicon can answer: the burned-in address,
 * allocator figures, the reset cause, the radio power domain, the stored image's state, the crash
 * image, the CAN controller. On the host those answers come from the table in host_platform.c, and
 * these are the setters that fill it, so a test says what the device would have said and then runs
 * the real module against it.
 */

#ifndef PROTOCORE_HOST_PLATFORM_H
#define PROTOCORE_HOST_PLATFORM_H

#include "core_setup/board_profiles/protocore_platform.h"

#if !PROTOCORE_VENDOR_SILICON

/** @brief Largest crash image the host table holds. */
#ifndef PROTOCORE_HOST_CRASHDUMP_CAP
#define PROTOCORE_HOST_CRASHDUMP_CAP 512u
#endif

/** @brief Frames the host CAN queue holds before it drops, as the driver queue does. */
#ifndef PROTOCORE_HOST_CAN_DEPTH
#define PROTOCORE_HOST_CAN_DEPTH 16u
#endif

/** @brief Clear every stated fact. A test calls this in setUp; it is what a reboot would give. */
void protocore_host_platform_reset(void);

#if PROTOCORE_HAS_VENDOR_MAC
void protocore_host_set_mac(const uint8_t mac[6]); ///< the burned-in address
#endif

#if PROTOCORE_HAS_VENDOR_HEAP_INFO
void protocore_host_set_heap(uint32_t free_now, uint32_t min_free, uint32_t total,
                             uint32_t max_alloc); ///< allocator figures
#endif

#if PROTOCORE_HAS_VENDOR_PM
void protocore_host_set_brownout(int on);      ///< the reset cause
void protocore_host_set_die_temp_c(int16_t c); ///< the die temperature, INT16_MIN for no sensor
void protocore_host_set_cpu_mhz(uint16_t mhz); ///< the clock the part reports it is running at
#endif

#if PROTOCORE_HAS_VENDOR_BT
int protocore_host_bt_released(void); ///< the radio power domain was handed back
#endif

#if PROTOCORE_HAS_VENDOR_OTA
void protocore_host_set_img_state(uint8_t state); ///< PROTOCORE_PLATFORM_IMG_*
int protocore_host_img_committed(void);           ///< the module marked the running image valid
int protocore_host_img_rolled_back(void);         ///< the module asked for a rollback
#endif

#if PROTOCORE_HAS_VENDOR_COREDUMP
void protocore_host_set_crashdump(const uint8_t *image, uint32_t len);               ///< the stored crash image
void protocore_host_set_crash_summary(uint32_t pc, uint32_t addr, const char *task); ///< its summary
void protocore_host_set_crash_frames(const uint32_t *pc, uint32_t count);            ///< its backtrace
#endif

#if PROTOCORE_HAS_VENDOR_CAN
void protocore_host_can_push(const protocore_can_frame *f); ///< queue one received frame
uint32_t protocore_host_can_bitrate(void);                  ///< the rate it was opened at, 0 when closed
#endif

#endif // !PROTOCORE_VENDOR_SILICON

#endif // PROTOCORE_HOST_PLATFORM_H
