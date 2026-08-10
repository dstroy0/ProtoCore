// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_platform.h
 * @brief The one vendor/die selector for the whole library.
 *
 * Multi-vendor portability rests on a single rule: every silicon-specific layer (board profiles,
 * the crypto accelerator HAL, the physical MAC + PHY) is partitioned into a per-vendor subdir and a
 * common API header pulls in exactly ONE backend per build. This header owns the "which vendor" decision
 * so nothing downstream has to re-test toolchain-specific macros - a backend keys off `PC_VENDOR_*`, not
 * off `CONFIG_IDF_TARGET_*` / `STM32*` / `PICO_*` scattered across the tree.
 *
 * Exactly one of these is 1; every other is defined 0 (so `#if PC_VENDOR_ESP` is always valid, never
 * relies on an undefined-macro-is-0 fallback). The vendor is derived from the toolchain's own target macro:
 *
 *   - `PC_VENDOR_ESP` - any Espressif target (ESP-IDF `ESP_PLATFORM` / Arduino-ESP32 `ARDUINO_ARCH_ESP32`).
 *   - `PC_VENDOR_STM` - STM32 (Arduino_Core_STM32 `ARDUINO_ARCH_STM32` / STM32Cube `USE_HAL_DRIVER`).
 *   - `PC_VENDOR_RP`  - Raspberry Pi silicon (RP2040 / RP2350: `ARDUINO_ARCH_RP2040` / `PICO_*`).
 *   - `PC_VENDOR_TI`  - Texas Instruments (`__TI_COMPILER_VERSION__` or an explicit force).
 *   - `PROTOCORE_HOST` - the build runs on the machine that compiled it: no silicon, no scheduler,
 *     portable software everywhere. The test envs state it; otherwise it is what no detected vendor means.
 *
 * A file with a device path and a host path selects on `PROTOCORE_HOST`, never on a vendor's own macro:
 * `ARDUINO` names one vendor's toolchain, so keying on it drops every other vendor down the host path.
 *
 * ESP is detected first and stays byte-for-byte compatible with the pre-selector behavior: on every ESP
 * build `PC_VENDOR_ESP` is 1, and on host builds it is 0, exactly matching the old
 * `#if defined(CONFIG_IDF_TARGET_*)` test in board_profile.h.
 */

#ifndef PROTOCORE_PC_PLATFORM_H
#define PROTOCORE_PC_PLATFORM_H

#include <stdint.h>

/**
 * @brief Linkage for a leaf primitive whose body is cheaper than the call that reaches it.
 *
 * Stated here rather than in protocore_config.h because that header reaches this one through
 * board_profile.h before it defines anything of its own, so this is the earliest point the
 * linkage can be settled. protocore_config.h keeps the same definition behind #ifndef, which
 * covers a translation unit that arrives without this header.
 */
#ifndef PC_INLINE
#if defined(__GNUC__)
#define PC_INLINE static inline __attribute__((always_inline))
#else
#define PC_INLINE static inline
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
#define PC_VENDOR_ESP 1
#elif defined(ARDUINO_ARCH_STM32) || defined(USE_HAL_DRIVER) || defined(STM32_CORE_VERSION)
#define PC_VENDOR_STM 1
#elif defined(ARDUINO_ARCH_RP2040) || defined(PICO_RP2040) || defined(PICO_RP2350) || defined(PICO_SDK_VERSION_MAJOR)
#define PC_VENDOR_RP 1
#elif defined(__TI_COMPILER_VERSION__) || defined(PC_VENDOR_TI_FORCE)
#define PC_VENDOR_TI 1
#else
#define PROTOCORE_HOST 1
#endif

// Every one is defined (0 when not selected) so downstream code can `#if` it freely.
#ifndef PC_VENDOR_ESP
#define PC_VENDOR_ESP 0
#endif
#ifndef PC_VENDOR_STM
#define PC_VENDOR_STM 0
#endif
#ifndef PC_VENDOR_RP
#define PC_VENDOR_RP 0
#endif
#ifndef PC_VENDOR_TI
#define PC_VENDOR_TI 0
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
// silicon below; a build with no vendor answers 0 and turns one on with -DPC_HAS_<X>=1, which is
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
#ifndef PC_HAS_HW_AESGCM
#if PC_VENDOR_ESP
#define PC_HAS_HW_AESGCM 1
#elif PROTOCORE_HOST
#define PC_HAS_HW_AESGCM 0 // a unit-test build has no silicon by definition
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_HW_AESGCM (1 = accelerated AEAD in core_setup/hal/<vendor>, 0 = portable software AES + table GHASH, ~7.6x slower where measured). Choosing software is fine; defaulting into it is not."
#endif
#endif

// DH-2048 / RSA modexp. 1 = the vendor supplies an accelerated backend; 0 = the portable software
// Montgomery backend (core_setup/hal/portable), which is data-dependent and NOT constant time -
// see SECURITY.md, timing.
#ifndef PC_HAS_HW_BIGNUM
#if PC_VENDOR_ESP
#define PC_HAS_HW_BIGNUM 1
#elif PROTOCORE_HOST
#define PC_HAS_HW_BIGNUM 0 // a unit-test build has no silicon by definition
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_HW_BIGNUM (1 = accelerated backend in core_setup/hal/<vendor>, 0 = portable software Montgomery, which is not constant time). Choosing software crypto is fine; defaulting into it is not."
#endif
#endif

// SHA-1 / SHA-256 / SHA-512. 1 = the vendor supplies a hashing peripheral and the mbedtls backend
// over it; 0 = the portable software compression functions.
//
// One capability for the family: a part that ships a SHA block ships it for the digests it supports,
// and a build that has to fall back for one of them falls back for all of them rather than mixing a
// peripheral digest with a software one inside the same handshake transcript.
#ifndef PC_HAS_HW_SHA
#if PC_VENDOR_ESP
#define PC_HAS_HW_SHA 1
#elif PROTOCORE_HOST
#define PC_HAS_HW_SHA 0 // a unit-test build has no silicon by definition
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_HW_SHA (1 = a hashing peripheral in core_setup/hal/<vendor>, 0 = the portable software compression functions). Choosing software is fine; defaulting into it is not."
#endif
#endif

// AES block, and the CTR / CMAC / CCM modes over it. 1 = the vendor supplies an AES peripheral;
// 0 = the portable software AES.
//
// Separate from PC_HAS_HW_AESGCM: GHASH is its own multiplier and a part can ship one without the
// other, so a build states each. The block cipher is what this one answers for.
#ifndef PC_HAS_HW_AES
#if PC_VENDOR_ESP
#define PC_HAS_HW_AES 1
#elif PROTOCORE_HOST
#define PC_HAS_HW_AES 0 // a unit-test build has no silicon by definition
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_HW_AES (1 = an AES peripheral in core_setup/hal/<vendor>, 0 = the portable software AES). Choosing software is fine; defaulting into it is not."
#endif
#endif

// X25519 and ECDSA over P-256. 1 = the vendor supplies an accelerated curve backend; 0 = the
// portable software field arithmetic.
//
// Not the same axis as PC_HAS_HW_BIGNUM: that one answers for modexp over a 2048-bit modulus, this
// one for curve point math, and a part can accelerate either alone.
#ifndef PC_HAS_HW_ECC
#if PC_VENDOR_ESP
#define PC_HAS_HW_ECC 1
#elif PROTOCORE_HOST
#define PC_HAS_HW_ECC 0 // a unit-test build has no silicon by definition
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_HW_ECC (1 = an accelerated curve backend in core_setup/hal/<vendor>, 0 = the portable software field arithmetic, which is not constant time on every curve). Choosing software is fine; defaulting into it is not."
#endif
#endif

// mDNS / DNS-SD. 1 = the vendor ships its own responder component and the wrapper drives that;
// 0 = the portable responder in network_drivers/application/mdns_service, which answers over the
// UDP listener like every other datagram service.
//
// The vendor's does more than advertise: probing, conflict resolution, IPv6 records. Take it where
// it exists. The portable one is what makes the feature exist at all on a part that has none.
#ifndef PC_HAS_VENDOR_MDNS
#if PC_VENDOR_ESP
#define PC_HAS_VENDOR_MDNS 1
#elif PROTOCORE_HOST
#define PC_HAS_VENDOR_MDNS 0 // a unit-test build has no vendor component to call
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_VENDOR_MDNS (1 = the SDK's own responder component, 0 = the portable responder over the UDP listener). Choosing the portable one is fine; defaulting into it is not."
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
#ifndef PC_HAS_VENDOR_TLS
#if PC_VENDOR_ESP
#define PC_HAS_VENDOR_TLS 1
#elif PROTOCORE_HOST
#define PC_HAS_VENDOR_TLS 0 // a unit-test build has no SDK stack to drive
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_VENDOR_TLS (1 = the SDK's own TLS stack with X.509, 0 = the portable TLS 1.3 over the TCP record layer, raw public key only). Choosing the portable one is fine; defaulting into it is not."
#endif
#endif

// SNTP has no vendor seam: network_drivers/application/ntp_service is the client on every target. It
// asks a server over the UDP listener, keeps the epoch in its own state, and hands it out through
// pc_ntp_epoch(); nothing in libc moves.

// DNS resolution. 1 = the stack resolves names itself and the module marshals into it; 0 = the
// portable resolver in network_drivers/network/dns, which asks over the UDP listener.
//
// A stack resolver already knows the nameservers DHCP handed it and caches what it learns. The
// portable one asks PC_DNS_SERVER, once per call, and keeps nothing.
#ifndef PC_HAS_VENDOR_DNS_RESOLVER
#if PC_VENDOR_ESP
#define PC_HAS_VENDOR_DNS_RESOLVER 1
#elif PROTOCORE_HOST
#define PC_HAS_VENDOR_DNS_RESOLVER 0 // a unit-test build has no stack resolver to marshal into
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_VENDOR_DNS_RESOLVER (1 = the stack's own resolver, 0 = the portable resolver over the UDP listener). Choosing the portable one is fine; defaulting into it is not."
#endif
#endif

// Self-update. 1 = the SDK ships an updater that writes the other app partition and flips the boot
// selector; 0 = there is none, and the OTA service and the rollback policy have nothing to drive.
#ifndef PC_HAS_VENDOR_OTA
#if PC_VENDOR_ESP
#define PC_HAS_VENDOR_OTA 1
#elif PROTOCORE_HOST
#define PC_HAS_VENDOR_OTA 0 // a unit-test build has no partition table to write
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_VENDOR_OTA (1 = the SDK's own updater + boot selector, 0 = none, and the OTA service does not compile). Choosing none is fine; defaulting into it is not."
#endif
#endif

// Crash-image capture. 1 = the SDK writes a core dump to its own flash partition; 0 = there is none.
// The decoder that reads one is portable and is not gated on this.
#ifndef PC_HAS_VENDOR_COREDUMP
#if PC_VENDOR_ESP
#define PC_HAS_VENDOR_COREDUMP 1
#elif PROTOCORE_HOST
#define PC_HAS_VENDOR_COREDUMP 0 // a unit-test build has no crash partition to read
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_VENDOR_COREDUMP (1 = the SDK's own crash-image capture, 0 = none, and only the portable decoder compiles). Choosing none is fine; defaulting into it is not."
#endif
#endif

// WiFi driver. 1 = the SDK exposes the radio below the IP stack, which is what monitor mode and a
// vendor peer-to-peer protocol need; 0 = there is none, and both refuse. Not the same axis as
// having a network interface: a stack can carry IP over ethernet with no radio underneath it.
#ifndef PC_HAS_VENDOR_WIFI
#if PC_VENDOR_ESP
#define PC_HAS_VENDOR_WIFI 1
#elif PROTOCORE_HOST
#define PC_HAS_VENDOR_WIFI 0 // a unit-test build has no radio to put in monitor mode
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_VENDOR_WIFI (1 = an SDK WiFi driver reachable below the IP stack, 0 = none, and monitor mode and the peer-to-peer radio refuse). Choosing none is fine; defaulting into it is not."
#endif
#endif

// CAN controller. 1 = the SDK ships a CAN / TWAI driver; 0 = there is none and the bus capture
// refuses. The SocketCAN framing over it is portable and is not gated on this.
#ifndef PC_HAS_VENDOR_CAN
#if PC_VENDOR_ESP
#define PC_HAS_VENDOR_CAN 1
#elif PROTOCORE_HOST
#define PC_HAS_VENDOR_CAN 0 // a unit-test build has no controller to open
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_VENDOR_CAN (1 = an SDK CAN / TWAI driver, 0 = none, and the bus capture refuses). Choosing none is fine; defaulting into it is not."
#endif
#endif

// A stack with pcbs to bind. 1 = there is one to open a socket on; 0 = there is none, and every
// listener and outbound client refuses. The transport owners (tcp.h, udp.h) are portable either
// way - this answers whether anything is underneath them.
// Declared, not sniffed. Unlike the bus and pin seams, whose owners resolve to a refusing arm on
// their own, turning this on compiles whole transport translation units into every consumer - so an
// env states it and carries those sources, rather than inheriting it from a header being reachable.
#ifndef PC_HAS_NET_STACK
#if PC_VENDOR_ESP
#define PC_HAS_NET_STACK 1
#elif PROTOCORE_HOST
#define PC_HAS_NET_STACK 0 // until an env declares it and builds the transport
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_NET_STACK (1 = a stack with pcbs to bind, 0 = none, and every listener and client refuses). Choosing none is fine; defaulting into it is not."
#endif
#endif

// Tasks to run on. 1 = there is a scheduler, so the pipeline runs on its own worker and a delay
// sleeps; 0 = there is one context, the pipeline runs inline from the caller's loop, and a delay
// spins on the clock.
#ifndef PC_HAS_SCHEDULER
#if PC_VENDOR_ESP
#define PC_HAS_SCHEDULER 1
#elif PROTOCORE_HOST
#define PC_HAS_SCHEDULER 0 // one context: the caller's
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_SCHEDULER (1 = tasks the pipeline can run on, 0 = one context and an inline pipeline). Choosing one context is fine; defaulting into it is not."
#endif
#endif

// A factory MAC to read. 1 = the SDK hands back a burned-in address the device identity is derived
// from; 0 = there is none, and the identity comes from wherever the application puts it.
#ifndef PC_HAS_VENDOR_MAC
#if PC_VENDOR_ESP
#define PC_HAS_VENDOR_MAC 1
#elif PROTOCORE_HOST
#define PC_HAS_VENDOR_MAC 0 // a unit-test build has no burned-in address
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_VENDOR_MAC (1 = a burned-in address from the SDK, 0 = none). Choosing none is fine; defaulting into it is not."
#endif
#endif

// Heap and reset introspection. 1 = the SDK reports free / minimum-free heap and why the part last
// reset, which the health readouts and the guardrails report; 0 = there is none and they report 0.
#ifndef PC_HAS_VENDOR_HEAP_INFO
#if PC_VENDOR_ESP
#define PC_HAS_VENDOR_HEAP_INFO 1
#elif PROTOCORE_HOST
#define PC_HAS_VENDOR_HEAP_INFO 0 // the host allocator is not ours to measure
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_VENDOR_HEAP_INFO (1 = SDK heap and reset-reason readouts, 0 = none, and the health panel reports 0). Choosing none is fine; defaulting into it is not."
#endif
#endif

// Non-volatile key-value storage. 1 = the SDK keeps a key-value store across a reboot, which is
// what provisioned credentials are written to; 0 = there is none and provisioning has nowhere to
// put them.
#ifndef PC_HAS_VENDOR_NVS
#if PC_VENDOR_ESP
#define PC_HAS_VENDOR_NVS 1
#elif PROTOCORE_HOST
#define PC_HAS_VENDOR_NVS 0 // nothing here outlives the process
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_VENDOR_NVS (1 = an SDK key-value store that survives a reboot, 0 = none, and provisioning does not compile). Choosing none is fine; defaulting into it is not."
#endif
#endif

// A Bluetooth controller whose memory can be released. 1 = the SDK ships one; 0 = there is none and
// there is nothing to release. Its own axis from CONFIG_BT_ENABLED, which says whether a given
// build compiled it in.
#ifndef PC_HAS_VENDOR_BT
#if PC_VENDOR_ESP
#define PC_HAS_VENDOR_BT 1
#elif PROTOCORE_HOST
#define PC_HAS_VENDOR_BT 0 // no controller, so no memory to hand back
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_VENDOR_BT (1 = an SDK Bluetooth controller, 0 = none). Choosing none is fine; defaulting into it is not."
#endif
#endif

// Power management. 1 = the SDK reports why the part reset, sets the CPU clock, reads the die
// temperature and gates a radio's power domain; 0 = there is none of that to bind to.
#ifndef PC_HAS_VENDOR_PM
#if PC_VENDOR_ESP
#define PC_HAS_VENDOR_PM 1
#elif PROTOCORE_HOST
#define PC_HAS_VENDOR_PM 0 // no clock to set, no die to measure
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_VENDOR_PM (1 = SDK reset-reason / CPU clock / die temperature / power-domain gating, 0 = none, and the power plan is advice with nothing to apply it to). Choosing none is fine; defaulting into it is not."
#endif
#endif

// An internal RAM segment a pool can overflow. 1 = link-time placement is bounded and a pool that
// does not fit has to be moved or acknowledged; 0 = one address space, so the budget guards have
// nothing to protect.
#ifndef PC_HAS_BOUNDED_DRAM
#if PC_VENDOR_ESP
#define PC_HAS_BOUNDED_DRAM 1
#elif PROTOCORE_HOST
#define PC_HAS_BOUNDED_DRAM 0 // the host has as much as it asks for
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_BOUNDED_DRAM (1 = a bounded internal RAM segment the budget guards check, 0 = one address space). Choosing one address space is fine; defaulting into it is not."
#endif
#endif

// External RAM a pool can be placed in. 1 = the toolchain has an attribute that moves a BSS object
// out of internal DRAM; 0 = there is one memory and a pool stays where it is declared. It answers
// only for the attribute existing: whether a given board is wired for it, and whether a given pool
// should use it, are the PC_*_IN_PSRAM flags.
#ifndef PC_HAS_PSRAM
#if PC_VENDOR_ESP
#define PC_HAS_PSRAM 1
#elif PROTOCORE_HOST
#define PC_HAS_PSRAM 0 // a unit-test build has one flat address space
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_PSRAM (1 = an external-RAM placement attribute in its toolchain, 0 = one memory, and every pool stays in it). Choosing one memory is fine; defaulting into it is not."
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
uintptr_t pc_platform_context_id(void);

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

#if PC_VENDOR_ESP

#include "driver/gpio.h"          // PC_ALLOW_LATE_INCLUDE: ordered - see above
#include "driver/uart.h"          // PC_ALLOW_LATE_INCLUDE: ordered - see above
#include "esp_cpu.h"              // PC_ALLOW_LATE_INCLUDE: ordered - see above
#include "esp_idf_version.h"      // PC_ALLOW_LATE_INCLUDE: ordered - names the IDF the driver headers came from
#include "esp_random.h"           // PC_ALLOW_LATE_INCLUDE: ordered - see above
#include "esp_system.h"           // PC_ALLOW_LATE_INCLUDE: ordered - see above
#include "esp_timer.h"            // PC_ALLOW_LATE_INCLUDE: ordered - see above
#include "freertos/FreeRTOS.h"    // PC_ALLOW_LATE_INCLUDE: ordered - only exists once the vendor above resolved to ESP
#include "freertos/queue.h"       // PC_ALLOW_LATE_INCLUDE: ordered - see above
#include "freertos/semphr.h"      // PC_ALLOW_LATE_INCLUDE: ordered - see above
#include "freertos/task.h"        // PC_ALLOW_LATE_INCLUDE: ordered - see above
#include "lwip/igmp.h"            // PC_ALLOW_LATE_INCLUDE: ordered - see above
#include "lwip/pbuf.h"            // PC_ALLOW_LATE_INCLUDE: ordered - see above
#include "lwip/priv/tcpip_priv.h" // PC_ALLOW_LATE_INCLUDE: ordered - see above
#include "lwip/tcp.h"             // PC_ALLOW_LATE_INCLUDE: ordered - see above
#include "lwip/udp.h"             // PC_ALLOW_LATE_INCLUDE: ordered - see above

typedef QueueHandle_t pc_platform_queue;
typedef StaticQueue_t pc_platform_queue_ctrl; ///< a caller-owned queue control block
typedef SemaphoreHandle_t pc_platform_mutex;
typedef StaticSemaphore_t pc_platform_mutex_ctrl; ///< a caller-owned mutex control block
typedef TaskHandle_t pc_platform_task;
typedef TaskFunction_t pc_platform_task_fn;
typedef BaseType_t pc_platform_status;
typedef TickType_t pc_platform_ticks;

typedef struct tcp_pcb pc_pcb;                  ///< a TCP control block
typedef struct pbuf pc_pbuf;                    ///< a received packet buffer chain
typedef err_t pc_net_err;                       ///< a network stack result
typedef struct tcpip_api_call_data pc_net_call; ///< the marshal record for a stack call

#define PC_PLATFORM_OK pdTRUE
#define PC_PLATFORM_PASS pdPASS
#define PC_PLATFORM_FALSE pdFALSE
#define PC_PLATFORM_WAIT_FOREVER portMAX_DELAY
#define PC_PLATFORM_CORES portNUM_PROCESSORS

#define pc_platform_queue_create xQueueCreateStatic
#define pc_platform_queue_send xQueueSendToBack
#define pc_platform_queue_send_front xQueueSendToFront
#define pc_platform_queue_send_isr xQueueSendToBackFromISR
#define pc_platform_queue_recv xQueueReceive
#define pc_platform_queue_waiting uxQueueMessagesWaiting
#define pc_platform_queue_waiting_isr uxQueueMessagesWaitingFromISR
#define pc_platform_queue_delete vQueueDelete

// A mutex over a caller-owned control block, so the object is BSS and no allocator runs
// (SRC_LAW rule 2). take blocks until the holder releases; a caller that cannot block passes its
// own tick budget instead of PC_PLATFORM_WAIT_FOREVER.
#define pc_platform_mutex_create xSemaphoreCreateMutexStatic
#define pc_platform_mutex_take xSemaphoreTake
#define pc_platform_mutex_give xSemaphoreGive

#define pc_platform_task_start xTaskCreatePinnedToCore
#define pc_platform_task_stop vTaskDelete
#define pc_platform_task_notify xTaskNotifyGive
#define pc_platform_task_wait ulTaskNotifyTake
#define pc_platform_task_delay vTaskDelay
#define pc_platform_task_yield_from_isr portYIELD_FROM_ISR
#define pc_platform_task_self xTaskGetCurrentTaskHandle

// Buses. The bridge and the peripheral drivers drive UART / SPI / I2C; these are the IDF C
// drivers that Arduino's HardwareSerial, SPI and Wire objects are built over, so the core reaches
// a bus without naming a framework. A unit the SoC does not have fails closed rather than trapping.
#ifndef PC_UART_RX_BUF
#define PC_UART_RX_BUF 512 // driver RX ring per unit; a bridge transaction is far smaller
#endif
#define PC_UART_UNITS SOC_UART_NUM

// 8N1, no flow control, on @p rx and @p tx; -1 on either leaves that pin at the unit's default.
PC_INLINE int pc_platform_uart_begin(uint8_t unit, uint32_t baud, int rx, int tx)
{
    if (unit >= PC_UART_UNITS)
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
        return uart_driver_install((uart_port_t)unit, PC_UART_RX_BUF, 0, 0, NULL, 0) == ESP_OK ? 1 : 0;
    }
    return 1;
}
#define pc_platform_uart_write(unit, buf, len) uart_write_bytes((uart_port_t)(unit), (const char *)(buf), (len))
#define pc_platform_uart_read(unit, buf, len, ms) uart_read_bytes((uart_port_t)(unit), (buf), (len), pdMS_TO_TICKS(ms))
PC_INLINE uint32_t pc_platform_uart_available(uint8_t unit)
{
    size_t n = 0;
    return (uart_get_buffered_data_len((uart_port_t)unit, &n) == ESP_OK) ? (uint32_t)n : 0u;
}

#define PC_SPI_MSBFIRST 0
#define PC_SPI_LSBFIRST 1

// Bits clocked per SCLK in the data phase.
#define PC_SPI_LANES_1 1
#define PC_SPI_LANES_2 2
#define PC_SPI_LANES_4 4

// SPI. The pins come from the caller because they are a board fact, not a library one - a bridge
// target names its own bus. Bring a host up once with the pins, then run transactions on it.
// @p host selects the controller: 0 is the general-purpose one, 1 the second where the die has it.
// @p quadwp and @p quadhd are the third and fourth data lines, -1 when the bus is single or dual.
int pc_platform_spi_begin(uint8_t host, int mosi, int miso, int sclk, int quadwp, int quadhd);
int pc_platform_spi_txn(uint8_t host, uint32_t hz, uint8_t bit_order, uint8_t mode, const uint8_t *tx, uint8_t *rx,
                        uint32_t len);

// A framed transfer: a @p cmd_bits command, an @p addr_bits address, @p dummy_bits idle clocks,
// then the data phase at @p lanes bits per clock. The controller drives each phase, so the data
// buffer holds data alone. A zero bit count omits that phase; @p tx or @p rx may be NULL for a
// one-way data phase, and @p len 0 sends the phases with no data phase at all.
int pc_platform_spi_txn_ext(uint8_t host, uint32_t hz, uint8_t bit_order, uint8_t mode, uint16_t cmd, uint8_t cmd_bits,
                            uint32_t addr, uint8_t addr_bits, uint8_t dummy_bits, uint8_t lanes, const uint8_t *tx,
                            uint8_t *rx, uint32_t len);

// I2C, on the same terms: install the port once on the caller's pins, then address a device on it.
// @p bus selects the controller. Each call carries its own timeout in milliseconds, so a wedged
// device stops one transfer rather than the loop (SRC_LAW rule 5). write_read is one transaction
// with a repeated start, which is what a register read is: name the register, then turn the bus
// around without releasing it.
//
// An address is 7-bit unless it carries PC_I2C_ADDR_10BIT, which selects the two-byte form: a
// 11110xx byte holding the two high bits, then the low eight. The flag rides in bit 15, so both
// address widths are the same argument type.
#define PC_I2C_ADDR_10BIT 0x8000u
#define PC_I2C_ADDR_MASK 0x03FFu
#define PC_I2C_GENERAL_CALL 0x00u ///< the address every device on the bus answers

int pc_platform_i2c_begin(uint8_t bus, int sda, int scl, uint32_t hz);
int pc_platform_i2c_write(uint8_t bus, uint16_t addr, const uint8_t *buf, uint32_t len, uint32_t ms);
int pc_platform_i2c_read(uint8_t bus, uint16_t addr, uint8_t *buf, uint32_t len, uint32_t ms);
int pc_platform_i2c_write_read(uint8_t bus, uint16_t addr, const uint8_t *w, uint32_t wlen, uint8_t *r, uint32_t rlen,
                               uint32_t ms);

// Set @p bus to @p hz. Standard mode is 100 kHz, fast 400 kHz, fast-plus 1 MHz.
int pc_platform_i2c_set_clock(uint8_t bus, uint32_t hz);

// Address @p addr and stop, reporting whether anything drove ACK. This is the address-only cycle
// the transfer calls refuse, and what a bus scan is built from.
int pc_platform_i2c_probe(uint8_t bus, uint16_t addr, uint32_t ms);

// Clock SCL until the device holding SDA low releases it, then drive a stop. The port is
// uninstalled for the duration and reinstalled on the same pins afterwards.
int pc_platform_i2c_recover(uint8_t bus, int sda, int scl);

// Entropy. The ESP32 RNG is a true hardware source: it samples thermal / RF analog noise rather
// than running a deterministic generator, so this is the one the key material is drawn from.
// esp_random() is the IDF entry point Arduino random() is built over.
#define pc_platform_rand_u32() ((uint32_t)esp_random())
#define pc_platform_rand_fill(buf, len) esp_fill_random((buf), (len))

// GPIO. The IDF driver is C and is what Arduino digitalWrite()/pinMode() sit on, so the core
// reaches the pins through these rather than through a framework it is not allowed to name.
#define PC_GPIO_IN 0
#define PC_GPIO_OUT 1
#define PC_GPIO_IN_PULLUP 2
#define PC_GPIO_IN_PULLDOWN 3
#define PC_GPIO_LOW 0
#define PC_GPIO_HIGH 1

PC_INLINE void pc_platform_gpio_mode(uint8_t pin, uint8_t mode)
{
    gpio_num_t g = (gpio_num_t)pin;
    gpio_set_direction(g, (mode == PC_GPIO_OUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
    gpio_set_pull_mode(g, (mode == PC_GPIO_IN_PULLUP)     ? GPIO_PULLUP_ONLY
                          : (mode == PC_GPIO_IN_PULLDOWN) ? GPIO_PULLDOWN_ONLY
                                                          : GPIO_FLOATING);
}
#define pc_platform_gpio_write(pin, level) gpio_set_level((gpio_num_t)(pin), (level) ? 1 : 0)
#define pc_platform_gpio_read(pin) ((uint8_t)gpio_get_level((gpio_num_t)(pin)))

// Time base. esp_timer/esp_cpu are the IDF primitives underneath Arduino millis()/micros(), so
// these are the same counters by a name the core can call from C.
// Reboot. esp_restart() is the IDF entry point Arduino's ESP.restart() calls, so the core asks for
// a restart without naming a framework object. It does not return.
#define pc_platform_restart() esp_restart()

#define pc_platform_micros() ((uint32_t)esp_timer_get_time())
#define pc_platform_millis() ((uint32_t)(esp_timer_get_time() / 1000))
#define pc_platform_cycles() ((uint32_t)esp_cpu_get_cycle_count())

#define PC_NET_OK ERR_OK
#define PC_NET_ERR_MEM ERR_MEM
#define PC_NET_ERR_BUF ERR_BUF
#define PC_NET_ERR_VAL ERR_VAL
#define PC_NET_ERR_ARG ERR_ARG
#define PC_NET_ERR_USE ERR_USE
#define PC_NET_ERR_CONN ERR_CONN
#define PC_NET_ERR_CLSD ERR_CLSD
#define PC_NET_ERR_RST ERR_RST
#define PC_NET_ERR_ABRT ERR_ABRT
#define PC_NET_ADDR_ANY IP_ANY_TYPE
#define PC_NET_TYPE_ANY IPADDR_TYPE_ANY
#define PC_NET_TYPE_V4 IPADDR_TYPE_V4
#define PC_NET_WRITE_COPY TCP_WRITE_FLAG_COPY

#define pc_net_new tcp_new_ip_type
#define pc_net_bind tcp_bind
#define pc_net_listen tcp_listen_with_backlog
#define pc_net_connect tcp_connect
#define pc_net_close tcp_close
#define pc_net_abort tcp_abort
#define pc_net_arg tcp_arg
#define pc_net_on_accept tcp_accept
#define pc_net_on_recv tcp_recv
#define pc_net_on_sent tcp_sent
#define pc_net_on_err tcp_err
#define pc_net_write tcp_write
#define pc_net_output tcp_output
#define pc_net_recved tcp_recved
#define pc_net_sndbuf tcp_sndbuf
#define pc_net_nagle_disable tcp_nagle_disable
#define pc_net_pbuf_free pbuf_free
#define pc_net_pbuf_copy pbuf_copy_partial
#define pc_net_pbuf_alloc pbuf_alloc
#define pc_net_call_marshal tcpip_api_call

#define PC_NET_PBUF_TRANSPORT PBUF_TRANSPORT
#define PC_NET_PBUF_RAM PBUF_RAM
#define PC_NET_ADDR_ANY4 IP4_ADDR_ANY
#define PC_NET_ADDR_ANY4_P IP4_ADDR_ANY4
#define PC_NET_OPT_REUSEADDR SOF_REUSEADDR

typedef struct udp_pcb pc_udp_pcb;
typedef ip_addr_t pc_net_ip;

#define pc_net_udp_new udp_new
#define pc_net_udp_bind udp_bind
#define pc_net_udp_recv udp_recv
#define pc_net_udp_sendto udp_sendto
#define pc_net_udp_remove udp_remove
#define pc_net_opt_set ip_set_option
#define pc_net_ip_parse ipaddr_aton
#define pc_net_ip_print ipaddr_ntoa_r
#define pc_net_ip_is_v4 IP_IS_V4
#define pc_net_ip_is_v6 IP_IS_V6
#define pc_net_ip_as_v4 ip_2_ip4
#define pc_net_ip_as_v6 ip_2_ip6
// The v6 address is four network-order words, so its sixteen bytes are the address as it travels.
// Reached as bytes rather than words: a word read gives the host's byte order, not the wire's.
#define pc_net_ip6_bytes(a) ((const uint8_t *)ip_2_ip6(a)->addr)
#define pc_net_ip6_wbytes(a) ((uint8_t *)ip_2_ip6(a)->addr)
#define pc_net_ip6_mark(a) IP_SET_TYPE_VAL(*(a), IPADDR_TYPE_V6)
#define pc_net_ip4_u32 ip4_addr_get_u32
#define pc_net_ip4_set IP_ADDR4
#define pc_net_rcv_wnd_update tcp_update_rcv_ann_wnd
#define pc_net_ip4_is_multicast ip4_addr_ismulticast
#define PC_NET_HAS_IGMP LWIP_IGMP
#define PC_NET_HAS_IPV6 LWIP_IPV6
#define pc_net_igmp_join igmp_joingroup
#define pc_net_igmp_leave igmp_leavegroup

#elif PROTOCORE_HOST

// No vendor stack, so the same surface comes from a host driver the test environment puts on the
// include path (test/mocks/pc_net_host.h), exactly the way it supplies <Arduino.h>. Guarded on
// presence, so a build without that path simply has no transport.
#if defined(__has_include)
#if __has_include("pc_net_host.h")
#include "pc_net_host.h" // PC_ALLOW_LATE_INCLUDE: ordered - the host driver for the block above
#endif
#endif

#endif // PC_VENDOR_ESP

// Whether a bus seam exists to call. Silicon has one; a host build has one only when the test
// mock above supplied it, and the bus owners key their host arm off this so a driver's real
// composition runs against the capture rather than being stubbed out at the owner.
#ifndef PC_PLATFORM_HAS_BUS
#define PC_PLATFORM_HAS_BUS 0
#endif

// I2C / SPI / UART master. 1 = there is a seam to drive; 0 = there is none and every bus owner
// resolves to its refusing arm. It sits here rather than with the vendor capabilities above because
// it reads the seam macro the block above establishes.
#ifndef PC_HAS_BUS
#if PC_VENDOR_ESP
#define PC_HAS_BUS 1
#elif PROTOCORE_HOST
#define PC_HAS_BUS PC_PLATFORM_HAS_BUS
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_BUS (1 = an I2C / SPI / UART master in core_setup/hal/<vendor>, 0 = none, and every bus owner refuses). Choosing none is fine; defaulting into it is not."
#endif
#endif

// Whether a pin seam exists to call. Silicon has one; a host build has one only when the test mock
// above supplied it, and the pin drivers key their host arm off this so a driver's real sampling
// runs against the pin table rather than being stubbed out.
#ifndef PC_PLATFORM_HAS_GPIO
#define PC_PLATFORM_HAS_GPIO 0
#endif

// Digital pins. 1 = there is a seam to drive; 0 = there is none and every pin driver resolves to its
// refusing arm. Its own capability rather than a term of PC_HAS_BUS: a part can carry pins without
// carrying an I2C / SPI / UART master.
#ifndef PC_HAS_GPIO
#if PC_VENDOR_ESP
#define PC_HAS_GPIO 1
#elif PROTOCORE_HOST
#define PC_HAS_GPIO PC_PLATFORM_HAS_GPIO
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_GPIO (1 = pc_platform_gpio_mode / _read / _write in core_setup/hal/<vendor>, 0 = none, and every pin driver refuses). Choosing none is fine; defaulting into it is not."
#endif
#endif

// A single "targets real silicon" convenience (any vendor backend, i.e. not the host software floor).
#define PC_VENDOR_SILICON (PC_VENDOR_ESP || PC_VENDOR_STM || PC_VENDOR_RP || PC_VENDOR_TI)

#endif // PROTOCORE_PC_PLATFORM_H
