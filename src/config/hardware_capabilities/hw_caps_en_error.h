// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hw_caps_en_error.h
 * @brief Every hardware capability the build must state, and the refusal when it did not.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HW_CAPS_EN_ERROR_H
#define PROTOCORE_HW_CAPS_EN_ERROR_H

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
// test/core_setup/ tests these to decide which backend TU compiles, and src/ tests them where a software
// path and a hardware path both exist. There is no weak symbol behind any of them - linking no
// backend is an undefined reference, linking two is a duplicate definition, and both fail the build
// rather than silently selecting one.

// AES-GCM. 1 = the vendor supplies an accelerated AEAD (test/core_setup/hal/<vendor>); 0 = the portable
// software backend, which is software AES plus a table GHASH.
//
// Not a small difference and not a preference: measured sealing 1 KiB on an ESP32-S3 at 240 MHz, the
// vendor AEAD is 81,085 cycles and the software path 616,567 - 7.6x. Hand it to the vendor whenever
// there is one; choosing software is legitimate where there is not, but it has to be chosen.
#ifndef PROTOCORE_HAS_HW_AESGCM
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_HW_AESGCM (1 = accelerated AEAD in test/core_setup/hal/<vendor>, 0 = portable software AES + table GHASH, ~7.6x slower where measured). Choosing software is fine; defaulting into it is not."
#endif

// DH-2048 / RSA modexp. 1 = the vendor supplies an accelerated backend; 0 = the portable software
// Montgomery backend (test/core_setup/hal/portable), which is data-dependent and NOT constant time -
// see SECURITY.md, timing.
#ifndef PROTOCORE_HAS_HW_BIGNUM
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_HW_BIGNUM (1 = accelerated backend in test/core_setup/hal/<vendor>, 0 = portable software Montgomery, which is not constant time). Choosing software crypto is fine; defaulting into it is not."
#endif

// SHA-1 / SHA-256 / SHA-512. 1 = the vendor supplies a hashing peripheral and a backend over it;
// 0 = the portable software compression functions.
//
// One capability for the family: a part that ships a SHA block ships it for the digests it supports,
// and a build that has to fall back for one of them falls back for all of them rather than mixing a
// peripheral digest with a software one inside the same handshake transcript.
#ifndef PROTOCORE_HAS_HW_SHA
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_HW_SHA (1 = a hashing peripheral in test/core_setup/hal/<vendor>, 0 = the portable software compression functions). Choosing software is fine; defaulting into it is not."
#endif

// AES block, and the CTR / CMAC / CCM modes over it. 1 = the vendor supplies an AES peripheral;
// 0 = the portable software AES.
//
// Separate from PROTOCORE_HAS_HW_AESGCM: GHASH is its own multiplier and a part can ship one without the
// other, so a build states each. The block cipher is what this one answers for.
#ifndef PROTOCORE_HAS_HW_AES
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_HW_AES (1 = an AES peripheral in test/core_setup/hal/<vendor>, 0 = the portable software AES). Choosing software is fine; defaulting into it is not."
#endif

// X25519 and ECDSA over P-256. 1 = the vendor supplies an accelerated curve backend; 0 = the
// portable software field arithmetic.
//
// Not the same axis as PROTOCORE_HAS_HW_BIGNUM: that one answers for modexp over a 2048-bit modulus, this
// one for curve point math, and a part can accelerate either alone.
#ifndef PROTOCORE_HAS_HW_ECC
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_HW_ECC (1 = an accelerated curve backend in test/core_setup/hal/<vendor>, 0 = the portable software field arithmetic, which is not constant time on every curve). Choosing software is fine; defaulting into it is not."
#endif

// mDNS / DNS-SD. 1 = the vendor ships its own responder component and the wrapper drives that;
// 0 = the portable responder in network_drivers/application/mdns_service, which answers over the
// UDP listener like every other datagram service.
//
// The vendor's does more than advertise: probing, conflict resolution, IPv6 records. Take it where
// it exists. The portable one is what makes the feature exist at all on a part that has none.
#ifndef PROTOCORE_HAS_VENDOR_MDNS
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_MDNS (1 = the SDK's own responder component, 0 = the portable responder over the UDP listener). Choosing the portable one is fine; defaulting into it is not."
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
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_DNS_RESOLVER (1 = the stack's own resolver, 0 = the portable resolver over the UDP listener). Choosing the portable one is fine; defaulting into it is not."
#endif

// WiFi driver. 1 = the SDK exposes the radio below the IP stack, which is what monitor mode and a
// vendor peer-to-peer protocol need; 0 = there is none, and both refuse. Not the same axis as
// having a network interface: a stack can carry IP over ethernet with no radio underneath it.
#ifndef PROTOCORE_HAS_VENDOR_WIFI
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_WIFI (1 = an SDK WiFi driver reachable below the IP stack, 0 = none, and monitor mode and the peer-to-peer radio refuse). Choosing none is fine; defaulting into it is not."
#endif

// A stack with pcbs to bind. 1 = there is one to open a socket on; 0 = there is none, and every
// listener and outbound client refuses. The transport owners (tcp.h, udp.h) are portable either
// way - this answers whether anything is underneath them.
// Declared, not sniffed. Unlike the bus and pin seams, whose owners resolve to a refusing arm on
// their own, turning this on compiles whole transport translation units into every consumer - so an
// env states it and carries those sources, rather than inheriting it from a header being reachable.
#ifndef PROTOCORE_HAS_NET_STACK
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_NET_STACK (1 = a stack with pcbs to bind, 0 = none, and every listener and client refuses). Choosing none is fine; defaulting into it is not."
#endif

// Non-volatile key-value storage. 1 = the SDK keeps a key-value store across a reboot, which is
// what provisioned credentials are written to; 0 = there is none and provisioning has nowhere to
// put them.
#ifndef PROTOCORE_HAS_VENDOR_NVS
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_NVS (1 = an SDK key-value store that survives a reboot, 0 = none, and provisioning does not compile). Choosing none is fine; defaulting into it is not."
#endif

// An internal RAM segment a pool can overflow. 1 = link-time placement is bounded and a pool that
// does not fit has to be moved or acknowledged; 0 = one address space, so the budget guards have
// nothing to protect.
#ifndef PROTOCORE_HAS_BOUNDED_DRAM
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_BOUNDED_DRAM (1 = a bounded internal RAM segment the budget guards check, 0 = one address space). Choosing one address space is fine; defaulting into it is not."
#endif

// External RAM a pool can be placed in. 1 = the toolchain has an attribute that moves a BSS object
// out of internal DRAM; 0 = there is one memory and a pool stays where it is declared. It answers
// only for the attribute existing: whether a given board is wired for it, and whether a given pool
// should use it, are the PROTOCORE_*_IN_PSRAM flags.
#ifndef PROTOCORE_HAS_PSRAM
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_PSRAM (1 = an external-RAM placement attribute in its toolchain, 0 = one memory, and every pool stays in it). Choosing one memory is fine; defaulting into it is not."
#endif

// I2C / SPI / UART master. 1 = there is a seam to drive; 0 = there is none and every bus owner
// resolves to its refusing arm. It sits here rather than with the vendor capabilities above because
// it reads the seam macro the block above establishes.
#ifndef PROTOCORE_HAS_BUS
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_BUS (1 = an I2C / SPI / UART master in test/core_setup/hal/<vendor>, 0 = none, and every bus owner refuses). Choosing none is fine; defaulting into it is not."
#endif

// Digital pins. 1 = there is a seam to drive; 0 = there is none and every pin driver resolves to its
// refusing arm. Its own capability rather than a term of PROTOCORE_HAS_BUS: a part can carry pins without
// carrying an I2C / SPI / UART master.
#ifndef PROTOCORE_HAS_GPIO
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_GPIO (1 = protocore_platform_gpio_mode / _read / _write in test/core_setup/hal/<vendor>, 0 = none, and every pin driver refuses). Choosing none is fine; defaulting into it is not."
#endif

// Tasks to run on. 1 = there is a scheduler, so the pipeline runs on its own worker and a delay
// sleeps; 0 = there is one context, the pipeline runs inline from the caller's loop, and a delay
// spins on the clock. It sits here rather than with the vendor capabilities above because it reads
// the seam macro the driver block establishes.
#ifndef PROTOCORE_HAS_SCHEDULER
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_SCHEDULER (1 = tasks the pipeline can run on, 0 = one context and an inline pipeline). Choosing one context is fine; defaulting into it is not."
#endif

// A factory MAC to read. 1 = the SDK hands back a burned-in address the device identity is derived
// from; 0 = there is none, and the identity comes from wherever the application puts it.
#ifndef PROTOCORE_HAS_VENDOR_MAC
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_MAC (1 = a burned-in address from the SDK, 0 = none). Choosing none is fine; defaulting into it is not."
#endif

// Heap and reset introspection. 1 = the SDK reports free / minimum-free heap and why the part last
// reset, which the health readouts and the guardrails report; 0 = there is none and they report 0.
#ifndef PROTOCORE_HAS_VENDOR_HEAP_INFO
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_HEAP_INFO (1 = SDK heap and reset-reason readouts, 0 = none, and the health panel reports 0). Choosing none is fine; defaulting into it is not."
#endif

// Power management. 1 = the SDK reports why the part reset, sets the CPU clock, reads the die
// temperature and gates a radio's power domain; 0 = there is none of that to bind to.
#ifndef PROTOCORE_HAS_VENDOR_PM
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_PM (1 = SDK reset-reason / CPU clock / die temperature / power-domain gating, 0 = none, and the power plan is advice with nothing to apply it to). Choosing none is fine; defaulting into it is not."
#endif

// A Bluetooth controller whose memory can be released. 1 = the SDK ships one; 0 = there is none and
// there is nothing to release. Its own axis from CONFIG_BT_ENABLED, which says whether a given
// build compiled it in.
#ifndef PROTOCORE_HAS_VENDOR_BT
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_BT (1 = an SDK Bluetooth controller, 0 = none). Choosing none is fine; defaulting into it is not."
#endif

// Self-update. 1 = the SDK ships an updater that writes the other app partition and flips the boot
// selector; 0 = there is none, and the OTA service and the rollback policy have nothing to drive.
#ifndef PROTOCORE_HAS_VENDOR_OTA
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_OTA (1 = the SDK's own updater + boot selector, 0 = none, and the OTA service does not compile). Choosing none is fine; defaulting into it is not."
#endif

// Crash-image capture. 1 = the SDK writes a core dump to its own flash partition; 0 = there is none.
// The decoder that reads one is portable and is not gated on this.
#ifndef PROTOCORE_HAS_VENDOR_COREDUMP
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_COREDUMP (1 = the SDK's own crash-image capture, 0 = none, and only the portable decoder compiles). Choosing none is fine; defaulting into it is not."
#endif

// CAN controller. 1 = the SDK ships a CAN / TWAI driver; 0 = there is none and the bus capture
// refuses. The SocketCAN framing over it is portable and is not gated on this.
#ifndef PROTOCORE_HAS_VENDOR_CAN
#error                                                                                                                 \
    "ProtoCore: this vendor must state PROTOCORE_HAS_VENDOR_CAN (1 = an SDK CAN / TWAI driver, 0 = none, and the bus capture refuses). Choosing none is fine; defaulting into it is not."
#endif

#endif // PROTOCORE_HW_CAPS_EN_ERROR_H
