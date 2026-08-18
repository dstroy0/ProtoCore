// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hw_caps_en.h
 * @brief Whether a seam exists to call at all: the floor under every hardware capability.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HW_CAPS_EN_H
#define PROTOCORE_HW_CAPS_EN_H

// A vendor answers these by defining them before this file is reached; anything it did not
// answer is 0, which is what "the part has no such seam" means. The PROTOCORE_HAS_* questions
// in hw_caps_en_error.h read them, so this file settles them first.

// TLS. 1 = the vendor ships a TLS stack and network_drivers/tls drives that (mbedTLS over lwIP on
// ESP); 0 = the portable TLS 1.3 in network_drivers/tls, the same hand-rolled stack the QUIC and
// DTLS handshakes already run: TLS_AES_128_GCM_SHA256, X25519, an Ed25519 raw public key.
//
// The two differ in what they will talk to, not just in speed. A vendor stack brings X.509: chain
// validation, name matching, RSA and ECDSA certificates, so a browser will connect. The portable
// one authenticates by raw public key and is what makes TLS exist at all on a part with no vendor
// stack. Take the vendor's wherever there is one.
// Whether a vendor engine exists to call. Silicon has one; a host build has one when an env states
// this, so the vendor BIO path is driven off silicon instead of resolving to the portable arm.
#ifndef PROTOCORE_PLATFORM_HAS_VENDOR_TLS
#define PROTOCORE_PLATFORM_HAS_VENDOR_TLS 0
#endif

// Whether a bus seam exists to call. Silicon has one; a host build has one only when the test
// mock above supplied it, and the bus owners key their host arm off this so a driver's real
// composition runs against the capture rather than being stubbed out at the owner.
#ifndef PROTOCORE_PLATFORM_HAS_BUS
#define PROTOCORE_PLATFORM_HAS_BUS 0
#endif

// Whether a pin seam exists to call. Silicon has one; a host build has one only when the test mock
// above supplied it, and the pin drivers key their host arm off this so a driver's real sampling
// runs against the pin table rather than being stubbed out.
#ifndef PROTOCORE_PLATFORM_HAS_GPIO
#define PROTOCORE_PLATFORM_HAS_GPIO 0
#endif

// Whether a scheduler seam exists to run on. Silicon has one; a host build has one only when the
// driver above supplied it, and the worker layer keys its host arm off this so the pipeline's real
// task / queue path runs against the mock rather than resolving to the inline arm.
#ifndef PROTOCORE_PLATFORM_HAS_SCHEDULER
#define PROTOCORE_PLATFORM_HAS_SCHEDULER 0
#endif

// Whether a seam exists to call. Silicon has one; a host build has one only when the
// driver above supplied a burned-in address, so the owners run against it instead of
// resolving to their refusing arm.
#ifndef PROTOCORE_PLATFORM_HAS_VENDOR_MAC
#define PROTOCORE_PLATFORM_HAS_VENDOR_MAC 0
#endif

// Whether a seam exists to call. Silicon has one; a host build has one only when the
// driver above supplied allocator figures, so the owners run against it instead of
// resolving to their refusing arm.
#ifndef PROTOCORE_PLATFORM_HAS_VENDOR_HEAP_INFO
#define PROTOCORE_PLATFORM_HAS_VENDOR_HEAP_INFO 0
#endif

// Whether a seam exists to call. Silicon has one; a host build has one only when the
// driver above supplied the reset cause, die temperature and CPU clock, so the owners run against it instead of
// resolving to their refusing arm.
#ifndef PROTOCORE_PLATFORM_HAS_VENDOR_PM
#define PROTOCORE_PLATFORM_HAS_VENDOR_PM 0
#endif

// Whether a seam exists to call. Silicon has one; a host build has one only when the
// driver above supplied a radio power domain to hand back, so the owners run against it instead of
// resolving to their refusing arm.
#ifndef PROTOCORE_PLATFORM_HAS_VENDOR_BT
#define PROTOCORE_PLATFORM_HAS_VENDOR_BT 0
#endif

// Whether a seam exists to call. Silicon has one; a host build has one only when the
// driver above supplied a stored image to mark, commit or roll back, so the owners run against it instead of
// resolving to their refusing arm.
#ifndef PROTOCORE_PLATFORM_HAS_VENDOR_OTA
#define PROTOCORE_PLATFORM_HAS_VENDOR_OTA 0
#endif

// Whether a seam exists to call. Silicon has one; a host build has one only when the
// driver above supplied a stored crash image to read, so the owners run against it instead of
// resolving to their refusing arm.
#ifndef PROTOCORE_PLATFORM_HAS_VENDOR_COREDUMP
#define PROTOCORE_PLATFORM_HAS_VENDOR_COREDUMP 0
#endif

// Whether a seam exists to call. Silicon has one; a host build has one only when the
// driver above supplied a CAN controller to open, so the owners run against it instead of
// resolving to their refusing arm.
#ifndef PROTOCORE_PLATFORM_HAS_VENDOR_CAN
#define PROTOCORE_PLATFORM_HAS_VENDOR_CAN 0
#endif

#endif // PROTOCORE_HW_CAPS_EN_H
