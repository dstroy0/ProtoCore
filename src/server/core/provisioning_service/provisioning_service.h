// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_PROVISIONING_H
#define PROTOCORE_PROVISIONING_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

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
 * @c work is PROTOCORE_PROVISIONING_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*form_field)(uint8_t *restrict, const char *, const char *, char *, size_t);
    proto_bool (*load)(uint8_t *restrict, char *, size_t, char *, size_t);
    void (*begin)(uint8_t *restrict, const char *);
    void (*clear)(uint8_t *restrict);
} ProvNs;
PROTOCORE_NS_LAYOUT(ProvNs, form_field, load, begin, clear);

/**
 * @brief Extract and URL-decode a field from an x-www-form-urlencoded body. .
 * @param work PROTOCORE_PROV_BORROW bytes the caller took. Not held past the call.
 * @param body Form body (e.g. "ssid=My+AP&psk=p%40ss")
 * @param key Field name (e.g. "ssid")
 * @param out Destination buffer
 * @param cap Capacity of out (>= 1)
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_prov_form_field(uint8_t *restrict work, const char *body, const char *key, char *out, size_t cap);
/**
 * @brief Load stored WiFi credentials from NVS.
 * @param work PROTOCORE_PROV_BORROW bytes the caller took. Not held past the call.
 * @param ssid Destination for the stored SSID (always null-terminated)
 * @param ssid_cap Capacity of ssid
 * @param psk Destination for the stored passphrase (always null-terminated)
 * @param psk_cap Capacity of psk
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_prov_load(uint8_t *restrict work, char *ssid, size_t ssid_cap, char *psk, size_t psk_cap);
/**
 * @brief Start the captive portal: softAP ap_ssid + catch-all DNS + form .
 * @param work PROTOCORE_PROV_BORROW bytes the caller took. Not held past the call.
 * @param ap_ssid Ap ssid
 */
void protocore_prov_begin(uint8_t *restrict work, const char *ap_ssid);
/**
 * @brief Erase stored credentials (forces re-provisioning on next boot).
 * @param work PROTOCORE_PROV_BORROW bytes the caller took. Not held past the call.
 */
void protocore_prov_clear(uint8_t *restrict work);

/**
 * @brief The PROTOCORE_PROVISIONING_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_provisioning_service_span(void);

/** @brief Module namespace. */
PROTOCORE_NS ProvNs Prov PROTOCORE_UNUSED = {.form_field = protocore_prov_form_field,
                                             .load = protocore_prov_load,
                                             .begin = protocore_prov_begin,
                                             .clear = protocore_prov_clear};

PROTOCORE_END_DECLS

#endif // PROTOCORE_PROVISIONING_H
