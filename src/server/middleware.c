// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file middleware.c
 * @brief Middleware chain + built-in fixed-window rate limiter for PC.
 *
 * use() registers a middleware; run_middleware() runs the chain in order (first MW_HALT stops
 * dispatch); enable_rate_limit() / rate_limit_check() implement a rollover-safe fixed-window
 * counter that answers 429 + Retry-After past the budget.
 */

#include "mmgr/frame.h" // the one frame engine
#include "protocore.h"
#include "server/clock/clock.h"     // pc_millis: the library clock, not the platform's
#include "shared_primitives/mime.h" // PC_MIME_TEXT_PLAIN
#include <stdio.h>

// Retry-After carries one number and nothing else.
static const pc_field RETRY_AFTER[] = {PC_U32, PC_END};

// ---------------------------------------------------------------------------
// Middleware chain + built-in rate limiter
// ---------------------------------------------------------------------------

// Both live here (internal linkage) because nothing outside this file reads either one: the
// dispatcher calls run_middleware() and rate_limit_check() and takes the answer. They share a
// context rather than getting one each because they are the same thing to a caller - the work that
// runs before a request is allowed to reach a route - and splitting them would be two owners for
// one decision point.
typedef struct
{
    Middleware middleware[MAX_MIDDLEWARE];
    uint8_t middleware_count;

    uint16_t rl_max;          ///< Max requests per window; 0 = rate limiting off.
    uint32_t rl_window_ms;    ///< Window length in milliseconds.
    uint32_t rl_window_start; ///< Start of the current window.
    uint16_t rl_count;        ///< Requests counted in the current window.
} MiddlewareCtx;
static MiddlewareCtx s_mw;

void pc_middleware_reset(void)
{
    // The chain and the rate-limit window are both per-run configuration, and a test case starts
    // with neither. One store: the zero state is the initial state (empty chain, limiter off).
    s_mw = (MiddlewareCtx){0};
}

void use(Middleware mw)
{
    if (mw == NULL || s_mw.middleware_count >= MAX_MIDDLEWARE)
    {
        return;
    }
    s_mw.middleware[s_mw.middleware_count++] = mw;
}

// Run the chain in registration order. The first middleware to return MW_HALT stops dispatch; it is
// responsible for having sent a response.
proto_bool run_middleware(uint8_t slot_id, HttpReq *req)
{
    for (uint8_t i = 0; i < s_mw.middleware_count; i++)
    {
        // s_mw.middleware[i] is never null here: use() is the only writer, and it always stores a
        // non-null entry together with the count increment that admits index i - a slot below
        // s_mw.middleware_count can never regress to null.
        if (s_mw.middleware[i] && s_mw.middleware[i](slot_id, req) == MW_HALT)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

void enable_rate_limit(uint16_t max_requests, uint32_t window_ms)
{
    s_mw.rl_max = max_requests;
    s_mw.rl_window_ms = window_ms;
    s_mw.rl_window_start = pc_millis();
    s_mw.rl_count = 0;
}

// Fixed-window counter. Unsigned subtraction is rollover-safe across the millis()
// wrap. On the request that tips past s_mw.rl_max, reply 429 + Retry-After and stop.
proto_bool rate_limit_check(uint8_t slot_id)
{
    if (s_mw.rl_max == 0 || s_mw.rl_window_ms == 0)
    {
        return PROTO_FALSE; // disabled
    }

    uint32_t now = pc_millis();
    if ((uint32_t)(now - s_mw.rl_window_start) >= s_mw.rl_window_ms)
    {
        s_mw.rl_window_start = now; // new window
        s_mw.rl_count = 0;
    }

    s_mw.rl_count++;
    if (s_mw.rl_count <= s_mw.rl_max)
    {
        return PROTO_FALSE; // within budget
    }

    // Over budget: advertise how long until the window resets, then 429.
    uint32_t elapsed = (uint32_t)(now - s_mw.rl_window_start);
    // The ":0" arm is unreachable: the check above either just reset s_mw.rl_window_start to `now`
    // (elapsed == 0) or left it in place because elapsed was already < s_mw.rl_window_ms - either way
    // elapsed < s_mw.rl_window_ms always holds here, so the ">" arm always taken.
    uint32_t remain_ms = (s_mw.rl_window_ms > elapsed) ? (s_mw.rl_window_ms - elapsed) : 0;
    char secs[12];
    // Fails closed to an empty string on its own, so there is no failure arm to write here.
    pc_frame_build(secs, sizeof(secs), RETRY_AFTER, (uint32_t)((remain_ms + 999) / 1000));
    proto_add_response_header(slot_id, "Retry-After", secs);
    send_text(slot_id, 429, PC_MIME_TEXT_PLAIN, "Too Many Requests");
    return PROTO_TRUE;
}
