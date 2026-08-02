// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file webhook.h
 * @brief Outbound webhooks / IFTTT (PC_ENABLE_WEBHOOK).
 *
 * Builds an IFTTT Maker webhook URL and its value1/value2/value3 JSON payload
 * (pure, host-tested), and fires them - or any JSON to any URL - via the outbound
 * http_client (a POST). Lets the device push an event to IFTTT, a Slack/Discord
 * incoming webhook, or your own REST endpoint. Needs PC_ENABLE_HTTP_CLIENT to
 * send; without it the API still compiles but pc_webhook_post() returns -1.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_WEBHOOK_H
#define PROTOCORE_WEBHOOK_H

#include "protocore_config.h"

#if PC_ENABLE_WEBHOOK

// ---------------------------------------------------------------------------
// Host-testable builders
// ---------------------------------------------------------------------------

/**
 * @brief Build the IFTTT Maker URL `https://maker.ifttt.com/trigger/<event>/with/key/<key>`.
 * @return characters written, or 0 if it would not fit @p cap (fail-closed).
 */
int pc_ifttt_url(const char *event, const char *key, char *out, size_t cap);

/**
 * @brief Build an IFTTT `{"value1":..,"value2":..,"value3":..}` JSON body.
 *
 * Null values are omitted; values are JSON-escaped for `"` and `\`. An all-null
 * call yields `{}`.
 * @return characters written, or 0 if it would not fit @p cap (fail-closed).
 */
int pc_ifttt_payload(const char *v1, const char *v2, const char *v3, char *out, size_t cap);

// ---------------------------------------------------------------------------
// Fire (ESP32 via http_client; HTTP_CLIENT_ERR_* on host)
// ---------------------------------------------------------------------------

/** @brief POST @p json (application/json) to @p url. @return HTTP status (>0) or negative error. */
int pc_webhook_post(const char *url, const char *json);

/**
 * @brief Trigger an IFTTT Maker event with up to three values.
 * @return HTTP status (>0) or a negative error (e.g. URL/payload build or transport).
 */
int pc_ifttt_trigger(const char *event, const char *key, const char *v1, const char *v2, const char *v3);

#endif // PC_ENABLE_WEBHOOK
#endif // PROTOCORE_WEBHOOK_H
