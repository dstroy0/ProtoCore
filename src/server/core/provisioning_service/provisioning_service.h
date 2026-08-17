// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file provisioning_service.h
 * @brief First-boot WiFi provisioning via a captive portal (PROTOCORE_ENABLE_PROVISIONING).
 *
 * When no WiFi credentials are stored, the device starts a softAP and a
 * catch-all DNS responder (via the transport-layer UDP service - no add-on library) so any
 * connected client is funneled to a credentials form. Submitted SSID/passphrase
 * are persisted to NVS and the device reboots into station mode. Uses only
 * `Physical.wifi_ap_init`, the library UDP transport, and the platform's key/value store; compiled
 * to stubs when disabled or when the platform carries no such store.
 *
 * The form-field parser (Prov.form_field) is the pure half of this module and is the
 * only non-trivial logic, so it is unit-tested off-target.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PROVISIONING_H
#define PROTOCORE_PROVISIONING_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_PROVISIONING

PROTOCORE_BEGIN_DECLS

// PROTOCORE_PROVISIONING_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief What form_field takes: body, key, out, cap. */
typedef struct
{
    const char *body; ///< Form body (e.g. "ssid=My+AP&psk=p%40ss")
    const char *key;  ///< Field name (e.g. "ssid")
    char *out;        ///< Destination buffer
    size_t cap;       ///< Capacity of out (>= 1)
} ProvFormFieldArgs;

/** @brief What load takes: ssid, ssid_cap, psk, psk_cap. */
typedef struct
{
    char *ssid;      ///< Destination for the stored SSID (always null-terminated)
    size_t ssid_cap; ///< Capacity of ssid
    char *psk;       ///< Destination for the stored passphrase (always null-terminated)
    size_t psk_cap;  ///< Capacity of psk
} ProvLoadArgs;

/** @brief What begin takes: ap_ssid. */
typedef struct
{
    const char *ap_ssid;
} ProvBeginArgs;

/**
 * @brief First-boot WiFi provisioning via a captive portal (PROTOCORE_ENABLE_PROVISIONING).
 *
 * A caller sets the members a call takes, invokes it through ::Prov with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Prov.form_field_args.body = ...;
 *   Prov.form_field_args.key = ...;
 *   Prov.form_field_args.out = ...;
 *   Prov.form_field_args.cap = ...;
 *   Prov.form_field(work);
 *   // Prov.ok is what the call reports
 *
 * @var ProvNs::form_field_args  what form_field takes: body, key, out, cap
 * @var ProvNs::load_args  what load takes: ssid, ssid_cap, psk, psk_cap
 * @var ProvNs::begin_args  what begin takes: ap_ssid
 * @var ProvNs::ok  true if the field was found, false otherwise (out set to "")
 * @var ProvNs::form_field  extract and URL-decode a field from an x-www-form-urlencoded body. ...
 * @var ProvNs::load  load stored WiFi credentials from NVS
 * @var ProvNs::begin  start the captive portal: softAP ap_ssid + catch-all DNS + form ...
 * @var ProvNs::clear  erase stored credentials (forces re-provisioning on next boot)
 *
 * @c work is PROTOCORE_PROVISIONING_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    ProvFormFieldArgs form_field_args;
    ProvLoadArgs load_args;
    ProvBeginArgs begin_args;

    proto_bool ok;

    void (*const form_field)(uint8_t *restrict work);
    void (*const load)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const clear)(uint8_t *restrict work);
} ProvNs;

/** @brief The one symbol this module exports. */
extern ProvNs Prov;

/**
 * @brief The PROTOCORE_PROVISIONING_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_provisioning_service_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PROVISIONING

#endif // PROTOCORE_PROVISIONING_H
