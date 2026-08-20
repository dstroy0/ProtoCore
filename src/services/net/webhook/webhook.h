// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file webhook.h
 * @brief Outbound webhooks / IFTTT (PROTOCORE_ENABLE_WEBHOOK).
 *
 * A webhook is an HTTP POST the device originates. The pattern itself is not standardized: no
 * IETF RFC defines "webhook", and the IFTTT Maker interface these builders target is one
 * service's own URI convention. What is standardized is everything that POST rides on.
 *
 * RFC 9110 sec 9.3.3: "The POST method requests that the target resource process the
 * representation enclosed in the request according to the resource's own specific semantics."
 * The resource is named by the target URI (RFC 9110 sec 7.1), here an https URI
 * (RFC 9110 sec 4.2.2) whose path is a sequence of segments (RFC 3986 sec 3.3). The content
 * (RFC 9110 sec 6.4) is a JSON object (RFC 8259 sec 4) carried under Content-Type
 * application/json (RFC 9110 sec 8.3; the media type is registered in RFC 8259 sec 11) with a
 * Content-Length taken from its octet count (RFC 9110 sec 8.6). The answer is a three-digit
 * status code (RFC 9110 sec 15.1): 2xx says the request was received, understood, and accepted
 * (RFC 9110 sec 15.3); 4xx and 5xx say it was not (RFC 9110 sec 15.5, sec 15.6).
 *
 * The two builders are pure and host-testable. Sending needs PROTOCORE_ENABLE_HTTP_CLIENT: on a
 * build without it ::WebhookNs::post reports -1 and nothing is transmitted.
 *
 * The module exports one symbol, @ref Webhook. Everything in webhook.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_WEBHOOK_H
#define PROTOCORE_WEBHOOK_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_WEBHOOK

PROTOCORE_BEGIN_DECLS

/** @brief RFC 9110 sec 7.1 / sec 6.4: what a POST names and what it carries. */
typedef struct
{
    const char *target_uri; ///< the https URI a POST is sent to (RFC 9110 sec 4.2.2)
    const char *content;    ///< the JSON object it carries (RFC 8259 sec 4)
} WebhookRequestArgs;
/** @brief The caller region a builder writes into. */
typedef struct
{
    char *out;  ///< where the built octets land
    size_t cap; ///< how much room they have, terminator included
} WebhookBuildArgs;
/** @brief The IFTTT Maker event: the two path segments its URI carries and its three values. */
typedef struct
{
    const char *event;  ///< the event name path segment (RFC 3986 sec 3.3)
    const char *key;    ///< the Maker key path segment
    const char *value1; ///< first member of the object; NULL omits it
    const char *value2; ///< second member; NULL omits it
    const char *value3; ///< third member; NULL omits it
} WebhookIftttArgs;
/**
 * @brief The outbound webhook module: build a target URI and a JSON object, then POST them.
 *
 * A caller sets the members a call takes, invokes it through ::Webhook, and reads the outcome off
 * the same handle. No slot member: one call runs at a time and names no session.
 *
 * No storage member: the builders write the caller's region, and the URI and content a trigger
 * builds live on that call's own frame, so nothing survives a call.
 *
 * @var WebhookNs::request  the target URI a POST names and the content it carries
 * @var WebhookNs::build    the caller region a builder writes into
 * @var WebhookNs::ifttt    the Maker event, key, and up to three values
 * @var WebhookNs::n        octets a builder wrote, 0 when the whole build would not fit
 * @var WebhookNs::i32      the status code a POST read back (RFC 9110 sec 15.1), or a negative
 *                          transport error
 * @var WebhookNs::ifttt_url      build the Maker target URI from @c ifttt.event and @c ifttt.key
 * @var WebhookNs::ifttt_payload  build the value1/value2/value3 object (RFC 8259 sec 4)
 * @var WebhookNs::post           POST @c request.content as application/json to
 *                                @c request.target_uri (RFC 9110 sec 9.3.3)
 * @var WebhookNs::ifttt_trigger  build the URI and the object into its own frames, then POST them
 */
typedef struct
{
    WebhookRequestArgs request; ///< what a POST names and carries
    WebhookBuildArgs build;     ///< where a builder writes
    WebhookIftttArgs ifttt;     ///< the Maker event fields
    int n;
    int i32;
} WebhookVars;

/** @brief The operands and the outcome. */
extern WebhookVars WebhookV;

/** @brief The entries. */
typedef struct
{
    void (*const ifttt_url)(uint8_t *restrict work);
    void (*const ifttt_payload)(uint8_t *restrict work);
    void (*const post)(uint8_t *restrict work);
    void (*const ifttt_trigger)(uint8_t *restrict work);
} WebhookNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in WebhookV or a region of the borrow at a fixed offset.
void protocore_webhook_ifttt_url(uint8_t *restrict work);
void protocore_webhook_ifttt_payload(uint8_t *restrict work);
void protocore_webhook_post(uint8_t *restrict work);
void protocore_webhook_ifttt_trigger(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Webhook.ifttt_url(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const WebhookNs Webhook __attribute__((unused)) = {
    .ifttt_url = protocore_webhook_ifttt_url,
    .ifttt_payload = protocore_webhook_ifttt_payload,
    .post = protocore_webhook_post,
    .ifttt_trigger = protocore_webhook_ifttt_trigger,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WEBHOOK

#endif // PROTOCORE_WEBHOOK_H
