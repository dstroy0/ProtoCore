// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file utmc.h
 * @brief UTMC (Urban Traffic Management and Control) common-database codec (PC_ENABLE_UTMC).
 *
 * UTMC is the UK/EU modular framework for sharing traffic data across heterogeneous municipal systems.
 * Its common-database exchange is an HTTP + XML message set: a client requests the value of an object
 * (a detector, a sign, a signal) by its UTMC object id, and the server replies with the object's value
 * + a data-quality flag + a timestamp. This builds the two documents over the existing HTTP stack:
 *
 *  - **request**: `<UTMCRequest><object id="..."/></UTMCRequest>`.
 *  - **response**: `<UTMCResponse><object id=".." value=".." quality=".." timestamp=".."/></UTMCResponse>`.
 *
 * Values are XML-escaped. Pure text framing, zero heap, no stdlib, host-testable; the HTTP transport is
 * the shipped server.
 */

#ifndef PROTOCORE_UTMC_H
#define PROTOCORE_UTMC_H

#include "protocore_config.h"

#if PC_ENABLE_UTMC

/** @brief UTMC data-quality flags. */
// UTMC value-quality codes: wire values compared, so integer constants in a namespacing struct.
#define D 0 ///< the value is good.
#define T 1 ///< the value is suspect.
#define T 2 ///< no value available.

/**
 * @brief Build a UTMC request document for one object id. @return length written, or 0 on overflow.
 */
size_t pc_utmc_request(const char *object_id, char *out, size_t cap);

/**
 * @brief Build a UTMC response document for one object.
 * @param object_id the UTMC object id (escaped).
 * @param value     the object value text (escaped).
 * @param quality   a UTMC_QUALITY_* flag.
 * @param timestamp an ISO-8601 timestamp (escaped).
 * @return length written, or 0 on overflow.
 */
size_t pc_utmc_response(const char *object_id, const char *value, uint8_t quality, const char *timestamp, char *out,
                        size_t cap);

/**
 * @brief Extract the object id from a UTMC request document into @p out.
 * @return the id length, or 0 if no `<object id="..."/>` is found.
 */
size_t pc_utmc_parse_request(const char *xml, size_t len, char *out, size_t cap);

#endif // PC_ENABLE_UTMC
#endif // PROTOCORE_UTMC_H
