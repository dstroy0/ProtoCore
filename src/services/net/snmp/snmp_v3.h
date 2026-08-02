// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_snmp_v3.h
 * @brief SNMPv3 User-based Security Model (USM) layer (PC_ENABLE_SNMP_V3).
 *
 * Adds authenticated and optionally encrypted SNMPv3 on top of the v1/v2c agent:
 * the v3 message framing (RFC 3412), engine discovery + timeliness (RFC 3414),
 * USM authentication and privacy. The decrypted, authenticated inner PDU is
 * dispatched through the shared MIB core ([`pc_snmp_dispatch_pdu()`](@ref pc_snmp_dispatch_pdu)),
 * so v1/v2c and v3 expose the same objects.
 *
 * Protocols implemented (a single authPriv user):
 *  - **Auth:** `usmHMAC192SHA256` (HMAC-SHA-256, 24-byte digest; RFC 7860) - net-snmp `-a SHA-256`.
 *  - **Priv:** `usmAesCfb128` (AES-128-CFB; RFC 3826) - net-snmp `-x AES`.
 *
 * The agent is the authoritative engine, so the per-user localized keys are
 * derived once in pc_snmp_v3_set_user() (no per-request 1 MB key expansion).
 */

#ifndef PROTOCORE_SNMP_V3_H
#define PROTOCORE_SNMP_V3_H

#include "protocore_config.h"

#if PC_ENABLE_SNMP_V3

/** @brief usmHMAC192SHA256 authentication-parameter length (192-bit truncation). */
#define SNMP_V3_AUTH_PARAM_LEN 24
/** @brief usmAesCfb128 privacy-parameter (salt) length. */
#define SNMP_V3_PRIV_PARAM_LEN 8

/**
 * @brief Reset the v3 layer and set the authoritative engine ID.
 *
 * Pass nullptr to keep the built-in default engine ID. Call before
 * pc_snmp_v3_set_user() (the localized keys depend on the engine ID). For a unique
 * per-device ID, derive @p id from the chip MAC.
 */
void pc_snmp_v3_init(const uint8_t *engine_id = NULL, size_t engine_id_len = 0);

/**
 * @brief Configure the (single) USM user and derive its localized keys.
 *
 * @param user       user name.
 * @param auth_pass  authentication password (>= 8 chars); nullptr/empty = no auth user.
 * @param priv_pass  privacy password (>= 8 chars); nullptr/empty = auth-only (no privacy).
 */
void pc_snmp_v3_set_user(const char *user, const char *auth_pass, const char *priv_pass);

/**
 * @brief Set the persisted engineBoots counter (load from NVS, increment per boot).
 *
 * Timeliness (RFC 3414 §3.2) relies on a monotonically increasing boot count;
 * the default is 1. engineTime is seconds since boot.
 */
void pc_snmp_v3_set_boots(uint32_t boots);

/** @brief Current authoritative engineBoots (for persisting back to NVS). */
uint32_t pc_snmp_v3_get_boots();

/**
 * @brief Process one SNMPv3 message and build the response (called by the agent).
 *
 * Handles engine discovery (Report with usmStatsUnknownEngineIDs), timeliness
 * (usmStatsNotInTimeWindows), authentication and decryption, then dispatches the
 * inner PDU and returns an authenticated/encrypted response. Returns 0 to send
 * nothing.
 */
size_t pc_snmp_v3_process(const uint8_t *req, size_t req_len, uint8_t *resp, size_t pc_resp_cap);

#endif // PC_ENABLE_SNMP_V3

#endif // PROTOCORE_SNMP_V3_H
