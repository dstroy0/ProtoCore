// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file provisioning_service.h
 * @brief First-boot WiFi provisioning via a captive portal (PC_ENABLE_PROVISIONING).
 *
 * When no WiFi credentials are stored, the device starts a softAP and a
 * catch-all DNS responder (via the transport-layer UDP service - no add-on library) so any
 * connected client is funneled to a credentials form. Submitted SSID/passphrase
 * are persisted to NVS and the device reboots into station mode. Uses only
 * `init_wifi_ap_physical()`, the library UDP transport, and `Preferences`; compiled to stubs when disabled
 * or off-Arduino.
 *
 * The form-field parser (pc_prov_form_field) is always compiled and is the
 * only non-trivial logic, so it is unit-tested off-target.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PROVISIONING_H
#define PROTOCORE_PROVISIONING_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_PROVISIONING

/**
 * @brief Extract and URL-decode a field from an x-www-form-urlencoded body.
 *
 * Finds `@p key=` in @p body (matching only whole field names, i.e. at the
 * start or just after `&`), copies its value up to the next `&` or end into
 * @p out, decoding `+` to space and `%XX` hex escapes. Always null-terminates.
 *
 * @param body  Form body (e.g. "ssid=My+AP&psk=p%40ss").
 * @param key   Field name (e.g. "ssid").
 * @param out   Destination buffer.
 * @param cap   Capacity of @p out (>= 1).
 * @return true if the field was found, false otherwise (out set to "").
 */
proto_bool pc_prov_form_field(const char *body, const char *key, char *out, size_t cap);

/**
 * @brief Load stored WiFi credentials from NVS.
 * @param ssid      Destination for the stored SSID (always null-terminated).
 * @param ssid_cap  Capacity of @p ssid.
 * @param psk       Destination for the stored passphrase (always null-terminated).
 * @param psk_cap   Capacity of @p psk.
 * @return true if a non-empty SSID is stored (the app should connect in STA mode).
 */
proto_bool pc_provisioning_load(char *ssid, size_t ssid_cap, char *psk, size_t psk_cap);

/**
 * @brief Start the captive portal: softAP @p ap_ssid + catch-all DNS + form routes.
 *
 * Registers a catch-all `GET` route (the credentials form) and `POST /save` (persist + reboot)
 * on @p server. The catch-all DNS responder runs from a transport-layer UDP callback,
 * so no per-loop servicing is required. Call after begin().
 */
void pc_provisioning_begin(const char *ap_ssid);

/** @brief Erase stored credentials (forces re-provisioning on next boot). */
void pc_provisioning_clear(void);

#endif // PC_ENABLE_PROVISIONING

PROTO_END_DECLS

#endif // PROTOCORE_PROVISIONING_H
