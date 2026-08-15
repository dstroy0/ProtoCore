// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_platform.c
 * @brief Espressif answers to the platform questions the core asks.
 *
 * The core cannot name a vendor, so it asks protocore_platform_*() and this supplies it: the
 * execution context id, the burned-in address, allocator figures, the reset cause, the radio power
 * domain, the stored image's rollback state, the crash image, and the CAN controller.
 *
 * Each body is the vendor call and the translation onto the library's own values. No state lives
 * here except the CAN driver's open flag, which the driver install/uninstall pair requires.
 */

#include "core_setup/board_profiles/protocore_platform.h"

#if PROTOCORE_VENDOR_ESP

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

uintptr_t protocore_platform_context_id(void)
{
    return (uintptr_t)xTaskGetCurrentTaskHandle();
}

#if PROTOCORE_HAS_VENDOR_MAC
#include "esp_mac.h"

int protocore_platform_mac_read(uint8_t mac[6])
{
    return esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK; // the stable factory address
}
#endif // PROTOCORE_HAS_VENDOR_MAC

#if PROTOCORE_HAS_VENDOR_HEAP_INFO
#include "esp_heap_caps.h"
#include "esp_system.h"

uint32_t protocore_platform_heap_free(void)
{
    return (uint32_t)esp_get_free_heap_size();
}

uint32_t protocore_platform_heap_min_free(void)
{
    return (uint32_t)esp_get_minimum_free_heap_size();
}

uint32_t protocore_platform_heap_size(void)
{
    return (uint32_t)heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
}

uint32_t protocore_platform_heap_max_alloc(void)
{
    return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
}

// The high-water mark counts stack words never touched; the seam reports bytes.
uint32_t protocore_platform_stack_free(void)
{
    return (uint32_t)uxTaskGetStackHighWaterMark(NULL) * (uint32_t)sizeof(StackType_t);
}
#endif // PROTOCORE_HAS_VENDOR_HEAP_INFO

#if PROTOCORE_HAS_VENDOR_PM
#include "esp_system.h"

int protocore_platform_reset_was_brownout(void)
{
    return esp_reset_reason() == ESP_RST_BROWNOUT;
}

int16_t protocore_platform_die_temp_c(void)
{
#if defined(SOC_TEMP_SENSOR_SUPPORTED) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) ||  \
    defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32P4)
    float t = temperatureRead();
    // The driver reports a sentinel far outside any real die temperature when the sensor is not up.
    if (t < -60.0f || t > 200.0f)
    {
        return INT16_MIN;
    }
    return (int16_t)(t + (t < 0 ? -0.5f : 0.5f));
#else
    return INT16_MIN; // this part has no usable internal sensor
#endif
}

uint16_t protocore_platform_cpu_mhz(void)
{
    return (uint16_t)getCpuFrequencyMhz();
}

int protocore_platform_set_cpu_mhz(uint32_t mhz)
{
    return setCpuFrequencyMhz(mhz) ? 1 : 0;
}
#endif // PROTOCORE_HAS_VENDOR_PM

#if PROTOCORE_HAS_VENDOR_BT
#include "esp_bt.h"

int protocore_platform_bt_release(void)
{
    // Disable before release: releasing an enabled controller's memory is rejected, and the whole
    // point is to drop the domain rather than report a success that did not happen.
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED)
    {
        esp_bt_controller_disable();
    }
    return esp_bt_controller_mem_release(ESP_BT_MODE_BTDM) == ESP_OK;
}
#endif // PROTOCORE_HAS_VENDOR_BT

#if PROTOCORE_HAS_VENDOR_OTA
#include "esp_ota_ops.h"

uint8_t protocore_platform_img_state(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (!running || esp_ota_get_state_partition(running, &st) != ESP_OK)
    {
        return PROTOCORE_PLATFORM_IMG_UNDEFINED;
    }
    switch (st)
    {
    case ESP_OTA_IMG_NEW:
        return PROTOCORE_PLATFORM_IMG_NEW;
    case ESP_OTA_IMG_PENDING_VERIFY:
        return PROTOCORE_PLATFORM_IMG_PENDING_VERIFY;
    case ESP_OTA_IMG_VALID:
        return PROTOCORE_PLATFORM_IMG_VALID;
    case ESP_OTA_IMG_INVALID:
        return PROTOCORE_PLATFORM_IMG_INVALID;
    case ESP_OTA_IMG_ABORTED:
        return PROTOCORE_PLATFORM_IMG_ABORTED;
    default:
        return PROTOCORE_PLATFORM_IMG_UNDEFINED;
    }
}

void protocore_platform_img_commit(void)
{
    esp_ota_mark_app_valid_cancel_rollback();
}

void protocore_platform_img_rollback(void)
{
    esp_ota_mark_app_invalid_rollback_and_reboot(); // does not return
}
#endif // PROTOCORE_HAS_VENDOR_OTA

#if PROTOCORE_HAS_VENDOR_COREDUMP
#include "esp_core_dump.h"
#include "esp_partition.h"

// The image's address is flash-absolute and a partition read wants an offset within the partition,
// so every reader converts here once.
static int crashdump_locate(const esp_partition_t **part_out, uint32_t *base_out, uint32_t *size_out)
{
    size_t addr = 0;
    size_t size = 0;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK || size == 0)
    {
        return 0;
    }
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    if (!part || addr < part->address || (addr - part->address) + size > part->size)
    {
        return 0;
    }
    *part_out = part;
    *base_out = (uint32_t)(addr - part->address);
    *size_out = (uint32_t)size;
    return 1;
}

uint32_t protocore_platform_crashdump_size(void)
{
    const esp_partition_t *part = NULL;
    uint32_t base = 0;
    uint32_t size = 0;
    if (!crashdump_locate(&part, &base, &size))
    {
        return 0;
    }
    return esp_core_dump_image_check() == ESP_OK ? size : 0;
}

int protocore_platform_crashdump_read(uint32_t offset, uint8_t *buf, uint32_t len)
{
    const esp_partition_t *part = NULL;
    uint32_t base = 0;
    uint32_t size = 0;
    if (!crashdump_locate(&part, &base, &size) || offset > size || len > size - offset)
    {
        return 0;
    }
    return esp_partition_read(part, base + offset, buf, len) == ESP_OK;
}

int protocore_platform_crashdump_erase(void)
{
    return esp_core_dump_image_erase() == ESP_OK;
}

int protocore_platform_crashdump_summary(protocore_crash_summary *out)
{
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
    esp_core_dump_summary_t s;
    for (uint32_t z = 0; z < sizeof(s); z++)
    {
        ((uint8_t *)&s)[z] = 0;
    }
    if (esp_core_dump_get_summary(&s) != ESP_OK)
    {
        return 0;
    }
    out->pc = (uint32_t)s.exc_pc;
    uint32_t i = 0;
    while (i + 1 < PROTOCORE_PLATFORM_CRASH_TASK_MAX && s.exc_task[i] != '\0')
    {
        out->task[i] = s.exc_task[i];
        i++;
    }
    out->task[i] = '\0';

#if CONFIG_IDF_TARGET_ARCH_XTENSA
    // The windowed ABI lets the part walk its own stack, so a real backtrace is stored.
    out->fault_addr = (uint32_t)s.ex_info.exc_vaddr;
    out->has_fault_addr = 1;
    uint32_t depth = (uint32_t)s.exc_bt_info.depth;
    if (depth > PROTOCORE_PLATFORM_CRASH_FRAMES)
    {
        depth = PROTOCORE_PLATFORM_CRASH_FRAMES;
    }
    for (uint32_t f = 0; f < depth; f++)
    {
        out->frame_pc[f] = (uint32_t)s.exc_bt_info.bt[f];
    }
    out->frame_count = (uint8_t)depth;
#else
    // A stack dump, not a backtrace: unwinding it needs debug information held off the device, so
    // the trap cause and value are reported and no frames are invented.
    out->fault_addr = (uint32_t)s.ex_info.mtval;
    out->has_fault_addr = 1;
    out->frame_count = 0;
#endif
    return 1;
#else
    (void)out;
    return 0; // built without flash/ELF crash images, so there is no summary to read
#endif
}
#endif // PROTOCORE_HAS_VENDOR_COREDUMP

#if PROTOCORE_HAS_VENDOR_CAN
#include "driver/twai.h"

static int s_can_open;

// The controller's timing terms for the bitrates the capture offers. An unlisted rate is refused
// rather than approximated: a wrong prescaler samples every frame in the wrong place.
static int can_timing(uint32_t bitrate, twai_timing_config_t *t)
{
    if (bitrate == 1000000u)
    {
        twai_timing_config_t c = TWAI_TIMING_CONFIG_1MBITS();
        *t = c;
        return 1;
    }
    if (bitrate == 500000u)
    {
        twai_timing_config_t c = TWAI_TIMING_CONFIG_500KBITS();
        *t = c;
        return 1;
    }
    if (bitrate == 250000u)
    {
        twai_timing_config_t c = TWAI_TIMING_CONFIG_250KBITS();
        *t = c;
        return 1;
    }
    if (bitrate == 125000u)
    {
        twai_timing_config_t c = TWAI_TIMING_CONFIG_125KBITS();
        *t = c;
        return 1;
    }
    return 0;
}

int protocore_platform_can_open(int tx_pin, int rx_pin, uint32_t bitrate)
{
    twai_timing_config_t timing;
    if (s_can_open || !can_timing(bitrate, &timing))
    {
        return 0;
    }
    twai_general_config_t gen =
        TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)tx_pin, (gpio_num_t)rx_pin, TWAI_MODE_LISTEN_ONLY);
    twai_filter_config_t filt = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&gen, &timing, &filt) != ESP_OK)
    {
        return 0;
    }
    if (twai_start() != ESP_OK)
    {
        twai_driver_uninstall();
        return 0;
    }
    s_can_open = 1;
    return 1;
}

int protocore_platform_can_recv(protocore_can_frame *out)
{
    twai_message_t m;
    if (!s_can_open || twai_receive(&m, 0) != ESP_OK) // 0 ticks = non-blocking drain
    {
        return 0;
    }
    out->id = m.identifier;
    out->ext = (uint8_t)(m.extd != 0);
    out->rtr = (uint8_t)(m.rtr != 0);
    out->len = m.data_length_code > 8 ? 8 : m.data_length_code;
    for (uint8_t i = 0; i < out->len; i++)
    {
        out->data[i] = m.data[i];
    }
    return 1;
}

void protocore_platform_can_close(void)
{
    if (!s_can_open)
    {
        return;
    }
    twai_stop();
    twai_driver_uninstall();
    s_can_open = 0;
}
#endif // PROTOCORE_HAS_VENDOR_CAN

#endif // PROTOCORE_VENDOR_ESP
