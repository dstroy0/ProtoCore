// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file openadr.h
 * @brief OpenADR 3.0 (Open Automated Demand Response) JSON codec (PC_ENABLE_OPENADR).
 *
 * OpenADR 3.0 is the demand-response protocol as a REST/JSON API (over HTTP + OAuth2, both already
 * shipped): a VTN (server) posts `event` objects to VENs, and VENs post `report` objects back. This
 * builds the two core JSON objects into a caller buffer, so a device is an OpenADR 3.0 VEN over the
 * existing HTTP client/server:
 *
 *  - **event**: `programID`, `eventName`, and an `intervals` array each carrying a `SIMPLE`/`price`/
 *    `LOAD_CONTROL` payload point (start + duration + a numeric value) - a demand-response signal.
 *  - **report**: `programID`, `eventID`, a `resourceName`, and a reading (a value at a point in time) -
 *    the VEN's telemetry back to the VTN.
 *
 * Pure JSON text framing (strings escaped), zero heap, no stdlib, host-testable; the OAuth2 token +
 * HTTP transport are the existing services.
 */

#ifndef PROTOCORE_OPENADR_H
#define PROTOCORE_OPENADR_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_OPENADR

/** @brief One OpenADR interval payload point (a single value over a time interval). */
struct OpenAdrInterval
{
    uint32_t start;    ///< interval start (epoch seconds).
    uint32_t duration; ///< interval duration (seconds).
    const char *type;  ///< payload type, e.g. "SIMPLE", "PRICE", "LOAD_CONTROL".
    double value;      ///< the payload value (a level, price, or setpoint).
};

/**
 * @brief Build an OpenADR 3.0 event JSON object.
 * @param program_id the program id (string; escaped).
 * @param event_name the event name (string; escaped).
 * @param intervals  the interval payload points.
 * @param count      number of intervals.
 * @return length written (excl NUL), or 0 on overflow.
 */
size_t pc_openadr_event(const char *program_id, const char *event_name, const OpenAdrInterval *intervals, size_t count,
                        char *out, size_t cap);

/**
 * @brief Build an OpenADR 3.0 report JSON object (one reading for one resource).
 * @param program_id    the program id.
 * @param event_id      the event id this report answers.
 * @param resource_name the reporting resource.
 * @param value         the reading value.
 * @param timestamp     the reading time (epoch seconds).
 * @return length written (excl NUL), or 0 on overflow.
 */
size_t pc_openadr_report(const char *program_id, const char *event_id, const char *resource_name, double value,
                         uint32_t timestamp, char *out, size_t cap);

#endif // PC_ENABLE_OPENADR
#endif // PROTOCORE_OPENADR_H
