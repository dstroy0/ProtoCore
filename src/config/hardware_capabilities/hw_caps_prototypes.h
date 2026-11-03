// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hw_caps_prototypes.h
 * @brief The seams each hardware capability promises, declared only where the capability is 1.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HW_CAPS_PROTOTYPES_H
#define PROTOCORE_HW_CAPS_PROTOTYPES_H

// ---------------------------------------------------------------------------
// Device facts, power domains and stored images
// ---------------------------------------------------------------------------
//
// The core cannot name a vendor, so it asks here. Each seam sits under the capability that answers
// whether the part carries the thing at all: a build whose capability is 0 gets no declaration, so
// reaching for it is a compile error rather than a link-time surprise, and the owning module keys
// its own refusing arm off the same macro.
//
// Every one is implemented once per arm - test/core_setup/hal/<vendor> on silicon,
// test/core_setup/hal/portable on the host - so a test drives the real module against the mock rather
// than compiling the module out. Plain int and uint32_t only: this header carries <stdint.h> and
// nothing else, and it is reached before the library's own types exist.

#if PROTOCORE_HAS_VENDOR_MAC
/** @brief The part's burned-in station address, six bytes. 0 when there was none to read. */
int protocore_platform_mac_read(uint8_t mac[6]);
#endif
#if PROTOCORE_HAS_VENDOR_HEAP_INFO
uint32_t protocore_platform_heap_free(void);      ///< bytes free in the allocator right now
uint32_t protocore_platform_heap_min_free(void);  ///< its low-water mark since boot
uint32_t protocore_platform_heap_size(void);      ///< its total size
uint32_t protocore_platform_heap_max_alloc(void); ///< the largest single block it would hand out now
uint32_t protocore_platform_stack_free(void);     ///< bytes never touched on the calling task's stack
#endif
#if PROTOCORE_HAS_VENDOR_PM
/** @brief The reset that started this boot was a brownout. Reads the cause; the caller latches it. */
int protocore_platform_reset_was_brownout(void);

/** @brief Die temperature in whole degrees C, or INT16_MIN where the part has no usable sensor. */
int16_t protocore_platform_die_temp_c(void);

/** @brief The CPU clock the part is running at, in MHz. */
uint16_t protocore_platform_cpu_mhz(void);

/** @brief Set the CPU clock to @p mhz. 0 when the part refused it. */
int protocore_platform_set_cpu_mhz(uint32_t mhz);
#endif
#if PROTOCORE_HAS_VENDOR_BT
/** @brief Disable the Bluetooth controller and hand its RAM back. 0 when nothing was released. */
int protocore_platform_bt_release(void);
#endif
#if PROTOCORE_HAS_VENDOR_OTA
uint8_t protocore_platform_img_state(void); ///< the running image's state, PROTOCORE_PLATFORM_IMG_*
void protocore_platform_img_commit(void);   ///< mark it valid and cancel the pending rollback
void protocore_platform_img_rollback(void); ///< mark it invalid and reboot into the previous one

/** @brief Longest partition label a walk carries, terminator included. */
#define PROTOCORE_PLATFORM_PARTITION_LABEL_MAX 17

/** @brief One entry of the flash partition table, in the library's own shape rather than a vendor's. */
typedef struct
{
    char label[PROTOCORE_PLATFORM_PARTITION_LABEL_MAX]; ///< the name the table gives it
    uint32_t address;                                   ///< offset in flash
    uint32_t size;                                      ///< bytes it spans
    uint8_t type;                                       ///< the table's type code
    uint8_t subtype;                                    ///< the table's subtype code
    uint8_t running;                                    ///< this is the image currently executing
} protocore_platform_partition;

/**
 * @brief Walk the partition table into @p out, at most @p max entries.
 *
 * The walk is the vendor's - only the SDK knows how the table is stored - so it lives beside the
 * vendor's other seams rather than in src/, which never includes a vendor header.
 *
 * @return entries written. 0 where the part has no partition table to walk.
 */
uint8_t protocore_platform_partition_walk(protocore_platform_partition *out, uint8_t max);
#endif
#if PROTOCORE_HAS_VENDOR_COREDUMP
/** @brief Backtrace frames a crash summary carries. */
#ifndef PROTOCORE_PLATFORM_CRASH_FRAMES
#define PROTOCORE_PLATFORM_CRASH_FRAMES 32
#endif
/** @brief Longest faulting-task name a summary carries, terminator included. */
#define PROTOCORE_PLATFORM_CRASH_TASK_MAX 32

/**
 * @brief One crash, in the library's own shape rather than a vendor's.
 *
 * @ref frame_count is 0 where the part stores a stack dump rather than a walkable backtrace: those
 * need debug information that lives off the device, so the frames are absent, not invented.
 */
typedef struct
{
    uint32_t pc;                                        ///< the faulting program counter
    uint32_t fault_addr;                                ///< the address the fault names
    uint8_t has_fault_addr;                             ///< that address is meaningful
    char task[PROTOCORE_PLATFORM_CRASH_TASK_MAX];       ///< the faulting task's name
    uint32_t frame_pc[PROTOCORE_PLATFORM_CRASH_FRAMES]; ///< return addresses, as the part stored them
    uint8_t frame_count;                                ///< frames present in @ref frame_pc
} protocore_crash_summary;

/** @brief Bytes of stored crash image, once it verifies. 0 when there is none or it is corrupt. */
uint32_t protocore_platform_crashdump_size(void);
/** @brief Read @p len bytes at @p offset within the crash image. 0 on a short or failed read. */
int protocore_platform_crashdump_read(uint32_t offset, uint8_t *buf, uint32_t len);
/** @brief Discard the stored crash image. */
int protocore_platform_crashdump_erase(void);
/** @brief The crash summary, where the stored image carries one. */
int protocore_platform_crashdump_summary(protocore_crash_summary *out);
#endif
#if PROTOCORE_HAS_VENDOR_CAN
/** @brief One received CAN frame, in the library's own shape rather than a vendor's. */
typedef struct
{
    uint32_t id;     ///< the identifier: 11-bit, or 29-bit when @ref ext is set
    uint8_t ext;     ///< the identifier is the 29-bit extended form
    uint8_t rtr;     ///< remote transmission request, so the frame carries no data
    uint8_t len;     ///< data bytes present, 0 to 8
    uint8_t data[8]; ///< the payload
} protocore_can_frame;

/** @brief Open the controller on @p tx_pin / @p rx_pin at @p bitrate, listening to every id. */
int protocore_platform_can_open(int tx_pin, int rx_pin, uint32_t bitrate);
/** @brief Take one frame from the driver queue without blocking. 0 when the queue is empty. */
int protocore_platform_can_recv(protocore_can_frame *out);
/** @brief Stop the controller and release it. */
void protocore_platform_can_close(void);
#endif

#endif // PROTOCORE_HW_CAPS_PROTOTYPES_H
