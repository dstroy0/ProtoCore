// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_answers.h
 * @brief Espressif's answers to the platform contract.
 *
 * Included by vendor/vendor_detect.h once the vendor axis has resolved, and by nothing else. Every
 * capability protocore_platform.h asks about is answered here; one it does not answer is refused
 * there by name rather than defaulted.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_VENDOR_ESP_ANSWERS_H
#define PROTOCORE_VENDOR_ESP_ANSWERS_H

#ifndef PROTOCORE_HAS_HW_AESGCM
#define PROTOCORE_HAS_HW_AESGCM 1
#endif

#ifndef PROTOCORE_HAS_HW_BIGNUM
#define PROTOCORE_HAS_HW_BIGNUM 1
#endif

#ifndef PROTOCORE_HAS_HW_SHA
#define PROTOCORE_HAS_HW_SHA 1
#endif

#ifndef PROTOCORE_HAS_HW_AES
#define PROTOCORE_HAS_HW_AES 1
#endif

#ifndef PROTOCORE_HAS_HW_ECC
#define PROTOCORE_HAS_HW_ECC 1
#endif

#ifndef PROTOCORE_HAS_VENDOR_MDNS
#define PROTOCORE_HAS_VENDOR_MDNS 1
#endif

#ifndef PROTOCORE_HAS_VENDOR_TLS
#define PROTOCORE_HAS_VENDOR_TLS 1
#endif

#ifndef PROTOCORE_HAS_VENDOR_DNS_RESOLVER
#define PROTOCORE_HAS_VENDOR_DNS_RESOLVER 1
#endif

#ifndef PROTOCORE_HAS_VENDOR_WIFI
#define PROTOCORE_HAS_VENDOR_WIFI 1
#endif

#ifndef PROTOCORE_HAS_NET_STACK
#define PROTOCORE_HAS_NET_STACK 1
#endif

#ifndef PROTOCORE_HAS_VENDOR_NVS
#define PROTOCORE_HAS_VENDOR_NVS 1
#endif

#ifndef PROTOCORE_HAS_BOUNDED_DRAM
#define PROTOCORE_HAS_BOUNDED_DRAM 1
#endif

#ifndef PROTOCORE_HAS_PSRAM
#define PROTOCORE_HAS_PSRAM 1
#endif

#include "driver/gpio.h"     // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - see above
#include "driver/uart.h"     // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - see above
#include "esp_cpu.h"         // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - see above
#include "esp_idf_version.h" // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - names the IDF the driver headers came from
#include "esp_random.h"      // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - see above
#include "esp_system.h"      // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - see above
#include "esp_timer.h"       // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - see above
#include "freertos/FreeRTOS.h" // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - only exists once the vendor above resolved to ESP
#include "freertos/queue.h"    // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - see above
#include "freertos/semphr.h"      // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - see above
#include "freertos/task.h"        // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - see above
#include "lwip/igmp.h"            // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - see above
#include "lwip/pbuf.h"            // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - see above
#include "lwip/priv/tcpip_priv.h" // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - see above
#include "lwip/tcp.h"             // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - see above
#include "lwip/udp.h"             // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - see above

typedef QueueHandle_t protocore_platform_queue;
typedef StaticQueue_t protocore_platform_queue_ctrl; ///< a caller-owned queue control block
typedef SemaphoreHandle_t protocore_platform_mutex;
typedef StaticSemaphore_t protocore_platform_mutex_ctrl; ///< a caller-owned mutex control block
typedef TaskHandle_t protocore_platform_task;
typedef TaskFunction_t protocore_platform_task_fn;
typedef BaseType_t protocore_platform_status;
typedef TickType_t protocore_platform_ticks;

typedef struct tcp_pcb protocore_pcb;                  ///< a TCP control block
typedef struct pbuf protocore_pbuf;                    ///< a received packet buffer chain
typedef err_t protocore_net_err;                       ///< a network stack result
typedef struct tcpip_api_call_data protocore_net_call; ///< the marshal record for a stack call

#define PROTOCORE_PLATFORM_OK pdTRUE
#define PROTOCORE_PLATFORM_PASS pdPASS
#define PROTOCORE_PLATFORM_FALSE pdFALSE
#define PROTOCORE_PLATFORM_WAIT_FOREVER portMAX_DELAY
#define PROTOCORE_PLATFORM_CORES portNUM_PROCESSORS

#define protocore_platform_queue_create xQueueCreateStatic
#define protocore_platform_queue_send xQueueSendToBack
#define protocore_platform_queue_send_front xQueueSendToFront
#define protocore_platform_queue_send_isr xQueueSendToBackFromISR
#define protocore_platform_queue_recv xQueueReceive
#define protocore_platform_queue_waiting uxQueueMessagesWaiting
#define protocore_platform_queue_waiting_isr uxQueueMessagesWaitingFromISR
#define protocore_platform_queue_delete vQueueDelete

// A mutex over a caller-owned control block, so the object is BSS and no allocator runs
// (SRC_LAW rule 2). take blocks until the holder releases; a caller that cannot block passes its
// own tick budget instead of PROTOCORE_PLATFORM_WAIT_FOREVER.
#define protocore_platform_mutex_create xSemaphoreCreateMutexStatic
#define protocore_platform_mutex_take xSemaphoreTake
#define protocore_platform_mutex_give xSemaphoreGive

#define protocore_platform_task_start xTaskCreatePinnedToCore
#define protocore_platform_task_stop vTaskDelete
#define protocore_platform_task_notify xTaskNotifyGive
#define protocore_platform_task_wait ulTaskNotifyTake
#define protocore_platform_task_delay vTaskDelay
#define protocore_platform_task_yield_from_isr portYIELD_FROM_ISR
#define protocore_platform_task_self xTaskGetCurrentTaskHandle

// Buses. The bridge and the peripheral drivers drive UART / SPI / I2C; these are the IDF C
// drivers that Arduino's HardwareSerial, SPI and Wire objects are built over, so the core reaches
// a bus without naming a framework. A unit the SoC does not have fails closed rather than trapping.
#ifndef PROTOCORE_UART_RX_BUF
#define PROTOCORE_UART_RX_BUF 512 // driver RX ring per unit; a bridge transaction is far smaller
#endif
#define PROTOCORE_UART_UNITS SOC_UART_NUM

// 8N1, no flow control, on @p rx and @p tx; -1 on either leaves that pin at the unit's default.
PROTOCORE_INLINE int protocore_platform_uart_begin(uint8_t unit, uint32_t baud, int rx, int tx)
{
    if (unit >= PROTOCORE_UART_UNITS)
    {
        return 0;
    }
    uart_config_t c = {0};
    c.baud_rate = (int)baud;
    c.data_bits = UART_DATA_8_BITS;
    c.parity = UART_PARITY_DISABLE;
    c.stop_bits = UART_STOP_BITS_1;
    c.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    // IDF 5 names the default source clock UART_SCLK_DEFAULT; IDF 4 names the same one UART_SCLK_APB.
#if ESP_IDF_VERSION_MAJOR >= 5
    c.source_clk = UART_SCLK_DEFAULT;
#else
    c.source_clk = UART_SCLK_APB;
#endif
    if (uart_param_config((uart_port_t)unit, &c) != ESP_OK)
    {
        return 0;
    }
    // The pins are routed through the GPIO matrix before the driver installs its ring.
    if (uart_set_pin((uart_port_t)unit, tx < 0 ? UART_PIN_NO_CHANGE : tx, rx < 0 ? UART_PIN_NO_CHANGE : rx,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
    {
        return 0;
    }
    if (!uart_is_driver_installed((uart_port_t)unit))
    {
        return uart_driver_install((uart_port_t)unit, PROTOCORE_UART_RX_BUF, 0, 0, NULL, 0) == ESP_OK ? 1 : 0;
    }
    return 1;
}
#define protocore_platform_uart_write(unit, buf, len) uart_write_bytes((uart_port_t)(unit), (const char *)(buf), (len))
#define protocore_platform_uart_read(unit, buf, len, ms)                                                               \
    uart_read_bytes((uart_port_t)(unit), (buf), (len), pdMS_TO_TICKS(ms))
PROTOCORE_INLINE uint32_t protocore_platform_uart_available(uint8_t unit)
{
    size_t n = 0;
    return (uart_get_buffered_data_len((uart_port_t)unit, &n) == ESP_OK) ? (uint32_t)n : 0u;
}

#define PROTOCORE_SPI_MSBFIRST 0
#define PROTOCORE_SPI_LSBFIRST 1

// Bits clocked per SCLK in the data phase.
#define PROTOCORE_SPI_LANES_1 1
#define PROTOCORE_SPI_LANES_2 2
#define PROTOCORE_SPI_LANES_4 4

// SPI. The pins come from the caller because they are a board fact, not a library one - a bridge
// target names its own bus. Bring a host up once with the pins, then run transactions on it.
// @p host selects the controller: 0 is the general-purpose one, 1 the second where the die has it.
// @p quadwp and @p quadhd are the third and fourth data lines, -1 when the bus is single or dual.
int protocore_platform_spi_begin(uint8_t host, int mosi, int miso, int sclk, int quadwp, int quadhd);
int protocore_platform_spi_txn(uint8_t host, uint32_t hz, uint8_t bit_order, uint8_t mode, const uint8_t *tx,
                               uint8_t *rx, uint32_t len);

// A framed transfer: a @p cmd_bits command, an @p addr_bits address, @p dummy_bits idle clocks,
// then the data phase at @p lanes bits per clock. The controller drives each phase, so the data
// buffer holds data alone. A zero bit count omits that phase; @p tx or @p rx may be NULL for a
// one-way data phase, and @p len 0 sends the phases with no data phase at all.
int protocore_platform_spi_txn_ext(uint8_t host, uint32_t hz, uint8_t bit_order, uint8_t mode, uint16_t cmd,
                                   uint8_t cmd_bits, uint32_t addr, uint8_t addr_bits, uint8_t dummy_bits,
                                   uint8_t lanes, const uint8_t *tx, uint8_t *rx, uint32_t len);

// I2C, on the same terms: install the port once on the caller's pins, then address a device on it.
// @p bus selects the controller. Each call carries its own timeout in milliseconds, so a wedged
// device stops one transfer rather than the loop (SRC_LAW rule 5). write_read is one transaction
// with a repeated start, which is what a register read is: name the register, then turn the bus
// around without releasing it.
//
// An address is 7-bit unless it carries PROTOCORE_I2C_ADDR_10BIT, which selects the two-byte form: a
// 11110xx byte holding the two high bits, then the low eight. The flag rides in bit 15, so both
// address widths are the same argument type.
#define PROTOCORE_I2C_ADDR_10BIT 0x8000u
#define PROTOCORE_I2C_ADDR_MASK 0x03FFu
#define PROTOCORE_I2C_GENERAL_CALL 0x00u ///< the address every device on the bus answers

int protocore_platform_i2c_begin(uint8_t bus, int sda, int scl, uint32_t hz);
int protocore_platform_i2c_write(uint8_t bus, uint16_t addr, const uint8_t *buf, uint32_t len, uint32_t ms);
int protocore_platform_i2c_read(uint8_t bus, uint16_t addr, uint8_t *buf, uint32_t len, uint32_t ms);
int protocore_platform_i2c_write_read(uint8_t bus, uint16_t addr, const uint8_t *w, uint32_t wlen, uint8_t *r,
                                      uint32_t rlen, uint32_t ms);

// Set @p bus to @p hz. Standard mode is 100 kHz, fast 400 kHz, fast-plus 1 MHz.
int protocore_platform_i2c_set_clock(uint8_t bus, uint32_t hz);

// Address @p addr and stop, reporting whether anything drove ACK. This is the address-only cycle
// the transfer calls refuse, and what a bus scan is built from.
int protocore_platform_i2c_probe(uint8_t bus, uint16_t addr, uint32_t ms);

// Clock SCL until the device holding SDA low releases it, then drive a stop. The port is
// uninstalled for the duration and reinstalled on the same pins afterwards.
int protocore_platform_i2c_recover(uint8_t bus, int sda, int scl);

// Entropy. The ESP32 RNG is a true hardware source: it samples thermal / RF analog noise rather
// than running a deterministic generator, so this is the one the key material is drawn from.
// esp_random() is the IDF entry point Arduino random() is built over.
#define protocore_platform_rand_u32() ((uint32_t)esp_random())
#define protocore_platform_rand_fill(buf, len) esp_fill_random((buf), (len))

// GPIO. The IDF driver is C and is what Arduino digitalWrite()/pinMode() sit on, so the core
// reaches the pins through these rather than through a framework it is not allowed to name.
#define PROTOCORE_GPIO_IN 0
#define PROTOCORE_GPIO_OUT 1
#define PROTOCORE_GPIO_IN_PULLUP 2
#define PROTOCORE_GPIO_IN_PULLDOWN 3
#define PROTOCORE_GPIO_LOW 0
#define PROTOCORE_GPIO_HIGH 1

PROTOCORE_INLINE void protocore_platform_gpio_mode(uint8_t pin, uint8_t mode)
{
    gpio_num_t g = (gpio_num_t)pin;
    gpio_set_direction(g, (mode == PROTOCORE_GPIO_OUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
    gpio_set_pull_mode(g, (mode == PROTOCORE_GPIO_IN_PULLUP)     ? GPIO_PULLUP_ONLY
                          : (mode == PROTOCORE_GPIO_IN_PULLDOWN) ? GPIO_PULLDOWN_ONLY
                                                                 : GPIO_FLOATING);
}
#define protocore_platform_gpio_write(pin, level) gpio_set_level((gpio_num_t)(pin), (level) ? 1 : 0)
#define protocore_platform_gpio_read(pin) ((uint8_t)gpio_get_level((gpio_num_t)(pin)))

// Time base. esp_timer/esp_cpu are the IDF primitives underneath Arduino millis()/micros(), so
// these are the same counters by a name the core can call from C.
// Reboot. esp_restart() is the IDF entry point Arduino's ESP.restart() calls, so the core asks for
// a restart without naming a framework object. It does not return.
#define protocore_platform_restart() esp_restart()

#define protocore_platform_micros() ((uint32_t)esp_timer_get_time())
#define protocore_platform_millis() ((uint32_t)(esp_timer_get_time() / 1000))
#define protocore_platform_cycles() ((uint32_t)esp_cpu_get_cycle_count())

#define PROTOCORE_NET_OK ERR_OK
#define PROTOCORE_NET_ERR_MEM ERR_MEM
#define PROTOCORE_NET_ERR_BUF ERR_BUF
#define PROTOCORE_NET_ERR_VAL ERR_VAL
#define PROTOCORE_NET_ERR_ARG ERR_ARG
#define PROTOCORE_NET_ERR_USE ERR_USE
#define PROTOCORE_NET_ERR_CONN ERR_CONN
#define PROTOCORE_NET_ERR_CLSD ERR_CLSD
#define PROTOCORE_NET_ERR_RST ERR_RST
#define PROTOCORE_NET_ERR_ABRT ERR_ABRT
// The resolver reports this while the query is on the wire: not a failure, and not an answer.
#define PROTOCORE_NET_ERR_INPROGRESS ERR_INPROGRESS
#define PROTOCORE_NET_ADDR_ANY IP_ANY_TYPE
#define PROTOCORE_NET_TYPE_ANY IPADDR_TYPE_ANY
#define PROTOCORE_NET_TYPE_V4 IPADDR_TYPE_V4
#define PROTOCORE_NET_WRITE_COPY TCP_WRITE_FLAG_COPY

#define protocore_net_new tcp_new_ip_type
#define protocore_net_bind tcp_bind
#define protocore_net_listen tcp_listen_with_backlog
#define protocore_net_connect tcp_connect
#define protocore_net_close tcp_close
#define protocore_net_abort tcp_abort
#define protocore_net_arg tcp_arg
#define protocore_net_on_accept tcp_accept
#define protocore_net_on_recv tcp_recv
#define protocore_net_on_sent tcp_sent
#define protocore_net_on_err tcp_err
#define protocore_net_write tcp_write
#define protocore_net_output tcp_output
#define protocore_net_recved tcp_recved
#define protocore_net_sndbuf tcp_sndbuf
#define protocore_net_nagle_disable tcp_nagle_disable
#define protocore_net_pbuf_free pbuf_free
#define protocore_net_pbuf_copy pbuf_copy_partial
#define protocore_net_pbuf_alloc pbuf_alloc
#define protocore_net_call_marshal tcpip_api_call

#define PROTOCORE_NET_PBUF_TRANSPORT PBUF_TRANSPORT
#define PROTOCORE_NET_PBUF_RAM PBUF_RAM
#define PROTOCORE_NET_ADDR_ANY4 IP4_ADDR_ANY
#define PROTOCORE_NET_ADDR_ANY4_P IP4_ADDR_ANY4
#define PROTOCORE_NET_OPT_REUSEADDR SOF_REUSEADDR

typedef struct udp_pcb protocore_udp_pcb;
typedef ip_addr_t protocore_net_ip;

#define protocore_net_udp_new udp_new
#define protocore_net_udp_bind udp_bind
#define protocore_net_udp_recv udp_recv
#define protocore_net_udp_sendto udp_sendto
#define protocore_net_udp_remove udp_remove
#define protocore_net_opt_set ip_set_option
#define protocore_net_ip_parse ipaddr_aton
#define protocore_net_ip_print ipaddr_ntoa_r
#define protocore_net_ip_is_v4 IP_IS_V4
#define protocore_net_ip_is_v6 IP_IS_V6
#define protocore_net_ip_as_v4 ip_2_ip4
#define protocore_net_ip_as_v6 ip_2_ip6
// The v6 address is four network-order words, so its sixteen bytes are the address as it travels.
// Reached as bytes rather than words: a word read gives the host's byte order, not the wire's.
#define protocore_net_ip6_bytes(a) ((const uint8_t *)ip_2_ip6(a)->addr)
#define protocore_net_ip6_wbytes(a) ((uint8_t *)ip_2_ip6(a)->addr)
#define protocore_net_ip6_mark(a) IP_SET_TYPE_VAL(*(a), IPADDR_TYPE_V6)
#define protocore_net_ip4_u32 ip4_addr_get_u32
#define protocore_net_ip4_set IP_ADDR4
#define protocore_net_rcv_wnd_update tcp_update_rcv_ann_wnd
#define protocore_net_ip4_is_multicast ip4_addr_ismulticast
#define PROTOCORE_NET_HAS_IGMP LWIP_IGMP
#define PROTOCORE_NET_HAS_IPV6 LWIP_IPV6
// RFC 1034 sec 5.3.1: the resolver is asked for a name and answers now, or later through the
// callback. PROTOCORE_NET_OK means the stack already held it and @p addr is filled; _INPROGRESS
// means the query left and @p found fires when it lands.
#include "lwip/dns.h" // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - see above
#define protocore_net_dns_resolve(host, addr, found, arg) dns_gethostbyname((host), (addr), (found), (arg))

#define protocore_net_igmp_join igmp_joingroup
#define protocore_net_igmp_leave igmp_leavegroup

#ifndef PROTOCORE_HAS_BUS
#define PROTOCORE_HAS_BUS 1
#endif

#ifndef PROTOCORE_HAS_GPIO
#define PROTOCORE_HAS_GPIO 1
#endif

#ifndef PROTOCORE_HAS_SCHEDULER
#define PROTOCORE_HAS_SCHEDULER 1
#endif

#ifndef PROTOCORE_HAS_VENDOR_MAC
#define PROTOCORE_HAS_VENDOR_MAC 1
#endif

#ifndef PROTOCORE_HAS_VENDOR_HEAP_INFO
#define PROTOCORE_HAS_VENDOR_HEAP_INFO 1
#endif

#ifndef PROTOCORE_HAS_VENDOR_PM
#define PROTOCORE_HAS_VENDOR_PM 1
#endif

#ifndef PROTOCORE_HAS_VENDOR_BT
#define PROTOCORE_HAS_VENDOR_BT 1
#endif

#ifndef PROTOCORE_HAS_VENDOR_OTA
#define PROTOCORE_HAS_VENDOR_OTA 1
#endif

#ifndef PROTOCORE_HAS_VENDOR_COREDUMP
#define PROTOCORE_HAS_VENDOR_COREDUMP 1
#endif

#ifndef PROTOCORE_HAS_VENDOR_CAN
#define PROTOCORE_HAS_VENDOR_CAN 1
#endif

// What a BIO returns to the record engine when no octet moved and the call is to be retried. Taken
// from the engine that owns them rather than restated: a BIO that invents its own would be read as a
// fatal error and drop the session.
#if PROTOCORE_HAS_VENDOR_TLS
#include <mbedtls/ssl.h> // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - only exists once the vendor arm resolved
#define PROTOCORE_PLATFORM_TLS_WANT_READ MBEDTLS_ERR_SSL_WANT_READ
#define PROTOCORE_PLATFORM_TLS_WANT_WRITE MBEDTLS_ERR_SSL_WANT_WRITE
#endif

#endif // PROTOCORE_VENDOR_ESP_ANSWERS_H
