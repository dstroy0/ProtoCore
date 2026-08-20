// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file host_answers.h
 * @brief The host build's answers to the platform contract: no silicon anywhere.
 *
 * Included by vendor/vendor_detect.h once the vendor axis has resolved, and by nothing else. Every
 * capability protocore_platform.h asks about is answered here; one it does not answer is refused
 * there by name rather than defaulted.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_VENDOR_HOST_ANSWERS_H
#define PROTOCORE_VENDOR_HOST_ANSWERS_H

#ifndef PROTOCORE_HAS_HW_AESGCM
#define PROTOCORE_HAS_HW_AESGCM 0 // a unit-test build has no silicon by definition
#endif

#ifndef PROTOCORE_HAS_HW_BIGNUM
#define PROTOCORE_HAS_HW_BIGNUM 0 // a unit-test build has no silicon by definition
#endif

#ifndef PROTOCORE_HAS_HW_SHA
#define PROTOCORE_HAS_HW_SHA 0 // a unit-test build has no silicon by definition
#endif

#ifndef PROTOCORE_HAS_HW_AES
#define PROTOCORE_HAS_HW_AES 0 // a unit-test build has no silicon by definition
#endif

#ifndef PROTOCORE_HAS_HW_ECC
#define PROTOCORE_HAS_HW_ECC 0 // a unit-test build has no silicon by definition
#endif

#ifndef PROTOCORE_HAS_VENDOR_MDNS
#define PROTOCORE_HAS_VENDOR_MDNS 0 // a unit-test build has no vendor component to call
#endif

#ifndef PROTOCORE_HAS_VENDOR_DNS_RESOLVER
#define PROTOCORE_HAS_VENDOR_DNS_RESOLVER 0 // a unit-test build has no stack resolver to marshal into
#endif

#ifndef PROTOCORE_HAS_VENDOR_WIFI
#define PROTOCORE_HAS_VENDOR_WIFI 0 // a unit-test build has no radio to put in monitor mode
#endif

#ifndef PROTOCORE_HAS_NET_STACK
#define PROTOCORE_HAS_NET_STACK 0 // until an env declares it and builds the transport
#endif

#ifndef PROTOCORE_HAS_VENDOR_NVS
#define PROTOCORE_HAS_VENDOR_NVS 0 // nothing here outlives the process
#endif

#ifndef PROTOCORE_HAS_BOUNDED_DRAM
#define PROTOCORE_HAS_BOUNDED_DRAM 0 // the host has as much as it asks for
#endif

#ifndef PROTOCORE_HAS_PSRAM
#define PROTOCORE_HAS_PSRAM 0 // a unit-test build has one flat address space
#endif

// No vendor stack, so the same surface comes from a host driver the test environment puts on the
// include path (test/core_setup/hal/host/protocore_net_host.h), exactly the way it supplies <Arduino.h>. Guarded on
// presence, so a build without that path simply has no transport.
#if defined(__has_include)
#if __has_include("protocore_net_host.h")
#include "protocore_net_host.h" // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - the host driver for the block above
#endif
#endif

#ifndef PROTOCORE_HAS_BUS
#define PROTOCORE_HAS_BUS PROTOCORE_PLATFORM_HAS_BUS
#endif

#ifndef PROTOCORE_HAS_GPIO
#define PROTOCORE_HAS_GPIO PROTOCORE_PLATFORM_HAS_GPIO
#endif

#ifndef PROTOCORE_HAS_SCHEDULER
#define PROTOCORE_HAS_SCHEDULER PROTOCORE_PLATFORM_HAS_SCHEDULER
#endif

#ifndef PROTOCORE_HAS_VENDOR_MAC
#define PROTOCORE_HAS_VENDOR_MAC PROTOCORE_PLATFORM_HAS_VENDOR_MAC
#endif

#ifndef PROTOCORE_HAS_VENDOR_HEAP_INFO
#define PROTOCORE_HAS_VENDOR_HEAP_INFO PROTOCORE_PLATFORM_HAS_VENDOR_HEAP_INFO
#endif

#ifndef PROTOCORE_HAS_VENDOR_PM
#define PROTOCORE_HAS_VENDOR_PM PROTOCORE_PLATFORM_HAS_VENDOR_PM
#endif

#ifndef PROTOCORE_HAS_VENDOR_BT
#define PROTOCORE_HAS_VENDOR_BT PROTOCORE_PLATFORM_HAS_VENDOR_BT
#endif

#ifndef PROTOCORE_HAS_VENDOR_OTA
#define PROTOCORE_HAS_VENDOR_OTA PROTOCORE_PLATFORM_HAS_VENDOR_OTA
#endif

#ifndef PROTOCORE_HAS_VENDOR_COREDUMP
#define PROTOCORE_HAS_VENDOR_COREDUMP PROTOCORE_PLATFORM_HAS_VENDOR_COREDUMP
#endif

#ifndef PROTOCORE_HAS_VENDOR_CAN
#define PROTOCORE_HAS_VENDOR_CAN PROTOCORE_PLATFORM_HAS_VENDOR_CAN
#endif

#endif // PROTOCORE_VENDOR_HOST_ANSWERS_H
