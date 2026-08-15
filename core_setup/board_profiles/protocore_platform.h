// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_platform.h
 * @brief The one vendor/die selector for the whole library.
 *
 * Multi-vendor portability rests on a single rule: every silicon-specific layer (board profiles,
 * the crypto accelerator HAL, the physical MAC + PHY) is partitioned into a per-vendor subdir and a
 * common API header pulls in exactly ONE backend per build. This header owns the "which vendor" decision
 * so nothing downstream has to re-test toolchain-specific macros - a backend keys off `PROTOCORE_VENDOR_*`, not
 * off `CONFIG_IDF_TARGET_*` / `STM32*` / `PICO_*` scattered across the tree.
 *
 * Exactly one of these is 1; every other is defined 0 (so `#if PROTOCORE_VENDOR_ESP` is always valid, never
 * relies on an undefined-macro-is-0 fallback). The vendor is derived from the toolchain's own target macro:
 *
 *   - `PROTOCORE_VENDOR_ESP` - any Espressif target (ESP-IDF `ESP_PLATFORM` / Arduino-ESP32 `ARDUINO_ARCH_ESP32`).
 *   - `PROTOCORE_VENDOR_STM` - STM32 (Arduino_Core_STM32 `ARDUINO_ARCH_STM32` / STM32Cube `USE_HAL_DRIVER`).
 *   - `PROTOCORE_VENDOR_RP`  - Raspberry Pi silicon (RP2040 / RP2350: `ARDUINO_ARCH_RP2040` / `PICO_*`).
 *   - `PROTOCORE_VENDOR_TI`  - Texas Instruments (`__TI_COMPILER_VERSION__` or an explicit force).
 *   - `PROTOCORE_HOST` - the build runs on the machine that compiled it: no silicon, no scheduler,
 *     portable software everywhere. The test envs state it; otherwise it is what no detected vendor means.
 *
 * A file with a device path and a host path selects on `PROTOCORE_HOST`, never on a vendor's own macro:
 * `ARDUINO` names one vendor's toolchain, so keying on it drops every other vendor down the host path.
 *
 * ESP is detected first and stays byte-for-byte compatible with the pre-selector behavior: on every ESP
 * build `PROTOCORE_VENDOR_ESP` is 1, and on host builds it is 0, exactly matching the old
 * `#if defined(CONFIG_IDF_TARGET_*)` test in board_profile.h.
 */

#ifndef PROTOCORE_PLATFORM_H
#define PROTOCORE_PLATFORM_H

#include <stdint.h>

/**
 * @brief Linkage for a leaf primitive whose body is cheaper than the call that reaches it.
 *
 * Stated here rather than in protocore_config.h because that header reaches this one through
 * board_profile.h before it defines anything of its own, so this is the earliest point the
 * linkage can be settled. protocore_config.h keeps the same definition behind #ifndef, which
 * covers a translation unit that arrives without this header.
 */
#ifndef PROTOCORE_INLINE
#if defined(__GNUC__)
#define PROTOCORE_INLINE static inline __attribute__((always_inline))
#else
#define PROTOCORE_INLINE static inline
#endif
#endif

// sdkconfig.h carries CONFIG_IDF_TARGET_* on ESP-IDF / Arduino-ESP32 builds and is absent on host and on
// other vendors' toolchains. Pull it in here (guarded by __has_include) so vendor + die detection is
// include-order-independent, the same reason board_profile.h does it. Preceded only by the include guard.
#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

// ---------------------------------------------------------------------------
// Vendor axis - exactly one is 1.
// ---------------------------------------------------------------------------
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#define PROTOCORE_VENDOR_ESP 1
#elif defined(ARDUINO_ARCH_STM32) || defined(USE_HAL_DRIVER) || defined(STM32_CORE_VERSION)
#define PROTOCORE_VENDOR_STM 1
#elif defined(ARDUINO_ARCH_RP2040) || defined(PICO_RP2040) || defined(PICO_RP2350) || defined(PICO_SDK_VERSION_MAJOR)
#define PROTOCORE_VENDOR_RP 1
#elif defined(__TI_COMPILER_VERSION__) || defined(PROTOCORE_VENDOR_TI_FORCE)
#define PROTOCORE_VENDOR_TI 1
#else
#define PROTOCORE_HOST 1
#endif

// Every one is defined (0 when not selected) so downstream code can `#if` it freely.
#ifndef PROTOCORE_VENDOR_ESP
#define PROTOCORE_VENDOR_ESP 0
#endif
#ifndef PROTOCORE_VENDOR_STM
#define PROTOCORE_VENDOR_STM 0
#endif
#ifndef PROTOCORE_VENDOR_RP
#define PROTOCORE_VENDOR_RP 0
#endif
#ifndef PROTOCORE_VENDOR_TI
#define PROTOCORE_VENDOR_TI 0
#endif
#ifndef PROTOCORE_HOST
#define PROTOCORE_HOST 0
#endif

// ---------------------------------------------------------------------------
// Capabilities - what this part has.
// ---------------------------------------------------------------------------
//
// There are two paths, and a capability is what selects between them: the hardware path where the
// part has the thing, the software path where it does not. Nothing keys on which build this is.
//
// Each is #ifndef, so a build states what it has by defining it. A detected vendor answers for its
// silicon below; a build with no vendor answers 0 and turns one on with -DPROTOCORE_HAS_<X>=1, which is
// how a suite drives a hardware path on a machine that has no hardware.
//
// core_setup/ tests these to decide which backend TU compiles, and src/ tests them where a software
// path and a hardware path both exist. There is no weak symbol behind any of them - linking no
// backend is an undefined reference, linking two is a duplicate definition, and both fail the build
// rather than silently selecting one.

// AES-GCM. 1 = the vendor supplies an accelerated AEAD (core_setup/hal/<vendor>); 0 = the portable
// software backend, which is software AES plus a table GHASH.
//
// Not a small difference and not a preference: measured sealing 1 KiB on an ESP32-S3 at 240 MHz, the
// vendor AEAD is 81,085 cycles and the software path 616,567 - 7.6x. Hand it to the vendor whenever
// there is one; choosing software is legitimate where there is not, but it has to be chosen.
#ifndef PROTOCORE_HAS_HW_AESGCM
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_HW_AESGCM 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_HW_AESGCM 0 // a unit-test build has no silicon by definition
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_HW_AESGCM (1 = accelerated AEAD in core_setup/hal/<vendor>, 0 = portable software AES + table GHASH, ~7.6x slower where measured). Choosing software is fine; defaulting into it is not."
#endif
#endif

// DH-2048 / RSA modexp. 1 = the vendor supplies an accelerated backend; 0 = the portable software
// Montgomery backend (core_setup/hal/portable), which is data-dependent and NOT constant time -
// see SECURITY.md, timing.
#ifndef PROTOCORE_HAS_HW_BIGNUM
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_HW_BIGNUM 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_HW_BIGNUM 0 // a unit-test build has no silicon by definition
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_HW_BIGNUM (1 = accelerated backend in core_setup/hal/<vendor>, 0 = portable software Montgomery, which is not constant time). Choosing software crypto is fine; defaulting into it is not."
#endif
#endif

// SHA-1 / SHA-256 / SHA-512. 1 = the vendor supplies a hashing peripheral and the mbedtls backend
// over it; 0 = the portable software compression functions.
//
// One capability for the family: a part that ships a SHA block ships it for the digests it supports,
// and a build that has to fall back for one of them falls back for all of them rather than mixing a
// peripheral digest with a software one inside the same handshake transcript.
#ifndef PROTOCORE_HAS_HW_SHA
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_HW_SHA 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_HW_SHA 0 // a unit-test build has no silicon by definition
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_HW_SHA (1 = a hashing peripheral in core_setup/hal/<vendor>, 0 = the portable software compression functions). Choosing software is fine; defaulting into it is not."
#endif
#endif

// AES block, and the CTR / CMAC / CCM modes over it. 1 = the vendor supplies an AES peripheral;
// 0 = the portable software AES.
//
// Separate from PROTOCORE_HAS_HW_AESGCM: GHASH is its own multiplier and a part can ship one without the
// other, so a build states each. The block cipher is what this one answers for.
#ifndef PROTOCORE_HAS_HW_AES
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_HW_AES 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_HW_AES 0 // a unit-test build has no silicon by definition
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_HW_AES (1 = an AES peripheral in core_setup/hal/<vendor>, 0 = the portable software AES). Choosing software is fine; defaulting into it is not."
#endif
#endif

// X25519 and ECDSA over P-256. 1 = the vendor supplies an accelerated curve backend; 0 = the
// portable software field arithmetic.
//
// Not the same axis as PROTOCORE_HAS_HW_BIGNUM: that one answers for modexp over a 2048-bit modulus, this
// one for curve point math, and a part can accelerate either alone.
#ifndef PROTOCORE_HAS_HW_ECC
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_HW_ECC 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_HW_ECC 0 // a unit-test build has no silicon by definition
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_HW_ECC (1 = an accelerated curve backend in core_setup/hal/<vendor>, 0 = the portable software field arithmetic, which is not constant time on every curve). Choosing software is fine; defaulting into it is not."
#endif
#endif

// mDNS / DNS-SD. 1 = the vendor ships its own responder component and the wrapper drives that;
// 0 = the portable responder in network_drivers/application/mdns_service, which answers over the
// UDP listener like every other datagram service.
//
// The vendor's does more than advertise: probing, conflict resolution, IPv6 records. Take it where
// it exists. The portable one is what makes the feature exist at all on a part that has none.
#ifndef PROTOCORE_HAS_VENDOR_MDNS
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_VENDOR_MDNS 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_VENDOR_MDNS 0 // a unit-test build has no vendor component to call
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_MDNS (1 = the SDK's own responder component, 0 = the portable responder over the UDP listener). Choosing the portable one is fine; defaulting into it is not."
#endif
#endif

// TLS. 1 = the vendor ships a TLS stack and network_drivers/tls drives that (mbedTLS over lwIP on
// ESP); 0 = the portable TLS 1.3 in network_drivers/tls, the same hand-rolled stack the QUIC and
// DTLS handshakes already run: TLS_AES_128_GCM_SHA256, X25519, an Ed25519 raw public key.
//
// The two differ in what they will talk to, not just in speed. A vendor stack brings X.509: chain
// validation, name matching, RSA and ECDSA certificates, so a browser will connect. The portable
// one authenticates by raw public key and is what makes TLS exist at all on a part with no vendor
// stack. Take the vendor's wherever there is one.
#ifndef PROTOCORE_HAS_VENDOR_TLS
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_VENDOR_TLS 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_VENDOR_TLS 0 // a unit-test build has no SDK stack to drive
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_TLS (1 = the SDK's own TLS stack with X.509, 0 = the portable TLS 1.3 over the TCP record layer, raw public key only). Choosing the portable one is fine; defaulting into it is not."
#endif
#endif


// SNTP has no vendor seam: network_drivers/application/ntp_service is the client on every target. It
// asks a server over the UDP listener, keeps the epoch in its own state, and hands it out through
// protocore_ntp_epoch(); nothing in libc moves.

// DNS resolution. 1 = the stack resolves names itself and the module marshals into it; 0 = the
// portable resolver in network_drivers/network/dns, which asks over the UDP listener.
//
// A stack resolver already knows the nameservers DHCP handed it and caches what it learns. The
// portable one asks PROTOCORE_DNS_SERVER, once per call, and keeps nothing.
#ifndef PROTOCORE_HAS_VENDOR_DNS_RESOLVER
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_VENDOR_DNS_RESOLVER 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_VENDOR_DNS_RESOLVER 0 // a unit-test build has no stack resolver to marshal into
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_DNS_RESOLVER (1 = the stack's own resolver, 0 = the portable resolver over the UDP listener). Choosing the portable one is fine; defaulting into it is not."
#endif
#endif

// WiFi driver. 1 = the SDK exposes the radio below the IP stack, which is what monitor mode and a
// vendor peer-to-peer protocol need; 0 = there is none, and both refuse. Not the same axis as
// having a network interface: a stack can carry IP over ethernet with no radio underneath it.
#ifndef PROTOCORE_HAS_VENDOR_WIFI
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_VENDOR_WIFI 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_VENDOR_WIFI 0 // a unit-test build has no radio to put in monitor mode
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_WIFI (1 = an SDK WiFi driver reachable below the IP stack, 0 = none, and monitor mode and the peer-to-peer radio refuse). Choosing none is fine; defaulting into it is not."
#endif
#endif

// A stack with pcbs to bind. 1 = there is one to open a socket on; 0 = there is none, and every
// listener and outbound client refuses. The transport owners (tcp.h, udp.h) are portable either
// way - this answers whether anything is underneath them.
// Declared, not sniffed. Unlike the bus and pin seams, whose owners resolve to a refusing arm on
// their own, turning this on compiles whole transport translation units into every consumer - so an
// env states it and carries those sources, rather than inheriting it from a header being reachable.
#ifndef PROTOCORE_HAS_NET_STACK
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_NET_STACK 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_NET_STACK 0 // until an env declares it and builds the transport
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_NET_STACK (1 = a stack with pcbs to bind, 0 = none, and every listener and client refuses). Choosing none is fine; defaulting into it is not."
#endif
#endif

// Non-volatile key-value storage. 1 = the SDK keeps a key-value store across a reboot, which is
// what provisioned credentials are written to; 0 = there is none and provisioning has nowhere to
// put them.
#ifndef PROTOCORE_HAS_VENDOR_NVS
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_VENDOR_NVS 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_VENDOR_NVS 0 // nothing here outlives the process
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_NVS (1 = an SDK key-value store that survives a reboot, 0 = none, and provisioning does not compile). Choosing none is fine; defaulting into it is not."
#endif
#endif

// An internal RAM segment a pool can overflow. 1 = link-time placement is bounded and a pool that
// does not fit has to be moved or acknowledged; 0 = one address space, so the budget guards have
// nothing to protect.
#ifndef PROTOCORE_HAS_BOUNDED_DRAM
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_BOUNDED_DRAM 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_BOUNDED_DRAM 0 // the host has as much as it asks for
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_BOUNDED_DRAM (1 = a bounded internal RAM segment the budget guards check, 0 = one address space). Choosing one address space is fine; defaulting into it is not."
#endif
#endif

// External RAM a pool can be placed in. 1 = the toolchain has an attribute that moves a BSS object
// out of internal DRAM; 0 = there is one memory and a pool stays where it is declared. It answers
// only for the attribute existing: whether a given board is wired for it, and whether a given pool
// should use it, are the PROTOCORE_*_IN_PSRAM flags.
#ifndef PROTOCORE_HAS_PSRAM
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_PSRAM 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_PSRAM 0 // a unit-test build has one flat address space
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_PSRAM (1 = an external-RAM placement attribute in its toolchain, 0 = one memory, and every pool stays in it). Choosing one memory is fine; defaulting into it is not."
#endif
#endif

// ---------------------------------------------------------------------------
// Execution context identity
// ---------------------------------------------------------------------------
//
// "Which execution context is running me" is a platform question, not a core one, so the core asks
// here instead of naming an RTOS. Used by the pools' debug owner tripwire to catch a borrow crossing
// tasks; it is only ever compared for equality, never interpreted.
//
// Returns 0 where there is no such concept (host builds): a single context, so every comparison
// trivially agrees and the tripwire is a no-op rather than a false alarm.
uintptr_t protocore_platform_context_id(void);





// The stored image's state, in the order a rollback walks it. These values are the library's own;
// each arm maps its vendor's onto them, so a caller reads one set whatever it is running on.
#define PROTOCORE_PLATFORM_IMG_NEW 0
#define PROTOCORE_PLATFORM_IMG_PENDING_VERIFY 1
#define PROTOCORE_PLATFORM_IMG_VALID 2
#define PROTOCORE_PLATFORM_IMG_INVALID 3
#define PROTOCORE_PLATFORM_IMG_ABORTED 4
#define PROTOCORE_PLATFORM_IMG_UNDEFINED 0xFF




// ---------------------------------------------------------------------------
// The target's scheduler and network stack, under our names
// ---------------------------------------------------------------------------
//
// The core needs queues, tasks and TCP, and every target already has them, so these are aliases,
// not a layer: our name expands to the target's call, one for one, with no wrapper function, no
// translation and no state of our own. The whole cost is the name.
//
// A target that supplies none of it defines these to nothing, the same way board_profile.h answers
// the rest of the platform questions.

#if PROTOCORE_VENDOR_ESP

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

#elif PROTOCORE_HOST

// No vendor stack, so the same surface comes from a host driver the test environment puts on the
// include path (core_setup/hal/host/protocore_net_host.h), exactly the way it supplies <Arduino.h>. Guarded on
// presence, so a build without that path simply has no transport.
#if defined(__has_include)
#if __has_include("protocore_net_host.h")
#include "protocore_net_host.h" // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - the host driver for the block above
#endif
#endif

#endif // PROTOCORE_VENDOR_ESP

// Whether a bus seam exists to call. Silicon has one; a host build has one only when the test
// mock above supplied it, and the bus owners key their host arm off this so a driver's real
// composition runs against the capture rather than being stubbed out at the owner.
#ifndef PROTOCORE_PLATFORM_HAS_BUS
#define PROTOCORE_PLATFORM_HAS_BUS 0
#endif

// I2C / SPI / UART master. 1 = there is a seam to drive; 0 = there is none and every bus owner
// resolves to its refusing arm. It sits here rather than with the vendor capabilities above because
// it reads the seam macro the block above establishes.
#ifndef PROTOCORE_HAS_BUS
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_BUS 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_BUS PROTOCORE_PLATFORM_HAS_BUS
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_BUS (1 = an I2C / SPI / UART master in core_setup/hal/<vendor>, 0 = none, and every bus owner refuses). Choosing none is fine; defaulting into it is not."
#endif
#endif

// Whether a pin seam exists to call. Silicon has one; a host build has one only when the test mock
// above supplied it, and the pin drivers key their host arm off this so a driver's real sampling
// runs against the pin table rather than being stubbed out.
#ifndef PROTOCORE_PLATFORM_HAS_GPIO
#define PROTOCORE_PLATFORM_HAS_GPIO 0
#endif

// Digital pins. 1 = there is a seam to drive; 0 = there is none and every pin driver resolves to its
// refusing arm. Its own capability rather than a term of PROTOCORE_HAS_BUS: a part can carry pins without
// carrying an I2C / SPI / UART master.
#ifndef PROTOCORE_HAS_GPIO
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_GPIO 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_GPIO PROTOCORE_PLATFORM_HAS_GPIO
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_GPIO (1 = protocore_platform_gpio_mode / _read / _write in core_setup/hal/<vendor>, 0 = none, and every pin driver refuses). Choosing none is fine; defaulting into it is not."
#endif
#endif

// Whether a scheduler seam exists to run on. Silicon has one; a host build has one only when the
// driver above supplied it, and the worker layer keys its host arm off this so the pipeline's real
// task / queue path runs against the mock rather than resolving to the inline arm.
#ifndef PROTOCORE_PLATFORM_HAS_SCHEDULER
#define PROTOCORE_PLATFORM_HAS_SCHEDULER 0
#endif

// Tasks to run on. 1 = there is a scheduler, so the pipeline runs on its own worker and a delay
// sleeps; 0 = there is one context, the pipeline runs inline from the caller's loop, and a delay
// spins on the clock. It sits here rather than with the vendor capabilities above because it reads
// the seam macro the driver block establishes.
#ifndef PROTOCORE_HAS_SCHEDULER
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_SCHEDULER 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_SCHEDULER PROTOCORE_PLATFORM_HAS_SCHEDULER
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_SCHEDULER (1 = tasks the pipeline can run on, 0 = one context and an inline pipeline). Choosing one context is fine; defaulting into it is not."
#endif
#endif

// Whether a seam exists to call. Silicon has one; a host build has one only when the
// driver above supplied a burned-in address, so the owners run against it instead of
// resolving to their refusing arm.
#ifndef PROTOCORE_PLATFORM_HAS_VENDOR_MAC
#define PROTOCORE_PLATFORM_HAS_VENDOR_MAC 0
#endif

// A factory MAC to read. 1 = the SDK hands back a burned-in address the device identity is derived
// from; 0 = there is none, and the identity comes from wherever the application puts it.
#ifndef PROTOCORE_HAS_VENDOR_MAC
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_VENDOR_MAC 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_VENDOR_MAC PROTOCORE_PLATFORM_HAS_VENDOR_MAC
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_MAC (1 = a burned-in address from the SDK, 0 = none). Choosing none is fine; defaulting into it is not."
#endif
#endif

// Whether a seam exists to call. Silicon has one; a host build has one only when the
// driver above supplied allocator figures, so the owners run against it instead of
// resolving to their refusing arm.
#ifndef PROTOCORE_PLATFORM_HAS_VENDOR_HEAP_INFO
#define PROTOCORE_PLATFORM_HAS_VENDOR_HEAP_INFO 0
#endif

// Heap and reset introspection. 1 = the SDK reports free / minimum-free heap and why the part last
// reset, which the health readouts and the guardrails report; 0 = there is none and they report 0.
#ifndef PROTOCORE_HAS_VENDOR_HEAP_INFO
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_VENDOR_HEAP_INFO 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_VENDOR_HEAP_INFO PROTOCORE_PLATFORM_HAS_VENDOR_HEAP_INFO
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_HEAP_INFO (1 = SDK heap and reset-reason readouts, 0 = none, and the health panel reports 0). Choosing none is fine; defaulting into it is not."
#endif
#endif

// Whether a seam exists to call. Silicon has one; a host build has one only when the
// driver above supplied the reset cause, die temperature and CPU clock, so the owners run against it instead of
// resolving to their refusing arm.
#ifndef PROTOCORE_PLATFORM_HAS_VENDOR_PM
#define PROTOCORE_PLATFORM_HAS_VENDOR_PM 0
#endif

// Power management. 1 = the SDK reports why the part reset, sets the CPU clock, reads the die
// temperature and gates a radio's power domain; 0 = there is none of that to bind to.
#ifndef PROTOCORE_HAS_VENDOR_PM
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_VENDOR_PM 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_VENDOR_PM PROTOCORE_PLATFORM_HAS_VENDOR_PM
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_PM (1 = SDK reset-reason / CPU clock / die temperature / power-domain gating, 0 = none, and the power plan is advice with nothing to apply it to). Choosing none is fine; defaulting into it is not."
#endif
#endif

// Whether a seam exists to call. Silicon has one; a host build has one only when the
// driver above supplied a radio power domain to hand back, so the owners run against it instead of
// resolving to their refusing arm.
#ifndef PROTOCORE_PLATFORM_HAS_VENDOR_BT
#define PROTOCORE_PLATFORM_HAS_VENDOR_BT 0
#endif

// A Bluetooth controller whose memory can be released. 1 = the SDK ships one; 0 = there is none and
// there is nothing to release. Its own axis from CONFIG_BT_ENABLED, which says whether a given
// build compiled it in.
#ifndef PROTOCORE_HAS_VENDOR_BT
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_VENDOR_BT 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_VENDOR_BT PROTOCORE_PLATFORM_HAS_VENDOR_BT
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_BT (1 = an SDK Bluetooth controller, 0 = none). Choosing none is fine; defaulting into it is not."
#endif
#endif

// Whether a seam exists to call. Silicon has one; a host build has one only when the
// driver above supplied a stored image to mark, commit or roll back, so the owners run against it instead of
// resolving to their refusing arm.
#ifndef PROTOCORE_PLATFORM_HAS_VENDOR_OTA
#define PROTOCORE_PLATFORM_HAS_VENDOR_OTA 0
#endif

// Self-update. 1 = the SDK ships an updater that writes the other app partition and flips the boot
// selector; 0 = there is none, and the OTA service and the rollback policy have nothing to drive.
#ifndef PROTOCORE_HAS_VENDOR_OTA
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_VENDOR_OTA 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_VENDOR_OTA PROTOCORE_PLATFORM_HAS_VENDOR_OTA
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_OTA (1 = the SDK's own updater + boot selector, 0 = none, and the OTA service does not compile). Choosing none is fine; defaulting into it is not."
#endif
#endif

// Whether a seam exists to call. Silicon has one; a host build has one only when the
// driver above supplied a stored crash image to read, so the owners run against it instead of
// resolving to their refusing arm.
#ifndef PROTOCORE_PLATFORM_HAS_VENDOR_COREDUMP
#define PROTOCORE_PLATFORM_HAS_VENDOR_COREDUMP 0
#endif

// Crash-image capture. 1 = the SDK writes a core dump to its own flash partition; 0 = there is none.
// The decoder that reads one is portable and is not gated on this.
#ifndef PROTOCORE_HAS_VENDOR_COREDUMP
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_VENDOR_COREDUMP 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_VENDOR_COREDUMP PROTOCORE_PLATFORM_HAS_VENDOR_COREDUMP
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_COREDUMP (1 = the SDK's own crash-image capture, 0 = none, and only the portable decoder compiles). Choosing none is fine; defaulting into it is not."
#endif
#endif

// Whether a seam exists to call. Silicon has one; a host build has one only when the
// driver above supplied a CAN controller to open, so the owners run against it instead of
// resolving to their refusing arm.
#ifndef PROTOCORE_PLATFORM_HAS_VENDOR_CAN
#define PROTOCORE_PLATFORM_HAS_VENDOR_CAN 0
#endif

// CAN controller. 1 = the SDK ships a CAN / TWAI driver; 0 = there is none and the bus capture
// refuses. The SocketCAN framing over it is portable and is not gated on this.
#ifndef PROTOCORE_HAS_VENDOR_CAN
#if PROTOCORE_VENDOR_ESP
#define PROTOCORE_HAS_VENDOR_CAN 1
#elif PROTOCORE_HOST
#define PROTOCORE_HAS_VENDOR_CAN PROTOCORE_PLATFORM_HAS_VENDOR_CAN
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_CAN (1 = an SDK CAN / TWAI driver, 0 = none, and the bus capture refuses). Choosing none is fine; defaulting into it is not."
#endif
#endif

// A single "targets real silicon" convenience (any vendor backend, i.e. not the host software floor).
#define PROTOCORE_VENDOR_SILICON                                                                                       \
    (PROTOCORE_VENDOR_ESP || PROTOCORE_VENDOR_STM || PROTOCORE_VENDOR_RP || PROTOCORE_VENDOR_TI)

// The seams each capability above resolved to. They are declared here, after the switching,
// so every guard is read at its settled value.
// What a BIO returns to the record engine when no octet moved and the call is to be retried. The
// engine owns these values, so they are taken from it rather than restated: a BIO that invents its
// own would be read as a fatal error and drop the session. Only defined where a vendor engine
// exists, which is the only arm that runs a BIO - a build that enables a TLS client without one
// fails on the module's own #error rather than silently sending a wrong sentinel.
#if PROTOCORE_HAS_VENDOR_TLS
#if PROTOCORE_VENDOR_SILICON
#include <mbedtls/ssl.h> // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - only exists once the vendor arm resolved
#define PROTOCORE_PLATFORM_TLS_WANT_READ MBEDTLS_ERR_SSL_WANT_READ
#define PROTOCORE_PLATFORM_TLS_WANT_WRITE MBEDTLS_ERR_SSL_WANT_WRITE
#else
// The host arm of the same capability. An env states this capability to drive the vendor BIO path
// off silicon, where there is no SDK to take the values from, so the host owns both ends of the
// contract: the BIO returns these and the stand-in engine reads them. Negative, so they can never
// be confused with a byte count, and distinct from each other.
#define PROTOCORE_PLATFORM_TLS_WANT_READ (-0x7101)
#define PROTOCORE_PLATFORM_TLS_WANT_WRITE (-0x7102)
#endif
#endif
// ---------------------------------------------------------------------------
// Device facts, power domains and stored images
// ---------------------------------------------------------------------------
//
// The core cannot name a vendor, so it asks here. Each seam sits under the capability that answers
// whether the part carries the thing at all: a build whose capability is 0 gets no declaration, so
// reaching for it is a compile error rather than a link-time surprise, and the owning module keys
// its own refusing arm off the same macro.
//
// Every one is implemented once per arm - core_setup/hal/<vendor> on silicon,
// core_setup/hal/portable on the host - so a test drives the real module against the mock rather
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

#endif // PROTOCORE_PLATFORM_H
